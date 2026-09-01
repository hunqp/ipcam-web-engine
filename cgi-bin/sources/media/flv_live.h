#ifndef FLV_LIVE_H
#define FLV_LIVE_H

#include <list>
#include <cstddef>
#include <cstdint>
#include <pthread.h>

#include "flv_client.h"
#include "flv_stream.h"

class FlvLive {
public:
     FlvLive(const char *name);
    ~FlvLive();

    const char *name() const;
    bool isEmpty();
    void cleanup();
    void addToStream(int cId);
    void deleteFromStream(int cId);

    /* Send audio frames to all clients */
    void sendFrame(
        FlvStream::Codecs codec, 
        const uint8_t *data,
        size_t size,
        uint64_t timestamp, 
        uint32_t samplerate,
        uint8_t numChannels,
        uint8_t bitsPerSample);
        
    /* Send video frames to all clients */
    void sendFrame(
        FlvStream::Codecs codec, 
        uint64_t timestamp, 
        bool idr, 
        const FlvNalList &nals);

private:
    std::list<FlvClient>::iterator release(std::list<FlvClient>::iterator it, bool needToCloseSockets);
    void createVpsSpsPpsCache(FlvStream::Codecs codec, const FlvNalList &nals);

private:
    const char *mName;
    FlvNalUnit mVPSCache {};
    FlvNalUnit mSPSCache {};
    FlvNalUnit mPPSCache {};
    pthread_mutex_t mMutex;
    std::list<FlvClient> mCIDs {};
    FlvStream::Codecs mCodec = FlvStream::Codecs::NONE;
};

#endif /* FLV_LIVE_H */
