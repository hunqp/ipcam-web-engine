#include "flv_client.h"

FlvClient::Transport FlvClient::sTransport {};

void FlvClient::setTransport(const Transport &transport) {
    sTransport = transport;
}

bool FlvClient::outgoing(const uint8_t *data, size_t size, void *user) {
    FlvClient *me = static_cast<FlvClient *>(user);
    if (!me) {
        return false;
    }
    return me->sendBinary(data, size);
}

FlvClient::FlvClient(int cid) :
    mId(cid),
    mStream(outgoing, this) {
}

int FlvClient::getId() {
    return mId;
}

void FlvClient::closure() {
    if (sTransport.closePeer) {
        sTransport.closePeer(mId);
    }
}

bool FlvClient::sendBinary(const void *data, size_t size) {
    if (!sTransport.sendBinary) {
        return false;
    }
    return sTransport.sendBinary(mId, data, size);
}

bool FlvClient::isWaitIdr() const {
    return mWaitIdr;
}

bool FlvClient::needsVideoHeaders(FlvStream::Codecs codec) const {
    if (mVideoCodec != codec) {
        return true;
    }
    if (codec == FlvStream::Codecs::H264) {
        return (mSps.empty() || mPps.empty());
    }
    if (codec == FlvStream::Codecs::H265) {
        return (mVps.empty() || mSps.empty() || mPps.empty());
    }
    return true;
}

void FlvClient::setVideoHeaders(
    FlvStream::Codecs codec,
    const FlvNalUnit *vps,
    const FlvNalUnit *sps,
    const FlvNalUnit *pps) {
    /**/
    if (mVideoCodec != codec) {
        mVps.clear();
        mSps.clear();
        mPps.clear();
        mVideoCodec = codec;
        mIsSequenceHeaderWritten = false;
        mWaitIdr = true;
    }
    if (mVps.empty() && vps && !vps->empty()) {
        mVps = *vps;
    }
    if (mSps.empty() && sps && !sps->empty()) {
        mSps = *sps;
    }
    if (mPps.empty() && pps && !pps->empty()) {
        mPps = *pps;
    }
}

bool FlvClient::hasBaseTimestamp() const {
    return mBaseTimestamp != 0;
}

void FlvClient::setBaseTimestamp(uint64_t timestamp) {
    mBaseTimestamp = timestamp;
}

uint64_t FlvClient::getBaseTimestamp() const {
    return mBaseTimestamp;
}

bool FlvClient::writeFrame(
    FlvStream::Codecs codec, 
    const FlvNalList &frame, 
    uint32_t timestamp) {
    /**/
    if (mVideoCodec != codec) {
        mVideoCodec = codec;
        mWaitIdr = true;
        mIsSequenceHeaderWritten = false;
    }
    if (mWaitIdr) {
        bool idr = false;
        if (codec == FlvStream::Codecs::H264) {
            idr = FlvStream::IsH264KeyFrame(frame);
        } else {
            idr = FlvStream::IsH265KeyFrame(frame);
        }
        if (!idr) {
            return true;
        }
        mWaitIdr = false;
    }
    if (!mIsSequenceHeaderWritten) {
        if (needsVideoHeaders(codec)) {
            return true;
        }
        if (!writeSequenceHeader()) {
            return false;
        }
    }
    if (codec == FlvStream::Codecs::H264) {
        return mStream.writeAvcFrame(frame, timestamp);
    }
    return mStream.writeHevcFrame(frame, timestamp);
}

bool FlvClient::writeFrame(
    FlvStream::Codecs codec,
    uint32_t samplerate,
    uint8_t numChannels,
    uint8_t bitsPerSample,
    const uint8_t *data,
    size_t size,
    uint32_t timestamp) {
    /**/
    if (!writeHeader()) {
        return false;
    }

    if (mAudioCodec != codec) {
        mAudioCodec = codec;
        mAAC.sequenceHeaderSize = 0;
        mAAC.isSequenceHeaderWritten = false;
    }

    if (codec == FlvStream::Codecs::AAC) {
        FlvAacAdtsHeader adts;
        if (!FlvStream::ParserAacAdts(data, size, &adts)) {
            return true;
        }
        if (!mAAC.isSequenceHeaderWritten ||
            mAAC.sequenceHeaderSize != sizeof(adts.sequenceHeader) ||
            mAAC.sequenceHeaders[0] != adts.sequenceHeader[0] ||
            mAAC.sequenceHeaders[1] != adts.sequenceHeader[1]) {
            mAAC.sequenceHeaders[0] = adts.sequenceHeader[0];
            mAAC.sequenceHeaders[1] = adts.sequenceHeader[1];
            mAAC.sequenceHeaderSize = sizeof(adts.sequenceHeader);
            if (!mStream.writeAacSequenceHeader(mAAC.sequenceHeaders, mAAC.sequenceHeaderSize, timestamp)) {
                return false;
            }
            mAAC.isSequenceHeaderWritten = true;
        }
        return mStream.writeAacFrame(data + adts.headerSize, adts.payloadSize, timestamp);
    }

    return mStream.writeAudioFrame(
        codec,
        FlvStream::audioRateFromHz(samplerate),
        bitsPerSample,
        numChannels,
        data,
        size,
        timestamp);
}

bool FlvClient::writeHeader() {
    if (mIsHeaderWritten) {
        return true;
    }
    if (!mStream.writeHeader(true, true)) {
        return false;
    }
    mIsHeaderWritten = true;
    return true;
}

bool FlvClient::writeSequenceHeader() {
    if (mIsSequenceHeaderWritten) {
        return true;
    }
    if (needsVideoHeaders(mVideoCodec) || !writeHeader()) {
        return false;
    }
    bool success = mVideoCodec == FlvStream::Codecs::H264 ?
                   mStream.writeAvcSequenceHeader(mSps, mPps, 0) :
                   mStream.writeHevcSequenceHeader(mVps, mSps, mPps, 0);
    if (!success) {
        return false;
    }
    mIsSequenceHeaderWritten = true;
    return true;
}
