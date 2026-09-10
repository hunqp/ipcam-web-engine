#ifndef FLV_CLIENT_H
#define FLV_CLIENT_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include "flv_stream.h"

class FlvClient {
public:
    /**
     * Transport used to reach the peer behind an FlvClient. FlvClient only
     * knows about this plain function-pointer interface, never about the
     * concrete transport (WebSocket, TCP, ...) that implements it - the
     * integration layer (streamer.cpp) is the only place allowed to know
     * both sides and wire them together via setTransport().
     */
    struct Transport {
        void (*closePeer)(int clientId) = nullptr;
        bool (*sendBinary)(int clientId, const void *data, size_t size) = nullptr;
    };

    /** Installs the transport shared by every FlvClient. Call once at startup,
     *  before any client can be created, from the integration layer. */
    static void setTransport(const Transport &transport);

     explicit FlvClient(int cid);
    ~FlvClient() = default;

    int getId();
    void closure();
    bool isWaitIdr() const;
    bool hasBaseTimestamp() const;
    uint64_t getBaseTimestamp() const;
    void setBaseTimestamp(uint64_t timestamp);
    bool needsVideoHeaders(FlvStream::Codecs codec) const;
    
    void setVideoHeaders(
        FlvStream::Codecs codec,
        const FlvNalUnit *vps,
        const FlvNalUnit *sps,
        const FlvNalUnit *pps);

    /* Write video frames */
    bool writeFrame(
        FlvStream::Codecs codec, 
        const FlvNalList &frame, 
        uint32_t timestamp);

    /* Write audio frames */
    bool writeFrame(
        FlvStream::Codecs codec,
        uint32_t samplerate,
        uint8_t numChannels,
        uint8_t bitsPerSample,
        const uint8_t *data,
        size_t size,
        uint32_t timestamp);

private:
    bool writeHeader();
    bool writeSequenceHeader();
    bool sendBinary(const void *data, size_t size);
    static bool outgoing(const uint8_t *data, size_t size, void *user);

private:
    static Transport sTransport;

    int mId = -1;
    FlvNalUnit mVps {};
    FlvNalUnit mSps {};
    FlvNalUnit mPps {};
    FlvStream mStream {};
    bool mWaitIdr = true;
    uint64_t mBaseTimestamp = 0;
    bool mIsHeaderWritten = false;
    bool mIsSequenceHeaderWritten = false;
    FlvStream::Codecs mVideoCodec = FlvStream::Codecs::NONE;
    FlvStream::Codecs mAudioCodec = FlvStream::Codecs::NONE;
    
    struct {
        uint8_t sequenceHeaderSize = 0;
        uint8_t sequenceHeaders[2] = {0, 0};
        bool isSequenceHeaderWritten = false;
    } mAAC;
};

#endif /* FLV_CLIENT_H */
