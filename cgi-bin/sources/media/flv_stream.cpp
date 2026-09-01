#include <climits>
#include "flv_stream.h"

static bool appendBytes(binary &data, const void *src, size_t len) {
    if (len == 0) {
        return true;
    }
    if (!src || data.size() > (SIZE_MAX - len)) {
        return false;
    }
    try {
        const uint8_t *bytes = static_cast<const uint8_t *>(src);
        data.insert(data.end(), bytes, bytes + len);
    } catch (...) {
        return false;
    }
    return true;
}

static bool appendU8(binary &data, uint8_t value) {
    try {
        data.push_back(value);
    } catch (...) {
        return false;
    }
    return true;
}

static bool appendBE(binary &data, uint32_t value, size_t size) {
    if (size == 0 || size > 4) {
        return false;
    }
    uint8_t bytes[4];
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = (uint8_t)((value >> ((size - i - 1) * 8)) & 0xff);
    }
    return appendBytes(data, bytes, size);
}

static uint32_t readBE(const uint8_t *data, size_t size) {
    uint32_t value = 0;
    for (size_t i = 0; i < size; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

static bool reserveBinary(binary &data, size_t size) {
    try {
        data.reserve(size);
    } catch (...) {
        return false;
    }
    return true;
}

static bool assignBytes(binary &data, const uint8_t *src, size_t len) {
    if (!src && len != 0) {
        return false;
    }
    try {
        if (len == 0) {
            data.clear();
        } else {
            data.assign(src, src + len);
        }
    } catch (...) {
        return false;
    }
    return true;
}

static size_t getStartCodeLen(const uint8_t *data, size_t size, size_t pos) {
    if (pos + 3 <= size && data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 1) {
        return 3; /* 00 00 01 */
    }
    if (pos + 4 <= size && data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 0 && data[pos + 3] == 1) {
        return 4; /* 00 00 00 01 */
    }
    return 0;
}

static bool needToIgnoreThisNal(uint8_t type, bool hevc) {
    if (hevc) {
        return (type == 32 || type == 33 || type == 34 || type == 35);
    }
    return (type == 7 || type == 8 || type == 9);
}

static bool makeHevcConfigureRecords(
    const FlvNalUnit &vps,
    const FlvNalUnit &sps,
    const FlvNalUnit &pps,
    binary &payload) {
    /**/
    const uint8_t FIXED[] = {
        0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        120 , 0xf0, 0x00, 0xfc, 0xfd, 0xf8,
        0xf8, 0x00, 0x00, 0x0f, 0x03,
    };
    const FlvNalUnit *nals[3] = {
        &vps, &sps, &pps
    };
    const uint8_t types[3] = {
        32, 33, 34
    };

    if (vps.empty() || sps.empty() || pps.empty()) {
        return false;
    }
    if (!appendBytes(payload, FIXED, sizeof(FIXED))) {
        return false;
    }

    for (size_t i = 0; i < 3; ++i) {
        if (nals[i]->size() > 0xffffu) {
            return false;
        }
        bool b =  appendU8(payload, (uint8_t)(0x80 | (types[i] & 0x3f))) &&
                  appendU8(payload, 0x00) &&
                  appendU8(payload, 0x01) &&
                  appendBE(payload, (uint32_t)nals[i]->size(), 2) &&
                  appendBytes(payload, nals[i]->data(), nals[i]->size());
        if (!b) {
            return false;
        }
    }

    return true;
}

/*--------------------------------------------------------------------------------------------*/

bool FlvNalList::append(const uint8_t *data, size_t size) {
    if (!data || size == 0) {
        return false;
    }
    try {
        /* Create a new empty std::vector<uint8_t> at the end of the NAL list */
        mItems.emplace_back();
    } catch (...) {
        return false;
    }
    if (!assignBytes(mItems.back(), data, size) || mItems.back().empty()) {
        mItems.pop_back();
        return false;
    }
    return true;
}

void FlvNalList::clear() {
    mItems.clear();
}

bool FlvNalList::empty() const {
    return mItems.empty();
}

size_t FlvNalList::count() const {
    return mItems.size();
}

const FlvNalUnit &FlvNalList::at(size_t index) const {
    return mItems[index];
}

bool FlvNalList::splitAnnexB(const uint8_t *data, size_t size) {
    clear();

    if (!data || size == 0) {
        return false;
    }

    size_t pos = 0;
    while (pos < size) {
        while (pos < size && getStartCodeLen(data, size, pos) == 0) {
            ++pos;
        }
        if (pos >= size) {
            break;
        }
        pos += getStartCodeLen(data, size, pos);
        size_t nalStart = pos;
        while (pos < size && getStartCodeLen(data, size, pos) == 0) {
            ++pos;
        }
        size_t nalEnd = pos;
        while (nalEnd > nalStart && data[nalEnd - 1] == 0) {
            --nalEnd;
        }
        if (nalEnd > nalStart && !append(data + nalStart, nalEnd - nalStart)) {
            clear();
            return false;
        }
    }
    return !empty();
}

bool FlvNalList::splitLengthPrefixed(const uint8_t *data, size_t size, size_t lengthSize) {
    clear();

    if (!data || size == 0 || lengthSize == 0 || lengthSize > 4) {
        return false;
    }

    size_t pos = 0;
    size_t count = 0;
    while (pos + lengthSize <= size) {
        uint32_t nalSize = readBE(data + pos, lengthSize);
        pos += lengthSize;
        if (nalSize == 0 || pos + nalSize > size) {
            return false;
        }
        pos += nalSize;
        ++count;
    }
    if (count == 0 || pos != size) {
        return false;
    }

    try {
        mItems.reserve(count);
    } catch (...) {
        return false;
    }

    pos = 0;
    while (pos + lengthSize <= size) {
        uint32_t nalSize = readBE(data + pos, lengthSize);
        pos += lengthSize;
        if (!append(data + pos, nalSize)) {
            clear();
            return false;
        }
        pos += nalSize;
    }
    return true;
}

/*--------------------------------------------------------------------------------------------*/

FlvStream::FlvStream(FlvSendStream sender, void *usrPtr) {
    mSender = sender;
    mUsrPtr = usrPtr;
}

bool FlvStream::writeTags(uint8_t tagType, uint32_t timestamp, const binary &payload) {
    if (!mSender || payload.size() > 0xffffffu) {
        return false;
    }
    binary tag {};
    if (!reserveBinary(tag, 15 + payload.size())) {
        return false;
    }
    bool b =  appendU8(tag, tagType) &&
              appendBE(tag, (uint32_t)payload.size(), 3) &&
              appendBE(tag, timestamp & 0xffffffu, 3) &&
              appendU8(tag, (uint8_t)((timestamp >> 24) & 0xff)) &&
              appendBE(tag, 0, 3) &&
              appendBytes(tag, payload.data(), payload.size()) &&
              appendBE(tag, (uint32_t)(11 + payload.size()), 4);
    if (b) {
        b = mSender(tag.data(), tag.size(), mUsrPtr);
    }
    return b;
}

bool FlvStream::writeHeader(bool hasAudio, bool hasVideo) {
    if (!mSender) {
        return false;
    }
    uint8_t flags = 0;
    if (hasAudio) flags |= 0x04;
    if (hasVideo) flags |= 0x01;
    const uint8_t header[] = {'F', 'L', 'V', 0x01, flags, 0, 0, 0, 9, 0, 0, 0, 0};
    return mSender(header, sizeof(header), mUsrPtr);
}

bool FlvStream::writeAvcSequenceHeader(const FlvNalUnit &sps, const FlvNalUnit &pps, uint32_t timestamp) {
    if (sps.size() < 4 || pps.empty() || sps.size() > 0xffffu || pps.size() > 0xffffu) {
        return false;
    }

    binary payload {};
    if (!reserveBinary(payload, 16 + sps.size() + pps.size())) {
        return false;
    }
    bool b =  appendU8(payload, 0x17) &&
              appendU8(payload, 0x00) &&
              appendBE(payload, 0, 3) &&
              appendU8(payload, 0x01) &&
              appendU8(payload, sps.data()[1]) &&
              appendU8(payload, sps.data()[2]) &&
              appendU8(payload, sps.data()[3]) &&
              appendU8(payload, 0xff) &&
              appendU8(payload, 0xe1) &&
              appendBE(payload, (uint32_t)sps.size(), 2) &&
              appendBytes(payload, sps.data(), sps.size()) &&
              appendU8(payload, 0x01) &&
              appendBE(payload, (uint32_t)pps.size(), 2) &&
              appendBytes(payload, pps.data(), pps.size());
    return b && writeTags(9, timestamp, payload);
}

bool FlvStream::writeAvcFrame(const FlvNalList &frame, uint32_t timestamp) {
    return writeVideoFrameTags(frame, timestamp, false);
}

bool FlvStream::writeHevcSequenceHeader(const FlvNalUnit &vps, const FlvNalUnit &sps, const FlvNalUnit &pps, uint32_t timestamp) {
    binary configure {};
    if (!makeHevcConfigureRecords(vps, sps, pps, configure)) {
        return false;
    }

    binary payload {};
    if (!reserveBinary(payload, 5 + configure.size())) {
        return false;
    }
    bool b =  appendU8(payload, 0x1c) &&
              appendU8(payload, 0x00) &&
              appendBE(payload, 0, 3) &&
              appendBytes(payload, configure.data(), configure.size());
    return b && writeTags(9, timestamp, payload);
}

bool FlvStream::writeHevcFrame(const FlvNalList &frame, uint32_t timestamp) {
    return writeVideoFrameTags(frame, timestamp, true);
}

bool FlvStream::writeVideoFrameTags(const FlvNalList &frame, uint32_t timestamp, bool hevc) {
    size_t payloadSize = 5;
    for (size_t i = 0; i < frame.count(); ++i) {
        const FlvNalUnit &nal = frame.at(i);
        uint8_t type = hevc ? GetH265NaluType(nal) : GetH264NaluType(nal);
        if (needToIgnoreThisNal(type, hevc)) {
            continue;
        }
        if (nal.size() > UINT32_MAX || nal.size() > SIZE_MAX - 4 || payloadSize > SIZE_MAX - 4 - nal.size()) {
            return false;
        }
        payloadSize += 4 + nal.size();
    }
    if (payloadSize == 5) {
        return true;
    }

    binary payload;
    if (!reserveBinary(payload, payloadSize)) {
        return false;
    }
    bool idr = hevc ? IsH265KeyFrame(frame) : IsH264KeyFrame(frame);
    bool b =  appendU8(payload, hevc ? (idr ? 0x1c : 0x2c) : (idr ? 0x17 : 0x27)) &&
              appendU8(payload, 0x01) &&
              appendBE(payload, 0, 3);
    for (size_t i = 0; b && i < frame.count(); ++i) {
        const FlvNalUnit &nal = frame.at(i);
        uint8_t type = hevc ? GetH265NaluType(nal) : GetH264NaluType(nal);
        if (!needToIgnoreThisNal(type, hevc)) {
            b = appendBE(payload, (uint32_t)nal.size(), 4) &&
                appendBytes(payload, nal.data(), nal.size());
        }
    }
    return b && writeTags(9, timestamp, payload);
}

bool FlvStream::ParserAacAdts(const uint8_t *data, size_t size, FlvAacAdtsHeader *header) {
    if (!data || size < 7 || !header) {
        return false;
    }
    if (data[0] != 0xff || (data[1] & 0xf0) != 0xf0) {
        return false;
    }

    uint8_t u8ProtectionAbsent = data[1] & 0x01;
    uint8_t profile = (data[2] >> 6) & 0x03;
    uint8_t sampleRateIndex = (data[2] >> 2) & 0x0f;
    uint8_t channelsConfiguration = (uint8_t)(((data[2] & 0x01) << 2) | ((data[3] >> 6) & 0x03));
    size_t frameSize = (size_t)(((data[3] & 0x03) << 11) | (data[4] << 3) | ((data[5] >> 5) & 0x07));
    size_t headerSize = u8ProtectionAbsent ? 7 : 9;

    if (sampleRateIndex == 0x0f || channelsConfiguration == 0 || frameSize < headerSize || frameSize > size) {
        return false;
    }

    header->profile = profile;
    header->sampleRateIndex = sampleRateIndex;
    header->channelsConfiguration = channelsConfiguration;
    header->headerSize = headerSize;
    header->payloadSize = frameSize - headerSize;
    header->sequenceHeader[0] = (uint8_t)(((profile + 1) << 3) | (sampleRateIndex >> 1));
    header->sequenceHeader[1] = (uint8_t)(((sampleRateIndex & 1) << 7) | (channelsConfiguration << 3));
    return true;
}

FlvAudioSampleRates FlvStream::audioSampleRateFromHz(uint32_t samplerate) {
    if (samplerate <= 12000) {
        return FlvAudioSampleRate8000;
    }
    if (samplerate <= 24000) {
        return FlvAudioSampleRate16000;
    }
    if (samplerate <= 38000) {
        return FlvAudioSampleRate32000;
    }
    if (samplerate <= 46050) {
        return FlvAudioSampleRate44100;
    }
    return FlvAudioSampleRate48000;
}

FlvAudioRates FlvStream::audioRateFromHz(uint32_t samplerate) {
    switch (audioSampleRateFromHz(samplerate)) {
    case FlvAudioSampleRate8000:
    return FlvAudioRate5500;
    case FlvAudioSampleRate16000:
    return FlvAudioRate11000;
    case FlvAudioSampleRate32000:
    return FlvAudioRate22000;
    case FlvAudioSampleRate44100:
    case FlvAudioSampleRate48000:
    default:
    return FlvAudioRate44000;
    }
}

uint8_t FlvStream::audioHeader(Codecs codec, FlvAudioRates rate, uint8_t bitsPerSample, uint8_t channels) {
    uint8_t soundSize = bitsPerSample > 8 ? 1 : 0;
    uint8_t soundType = channels > 1 ? 1 : 0;

    if (codec == Codecs::AAC) {
        return 0xaf;
    }
    if (codec == Codecs::G711A || codec == Codecs::G711U) {
        rate = FlvAudioRate5500;
        soundSize = 0;
        soundType = 0;
    }
    return (uint8_t)(((uint8_t)codec << 4) | (((uint8_t)rate & 0x03) << 2) | ((soundSize & 0x01) << 1) | (soundType & 0x01));
}

bool FlvStream::writeAudioFrame(
    Codecs codec,
    FlvAudioRates rate,
    uint8_t bitsPerSample,
    uint8_t numChannels,
    const uint8_t *data,
    size_t size,
    uint32_t timestamp) {
    /**/
    if (!data || size == 0 || codec == Codecs::NONE || codec == Codecs::AAC) {
        return false;
    }
    return writeAudioFrameTags(audioHeader(codec, rate, bitsPerSample, numChannels), data, size, timestamp, -1);
}

bool FlvStream::writeAacSequenceHeader(const uint8_t *sequenceHeader, size_t size, uint32_t timestamp) {
    return writeAudioFrameTags(0xaf, sequenceHeader, size, timestamp, 0);
}

bool FlvStream::writeAacFrame(const uint8_t *data, size_t size, uint32_t timestamp) {
    return writeAudioFrameTags(0xaf, data, size, timestamp, 1);
}

bool FlvStream::writeAudioFrameTags(uint8_t header, const uint8_t *data, size_t size, uint32_t timestamp, int aacPacketType) {
    if (!data || size == 0) {
        return false;
    }

    binary payload;
    if (!reserveBinary(payload, size + (aacPacketType >= 0 ? 2 : 1))) {
        return false;
    }
    bool b = appendU8(payload, header) &&
             (aacPacketType < 0 || appendU8(payload, (uint8_t)aacPacketType)) &&
             appendBytes(payload, data, size);
    return b && writeTags(8, timestamp, payload);
}

uint8_t FlvStream::GetH264NaluType(const FlvNalUnit &nal) {
    return nal.size() < 1 ? 0 : (uint8_t)(nal.data()[0] & 0x1f);
}

uint8_t FlvStream::GetH265NaluType(const FlvNalUnit &nal) {
    return nal.size() < 2 ? 0 : (uint8_t)((nal.data()[0] >> 1) & 0x3f);
}

bool FlvStream::IsH264KeyFrame(const FlvNalList &frame) {
    for (size_t i = 0; i < frame.count(); ++i) {
        if (GetH264NaluType(frame.at(i)) == 5) {
            return true;
        }
    }
    return false;
}

bool FlvStream::IsH265KeyFrame(const FlvNalList &frame) {
    for (size_t i = 0; i < frame.count(); ++i) {
        uint8_t type = GetH265NaluType(frame.at(i));
        if (type >= 16 && type <= 21) {
            return true;
        }
    }
    return false;
}
