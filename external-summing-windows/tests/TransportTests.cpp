#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#include "PortDiscovery.h"
#include "Protocol.h"

namespace
{
int failures = 0;

void expect(bool condition, const char* message)
{
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

bool approximatelyEqual(float actual, float expected) noexcept
{
    return std::abs(actual - expected) < 0.001f;
}

void testBufferPolicy()
{
    expect(approximatelyEqual(somma::getEngineTransmissionBufferMs(50.0f), 30.0f), "50 ms total assigns 30 ms to engine");
    expect(approximatelyEqual(somma::getReceiverTransmissionBufferMs(50.0f), 20.0f), "50 ms total assigns 20 ms to receiver");
    expect(approximatelyEqual(somma::sanitizeTotalTransmissionBufferMs(1000.0f), 500.0f), "total buffer is capped at 500 ms");
    expect(approximatelyEqual(somma::getEngineTransmissionBufferMs(500.0f), 300.0f), "maximum total assigns 300 ms to engine");
    expect(approximatelyEqual(somma::getReceiverTransmissionBufferMs(500.0f), 200.0f), "maximum total assigns 200 ms to receiver");
    expect(approximatelyEqual(somma::migrateLegacyTransmissionBufferMs(50.0f), 100.0f), "legacy per-stage value preserves total latency");
    expect(somma::sanitizeTotalTransmissionBufferMs(std::numeric_limits<float>::quiet_NaN())
               == somma::defaultTotalTransmissionBufferMs,
           "non-finite total falls back to default");
}

void testPortParsing()
{
    const auto legacy = somma::parsePortsText("senderToEngine=45580\nengineToReceiver=45581\ntransmissionBufferMs=50.0\n");
    expect(legacy.senderToEngine == 45580, "legacy sender port parsed");
    expect(legacy.engineToReceiver == 45581, "legacy receiver port parsed");
    expect(legacy.hasTransmissionBufferMs, "legacy buffer presence recorded");
    expect(!legacy.totalTransmissionBufferMs.has_value(), "legacy file has no total buffer");

    const auto current = somma::parsePortsText("transmissionBufferMs=20\ntotalTransmissionBufferMs=50\n");
    expect(current.totalTransmissionBufferMs.has_value() && *current.totalTransmissionBufferMs == 50.0f,
           "total buffer parsed");

    const auto invalid = somma::parsePortsText("senderToEngine=-1\ntransmissionBufferMs=0\ntotalTransmissionBufferMs=nan\n");
    expect(invalid.senderToEngine == somma::senderToEnginePort, "invalid port keeps default");
    expect(!invalid.hasTransmissionBufferMs, "invalid legacy buffer is not accepted");
    expect(!invalid.totalTransmissionBufferMs.has_value(), "invalid total buffer is not accepted");

    const auto localeDependent = somma::parsePortsText("transmissionBufferMs=20,5\n");
    expect(!localeDependent.hasTransmissionBufferMs, "decimal comma is rejected consistently across locales");
}

void testSequenceTracking()
{
    somma::BlockSequenceTracker tracker;
    expect(tracker.observe(10) == somma::BlockSequenceStatus::first, "first sequence accepted");
    expect(tracker.observe(11) == somma::BlockSequenceStatus::inOrder, "next sequence accepted");
    expect(tracker.observe(14) == somma::BlockSequenceStatus::forwardGap, "forward gap detected");
    expect(tracker.observe(13) == somma::BlockSequenceStatus::stale, "stale packet detected");
    expect(tracker.observe(0) == somma::BlockSequenceStatus::restart, "zero sequence restart detected");
    expect(tracker.observe(0) == somma::BlockSequenceStatus::stale, "duplicate zero sequence is stale");

    tracker.reset();
    expect(tracker.observe(UINT32_MAX - 1u) == somma::BlockSequenceStatus::first, "wrap sequence initialized");
    expect(tracker.observe(UINT32_MAX) == somma::BlockSequenceStatus::inOrder, "maximum sequence accepted");
    expect(tracker.observe(0) == somma::BlockSequenceStatus::inOrder, "sequence wraps naturally");
}

void testProtocolAndTimeline()
{
    somma::StereoAudioPacket packet;
    packet.header.packetType = static_cast<uint16_t>(somma::PacketType::senderAudio);
    packet.header.numSamples = 1;
    packet.header.firstSamplePosition = 8192;
    packet.header.sampleRate = 48000;
    packet.interleaved[0] = 0.25f;
    packet.interleaved[1] = -0.25f;

    somma::StereoAudioPacket decoded;
    expect(somma::decodeStereoAudioPacket(&packet,
                                          somma::getStereoAudioPacketSize(packet.header.numSamples),
                                          somma::PacketType::senderAudio,
                                          decoded),
           "valid packet decodes");
    expect(decoded.header.firstSamplePosition == 8192, "sample timeline position survives packet decode");
    expect(std::memcmp(decoded.interleaved.data(), packet.interleaved.data(), 2u * sizeof(float)) == 0,
           "PCM payload survives packet decode bit-for-bit");

    packet.interleaved[0] = std::numeric_limits<float>::infinity();
    expect(!somma::decodeStereoAudioPacket(&packet,
                                           somma::getStereoAudioPacketSize(packet.header.numSamples),
                                           somma::PacketType::senderAudio,
                                           decoded),
           "non-finite payload is rejected");

    int64_t nextPosition = 0;
    expect(somma::addSampleFrames(8192, 512, nextPosition) && nextPosition == 8704,
           "sample frame ranges advance by their exact frame count");
    expect(!somma::addSampleFrames(somma::invalidSamplePosition, 1, nextPosition),
           "missing host timeline positions are rejected");
    expect(!somma::addSampleFrames(std::numeric_limits<int64_t>::max(), 1, nextPosition),
           "sample timeline overflow is rejected");
}

void testTimelineStereoBufferAlignment()
{
    static somma::TimelineStereoBuffer sender12;
    static somma::TimelineStereoBuffer sender34;
    sender12.clear();
    sender34.clear();

    const float sender12Samples[] { 0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f, 0.4f, -0.4f };
    const float sender34Samples[] { 1.3f, -1.3f, 1.4f, -1.4f };
    bool discontinuity = false;
    expect(sender12.append(100, sender12Samples, 4, discontinuity) && !discontinuity,
           "first sender block is stored at its DAW frame position");
    expect(sender34.append(102, sender34Samples, 2, discontinuity) && !discontinuity,
           "second sender block may arrive at a later DAW frame position");
    expect(sender12.canReadRange(100, 4) && sender34.canReadRange(100, 4),
           "buffers can provide one common frame range despite different starts");

    const float expected12[] { 0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f, 0.4f, -0.4f };
    const float expected34[] { 0.0f, 0.0f, 0.0f, 0.0f, 1.3f, -1.3f, 1.4f, -1.4f };
    float rendered12[8] {};
    float rendered34[8] {};
    for (int64_t frame = 100; frame < 104; ++frame)
    {
        float left = 0.0f;
        float right = 0.0f;
        expect(sender12.readAt(frame, left, right) == somma::TimelineStereoBuffer::ReadResult::available,
               "first sender is read at the requested shared frame index");
        rendered12[static_cast<size_t>(frame - 100) * 2u] = left;
        rendered12[static_cast<size_t>(frame - 100) * 2u + 1u] = right;

        const auto result = sender34.readAt(frame, left, right);
        expect(result == (frame < 102 ? somma::TimelineStereoBuffer::ReadResult::beforeStream
                                     : somma::TimelineStereoBuffer::ReadResult::available),
               "second sender is silent before its own aligned start frame");
        rendered34[static_cast<size_t>(frame - 100) * 2u] = left;
        rendered34[static_cast<size_t>(frame - 100) * 2u + 1u] = right;
    }

    expect(std::memcmp(rendered12, expected12, sizeof(expected12)) == 0,
           "first sender frames are read without interpolation or value changes");
    expect(std::memcmp(rendered34, expected34, sizeof(expected34)) == 0,
           "second sender aligns to the same DAW frame timeline without shifting");

    static somma::TimelineStereoBuffer bitExactBuffer;
    bitExactBuffer.clear();
    const std::array<float, 4> specialFloatSamples {
        -0.0f,
        std::numeric_limits<float>::denorm_min(),
        0.25f,
        -0.75f,
    };
    expect(bitExactBuffer.append(200, specialFloatSamples.data(), 2, discontinuity),
           "special PCM float values enter the frame buffer");
    std::array<float, 4> copiedSpecialFloatSamples {};
    for (int64_t frame = 200; frame < 202; ++frame)
    {
        float left = 0.0f;
        float right = 0.0f;
        expect(bitExactBuffer.readAt(frame, left, right) == somma::TimelineStereoBuffer::ReadResult::available,
               "special PCM float frame is available exactly once");
        copiedSpecialFloatSamples[static_cast<size_t>(frame - 200) * 2u] = left;
        copiedSpecialFloatSamples[static_cast<size_t>(frame - 200) * 2u + 1u] = right;
    }
    expect(std::memcmp(copiedSpecialFloatSamples.data(), specialFloatSamples.data(), sizeof(specialFloatSamples)) == 0,
           "unity transport preserves signed zero and subnormal PCM bit patterns");

    const float gapSample[] { 0.5f, -0.5f };
    expect(sender12.append(105, gapSample, 1, discontinuity), "forward sample gaps are represented in the timeline buffer");
    expect(sender12.readAt(104, rendered12[0], rendered12[1]) == somma::TimelineStereoBuffer::ReadResult::available
               && rendered12[0] == 0.0f && rendered12[1] == 0.0f,
           "missing input frames become silence at their original positions");
    expect(sender12.readAt(105, rendered12[0], rendered12[1]) == somma::TimelineStereoBuffer::ReadResult::available
               && rendered12[0] == gapSample[0] && rendered12[1] == gapSample[1],
           "later frames are not shifted to close a missing-frame gap");
}
}

int main()
{
    testBufferPolicy();
    testPortParsing();
    testSequenceTracking();
    testProtocolAndTimeline();
    testTimelineStereoBufferAlignment();

    if (failures != 0)
    {
        std::cerr << failures << " transport test(s) failed\n";
        return 1;
    }

    std::cout << "All transport tests passed\n";
    return 0;
}
