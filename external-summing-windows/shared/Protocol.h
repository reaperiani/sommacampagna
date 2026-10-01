#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace somma
{
constexpr uint32_t protocolMagic = 0x534F4D4Du; // SOMM
constexpr uint16_t protocolVersion = 2;

constexpr uint16_t senderToEnginePort = 45570;
constexpr uint16_t engineToReceiverPort = 45571;
constexpr int maxSamplesPerPacket = 2048;
constexpr uint32_t minimumSampleRate = 8000;
constexpr uint32_t maximumSampleRate = 384000;
constexpr uint16_t maximumPairIndex = 7;
constexpr size_t maxBufferedFrames = 131072u;
constexpr float minTransmissionBufferMs = 1.0f;
constexpr float defaultTransmissionBufferMs = 50.0f;
constexpr float maxTransmissionBufferMs = 500.0f;
constexpr float minTotalTransmissionBufferMs = 20.0f;
constexpr float defaultTotalTransmissionBufferMs = 50.0f;
constexpr float maxTotalTransmissionBufferMs = 500.0f;
constexpr float engineTransmissionBufferShare = 0.6f;
constexpr int64_t invalidSamplePosition = std::numeric_limits<int64_t>::min();

inline bool isValidSamplePosition(int64_t samplePosition) noexcept
{
    return samplePosition != invalidSamplePosition;
}

inline bool addSampleFrames(int64_t samplePosition, uint32_t frames, int64_t& result) noexcept
{
    if (!isValidSamplePosition(samplePosition)
        || samplePosition > std::numeric_limits<int64_t>::max() - static_cast<int64_t>(frames))
    {
        return false;
    }

    result = samplePosition + static_cast<int64_t>(frames);
    return true;
}

class TimelineStereoBuffer
{
public:
    enum class ReadResult
    {
        beforeStream,
        available,
        unavailable,
    };

    void clear() noexcept
    {
        readPosition = 0;
        availableFrames = 0;
        firstSamplePosition = invalidSamplePosition;
        readSamplePosition = invalidSamplePosition;
        writeSamplePosition = invalidSamplePosition;
    }

    bool append(int64_t packetSamplePosition,
                const float* interleaved,
                uint16_t numSamples,
                bool& timelineDiscontinuity) noexcept
    {
        timelineDiscontinuity = false;
        if (!isValidSamplePosition(packetSamplePosition) || interleaved == nullptr || numSamples == 0)
            return false;

        int64_t packetEnd = 0;
        if (!addSampleFrames(packetSamplePosition, numSamples, packetEnd))
            return false;

        if (firstSamplePosition == invalidSamplePosition)
        {
            firstSamplePosition = packetSamplePosition;
            readSamplePosition = packetSamplePosition;
            writeSamplePosition = packetSamplePosition;
        }
        else if (packetSamplePosition < writeSamplePosition)
        {
            clear();
            firstSamplePosition = packetSamplePosition;
            readSamplePosition = packetSamplePosition;
            writeSamplePosition = packetSamplePosition;
            timelineDiscontinuity = true;
        }

        const auto gapFrames = packetSamplePosition > writeSamplePosition
                                 ? static_cast<uint64_t>(packetSamplePosition) - static_cast<uint64_t>(writeSamplePosition)
                                 : 0u;
        if (gapFrames > maxBufferedFrames
            || static_cast<size_t>(gapFrames) + numSamples > maxBufferedFrames - availableFrames)
        {
            clear();
            firstSamplePosition = packetSamplePosition;
            readSamplePosition = packetSamplePosition;
            writeSamplePosition = packetSamplePosition;
            timelineDiscontinuity = true;
        }
        else
        {
            for (uint64_t i = 0; i < gapFrames; ++i)
                pushFrame(0.0f, 0.0f);
        }

        for (uint16_t i = 0; i < numSamples; ++i)
            pushFrame(interleaved[static_cast<size_t>(i) * 2u], interleaved[static_cast<size_t>(i) * 2u + 1u]);

        return true;
    }

    bool canReadRange(int64_t rangeStart, uint32_t numSamples) const noexcept
    {
        if (firstSamplePosition == invalidSamplePosition)
            return false;

        int64_t rangeEnd = 0;
        if (!addSampleFrames(rangeStart, numSamples, rangeEnd))
            return false;

        if (rangeEnd <= firstSamplePosition)
            return true;

        const auto firstRequiredSample = maxSamplePosition(rangeStart, firstSamplePosition);
        return firstRequiredSample >= readSamplePosition && rangeEnd <= writeSamplePosition;
    }

    ReadResult readAt(int64_t samplePosition, float& left, float& right) noexcept
    {
        left = 0.0f;
        right = 0.0f;
        if (firstSamplePosition == invalidSamplePosition)
            return ReadResult::unavailable;
        if (samplePosition < firstSamplePosition)
            return ReadResult::beforeStream;
        if (samplePosition < readSamplePosition || samplePosition >= writeSamplePosition)
            return ReadResult::unavailable;

        while (readSamplePosition < samplePosition)
            discardFrame();

        left = ringL[readPosition];
        right = ringR[readPosition];
        discardFrame();
        return ReadResult::available;
    }

    size_t getAvailableFrames() const noexcept { return availableFrames; }
    int64_t getFirstSamplePosition() const noexcept { return firstSamplePosition; }
    int64_t getReadSamplePosition() const noexcept { return readSamplePosition; }
    int64_t getWriteSamplePosition() const noexcept { return writeSamplePosition; }

private:
    static int64_t maxSamplePosition(int64_t a, int64_t b) noexcept { return a > b ? a : b; }

    void pushFrame(float left, float right) noexcept
    {
        const auto writePosition = (readPosition + availableFrames) % maxBufferedFrames;
        ringL[writePosition] = left;
        ringR[writePosition] = right;
        ++availableFrames;
        ++writeSamplePosition;
    }

    void discardFrame() noexcept
    {
        readPosition = (readPosition + 1u) % maxBufferedFrames;
        --availableFrames;
        ++readSamplePosition;
    }

    std::array<float, maxBufferedFrames> ringL {};
    std::array<float, maxBufferedFrames> ringR {};
    size_t readPosition = 0;
    size_t availableFrames = 0;
    int64_t firstSamplePosition = invalidSamplePosition;
    int64_t readSamplePosition = invalidSamplePosition;
    int64_t writeSamplePosition = invalidSamplePosition;
};

enum class BlockSequenceStatus
{
    first,
    inOrder,
    forwardGap,
    stale,
    restart,
};

class BlockSequenceTracker
{
public:
    BlockSequenceStatus observe(uint32_t blockIndex, bool allowZeroRestart = true) noexcept
    {
        if (!haveSequence)
        {
            haveSequence = true;
            expectedBlock = blockIndex + 1u;
            return BlockSequenceStatus::first;
        }

        if (blockIndex == expectedBlock)
        {
            expectedBlock = blockIndex + 1u;
            return BlockSequenceStatus::inOrder;
        }

        if (allowZeroRestart && blockIndex == 0u && expectedBlock != 1u)
        {
            expectedBlock = 1u;
            return BlockSequenceStatus::restart;
        }

        const auto distance = blockIndex - expectedBlock;
        if (distance < 0x80000000u)
        {
            expectedBlock = blockIndex + 1u;
            return BlockSequenceStatus::forwardGap;
        }

        return BlockSequenceStatus::stale;
    }

    void reset() noexcept
    {
        haveSequence = false;
        expectedBlock = 0;
    }

private:
    uint32_t expectedBlock = 0;
    bool haveSequence = false;
};

enum class PacketType : uint16_t
{
    senderAudio = 1,
    mainStereoSum = 2,
};

struct PacketHeader
{
    uint32_t magic = protocolMagic;
    uint16_t version = protocolVersion;
    uint16_t packetType = 0;
    uint32_t sessionId = 0;
    uint32_t streamId = 0;
    uint32_t blockIndex = 0;
    int64_t firstSamplePosition = 0;
    uint32_t sampleRate = 48000;
    uint16_t numSamples = 0;
    uint16_t pairIndex = 0;
};

struct StereoAudioPacket
{
    PacketHeader header;
    std::array<float, maxSamplesPerPacket * 2> interleaved {};
};

constexpr size_t getStereoAudioPacketSize(uint16_t numSamples) noexcept
{
    return sizeof(PacketHeader) + static_cast<size_t>(numSamples) * 2u * sizeof(float);
}

constexpr size_t maximumInspectedDatagramSize = sizeof(StereoAudioPacket) + 1u;

static_assert(std::is_standard_layout_v<PacketHeader>);
static_assert(std::is_trivially_copyable_v<PacketHeader>);
static_assert(std::is_standard_layout_v<StereoAudioPacket>);
static_assert(std::is_trivially_copyable_v<StereoAudioPacket>);
static_assert(sizeof(PacketHeader) == 40u);
static_assert(offsetof(PacketHeader, firstSamplePosition) == 24u);
static_assert(offsetof(PacketHeader, sampleRate) == 32u);
static_assert(offsetof(PacketHeader, numSamples) == 36u);
static_assert(offsetof(StereoAudioPacket, interleaved) == sizeof(PacketHeader));
static_assert(sizeof(StereoAudioPacket) == sizeof(PacketHeader) + sizeof(float) * maxSamplesPerPacket * 2u);

inline bool isSupportedSampleRate(uint32_t sampleRate) noexcept
{
    return sampleRate >= minimumSampleRate && sampleRate <= maximumSampleRate;
}

inline bool isValidHeader(const PacketHeader& h)
{
    if (h.magic != protocolMagic || h.version != protocolVersion)
        return false;

    if (h.numSamples == 0 || h.numSamples > maxSamplesPerPacket)
        return false;

    if (!isSupportedSampleRate(h.sampleRate) || h.pairIndex > maximumPairIndex)
        return false;

    return true;
}

inline bool decodeStereoAudioPacket(const void* datagram,
                                    size_t datagramSize,
                                    PacketType expectedType,
                                    StereoAudioPacket& packet) noexcept
{
    if (datagram == nullptr || datagramSize < sizeof(PacketHeader) || datagramSize > sizeof(StereoAudioPacket))
        return false;

    PacketHeader header;
    std::memcpy(&header, datagram, sizeof(header));
    if (!isValidHeader(header)
        || header.packetType != static_cast<uint16_t>(expectedType)
        || datagramSize != getStereoAudioPacketSize(header.numSamples))
    {
        return false;
    }

    std::memcpy(&packet, datagram, datagramSize);
    const auto payloadValues = static_cast<size_t>(header.numSamples) * 2u;
    for (size_t i = 0; i < payloadValues; ++i)
    {
        if (!std::isfinite(packet.interleaved[i]))
            return false;
    }

    return true;
}
}
