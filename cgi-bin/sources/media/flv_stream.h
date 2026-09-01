#ifndef FLV_STREAM_H
#define FLV_STREAM_H

#include <cstddef>
#include <cstdint>
#include <vector>

using binary = std::vector<uint8_t>;
using FlvNalUnit = binary;

enum FlvAudioRates {
    FlvAudioRate5500 = 0,
    FlvAudioRate11000 = 1,
    FlvAudioRate22000 = 2,
    FlvAudioRate44000 = 3,
};

enum FlvAudioSampleRates {
    FlvAudioSampleRate8000 = 8000,
    FlvAudioSampleRate16000 = 16000,
    FlvAudioSampleRate32000 = 32000,
    FlvAudioSampleRate44100 = 44100,
    FlvAudioSampleRate48000 = 48000,
};

struct FlvAacAdtsHeader {
    uint8_t profile = 0;
    uint8_t sampleRateIndex = 0;
    uint8_t channelsConfiguration = 0;
    size_t headerSize = 0;
    size_t payloadSize = 0;
    uint8_t sequenceHeader[2] = {0, 0};
};

class FlvNalList {
public:
    bool append(const uint8_t *data, size_t size);
    void clear();
    bool empty() const;
    size_t count() const;
    const FlvNalUnit &at(size_t index) const;

    bool splitAnnexB(const uint8_t *data, size_t size);
    bool splitLengthPrefixed(const uint8_t *data, size_t size, size_t lengthSize);

private:
    std::vector<FlvNalUnit> mItems;
};

class FlvStream {
public:
    using FlvSendStream = bool (*)(const uint8_t *data, size_t size, void *user);

public:
    enum class Codecs {
        NONE = -1,
        /* Video Codecs Support */
        H264,
        H265,
        /* Audio Codecs Support */
        PCMLE = 3,
        G711A = 7,
        G711U = 8,
        AAC = 10,
    };

    FlvStream() = default;
    FlvStream(FlvSendStream sender, void *usrPtr);

    bool writeHeader(bool hasAudio, bool hasVideo);
    bool writeAvcSequenceHeader(const FlvNalUnit &sps, const FlvNalUnit &pps, uint32_t timestamp);
    bool writeAvcFrame(const FlvNalList &frame, uint32_t timestamp);
    bool writeHevcSequenceHeader(const FlvNalUnit &vps, const FlvNalUnit &sps, const FlvNalUnit &pps, uint32_t timestamp);
    bool writeHevcFrame(const FlvNalList &frame, uint32_t timestamp);
    bool writeAudioFrame(
        Codecs codec,
        FlvAudioRates rate,
        uint8_t bitsPerSample,
        uint8_t numChannels,
        const uint8_t *data,
        size_t size,
        uint32_t timestamp);
    bool writeAacSequenceHeader(const uint8_t *sequenceHeader, size_t size, uint32_t timestamp);
    bool writeAacFrame(const uint8_t *data, size_t size, uint32_t timestamp);

public:
    static bool ParserAacAdts(const uint8_t *data, size_t size, FlvAacAdtsHeader *header);
    static FlvAudioSampleRates audioSampleRateFromHz(uint32_t samplerate);
    static FlvAudioRates audioRateFromHz(uint32_t samplerate);
    static uint8_t audioHeader(Codecs codec, FlvAudioRates rate, uint8_t bitsPerSample, uint8_t channels);
    static uint8_t GetH264NaluType(const FlvNalUnit &nal);
    static uint8_t GetH265NaluType(const FlvNalUnit &nal);
    static bool IsH264KeyFrame(const FlvNalList &frame);
    static bool IsH265KeyFrame(const FlvNalList &frame);

private:
    bool writeTags(uint8_t tagType, uint32_t timestamp, const binary &payload);
    bool writeAudioFrameTags(uint8_t header, const uint8_t *data, size_t size, uint32_t timestamp, int aacPacketType);
    bool writeVideoFrameTags(const FlvNalList &frame, uint32_t timestamp, bool hevc);

private:
    void *mUsrPtr = NULL;
    FlvSendStream mSender = NULL;
};

#endif /* FLV_STREAM_H */
