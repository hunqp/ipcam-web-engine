#include "flv_live.h"

static uint32_t u32FlvTimestamp(uint64_t timestamp, uint64_t u64BaseTimestamp) {
    if (timestamp == 0 || timestamp < u64BaseTimestamp) {
        return 0;
    }
    uint64_t delta = timestamp - u64BaseTimestamp;
    if (u64BaseTimestamp > 100000000000000000ULL) {
        delta /= 1000000;
    } else if (u64BaseTimestamp > 100000000000000ULL) {
        delta /= 1000;
    }
    return delta > 0xffffffffULL ? 0xffffffffu : (uint32_t)delta;
}

FlvLive::FlvLive(const char *name) : mName(name) {
    pthread_mutex_init(&mMutex, NULL);
}

FlvLive::~FlvLive() {
    cleanup();
    pthread_mutex_destroy(&mMutex);
}

const char *FlvLive::name() const {
    return mName;
}

bool FlvLive::isEmpty() {
    pthread_mutex_lock(&mMutex);

    bool empty = mCIDs.empty();

    pthread_mutex_unlock(&mMutex);
    
    return empty;
}

void FlvLive::cleanup() {
    pthread_mutex_lock(&mMutex);

    while (!mCIDs.empty()) {
        release(mCIDs.begin(), true);
    }

    pthread_mutex_unlock(&mMutex);
}

void FlvLive::addToStream(int cId) {
    pthread_mutex_lock(&mMutex);

    mCIDs.emplace_front(cId);
    mCIDs.front().setVideoHeaders(mCodec, &mVPSCache, &mSPSCache, &mPPSCache);

    pthread_mutex_unlock(&mMutex);
}

void FlvLive::deleteFromStream(int cId) {
    pthread_mutex_lock(&mMutex);

    for (auto it = mCIDs.begin(); it != mCIDs.end(); ++it) {
        if (it->getId() == cId) {
            release(it, false);
            break;
        }
    }

    pthread_mutex_unlock(&mMutex);
}

void FlvLive::sendFrame(
    FlvStream::Codecs codec,
    const uint8_t *data,
    size_t size,
    uint64_t timestamp,
    uint32_t samplerate,
    uint8_t numChannels,
    uint8_t bitsPerSample) {

    if (codec != FlvStream::Codecs::PCMLE &&
        codec != FlvStream::Codecs::G711A &&
        codec != FlvStream::Codecs::G711U && 
        codec != FlvStream::Codecs::AAC) {
        return;
    }

    pthread_mutex_lock(&mMutex);

    /*
        @BOARDCAST: Send audio frames
    */
    for (auto it = mCIDs.begin(); it != mCIDs.end(); ) {
        if (!it->hasBaseTimestamp()) {
            it->setBaseTimestamp(timestamp);
        }
        uint32_t normalise = u32FlvTimestamp(timestamp, it->getBaseTimestamp());
        if (!it->writeFrame(codec, samplerate, numChannels, bitsPerSample, data, size, normalise)) {
            it = release(it, true);
        } else {
            ++it;
        }
    }

    pthread_mutex_unlock(&mMutex);
}

void FlvLive::sendFrame(
    FlvStream::Codecs codec, 
    uint64_t timestamp, 
    bool idr,
    const FlvNalList &nals) {
    /**/
    if (codec != FlvStream::Codecs::H264 && 
        codec != FlvStream::Codecs::H265) {
        return;
    }
    pthread_mutex_lock(&mMutex);

    /* 
        @UPDATED: Codec and Nals Caches
    */
    if (mCodec != codec) {
        mCodec = codec;
        mVPSCache.clear();
        mSPSCache.clear();
        mPPSCache.clear();
    }
    createVpsSpsPpsCache(codec, nals);

    /*
        @BOARDCAST: Send video frames
    */
    
    for (auto it = mCIDs.begin(); it != mCIDs.end(); ) {
        if (it->needsVideoHeaders(codec)) {
            it->setVideoHeaders(codec, &mVPSCache, &mSPSCache, &mPPSCache);
        }
        if (!it->hasBaseTimestamp() && (!it->isWaitIdr() || idr)) {
            it->setBaseTimestamp(timestamp);
        }
        uint32_t normalise = u32FlvTimestamp(timestamp, it->getBaseTimestamp());
        if (!it->writeFrame(codec, nals, normalise)) {
            it = release(it, true);
        } else {
            ++it;
        }
    }

    pthread_mutex_unlock(&mMutex);
}

void FlvLive::createVpsSpsPpsCache(FlvStream::Codecs codec, const FlvNalList &nals) {
    for (size_t id = 0; id < nals.count(); ++id) {
        const FlvNalUnit &nal = nals.at(id);
        if (codec == FlvStream::Codecs::H264) {
            uint8_t nalType = FlvStream::GetH264NaluType(nal);
            if (nalType == 7) {
                mSPSCache = nal;
            } else if (nalType == 8) {
                mPPSCache = nal;
            }
        } else if (codec == FlvStream::Codecs::H265) {
            uint8_t nalType = FlvStream::GetH265NaluType(nal);
            if (nalType == 32) {
                mVPSCache = nal;
            } else if (nalType == 33) {
                mSPSCache = nal;
            } else if (nalType == 34) {
                mPPSCache = nal;
            }
        }
    }
}

std::list<FlvClient>::iterator FlvLive::release(
    std::list<FlvClient>::iterator it,
    bool needToCloseSockets) {
    /**/
    if (needToCloseSockets) {
        it->closure();
    }
    return mCIDs.erase(it);
}
