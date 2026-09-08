/*
 * Copyright (C) 2016-2023  Davidson Francis <davidsondfgl@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include <netdb.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include "utf8.h"
#include "websockets.h"

#define PR_SEND(Client, buf, len)  SendAll((Client), (buf), (len), MSG_NOSIGNAL)
#define PR_RECV(sfd, buf, len)  recv((sfd)->ClientSock, (buf), (len), 0)

extern int GetHandshakeResponse(char *hsrequest, char **hsresponse);

typedef struct PeerConnection_t PeerConnection_t;

struct lw_wss_t {
    const char *Host;
    uint16_t Port;
    uint32_t TimeoutMs;
    uint8_t Total;
    lw_wss_events_t Events;
    bool RunningFlag;
    pthread_t Thread;
    int Sock;
    pthread_mutex_t ClientsMutex;
    PeerConnection_t *Clients;
    struct lw_wss_t *Next;
};

struct PeerConnection_t {
    int ClientSock;
    int State;
    struct lw_wss_t *ServerRef;
    pthread_mutex_t StateMutex;
    pthread_cond_t CloseCond;
    pthread_t TimeoutThread;
    bool CloseThread;
    pthread_mutex_t SendMutex;
    int32_t LastPongId;
    int32_t CurrentPingId;
    pthread_mutex_t PingMutex;
    int ClientId;
    char Path[128];
};

typedef struct {
    unsigned char Frame[configMESSAGE_LENGTH];
    unsigned char *Message;
    unsigned char ControlMessage[125];
    size_t CurrentPosition;
    size_t BytesRead;
    int FrameType;
    uint64_t FrameSize;
    int Error;
    PeerConnection_t *Client;
} FrameData_t;

#define CLIENT_VALID(cli)                                                  \
    ((cli) != NULL && (cli)->ServerRef != NULL &&                         \
    (cli) >= &(cli)->ServerRef->Clients[0] &&                           \
    (cli) < &(cli)->ServerRef->Clients[(cli)->ServerRef->Total] &&  \
    (cli)->ClientSock > -1)

static uint32_t GenerateId = 1;
static lw_wss_handle_t ServerList = NULL; 
static pthread_mutex_t DefaultGeneralMutex = PTHREAD_MUTEX_INITIALIZER;

static PeerConnection_t *FindClientById(int sfd) {
    PeerConnection_t *Client = NULL;

    pthread_mutex_lock(&DefaultGeneralMutex);
    for (lw_wss_handle_t ServerRef = ServerList; ServerRef; ServerRef = ServerRef->Next) {
        pthread_mutex_lock(&ServerRef->ClientsMutex);
        for (uint8_t id = 0; id < ServerRef->Total; ++id) {
            if (ServerRef->Clients[id].ClientId == sfd) {
                Client = &ServerRef->Clients[id];
                break;
            }
        }
        pthread_mutex_unlock(&ServerRef->ClientsMutex);
        if (Client) {
            break;
        }
    }
    pthread_mutex_unlock(&DefaultGeneralMutex);
    return Client;
}

static void CloseSocketFd(int fd) {
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}

static void SaveRequestPath(PeerConnection_t *Client, const char *request) {
    const char *methodEnd = strchr(request, ' ');
    if (!methodEnd) {
        Client->Path[0] = '\0';
        return;
    }

    const char *pathStart = methodEnd + 1;
    const char *pathEnd = strchr(pathStart, ' ');
    if (!pathEnd || pathEnd <= pathStart) {
        Client->Path[0] = '\0';
        return;
    }

    size_t len = (size_t)(pathEnd - pathStart);
    if (len >= sizeof(Client->Path)) {
        len = sizeof(Client->Path) - 1;
    }
    memcpy(Client->Path, pathStart, len);
    Client->Path[len] = '\0';
}

static int CreateThread(pthread_t *Thread, void *(*start)(void *), void *arg) {
    pthread_attr_t attr;
    size_t stackSize = configTHREAD_STACK_SIZE;
    int ret = pthread_attr_init(&attr);

    if (ret != 0) {
        return pthread_create(Thread, NULL, start, arg);
    }

#ifdef PTHREAD_STACK_MIN
    if (stackSize < PTHREAD_STACK_MIN) {
        stackSize = PTHREAD_STACK_MIN;
    }
#endif

    ret = pthread_attr_setstacksize(&attr, stackSize);
    if (ret != 0) {
        pthread_attr_destroy(&attr);
        return pthread_create(Thread, NULL, start, arg);
    }

    ret = pthread_create(Thread, &attr, start, arg);
    pthread_attr_destroy(&attr);
    return ret;
}

static int GetState(PeerConnection_t *Client) {
    int State;

    if (!CLIENT_VALID(Client)) {
        return (-1);
    }

    pthread_mutex_lock(&Client->StateMutex);
    State = Client->State;
    pthread_mutex_unlock(&Client->StateMutex);
    return (State);
}

static int SetState(PeerConnection_t *Client, int State) {
    if (!CLIENT_VALID(Client)) {
        return (-1);
    }

    if (State < configSTATE_CONNECTING || State > configSTATE_CLOSED) {
        return (-1);
    }

    pthread_mutex_lock(&Client->StateMutex);
    Client->State = State;
    pthread_mutex_unlock(&Client->StateMutex);
    return (0);
}

static ssize_t SendAll(
    PeerConnection_t *Client, const void *buf, size_t len, int flags) {
    const char *p;
    ssize_t ret;
    ssize_t r;

    ret = 0;

    if (!CLIENT_VALID(Client))
        return (-1);

    p = buf;
    pthread_mutex_lock(&Client->SendMutex);
    while (len) {
        r = send(Client->ClientSock, p, len, flags);
        if (r == -1) {
            pthread_mutex_unlock(&Client->SendMutex);
            return (-1);
        }
        p   += r;
        len -= r;
        ret += r;
    }
    pthread_mutex_unlock(&Client->SendMutex);
    return (ret);
}

static ssize_t SendAllIov(PeerConnection_t *Client, const struct iovec *iov,
    int iovcnt, int flags) {
    struct iovec vec[2];
    struct msghdr Message;
    ssize_t ret = 0;

    if (!CLIENT_VALID(Client) || iovcnt < 1 || iovcnt > 2)
        return (-1);

    memcpy(vec, iov, sizeof(*iov) * (size_t)iovcnt);
    memset(&Message, 0, sizeof(Message));
    Message.msg_iov = vec;
    Message.msg_iovlen = (size_t)iovcnt;

    pthread_mutex_lock(&Client->SendMutex);
    while (Message.msg_iovlen > 0) {
        ssize_t n = sendmsg(Client->ClientSock, &Message, flags);
        if (n <= 0) {
            pthread_mutex_unlock(&Client->SendMutex);
            return (-1);
        }
        ret += n;
        while (Message.msg_iovlen > 0 && (size_t)n >= Message.msg_iov[0].iov_len) {
            n -= (ssize_t)Message.msg_iov[0].iov_len;
            Message.msg_iov++;
            Message.msg_iovlen--;
        }
        if (Message.msg_iovlen > 0 && n > 0) {
            Message.msg_iov[0].iov_base = (char *)Message.msg_iov[0].iov_base + n;
            Message.msg_iov[0].iov_len -= (size_t)n;
        }
    }
    pthread_mutex_unlock(&Client->SendMutex);
    return (ret);
}

static void CloseClient(PeerConnection_t *Client, int lock) {
    if (!CLIENT_VALID(Client)) {
        return;
    }
    SetState(Client, configSTATE_CLOSED);
    CloseSocketFd(Client->ClientSock);

    if (lock) {
        pthread_mutex_lock(&Client->ServerRef->ClientsMutex);
    }
    Client->ClientSock = -1;
    pthread_cond_destroy(&Client->CloseCond);
    pthread_mutex_destroy(&Client->StateMutex);
    pthread_mutex_destroy(&Client->SendMutex);
    pthread_mutex_destroy(&Client->PingMutex);
    if (lock) {
        pthread_mutex_unlock(&Client->ServerRef->ClientsMutex);
    }
}

static void *CloseTimeout(void *p) {
    PeerConnection_t *conn = p;
    struct timespec ts;
    int State;

    pthread_mutex_lock(&conn->StateMutex);

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += WS_MS_TO_NS(WS_TIMEOUT_MS);

    while (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    while (conn->State != configSTATE_CLOSED &&
           pthread_cond_timedwait(&conn->CloseCond, &conn->StateMutex, &ts) !=
               ETIMEDOUT)
        ;

    State = conn->State;
    pthread_mutex_unlock(&conn->StateMutex);

    if (State == configSTATE_CLOSED)
        goto ESCAPE;

    CloseClient(conn, 1);
ESCAPE:
    return (NULL);
}

static int StartCloseTimeout(PeerConnection_t *Client) {
    int ret = 0;

    if (!CLIENT_VALID(Client))
        return (-1);

    pthread_mutex_lock(&Client->StateMutex);

    if (Client->State != configSTATE_OPEN) {
        ret = -1;
        goto ESCAPE;
    }

    Client->State = configSTATE_CLOSING;

    if (CreateThread(&Client->TimeoutThread, CloseTimeout, Client)) {
        ret = -1;
        goto ESCAPE;
    }
    Client->CloseThread = true;
ESCAPE:
    pthread_mutex_unlock(&Client->StateMutex);
    return ret;
}

static int SendFrame(PeerConnection_t *Client, const char *Message, uint64_t size, int type) {
    unsigned char frame[10];
    uint8_t headerLength;
    struct iovec iov[2];
    uint64_t length;

    if (!CLIENT_VALID(Client))
        return (-1);

    frame[0] = (configFIN | type);
    length = (uint64_t)size;

    if (length <= 125) {
        frame[1] = length & 0x7F;
        headerLength = 2;
    }

    else if (length >= 126 && length <= 65535) {
        frame[1] = 126;
        frame[2] = (length >> 8) & 255;
        frame[3] = length & 255;
        headerLength = 4;
    }

    else {
        frame[1] = 127;
        frame[2] = (unsigned char)((length >> 56) & 255);
        frame[3] = (unsigned char)((length >> 48) & 255);
        frame[4] = (unsigned char)((length >> 40) & 255);
        frame[5] = (unsigned char)((length >> 32) & 255);
        frame[6] = (unsigned char)((length >> 24) & 255);
        frame[7] = (unsigned char)((length >> 16) & 255);
        frame[8] = (unsigned char)((length >> 8) & 255);
        frame[9] = (unsigned char)(length & 255);
        headerLength = 10;
    }

    if (length > SIZE_MAX)
        return (-1);

    iov[0].iov_base = frame;
    iov[0].iov_len = headerLength;
    iov[1].iov_base = (void *)Message;
    iov[1].iov_len = (size_t)length;

    return (int)SendAllIov(Client, iov, 2, MSG_NOSIGNAL);
}

int lw_wss_send_str(int Client, const char *Message, uint64_t size) {
    return SendFrame(FindClientById(Client), Message, size, configFR_OP_TXT);
}

int lw_wss_send_bin(int Client, const char *Message, uint64_t size) {
    return SendFrame(FindClientById(Client), Message, size, configFR_OP_BIN);
}

int lw_wss_close_peer(int Client) {
    PeerConnection_t *cli = FindClientById(Client);

    unsigned char closeCode[2];
    int cc;

    if (!CLIENT_VALID(cli) || cli->ClientSock == -1)
        return (-1);

    cc = configCLSE_NORMAL;
    closeCode[0] = (cc >> 8);
    closeCode[1] = (cc & 0xFF);
    if (SendFrame(cli, (const char *)closeCode, sizeof(closeCode), configFR_OP_CLSE) < 0) {
        shutdown(cli->ClientSock, SHUT_RDWR);
        return (-1);
    }

    if (StartCloseTimeout(cli) < 0) {
        shutdown(cli->ClientSock, SHUT_RDWR);
        return (-1);
    }
    return (0);
}

const char *lw_wss_get_path(int sfd) {
    PeerConnection_t *Client = FindClientById(sfd);
    if (!CLIENT_VALID(Client)) {
        return NULL;
    }
    return Client->Path;
}

static inline int IsControlFrame(int frame) {
    return (frame == configFR_OP_CLSE || frame == configFR_OP_PING || frame == configFR_OP_PONG);
}

static int Handshake(FrameData_t *frame) {
    char *response;
    char *p;
    ssize_t n;

    if ((n = PR_RECV(frame->Client, frame->Frame, sizeof(frame->Frame) - 1)) < 0)
        return (-1);

    /* NUL-terminate so the string scans below (and the auth hook) stay in bounds
     * even when this Client slot is being reused from a previous connection. */
    frame->Frame[n] = '\0';

    p = strstr((const char *)frame->Frame, "\r\n\r\n");
    if (p == NULL) {

        return (-1);
    }
    frame->BytesRead = n;
    frame->CurrentPosition = (size_t)((ptrdiff_t)(p - (char *)frame->Frame)) + 4;
    SaveRequestPath(frame->Client, (const char *)frame->Frame);

    /* Authorise the upgrade before we commit to it. Must run before
     * GetHandshakeResponse(), which tokenises frame->Frame in place. */
    if (frame->Client->ServerRef->Events.OnAuthorise &&
        frame->Client->ServerRef->Events.OnAuthorise(
            frame->Client->ClientId, (const char *)frame->Frame) != 0) {
        static const char deny[] =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Connection: close\r\n"
            "Content-Length: 0\r\n"
            "\r\n";
        PR_SEND(frame->Client, deny, sizeof(deny) - 1);
        return (-1);
    }

    if (GetHandshakeResponse((char *)frame->Frame, &response) < 0) {
        return (-1);
    }

    if (PR_SEND(frame->Client, response, strlen(response)) < 0) {
        free(response);

        return (-1);
    }

    SetState(frame->Client, configSTATE_OPEN);

    if (frame->Client->ServerRef->Events.OnOpened) {
        frame->Client->ServerRef->Events.OnOpened(frame->Client->ClientId);
    }
    free(response);
    return (0);
}

static int SendCloseFrame(FrameData_t *frame, int closeCode) {
    int cc;

    if (closeCode != -1) {
        cc = closeCode;
        goto CUSTOM_CLOSE;
    }

    if (frame->FrameSize == 0 || frame->FrameSize > 2)
        goto SEND;

    if (frame->FrameSize == 1)
        cc = frame->ControlMessage[0];
    else
        cc = ((int)frame->ControlMessage[0]) << 8 | frame->ControlMessage[1];

    if ((cc < 1000 || cc > 1003) && (cc < 1007 || cc > 1011) &&
        (cc < 3000 || cc > 4999)) {
        cc = configCLSE_PROTERR;

    CUSTOM_CLOSE:
        frame->ControlMessage[0] = (cc >> 8);
        frame->ControlMessage[1] = (cc & 0xFF);

        if (SendFrame(frame->Client, (const char *)frame->ControlMessage, sizeof(char) * 2,
            configFR_OP_CLSE) < 0) {

            return (-1);
        }
        return (0);
    }

SEND:
    if (SendFrame(frame->Client, (const char *)frame->ControlMessage, frame->FrameSize,
        configFR_OP_CLSE) < 0) {

        return (-1);
    }
    return (0);
}

static inline int NextByte(FrameData_t *frame) {
    ssize_t n;

    if (frame->CurrentPosition == 0 || frame->CurrentPosition == frame->BytesRead) {
        if ((n = PR_RECV(frame->Client, frame->Frame, sizeof(frame->Frame))) <= 0) {
            frame->Error = 1;

            return (-1);
        }
        frame->BytesRead = (size_t)n;
        frame->CurrentPosition = 0;
    }
    return (frame->Frame[frame->CurrentPosition++]);
}

static bool CheckedAddU64(uint64_t a, uint64_t b, uint64_t *out) {
    if (a > UINT64_MAX - b)
        return (false);
    *out = a + b;
    return (true);
}

typedef struct {
    unsigned char *messageData;
    unsigned char *ControlMessage;
    uint8_t dataMasks[4];
    uint8_t controlMasks[4];
    uint64_t dataMessageIndex;
    uint64_t controlMessageIndex;
    uint64_t frameLength;
    uint64_t FrameSize;
#ifdef VALIDATE_UTF8
    uint32_t utf8_state;
#endif
    int32_t pongId;
    uint8_t opcode;
    uint8_t isFin;
    uint8_t mask;
    int currentByte;
} FrameState;

static int ValidateUtf8Text(FrameData_t *frame,
    FrameState *fsd) {
#ifndef VALIDATE_UTF8
    (void)frame;
    (void)fsd;
#endif
#ifdef VALIDATE_UTF8
    if (frame->FrameType != configFR_OP_TXT)
        return (0);

    if (fsd->isFin) {
        if (Utf8IsValidLengthState(
            fsd->messageData + (fsd->dataMessageIndex - fsd->frameLength),
            fsd->frameLength, fsd->utf8_state) != utf8ACCEPT) {

            frame->Error = 1;
            SendCloseFrame(frame, configCLSE_INVUTF8);
        }

        return (0);
    }

    fsd->utf8_state =
        Utf8IsValidLengthState(fsd->messageData +
            (fsd->dataMessageIndex - fsd->frameLength),
            fsd->frameLength, fsd->utf8_state);

    if (fsd->utf8_state == utf8REJECT) {

        frame->Error = 1;
        SendCloseFrame(frame, configCLSE_INVUTF8);
    }
#endif
    return (0);
}

static int HandlePongFrame(FrameData_t *frame,
    FrameState *fsd) {
    fsd->isFin = 0;

    if (fsd->FrameSize != sizeof(frame->Client->LastPongId))
        return (0);

    pthread_mutex_lock(&frame->Client->PingMutex);
    fsd->pongId = (fsd->ControlMessage[3] << 0) |
        (fsd->ControlMessage[2] << 8) |
        (fsd->ControlMessage[1] << 16) |
        (fsd->ControlMessage[0] << 24);
    if (fsd->pongId < 0 || fsd->pongId > frame->Client->CurrentPingId) {
        pthread_mutex_unlock(&frame->Client->PingMutex);
        return (0);
    }
    frame->Client->LastPongId = fsd->pongId;
    pthread_mutex_unlock(&frame->Client->PingMutex);

    return (0);
}

static int HandleCloseFrame(FrameData_t *frame,
    FrameState *fsd) {
#ifdef VALIDATE_UTF8
    if (fsd->FrameSize > 2 &&
        !Utf8IsValidLength(fsd->ControlMessage + 2, fsd->FrameSize - 2)) {

        frame->Error = 1;
        return (-1);
    }
#endif

    frame->FrameSize = fsd->FrameSize;
    frame->FrameType = configFR_OP_CLSE;
    free(fsd->messageData);
    return (0);
}

static int ReadSingleFrame(FrameData_t *frame,
    FrameState *fsd) {
    uint64_t *FrameSize;
    uint64_t next_size = 0;
    uint64_t alloc_size = 0;
    unsigned char *tmp;
    unsigned char *Message;
    uint64_t *msg_idx;
    uint8_t *masks;
    int currentByte;
    uint64_t i;

    if (IsControlFrame(fsd->opcode)) {
        FrameSize = &fsd->FrameSize;
        msg_idx = &fsd->controlMessageIndex;
        masks   = fsd->controlMasks;
        Message     = fsd->ControlMessage;
    }
    else {
        FrameSize = &frame->FrameSize;
        msg_idx = &fsd->dataMessageIndex;
        masks   = fsd->dataMasks;
        Message     = fsd->messageData;
    }

    if (fsd->frameLength == 126)
        fsd->frameLength = (((uint64_t)NextByte(frame)) << 8) | NextByte(frame);

    else if (fsd->frameLength == 127) {
        fsd->frameLength =
            (((uint64_t)NextByte(frame)) << 56) |
            (((uint64_t)NextByte(frame)) << 48) |
            (((uint64_t)NextByte(frame)) << 40) |
            (((uint64_t)NextByte(frame)) << 32) |
            (((uint64_t)NextByte(frame)) << 24) |
            (((uint64_t)NextByte(frame)) << 16) |
            (((uint64_t)NextByte(frame)) << 8)  |
            (((uint64_t)NextByte(frame)));
    }

    if (!CheckedAddU64(*FrameSize, fsd->frameLength, &next_size) ||
        next_size > configMAX_FRAME_LENGTH) {

        SendCloseFrame(frame, configCLSE_BIGMSG);
        frame->Error = 1;
        return (-1);
    }

    *FrameSize = next_size;

    masks[0] = NextByte(frame);
    masks[1] = NextByte(frame);
    masks[2] = NextByte(frame);
    masks[3] = NextByte(frame);

    if (frame->Error)
        return (-1);

    if (fsd->frameLength > 0) {
        if (!IsControlFrame(fsd->opcode)) {
            if (!CheckedAddU64(*msg_idx, fsd->frameLength, &alloc_size) ||
                !CheckedAddU64(alloc_size, fsd->isFin, &alloc_size) ||
                alloc_size > configMAX_FRAME_LENGTH + 1) {

                SendCloseFrame(frame, configCLSE_BIGMSG);
                frame->Error = 1;
                return (-1);
            }

            tmp = realloc(Message, alloc_size);
            if (!tmp) {

                SendCloseFrame(frame, configCLSE_BIGMSG);
                frame->Error = 1;
                return (-1);
            }
            Message = tmp;
            fsd->messageData = Message;
        }

        for (i = 0; i < fsd->frameLength; i++, (*msg_idx)++) {
            currentByte = NextByte(frame);
            if (currentByte == -1)
                return (-1);

            Message[*msg_idx] = currentByte ^ masks[i % 4];
        }
    }

    if (fsd->isFin && *FrameSize > 0) {
        if (!fsd->frameLength && !IsControlFrame(fsd->opcode)) {
            tmp = realloc(Message, *msg_idx + 1);
            if (!tmp) {

                frame->Error = 1;
                return (-1);
            }
            Message = tmp;
            fsd->messageData = Message;
        }
        Message[*msg_idx] = '\0';
    }

    return (0);
}

static int NextCompleteFrame(FrameData_t *frame) {
    FrameState fsd = {0};
    fsd.messageData = NULL;
    fsd.ControlMessage = frame->ControlMessage;

    #ifdef VALIDATE_UTF8
    fsd.utf8_state = utf8ACCEPT;
    #endif

    frame->FrameSize =  0;
    frame->FrameType = -1;
    frame->Message = NULL;

    do {
        fsd.currentByte = NextByte(frame);
        if (fsd.currentByte == -1)
            return (-1);

        fsd.isFin = (fsd.currentByte & 0xFF) >> configFIN_SHIFT;
        fsd.opcode = (fsd.currentByte & 0xF);

        if (fsd.currentByte & 0x70) {

            frame->Error = 1;
            break;
        }

        if ((frame->FrameType == -1 && fsd.opcode == configFR_OP_CONT) ||
            (frame->FrameType != -1 && !IsControlFrame(fsd.opcode) &&
                fsd.opcode != configFR_OP_CONT)) {

            frame->Error = 1;
            break;
        }

        if (fsd.opcode != configFR_OP_TXT &&
            fsd.opcode != configFR_OP_BIN &&
            fsd.opcode != configFR_OP_CONT &&
            fsd.opcode != configFR_OP_PING &&
            fsd.opcode != configFR_OP_PONG &&
            fsd.opcode != configFR_OP_CLSE) {

            frame->FrameType = fsd.opcode;
            frame->Error = 1;
            break;
        }

        if (GetState(frame->Client) == configSTATE_CLOSING &&
            fsd.opcode != configFR_OP_CLSE) {

            frame->Error = 1;
            break;
        }

        if (fsd.opcode != configFR_OP_CONT && !IsControlFrame(fsd.opcode))
            frame->FrameType = fsd.opcode;

        fsd.mask         = NextByte(frame);
        fsd.frameLength = fsd.mask & 0x7F;
        fsd.FrameSize   = 0;
        fsd.controlMessageIndex = 0;

        if (IsControlFrame(fsd.opcode) &&
            (!fsd.isFin || fsd.frameLength > 125)) {

            frame->Error = 1;
            break;
        }

        if (ReadSingleFrame(frame, &fsd) < 0)
            break;

        switch (fsd.opcode) {
            case configFR_OP_CONT:
            case configFR_OP_TXT: {
                ValidateUtf8Text(frame, &fsd);
                break;
            }
            case configFR_OP_PONG: {
                HandlePongFrame(frame, &fsd);
                break;
            }
            case configFR_OP_PING: {
                if (SendFrame(frame->Client, (const char *)frame->ControlMessage, fsd.FrameSize,
                    configFR_OP_PONG) < 0) {
                    frame->Error = 1;

                    goto DONE;
                }
                fsd.isFin = 0;
                break;
            }
            case configFR_OP_CLSE: {
                if (HandleCloseFrame(frame, &fsd) < 0)
                    goto DONE;
                return (0);
            }
        }

    } while (!fsd.isFin && !frame->Error);

DONE:
    if (frame->Error) {
        free(fsd.messageData);
        frame->Message = NULL;
        return (-1);
    }

    frame->Message = fsd.messageData;
    return (0);
}
static void *ServerListener(void *args) {
    FrameData_t frame = {0};
    PeerConnection_t *Client = args;
    frame.Client = Client;

    if (Handshake(&frame) < 0) {
        goto FORCE_CLOSE;
    }

    while (NextCompleteFrame(&frame) >= 0) {
        if ((frame.FrameType == configFR_OP_TXT || frame.FrameType == configFR_OP_BIN) && !frame.Error) {
            if (Client->ServerRef->Events.OnHandle) {
                Client->ServerRef->Events.OnHandle(Client->ClientId, frame.Message, frame.FrameSize, frame.FrameType);
            }
        }
        else if (frame.FrameType == configFR_OP_CLSE && !frame.Error) {
            if (GetState(Client) != configSTATE_CLOSING) {
                SetState(Client, configSTATE_CLOSING);
                SendCloseFrame(&frame, -1);
            }
            free(frame.Message);
            break;
        }
        free(frame.Message);
        frame.Message = NULL;
    }

    if (Client->ServerRef->Events.OnClosed) {
        Client->ServerRef->Events.OnClosed(Client->ClientId);
    }

FORCE_CLOSE:
    if (Client->CloseThread) {
        pthread_cond_signal(&Client->CloseCond);
        pthread_join(Client->TimeoutThread, NULL);
    }
    if (GetState(Client) != configSTATE_CLOSED) {
        CloseClient(Client, 1);
    }

    return NULL;
}

static void *AcceptPeerConnection(void *args) {
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    lw_wss_handle_t ServerRef = (lw_wss_handle_t)args;

    while (ServerRef->RunningFlag) {
        int newSocket = accept(ServerRef->Sock, (struct sockaddr*)&sa, &salen);
        if (newSocket < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!ServerRef->RunningFlag) {
                break;
            }
            struct timespec retryDelay = {0, 10 * 1000 * 1000};
            nanosleep(&retryDelay, NULL);
            continue;
        }

        if (ServerRef->TimeoutMs) {
            struct timeval tv = {
                .tv_sec  = ServerRef->TimeoutMs / 1000,
                .tv_usec = (ServerRef->TimeoutMs % 1000) * 1000
            };
            setsockopt(newSocket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
        }

        #ifdef TCP_NODELAY
            int flag = 1;
            setsockopt(newSocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
#endif

        pthread_mutex_lock(&DefaultGeneralMutex);
        pthread_mutex_lock(&ServerRef->ClientsMutex);
        uint8_t id = 0;
        for (id = 0; id < ServerRef->Total; ++id) {
            PeerConnection_t *ptr = &ServerRef->Clients[id];
            if (ptr->ClientSock == -1) {
                ptr->ServerRef = ServerRef;
                ptr->ClientSock = newSocket;
                ptr->State = configSTATE_CONNECTING;
                ptr->CloseThread = false;
                ptr->LastPongId = -1;
                ptr->CurrentPingId = -1;
                ptr->ClientId = GenerateId++;
                pthread_mutex_init(&ptr->StateMutex, NULL);
                pthread_mutex_init(&ptr->SendMutex, NULL);
                pthread_mutex_init(&ptr->PingMutex, NULL);
                pthread_cond_init(&ptr->CloseCond, NULL);
                break;
            }
        }
        pthread_mutex_unlock(&ServerRef->ClientsMutex);
        pthread_mutex_unlock(&DefaultGeneralMutex);

        if (id != ServerRef->Total) {
            pthread_t pid = 0;
            CreateThread(&pid, ServerListener, &ServerRef->Clients[id]);
            pthread_detach(pid);
        }
        else {
            CloseSocketFd(newSocket);
        }
    }

    return NULL;
}

static int BindSocket(lw_wss_handle_t ServerRef) {
    struct addrinfo hints = {
        .ai_flags    = AI_PASSIVE,
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM
    };
    char portString[8] = {0};
    snprintf(portString, sizeof(portString), "%u", ServerRef->Port);

    struct addrinfo *res = NULL;
    if (getaddrinfo(ServerRef->Host, portString, &hints, &res) != 0) {
        return -1;
    }

    int fd = -1, reuse = 1;
    for (struct addrinfo *r = res; r; r = r->ai_next) {
        fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (fd < 0) {
            continue;
        }
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (bind(fd, r->ai_addr, r->ai_addrlen) == 0) {
            break;
        }
        close(fd); 
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        return -2;
    }
    return fd;
}

static lw_wss_handle_t CreateServer(
    const char *Host,
    uint16_t Port,
    uint32_t TimeoutMs,
    uint8_t Total,
    const lw_wss_events_t *events) {
    if (Total == 0) {
        return NULL;
    }

    lw_wss_handle_t ServerRef = (lw_wss_handle_t)calloc(1, sizeof(*ServerRef));
    if (!ServerRef) {
        return NULL;
    }

    ServerRef->Host = Host;
    ServerRef->Port = Port;
    ServerRef->TimeoutMs = TimeoutMs;
    ServerRef->Total = Total;
    ServerRef->Sock = -1;
    if (events) {
        ServerRef->Events = *events;
    }

    ServerRef->Clients = (PeerConnection_t *)calloc(Total, sizeof(*ServerRef->Clients));
    if (!ServerRef->Clients) {
        free(ServerRef);
        return NULL;
    }

    for (uint8_t id = 0; id < Total; ++id) {
        ServerRef->Clients[id].ClientSock = -1;
        ServerRef->Clients[id].ServerRef = ServerRef;
    }

    pthread_mutex_init(&ServerRef->ClientsMutex, NULL);

    int fd = BindSocket(ServerRef);
    if (fd < 0) {
        pthread_mutex_destroy(&ServerRef->ClientsMutex);
        free(ServerRef->Clients);
        free(ServerRef);
        return NULL;
    }

    if (listen(fd, Total) < 0) {
        close(fd);
        pthread_mutex_destroy(&ServerRef->ClientsMutex);
        free(ServerRef->Clients);
        free(ServerRef);
        return NULL;
    }

    ServerRef->Sock = fd;
    ServerRef->RunningFlag = true;
    pthread_mutex_lock(&DefaultGeneralMutex);
    ServerRef->Next = ServerList;
    ServerList = ServerRef;
    pthread_mutex_unlock(&DefaultGeneralMutex);

    if (CreateThread(&ServerRef->Thread, AcceptPeerConnection, ServerRef)) {
        pthread_mutex_lock(&DefaultGeneralMutex);
        if (ServerList == ServerRef) {
            ServerList = ServerRef->Next;
        } else {
            for (lw_wss_handle_t it = ServerList; it; it = it->Next) {
                if (it->Next == ServerRef) {
                    it->Next = ServerRef->Next;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&DefaultGeneralMutex);
        ServerRef->RunningFlag = false;
        CloseSocketFd(ServerRef->Sock);
        pthread_mutex_destroy(&ServerRef->ClientsMutex);
        free(ServerRef->Clients);
        free(ServerRef);
        return NULL;
    }

    return ServerRef;
}

lw_wss_handle_t lw_wss_create(const char *Host, uint16_t Port, uint32_t TimeoutMs, uint8_t Total) {
    return CreateServer(Host, Port, TimeoutMs, Total, NULL);
}

void lw_wss_set_events(lw_wss_handle_t handle, const lw_wss_events_t *events) {
    if (handle && events) {
        handle->Events = *events;
    }
}

void lw_wss_delete(lw_wss_handle_t handle) {
    if (!handle) {
        return;
    }

    handle->RunningFlag = false;
    CloseSocketFd(handle->Sock);
    handle->Sock = -1;
    pthread_join(handle->Thread, NULL);

    pthread_mutex_lock(&DefaultGeneralMutex);
    if (ServerList == handle) {
        ServerList = handle->Next;
    } else {
        for (lw_wss_handle_t it = ServerList; it; it = it->Next) {
            if (it->Next == handle) {
                it->Next = handle->Next;
                break;
            }
        }
    }
    pthread_mutex_unlock(&DefaultGeneralMutex);

    pthread_mutex_destroy(&handle->ClientsMutex);
    free(handle->Clients);
    free(handle);
}
