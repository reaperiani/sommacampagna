#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Protocol.h"
#include "PortDiscovery.h"
#include "SummerDSP.h"

namespace
{
constexpr int numPairs = 8;
constexpr int numInputChannels = 16;
constexpr uint16_t fixedEngineBlockSamples = 512;
constexpr uint32_t streamTimeoutMs = 300;
constexpr size_t maxActiveStreams = 128;
constexpr int maxReceiveDatagramsPerIteration = 1024;

struct StreamState
{
    bool active = false;
    bool mustStartAtMixPosition = false;
    uint32_t streamId = 0;
    uint16_t pairIndex = 0;
    uint32_t sampleRate = 48000;
    uint32_t sessionId = 1;
    uint32_t lastSeenMs = 0;
    somma::TimelineStereoBuffer audio;
    somma::BlockSequenceTracker sequence;

    void clearBuffer() noexcept
    {
        audio.clear();
    }

    void reset() noexcept
    {
        active = false;
        streamId = 0;
        mustStartAtMixPosition = false;
        clearBuffer();
        sequence.reset();
    }
};

struct EngineSharedState
{
    std::atomic<int> consoleFlavor { 0 };
    std::atomic<float> driveDb { 0.0f };
    std::atomic<int> powerSupplyType { 0 };
    std::atomic<float> outputDb { 0.0f };
    std::atomic<float> gravityPct { 0.0f };
    std::atomic<float> requestedTotalBufferMs { somma::defaultTotalTransmissionBufferMs };
    std::atomic<float> appliedTotalBufferMs { somma::defaultTotalTransmissionBufferMs };
    std::atomic<bool> bufferChangePending { false };
    std::atomic<bool> bypass { false };

    std::array<std::atomic<float>, numInputChannels> inputMetersDb;
    std::atomic<float> sagPct { 0.0f };
    std::atomic<uint32_t> activeStreams { 0 };
    std::atomic<uint32_t> primedStreams { 0 };
    std::atomic<uint32_t> receivedPackets { 0 };
    std::atomic<uint32_t> invalidPackets { 0 };
    std::atomic<uint32_t> invalidTimelinePackets { 0 };
    std::atomic<uint32_t> rejectedPackets { 0 };
    std::atomic<uint32_t> sequenceResets { 0 };
    std::atomic<uint32_t> timelineDiscontinuities { 0 };
    std::atomic<uint32_t> stalePackets { 0 };
    std::atomic<uint32_t> underflows { 0 };
    std::atomic<uint32_t> backlogEvents { 0 };
    std::atomic<uint32_t> sendErrors { 0 };
    std::atomic<uint32_t> averageOccupancyFrames { 0 };
    std::atomic<uint32_t> targetBufferFrames { 0 };
    std::atomic<int64_t> mixSamplePosition { somma::invalidSamplePosition };
    std::atomic<bool> configPublished { false };

    EngineSharedState()
    {
        for (auto& m : inputMetersDb)
            m.store(-100.0f, std::memory_order_relaxed);
    }
};

uint32_t nowMs() { return juce::Time::getMillisecondCounter(); }

size_t getTargetBufferFrames(float bufferMs, uint32_t sampleRate) noexcept
{
    const auto safeBufferMs = somma::sanitizeTransmissionBufferMs(bufferMs);
    const auto safeSampleRate = sampleRate > 0 ? sampleRate : 48000u;
    const auto frames = static_cast<size_t>((static_cast<double>(safeBufferMs) * static_cast<double>(safeSampleRate)) / 1000.0);
    return juce::jlimit(static_cast<size_t>(fixedEngineBlockSamples), somma::maxBufferedFrames - static_cast<size_t>(fixedEngineBlockSamples), frames);
}

bool canMixRange(const std::array<StreamState, maxActiveStreams>& streams,
                 int64_t firstSamplePosition,
                 uint32_t numSamples,
                 uint32_t sessionId,
                 uint32_t sampleRate) noexcept
{
    for (const auto& stream : streams)
    {
        if (!stream.active || stream.sessionId != sessionId || stream.sampleRate != sampleRate)
            continue;

        const auto streamStart = stream.audio.getFirstSamplePosition();
        if (stream.mustStartAtMixPosition
            && streamStart != somma::invalidSamplePosition
            && streamStart > firstSamplePosition
            && static_cast<uint64_t>(streamStart) - static_cast<uint64_t>(firstSamplePosition) > numSamples)
        {
            return false;
        }

        if (!stream.audio.canReadRange(firstSamplePosition, numSamples))
            return false;
    }

    return true;
}

bool hasInputAtSamplePosition(const std::array<StreamState, maxActiveStreams>& streams,
                              int64_t samplePosition,
                              uint32_t sessionId,
                              uint32_t sampleRate) noexcept
{
    for (const auto& stream : streams)
    {
        if (!stream.active || stream.sessionId != sessionId || stream.sampleRate != sampleRate)
            continue;

        if (stream.audio.getFirstSamplePosition() <= samplePosition
            && stream.audio.getReadSamplePosition() <= samplePosition
            && stream.audio.getWriteSamplePosition() > samplePosition)
        {
            return true;
        }
    }

    return false;
}

somma::UdpPorts makePublishedPorts(uint16_t inputPort, uint16_t outputPort, float totalBufferMs)
{
    somma::UdpPorts ports;
    ports.senderToEngine = inputPort;
    ports.engineToReceiver = outputPort;
    ports.transmissionBufferMs = somma::getReceiverTransmissionBufferMs(totalBufferMs);
    ports.hasTransmissionBufferMs = true;
    ports.totalTransmissionBufferMs = somma::sanitizeTotalTransmissionBufferMs(totalBufferMs);
    return ports;
}
}

class EngineThread final : public juce::Thread
{
public:
    explicit EngineThread(EngineSharedState& s)
        : juce::Thread("SommaEngineThread"), shared(s)
    {
    }

    void run() override
    {
        juce::DatagramSocket inSocket;
        const auto selectedInPort = findAvailablePort(inSocket, somma::senderToEnginePort);

        juce::DatagramSocket outSocket;
        const auto selectedOutPort = findAvailablePortByProbe(somma::engineToReceiverPort);
        auto appliedTotalBufferMs = somma::sanitizeTotalTransmissionBufferMs(shared.appliedTotalBufferMs.load(std::memory_order_relaxed));
        bool configPublished = somma::writePorts(makePublishedPorts(selectedInPort, selectedOutPort, appliedTotalBufferMs));
        shared.configPublished.store(configPublished, std::memory_order_relaxed);

        std::array<std::array<float, somma::maxSamplesPerPacket>, numInputChannels> mixInputs {};
        std::array<float*, numInputChannels> mixPtrs {};
        std::array<float, somma::maxSamplesPerPacket> outL {};
        std::array<float, somma::maxSamplesPerPacket> outR {};
        std::array<char, somma::maximumInspectedDatagramSize> datagram;
        somma::StereoAudioPacket packet;
        somma::StereoAudioPacket outPacket;

        HotSummerDSP dsp;
        HotSummerParams params;
        uint32_t sampleRate = 48000;
        bool sampleRateLocked = false;
        dsp.prepare(static_cast<double>(sampleRate));

        uint32_t outBlock = 0;
        uint32_t inputSessionId = 1;
        const auto outputSessionId = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt());
        int64_t mixSamplePosition = somma::invalidSamplePosition;
        uint32_t lastConfigWriteMs = nowMs();

        while (!threadShouldExit())
        {
            int datagramsRead = 0;
            const auto nominalBlockMs = (static_cast<double>(fixedEngineBlockSamples) / static_cast<double>(sampleRate)) * 1000.0;
            const auto receiveDeadline = juce::Time::getMillisecondCounterHiRes() + juce::jlimit(0.25, 2.0, nominalBlockMs * 0.2);
            for (; datagramsRead < maxReceiveDatagramsPerIteration; ++datagramsRead)
            {
                if (datagramsRead > 0 && juce::Time::getMillisecondCounterHiRes() >= receiveDeadline)
                    break;

                const int bytes = inSocket.read(datagram.data(), static_cast<int>(datagram.size()), false);
                if (bytes <= 0)
                    break;

                if (!somma::decodeStereoAudioPacket(datagram.data(),
                                                    static_cast<size_t>(bytes),
                                                     somma::PacketType::senderAudio,
                                                     packet))
                {
                    shared.invalidPackets.fetch_add(1u, std::memory_order_relaxed);
                    continue;
                }

                if (!somma::isValidSamplePosition(packet.header.firstSamplePosition))
                {
                    shared.invalidTimelinePackets.fetch_add(1u, std::memory_order_relaxed);
                    continue;
                }

                if (sampleRateLocked && packet.header.sampleRate != sampleRate)
                {
                    shared.rejectedPackets.fetch_add(1u, std::memory_order_relaxed);
                    continue;
                }

                if (!sampleRateLocked)
                {
                    sampleRate = packet.header.sampleRate;
                    inputSessionId = packet.header.sessionId;
                    dsp.prepare(static_cast<double>(sampleRate));
                    sampleRateLocked = true;
                }

                if (packet.header.sessionId != inputSessionId)
                {
                    shared.rejectedPackets.fetch_add(1u, std::memory_order_relaxed);
                    continue;
                }

                auto* stream = findStream(packet.header.streamId);
                if (stream == nullptr)
                    stream = allocateStream(packet.header.streamId, nowMs());
                if (stream == nullptr)
                {
                    shared.rejectedPackets.fetch_add(1u, std::memory_order_relaxed);
                    continue;
                }

                auto& st = *stream;
                const auto sequenceStatus = st.sequence.observe(packet.header.blockIndex);
                if (sequenceStatus == somma::BlockSequenceStatus::stale)
                {
                    shared.stalePackets.fetch_add(1u, std::memory_order_relaxed);
                    continue;
                }

                if (sequenceStatus == somma::BlockSequenceStatus::forwardGap)
                    shared.sequenceResets.fetch_add(1u, std::memory_order_relaxed);

                const auto expectedSamplePosition = st.audio.getWriteSamplePosition();
                const bool streamTimelineJump = sequenceStatus == somma::BlockSequenceStatus::restart
                                             || (expectedSamplePosition != somma::invalidSamplePosition
                                                 && ((sequenceStatus == somma::BlockSequenceStatus::inOrder
                                                      && packet.header.firstSamplePosition != expectedSamplePosition)
                                                     || (sequenceStatus == somma::BlockSequenceStatus::forwardGap
                                                         && packet.header.firstSamplePosition < expectedSamplePosition)));
                if (streamTimelineJump)
                {
                    for (auto& activeStream : streams)
                    {
                        if (activeStream.active)
                        {
                            activeStream.clearBuffer();
                            activeStream.mustStartAtMixPosition = true;
                        }
                    }

                    mixSamplePosition = packet.header.firstSamplePosition;
                    shared.timelineDiscontinuities.fetch_add(1u, std::memory_order_relaxed);
                }

                if (st.audio.getAvailableFrames() > 0 && st.pairIndex != packet.header.pairIndex)
                {
                    st.clearBuffer();
                    shared.sequenceResets.fetch_add(1u, std::memory_order_relaxed);
                }

                st.pairIndex = packet.header.pairIndex;
                st.sampleRate = packet.header.sampleRate;
                st.sessionId = packet.header.sessionId;
                st.lastSeenMs = nowMs();

                bool timelineDiscontinuity = false;
                if (!st.audio.append(packet.header.firstSamplePosition,
                                     packet.interleaved.data(),
                                     packet.header.numSamples,
                                     timelineDiscontinuity))
                {
                    shared.invalidTimelinePackets.fetch_add(1u, std::memory_order_relaxed);
                    continue;
                }

                if (timelineDiscontinuity)
                    shared.timelineDiscontinuities.fetch_add(1u, std::memory_order_relaxed);

                if (mixSamplePosition == somma::invalidSamplePosition)
                    mixSamplePosition = packet.header.firstSamplePosition;
                shared.receivedPackets.fetch_add(1u, std::memory_order_relaxed);
            }

            if (datagramsRead == maxReceiveDatagramsPerIteration
                || inSocket.waitUntilReady(true, 0) > 0)
            {
                shared.backlogEvents.fetch_add(1u, std::memory_order_relaxed);
            }

            const uint32_t t = nowMs();
            const auto targetBufferFrames = getTargetBufferFrames(somma::getEngineTransmissionBufferMs(appliedTotalBufferMs), sampleRate);
            shared.targetBufferFrames.store(static_cast<uint32_t>(targetBufferFrames), std::memory_order_relaxed);
            size_t activeStreamCount = 0;
            for (auto& st : streams)
            {
                if (!st.active)
                    continue;

                if ((t - st.lastSeenMs) > streamTimeoutMs)
                {
                    st.reset();
                    continue;
                }

                ++activeStreamCount;
            }

            const auto requestedTotalBufferMs = somma::sanitizeTotalTransmissionBufferMs(shared.requestedTotalBufferMs.load(std::memory_order_relaxed));
            const bool bufferChangePending = requestedTotalBufferMs != appliedTotalBufferMs;
            shared.bufferChangePending.store(bufferChangePending, std::memory_order_relaxed);
            if (bufferChangePending && activeStreamCount == 0)
            {
                appliedTotalBufferMs = requestedTotalBufferMs;
                shared.appliedTotalBufferMs.store(appliedTotalBufferMs, std::memory_order_relaxed);
                shared.bufferChangePending.store(false, std::memory_order_relaxed);
                configPublished = somma::writePorts(makePublishedPorts(selectedInPort, selectedOutPort, appliedTotalBufferMs));
                shared.configPublished.store(configPublished, std::memory_order_relaxed);
                lastConfigWriteMs = nowMs();
            }

            const uint32_t configNow = nowMs();
            if (!configPublished && (configNow - lastConfigWriteMs) > 500u)
            {
                configPublished = somma::writePorts(makePublishedPorts(selectedInPort, selectedOutPort, appliedTotalBufferMs));
                shared.configPublished.store(configPublished, std::memory_order_relaxed);
                lastConfigWriteMs = configNow;
            }

            if (activeStreamCount == 0)
            {
                sampleRateLocked = false;
                mixSamplePosition = somma::invalidSamplePosition;
            }

            size_t readyStreamCount = 0;
            double totalOccupancy = 0.0;
            for (const auto& st : streams)
            {
                if (!st.active || st.sessionId != inputSessionId || st.sampleRate != sampleRate)
                    continue;

                totalOccupancy += static_cast<double>(st.audio.getAvailableFrames());
                if (mixSamplePosition != somma::invalidSamplePosition
                    && st.audio.canReadRange(mixSamplePosition, fixedEngineBlockSamples))
                {
                    ++readyStreamCount;
                }
            }

            shared.activeStreams.store(static_cast<uint32_t>(activeStreamCount), std::memory_order_relaxed);
            shared.primedStreams.store(static_cast<uint32_t>(readyStreamCount), std::memory_order_relaxed);
            shared.averageOccupancyFrames.store(activeStreamCount > 0
                                                     ? static_cast<uint32_t>(totalOccupancy / static_cast<double>(activeStreamCount))
                                                     : 0u,
                                                 std::memory_order_relaxed);
            shared.mixSamplePosition.store(mixSamplePosition, std::memory_order_relaxed);

            int64_t rangeEnd = 0;
            const auto requiredFrames = static_cast<uint32_t>(targetBufferFrames + fixedEngineBlockSamples);
            const bool rangeValid = somma::addSampleFrames(mixSamplePosition, requiredFrames, rangeEnd);
            const bool canProcessBlock = activeStreamCount > 0
                                      && sampleRateLocked
                                      && rangeValid
                                      && hasInputAtSamplePosition(streams, mixSamplePosition, inputSessionId, sampleRate)
                                      && canMixRange(streams,
                                                     mixSamplePosition,
                                                     requiredFrames,
                                                     inputSessionId,
                                                     sampleRate);

            if (!canProcessBlock)
            {
                juce::Thread::sleep(1);
                continue;
            }

            params.consoleFlavor = shared.consoleFlavor.load(std::memory_order_relaxed);
            params.compensatedDriveDb = shared.driveDb.load(std::memory_order_relaxed);
            params.powerSupplyType = shared.powerSupplyType.load(std::memory_order_relaxed);
            params.masterOutputDb = shared.outputDb.load(std::memory_order_relaxed);
            params.gravityPct = shared.gravityPct.load(std::memory_order_relaxed);
            params.bypass = shared.bypass.load(std::memory_order_relaxed);

            for (auto& channel : mixInputs)
                channel.fill(0.0f);

            for (auto& st : streams)
            {
                if (!st.active || st.sessionId != inputSessionId || st.sampleRate != sampleRate)
                    continue;

                const int base = st.pairIndex * 2;
                for (uint32_t i = 0; i < fixedEngineBlockSamples; ++i)
                {
                    const auto framePosition = mixSamplePosition + static_cast<int64_t>(i);
                    float left = 0.0f;
                    float right = 0.0f;
                    const auto result = st.audio.readAt(framePosition, left, right);
                    if (result == somma::TimelineStereoBuffer::ReadResult::available)
                    {
                        mixInputs[static_cast<size_t>(base)][i] += left;
                        mixInputs[static_cast<size_t>(base + 1)][i] += right;
                    }
                    else if (result == somma::TimelineStereoBuffer::ReadResult::unavailable)
                    {
                        shared.underflows.fetch_add(1u, std::memory_order_relaxed);
                    }
                }

                if (st.mustStartAtMixPosition
                    && st.audio.getFirstSamplePosition() <= mixSamplePosition)
                {
                    st.mustStartAtMixPosition = false;
                }
            }

            for (int ch = 0; ch < numInputChannels; ++ch)
            {
                mixPtrs[static_cast<size_t>(ch)] = mixInputs[static_cast<size_t>(ch)].data();
                float peak = 0.0f;
                for (uint32_t n = 0; n < fixedEngineBlockSamples; ++n)
                    peak = juce::jmax(peak, std::abs(mixInputs[static_cast<size_t>(ch)][n]));
                shared.inputMetersDb[static_cast<size_t>(ch)].store(juce::Decibels::gainToDecibels(peak, -100.0f), std::memory_order_relaxed);
            }

            dsp.process(mixPtrs.data(), outL.data(), outR.data(), fixedEngineBlockSamples, params);
            shared.sagPct.store(dsp.getBusStressPercent(), std::memory_order_relaxed);

            outPacket.header.magic = somma::protocolMagic;
            outPacket.header.version = somma::protocolVersion;
            outPacket.header.packetType = static_cast<uint16_t>(somma::PacketType::mainStereoSum);
            outPacket.header.sessionId = outputSessionId;
            outPacket.header.blockIndex = outBlock++;
            outPacket.header.firstSamplePosition = mixSamplePosition;
            outPacket.header.sampleRate = sampleRate;
            outPacket.header.numSamples = fixedEngineBlockSamples;

            for (uint32_t i = 0; i < fixedEngineBlockSamples; ++i)
            {
                outPacket.interleaved[static_cast<size_t>(i * 2u)] = outL[i];
                outPacket.interleaved[static_cast<size_t>(i * 2u + 1u)] = outR[i];
            }

            const int bytesToSend = static_cast<int>(somma::getStereoAudioPacketSize(fixedEngineBlockSamples));
            if (outSocket.write("127.0.0.1", static_cast<int>(selectedOutPort), reinterpret_cast<const char*>(&outPacket), bytesToSend) != bytesToSend)
                shared.sendErrors.fetch_add(1u, std::memory_order_relaxed);

            mixSamplePosition += fixedEngineBlockSamples;
            shared.mixSamplePosition.store(mixSamplePosition, std::memory_order_relaxed);
        }
    }

private:
    StreamState* findStream(uint32_t streamId) noexcept
    {
        for (auto& stream : streams)
        {
            if (stream.active && stream.streamId == streamId)
                return &stream;
        }

        return nullptr;
    }

    StreamState* allocateStream(uint32_t streamId, uint32_t timeNow) noexcept
    {
        for (auto& stream : streams)
        {
            if (!stream.active || (timeNow - stream.lastSeenMs) > streamTimeoutMs)
            {
                stream.reset();
                stream.active = true;
                stream.streamId = streamId;
                stream.lastSeenMs = timeNow;
                return &stream;
            }
        }

        return nullptr;
    }

    static uint16_t findAvailablePort(juce::DatagramSocket& socket, uint16_t preferred)
    {
        for (int i = 0; i < 200; ++i)
        {
            const auto candidate = static_cast<uint16_t>(preferred + i);
            if (socket.bindToPort((int) candidate, "127.0.0.1"))
                return candidate;
        }
        return preferred;
    }

    static uint16_t findAvailablePortByProbe(uint16_t preferred)
    {
        for (int i = 0; i < 200; ++i)
        {
            const auto candidate = static_cast<uint16_t>(preferred + i);
            juce::DatagramSocket probe;
            if (probe.bindToPort((int) candidate, "127.0.0.1"))
                return candidate;
        }
        return preferred;
    }

    EngineSharedState& shared;
    std::array<StreamState, maxActiveStreams> streams;
};

class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    explicit MainComponent(EngineSharedState& s)
        : shared(s)
    {
        addAndMakeVisible(flavor);
        flavor.addItemList({ "Signature", "Vintage British", "Discrete American", "Modern Ultra-Linear" }, 1);
        flavor.onChange = [this] { shared.consoleFlavor.store(flavor.getSelectedItemIndex(), std::memory_order_relaxed); };
        flavor.setSelectedItemIndex(0);

        configureSlider(drive, -12.0, 18.0, 0.0, [this](double v) { shared.driveDb.store((float) v, std::memory_order_relaxed); });
        configureSlider(output, -12.0, 12.0, 0.0, [this](double v) { shared.outputDb.store((float) v, std::memory_order_relaxed); });
        configureSlider(gravity, 0.0, 100.0, 0.0, [this](double v) { shared.gravityPct.store((float) v, std::memory_order_relaxed); });
        configureSlider(transmissionBuffer,
                        somma::minTotalTransmissionBufferMs,
                        somma::maxTotalTransmissionBufferMs,
                        shared.requestedTotalBufferMs.load(std::memory_order_relaxed),
                        [this](double v)
                        {
                            shared.requestedTotalBufferMs.store(somma::sanitizeTotalTransmissionBufferMs((float) v), std::memory_order_relaxed);
                        });
        transmissionBuffer.setRange(somma::minTotalTransmissionBufferMs, somma::maxTotalTransmissionBufferMs, 1.0);

        addAndMakeVisible(bufferStatus);
        bufferStatus.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(transportStatus);
        transportStatus.setJustificationType(juce::Justification::topLeft);

        addAndMakeVisible(psu);
        psu.addItemList({ "Custom", "Vintage", "Modern", "Mastering" }, 1);
        psu.onChange = [this] { shared.powerSupplyType.store(psu.getSelectedItemIndex(), std::memory_order_relaxed); };
        psu.setSelectedItemIndex(0);

        addAndMakeVisible(bypass);
        bypass.setButtonText("Bypass");
        bypass.onClick = [this] { shared.bypass.store(bypass.getToggleState(), std::memory_order_relaxed); };

        setSize(720, 620);
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff111418));
        g.setColour(juce::Colours::white);
        g.drawFittedText("sommacampagna_engine", 10, 8, getWidth() - 20, 24, juce::Justification::centred, 1);
        g.setFont(14.0f);
        g.drawFittedText("Console Flavor", 20, 26, 220, 18, juce::Justification::left, 1);
        g.drawFittedText("Power Supply", 300, 26, 220, 18, juce::Justification::left, 1);
        g.drawFittedText("Drive (dB)", 20, 68, 120, 18, juce::Justification::left, 1);
        g.drawFittedText("Output (dB)", 20, 106, 120, 18, juce::Justification::left, 1);
        g.drawFittedText("Gravity (%)", 20, 144, 120, 18, juce::Justification::left, 1);
        g.drawFittedText("Total Buffer Target (ms)", 20, 182, 220, 18, juce::Justification::left, 1);

        auto meterArea = juce::Rectangle<float>(20.0f, 300.0f, 400.0f, 280.0f);
        const float gap = 8.0f;
        const float w = (meterArea.getWidth() - gap * 3.0f) / 4.0f;
        const float h = (meterArea.getHeight() - gap * 3.0f) / 4.0f;
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                const int idx = r * 4 + c;
                const auto b = juce::Rectangle<float>(meterArea.getX() + c * (w + gap), meterArea.getY() + r * (h + gap), w, h);
                const float db = meterDb[(size_t) idx];
                juce::Colour col = juce::Colour(0xff1d2227);
                if (db >= -1.0f) col = juce::Colour(0xffff4545);
                else if (db >= -8.0f) col = juce::Colour(0xffffd249);
                else if (db >= -60.0f) col = juce::Colour(0xff6dff72);
                g.setColour(col);
                g.fillRoundedRectangle(b, 6.0f);
            }
        }

        g.setColour(juce::Colours::white);
        g.drawFittedText("BUS STRESS (SAG)", 480, 320, 180, 20, juce::Justification::centred, 1);
        g.setColour(juce::Colour(0xff2a3138));
        g.fillRect(480, 346, 180, 24);
        g.setColour(juce::Colour(0xffff7a45));
        g.fillRect(480, 346, static_cast<int>(1.8f * sagPct), 24);
        g.setColour(juce::Colours::white);
        g.drawRect(480, 346, 180, 24);
        g.drawFittedText(juce::String(sagPct, 1) + " %", 480, 374, 180, 20, juce::Justification::centred, 1);

        g.setColour(juce::Colours::lightgrey);
        g.setFont(11.0f);
        g.drawFittedText("AGPL-3.0 | No warranty | Source: github.com/reaperiani/sommacampagna",
                         20, getHeight() - 24, getWidth() - 40, 16, juce::Justification::centred, 1);
    }

    void resized() override
    {
        flavor.setBounds(20, 44, 260, 28);
        psu.setBounds(300, 44, 220, 28);
        drive.setBounds(20, 86, 300, 28);
        output.setBounds(20, 124, 300, 28);
        gravity.setBounds(20, 162, 300, 28);
        transmissionBuffer.setBounds(20, 200, 300, 28);
        bypass.setBounds(340, 198, 120, 28);
        bufferStatus.setBounds(20, 230, getWidth() - 40, 24);
        transportStatus.setBounds(20, 256, getWidth() - 40, 42);
    }

private:
    void timerCallback() override
    {
        for (int i = 0; i < numInputChannels; ++i)
            meterDb[(size_t) i] = shared.inputMetersDb[(size_t) i].load(std::memory_order_relaxed);
        sagPct = shared.sagPct.load(std::memory_order_relaxed);

        const auto requested = shared.requestedTotalBufferMs.load(std::memory_order_relaxed);
        const auto applied = shared.appliedTotalBufferMs.load(std::memory_order_relaxed);
        const auto pending = shared.bufferChangePending.load(std::memory_order_relaxed);
        bufferStatus.setText("Applied " + juce::String(applied, 0) + " ms: engine "
                                 + juce::String(somma::getEngineTransmissionBufferMs(applied), 0) + " ms / receiver "
                                 + juce::String(somma::getReceiverTransmissionBufferMs(applied), 0) + " ms"
                                 + (pending ? " | pending " + juce::String(requested, 0) + " ms (stop playback to apply)" : ""),
                             juce::dontSendNotification);

        transportStatus.setText("Streams " + juce::String(shared.activeStreams.load(std::memory_order_relaxed))
                                    + " active / " + juce::String(shared.primedStreams.load(std::memory_order_relaxed)) + " primed"
                                    + " | ring " + juce::String(shared.averageOccupancyFrames.load(std::memory_order_relaxed)) + "/"
                                    + juce::String(shared.targetBufferFrames.load(std::memory_order_relaxed)) + " fr"
                                    + " | mix frame " + juce::String(shared.mixSamplePosition.load(std::memory_order_relaxed)) + "\n"
                                    + "Rx " + juce::String(shared.receivedPackets.load(std::memory_order_relaxed))
                                    + " | invalid/untimed/rejected " + juce::String(shared.invalidPackets.load(std::memory_order_relaxed)) + "/"
                                    + juce::String(shared.invalidTimelinePackets.load(std::memory_order_relaxed)) + "/"
                                    + juce::String(shared.rejectedPackets.load(std::memory_order_relaxed))
                                    + " | seq/stale " + juce::String(shared.sequenceResets.load(std::memory_order_relaxed)) + "/"
                                    + juce::String(shared.stalePackets.load(std::memory_order_relaxed))
                                    + " | timeline/underflow " + juce::String(shared.timelineDiscontinuities.load(std::memory_order_relaxed)) + "/"
                                    + juce::String(shared.underflows.load(std::memory_order_relaxed))
                                    + " | backlog/send errors " + juce::String(shared.backlogEvents.load(std::memory_order_relaxed)) + "/"
                                    + juce::String(shared.sendErrors.load(std::memory_order_relaxed))
                                    + (shared.configPublished.load(std::memory_order_relaxed) ? " | config OK" : " | config ERROR"),
                                 juce::dontSendNotification);
        repaint();
    }

    void configureSlider(juce::Slider& s, double min, double max, double value, std::function<void(double)> cb)
    {
        addAndMakeVisible(s);
        s.setRange(min, max, 0.1);
        s.setValue(value);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
        s.onValueChange = [&, cb] { cb(s.getValue()); };
    }

    EngineSharedState& shared;
    juce::ComboBox flavor;
    juce::ComboBox psu;
    juce::Slider drive;
    juce::Slider output;
    juce::Slider gravity;
    juce::Slider transmissionBuffer;
    juce::ToggleButton bypass;
    juce::Label bufferStatus;
    juce::Label transportStatus;

    std::array<float, numInputChannels> meterDb {};
    float sagPct = 0.0f;
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(juce::String name, EngineSharedState& s)
        : DocumentWindow(name,
                         juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(s), true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class SommaEngineApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "sommacampagna_engine"; }
    const juce::String getApplicationVersion() override { return SOMMACAMPAGNA_VERSION; }

    void initialise(const juce::String&) override
    {
        const auto previousConfig = somma::readPorts();
        const auto initialTotalBufferMs = previousConfig.totalTransmissionBufferMs.has_value()
                                            ? *previousConfig.totalTransmissionBufferMs
                                            : (previousConfig.hasTransmissionBufferMs
                                                   ? somma::migrateLegacyTransmissionBufferMs(previousConfig.transmissionBufferMs)
                                                   : somma::defaultTotalTransmissionBufferMs);
        shared.requestedTotalBufferMs.store(initialTotalBufferMs, std::memory_order_relaxed);
        shared.appliedTotalBufferMs.store(initialTotalBufferMs, std::memory_order_relaxed);
        engineThread = std::make_unique<EngineThread>(shared);
        engineThread->startThread();
        mainWindow = std::make_unique<MainWindow>(getApplicationName(), shared);
    }

    void shutdown() override
    {
        if (engineThread != nullptr)
        {
            engineThread->signalThreadShouldExit();
            engineThread->stopThread(2000);
        }
        mainWindow.reset();
        engineThread.reset();
    }

private:
    EngineSharedState shared;
    std::unique_ptr<EngineThread> engineThread;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(SommaEngineApplication)
