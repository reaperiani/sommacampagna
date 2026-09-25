#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace somma
{
constexpr uint32_t protocolMagic = 0x534F4D4Du; // SOMM
constexpr uint16_t protocolVersion = 1;

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
static_assert(sizeof(PacketHeader) == 28u);
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
