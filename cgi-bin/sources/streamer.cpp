#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <strings.h>

#include "cgi_debug.h"
#include "basethread.h"
#include "streamer.h"
#include "flv_live.h"
#include "websockets.h"
#include "flv_stream.h"
#include "authorise.h"
#include "kiwi_ringbuffer.h"

#define FLV_LIVE0_NAME (const char*)"live0.flv"
#define FLV_LIVE1_NAME (const char*)"live1.flv"

static BaseThread sWss;
static BaseThread sFlvVideo0;
static BaseThread sFlvVideo1;
static BaseThread sFlvAudio0;
static FlvLive sFlvLive0(FLV_LIVE0_NAME);
static FlvLive sFlvLive1(FLV_LIVE1_NAME);

static inline FlvStream::Codecs mappingCodecs(KIWI_RB_MEDIA_ENCODE_TYPE codec) {
    switch (codec) {
    case KIWI_RB_MEDIA_ENCODER_FRAME_H264:
    return FlvStream::Codecs::H264;
    case KIWI_RB_MEDIA_ENCODER_FRAME_H265:
    return FlvStream::Codecs::H265;
    case KIWI_RB_MEDIA_ENCODER_FRAME_ALAW:
    return FlvStream::Codecs::G711A;
    case KIWI_RB_MEDIA_ENCODER_FRAME_ULAW:
    return FlvStream::Codecs::G711U;
    case KIWI_RB_MEDIA_ENCODER_FRAME_AAC:
    return FlvStream::Codecs::AAC;
    case KIWI_RB_MEDIA_ENCODER_FRAME_PCM:
    return FlvStream::Codecs::PCMLE;
    default:
    return FlvStream::Codecs::NONE;
    }
}

static inline void createStream() {
    if (!sFlvLive0.isEmpty())
        sFlvVideo0.start();
    if (!sFlvLive1.isEmpty()) 
        sFlvVideo1.start();
    if (!sFlvLive0.isEmpty() || !sFlvLive1.isEmpty()) 
        sFlvAudio0.start();
}

static inline void closedStream() {
   if (sFlvLive0.isEmpty())
        sFlvVideo0.close();
    if (sFlvLive1.isEmpty()) 
        sFlvVideo1.close();
    if (sFlvLive0.isEmpty() && sFlvLive1.isEmpty()) 
        sFlvAudio0.close();
}


void vPortFlvClosure(FlvClient *me) {
    WebSocketsCloseClient(me->getId());
}
bool xPortFlvSendBin(FlvClient *me, void *data, size_t size) {
    return (WebSocketsSendBinary(me->getId(), (const char*)data, (uint64_t)size) >= 0);
}

/**
 * Minimum privilege level allowed to watch the live stream. Mirrors eUserLevels
 * in main.h (Administrator = 0, Operator = 1, Customer = 2); a LARGER number
 * means a LOWER privilege, so "role <= WS_STREAM_MIN_ROLE" == "allowed".
 */
#define WS_STREAM_MIN_ROLE (2 /* Customer */)

/**
 * Pull the session JWT out of a raw WebSocket handshake. Browsers attach the
 * HttpOnly "vivoo_session" cookie automatically on a same-origin ws/wss
 * upgrade, so no front-end change is needed. Native clients that cannot set a
 * Cookie header may instead pass "?access_token=<jwt>" in the request URL.
 */
static std::string wsExtractToken(const char *raw) {
    if (!raw) {
        return std::string();
    }

    /* 1) Cookie: ... vivoo_session=<jwt> ... */
    for (const char *line = raw; line && *line; ) {
        const char *eol = strstr(line, "\r\n");
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        if (len == 0) {
            break; /* reached the blank line that ends the headers */
        }
        if (len > 7 && strncasecmp(line, "Cookie:", 7) == 0) {
            std::string hdr(line + 7, len - 7);
            const std::string key = std::string(JWT_AUTHORISE_SESSION) + "=";
            size_t pos = hdr.find(key);
            if (pos != std::string::npos) {
                pos += key.size();
                size_t end = hdr.find(';', pos);
                std::string v = hdr.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
                    v.erase(v.begin());
                }
                while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) {
                    v.pop_back();
                }
                if (!v.empty()) {
                    return v;
                }
            }
        }
        if (!eol) {
            break;
        }
        line = eol + 2;
    }

    /* 2) Fallback: access_token=<jwt> in the request-line query string */
    const char *lineEnd = strstr(raw, "\r\n");
    const char *q = (const char *)memchr(raw, '?', lineEnd ? (size_t)(lineEnd - raw) : strlen(raw));
    if (q && lineEnd) {
        const char *needle = "access_token=";
        for (const char *s = q; s && s + 13 <= lineEnd; ++s) {
            if (strncmp(s, needle, 13) == 0) {
                const char *b = s + 13;
                const char *e = b;
                while (e < lineEnd && *e != '&' && *e != ' ' && *e != '\r') {
                    ++e;
                }
                return std::string(b, (size_t)(e - b));
            }
        }
    }

    return std::string();
}

static int onWsAuthorise(int cId, const char *raw) {
    (void)cId;

    std::string token = wsExtractToken(raw);
    if (token.empty() || !jwt_authorise_validate_token(token)) {
        CGI_SYSW("WS upgrade rejected: missing or invalid session token\r\n");
        return -1;
    }
    if (jwt_authorise_get_role(token) > WS_STREAM_MIN_ROLE) {
        CGI_SYSW("WS upgrade rejected: insufficient privilege\r\n");
        return -1;
    }
    return 0;
}

static void onWsOpened(int cId) {
    const char *select = WebSocketsGetPath(cId);
    CGI_SYSD("Selected stream: %s\r\n", select ? select : "NULL");
    if (!select) {
        return;
    }
    
    FlvLive *ptr = NULL;

    if (strstr(select, FLV_LIVE0_NAME) != NULL) {
        ptr = &sFlvLive0;
    }
    else if (strstr(select, FLV_LIVE1_NAME) != NULL) {
        ptr = &sFlvLive1;
    }
    if (ptr) {
        CGI_SYSW("Stream opened: %d\r\n", cId);
        ptr->addToStream(cId);
    }
    else {
        CGI_SYSW("Can't opened: %d\r\n", cId);
    }
    createStream();
}

static void onWsClosed(int cId) {
    sFlvLive0.deleteFromStream(cId);
    sFlvLive1.deleteFromStream(cId);
    CGI_SYSW("Stream closed: %d\r\n", cId);
    closedStream();
}

static void onWsHandle(
    int client,
    const unsigned char *message,
    uint64_t msgSize,
    int type) {
    (void)client;
    (void)message;
    (void)msgSize;
    (void)type;
}

void InitStreamer(void) {
    signal(SIGPIPE, SIG_IGN);
    
    /*
        @PREPARE: WebSockets
    */
    sWss.onDoLoop([](bool &envir) {
        const int PORT = 9000;

        WebSocketEvents_t events = {0};
        events.OnOpened = onWsOpened;
        events.OnClosed = onWsClosed;
        events.OnHandle = onWsHandle;
        events.OnAuthorise = onWsAuthorise;
        WebSocketsHandle_t ws = WebSocketsCreate("127.0.0.1", PORT, WS_TIMEOUT_MS, WS_MAX_CLIENTS);
        assert(ws);
        WebSocketsSetEvents(ws, &events);

        while (envir) {
            sleep(1);
        }

        sFlvLive0.cleanup();
        sFlvLive1.cleanup();
        WebSocketsDelete(ws);
    });

    /*
        @PREPARE: Main stream video
    */
    sFlvVideo0.onDoLoop([](bool &envir) {
        KIWI_RB_MEDIA_HANDLE_T consumer = Kiwi_RingBuffer_Create("/tmp/venc0.shm", KIWI_RB_MEDIA_CONSUMER, 250 * 1024);
        if (!consumer) {
            return;
        }
        while (envir) {
            KIWI_RB_MEDIA_FRAMED_S frame {};
            int rc = Kiwi_RingBuffer_ReadFrom(consumer, &frame);
            if (rc != 0) {
                usleep(5000);
                continue;
            }
            /* Ring buffer is designed frame output as length-prefixed style */
            FlvNalList nals {};
            if (!nals.splitLengthPrefixed(frame.pData, frame.dataLen, 4)) {
                /* Invalid */
                continue;
            }

            sFlvLive0.sendFrame(
                mappingCodecs(frame.encoder),
                frame.timestamp != 0 ? frame.timestamp : frame.monotonic,
                frame.type == KIWI_RB_MEDIA_FRAME_HDR_TYPE_I,
                nals
            );

        }
        Kiwi_RingBuffer_Delete(consumer);
    });

    /*
        @PREPARE: Minor stream video
    */
    sFlvVideo1.onDoLoop([](bool &envir) {
        KIWI_RB_MEDIA_HANDLE_T consumer = Kiwi_RingBuffer_Create("/tmp/venc1.shm", KIWI_RB_MEDIA_CONSUMER, 100 * 1024);
        if (!consumer) {
            return;
        }
        while (envir) {
            KIWI_RB_MEDIA_FRAMED_S frame {};
            int rc = Kiwi_RingBuffer_ReadFrom(consumer, &frame);
            if (rc != 0) {
                usleep(5000);
                continue;
            }

            /* Ring buffer is designed frame output as length-prefixed style */
            FlvNalList nals {};
            if (!nals.splitLengthPrefixed(frame.pData, frame.dataLen, 4)) {
                /* Invalid */
                continue;
            }
            sFlvLive1.sendFrame(
                mappingCodecs(frame.encoder),
                frame.timestamp != 0 ? frame.timestamp : frame.monotonic,
                frame.type == KIWI_RB_MEDIA_FRAME_HDR_TYPE_I,
                nals
            );
        }
        Kiwi_RingBuffer_Delete(consumer);
    });

    /*
        @PREPARE: Main/Minor stream audio
    */
    sFlvAudio0.onDoLoop([](bool &envir) {
        KIWI_RB_MEDIA_HANDLE_T consumer = Kiwi_RingBuffer_Create("/tmp/aenc0.shm", KIWI_RB_MEDIA_CONSUMER, 50 * 1024);
        if (!consumer) {
            return;
        }
        while (envir) {
            KIWI_RB_MEDIA_FRAMED_S frame {};
            int rc = Kiwi_RingBuffer_ReadFrom(consumer, &frame);
            if (rc != 0) {
                usleep(5000);
                continue;
            }
            const uint8_t numChannels = 1;
            const uint32_t samplerate = 8000;
            const uint8_t bitsPerSample = 16;

            sFlvLive0.sendFrame(
                mappingCodecs(frame.encoder),
                frame.pData,
                frame.dataLen,
                frame.timestamp,
                samplerate,
                numChannels,
                bitsPerSample
            );
            sFlvLive1.sendFrame(
                mappingCodecs(frame.encoder),
                frame.pData,
                frame.dataLen,
                frame.timestamp,
                samplerate,
                numChannels,
                bitsPerSample
            );
        }
        Kiwi_RingBuffer_Delete(consumer);
    });

    sWss.start();
}
