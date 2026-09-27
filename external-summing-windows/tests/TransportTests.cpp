#include <cmath>
#include <cstdint>
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

void testProtocolAndClockCorrection()
{
    somma::StereoAudioPacket packet;
    packet.header.packetType = static_cast<uint16_t>(somma::PacketType::senderAudio);
    packet.header.numSamples = 1;
    packet.header.sampleRate = 48000;
    packet.interleaved[0] = 0.25f;
    packet.interleaved[1] = -0.25f;

    somma::StereoAudioPacket decoded;
    expect(somma::decodeStereoAudioPacket(&packet,
                                          somma::getStereoAudioPacketSize(packet.header.numSamples),
                                          somma::PacketType::senderAudio,
                                          decoded),
           "valid packet decodes");

    packet.interleaved[0] = std::numeric_limits<float>::infinity();
    expect(!somma::decodeStereoAudioPacket(&packet,
                                           somma::getStereoAudioPacketSize(packet.header.numSamples),
                                           somma::PacketType::senderAudio,
                                           decoded),
           "non-finite payload is rejected");

    expect(somma::getClockCorrectionTarget(16.0) == 0.0, "clock deadband includes boundary");
    expect(somma::getClockCorrectionTarget(100000.0) == somma::maxClockCorrectionRatio,
           "positive clock correction is capped");
    expect(somma::getClockCorrectionTarget(-100000.0) == -somma::maxClockCorrectionRatio,
           "negative clock correction is capped");
}
}

int main()
{
    testBufferPolicy();
    testPortParsing();
    testSequenceTracking();
    testProtocolAndClockCorrection();

    if (failures != 0)
    {
        std::cerr << failures << " transport test(s) failed\n";
        return 1;
    }

    std::cout << "All transport tests passed\n";
    return 0;
}
