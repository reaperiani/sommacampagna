#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace somma
{
constexpr uint32_t protocolMagic = 0x534F4D4Du; // SOMM
constexpr uint16_t protocolVersion = 1;

constexpr uint16_t senderToEnginePort = 45570;
constexpr uint16_t engineToReceiverPort = 45571;
constexpr int maxSamplesPerPacket = 2048;
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

inline bool isValidHeader(const PacketHeader& h)
{
    if (h.magic != protocolMagic || h.version != protocolVersion)
        return false;

    if (h.numSamples == 0 || h.numSamples > maxSamplesPerPacket)
        return false;

    return true;
}
}
