#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "Protocol.h"
#include "PortDiscovery.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
constexpr auto paramPair = "pairIndex";
constexpr auto paramGain = "preSendGainDb";
constexpr auto paramBypass = "bypassSend";
}

class SenderNetworkThread final : public juce::Thread
{
public:
    explicit SenderNetworkThread(SenderAudioProcessor& p)
        : juce::Thread("SommaSenderNetwork"), processor(p)
    {
    }

    void run() override
    {
        juce::DatagramSocket socket;
        if (!socket.bindToPort(0, "127.0.0.1"))
            processor.sendErrors.fetch_add(1u, std::memory_order_relaxed);
        processor.workerRunning.store(true, std::memory_order_release);

        uint16_t targetPort = somma::senderToEnginePort;
        uint32_t lastPortPollMs = 0;

        while (!threadShouldExit())
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (lastPortPollMs == 0 || (now - lastPortPollMs) > 500u)
            {
                targetPort = somma::readPorts().senderToEngine;
                processor.targetPort.store(targetPort, std::memory_order_relaxed);
                lastPortPollMs = now;
            }

            somma::StereoAudioPacket packet;
            int packetsSent = 0;
            while (packetsSent < 64 && processor.popPacket(packet))
            {
                const auto bytesToSend = somma::getStereoAudioPacketSize(packet.header.numSamples);
                const auto bytesWritten = socket.write("127.0.0.1",
                                                       static_cast<int>(targetPort),
                                                       reinterpret_cast<const char*>(&packet),
                                                       static_cast<int>(bytesToSend));
                if (bytesWritten == static_cast<int>(bytesToSend))
                    processor.packetsSent.fetch_add(1u, std::memory_order_relaxed);
                else
                    processor.sendErrors.fetch_add(1u, std::memory_order_relaxed);
                ++packetsSent;
            }

            if (packetsSent == 0)
                juce::Thread::sleep(1);
        }

        processor.workerRunning.store(false, std::memory_order_release);
    }

private:
    SenderAudioProcessor& processor;
};

SenderAudioProcessor::SenderAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    streamId = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt());
}

SenderAudioProcessor::~SenderAudioProcessor()
{
    stopNetworkThread();
}

void SenderAudioProcessor::prepareToPlay(double sampleRate, int)
{
    stopNetworkThread();
    const auto roundedSampleRate = std::isfinite(sampleRate) && sampleRate > 0.0
                                 ? static_cast<uint32_t>(std::lround(sampleRate))
                                 : 0u;
    currentSampleRate = somma::isSupportedSampleRate(roundedSampleRate) ? roundedSampleRate : 48000u;
    packetReadPosition.store(0, std::memory_order_relaxed);
    packetWritePosition.store(0, std::memory_order_relaxed);
    queueHighWater.store(0, std::memory_order_relaxed);
    droppedPackets.store(0, std::memory_order_relaxed);
    droppedFrames.store(0, std::memory_order_relaxed);
    packetsSent.store(0, std::memory_order_relaxed);
    sendErrors.store(0, std::memory_order_relaxed);
    targetPort.store(somma::senderToEnginePort, std::memory_order_relaxed);
    workerRunning.store(false, std::memory_order_relaxed);
    timelinePositionAvailable.store(false, std::memory_order_relaxed);
    startNetworkThread();
}

void SenderAudioProcessor::releaseResources()
{
    stopNetworkThread();
    packetReadPosition.store(0, std::memory_order_relaxed);
    packetWritePosition.store(0, std::memory_order_relaxed);
}

bool SenderAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SenderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto muteOutput = [&buffer]()
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
    };

    if (buffer.getNumChannels() < 2)
    {
        timelinePositionAvailable.store(false, std::memory_order_relaxed);
        muteOutput();
        return;
    }

    const bool bypass = apvts.getRawParameterValue(paramBypass)->load() > 0.5f;
    if (bypass)
    {
        timelinePositionAvailable.store(false, std::memory_order_relaxed);
        muteOutput();
        return;
    }

    const float gainDb = apvts.getRawParameterValue(paramGain)->load();
    const float gain = juce::Decibels::decibelsToGain(gainDb);
    const int pairIndex = juce::jlimit(0, 7, static_cast<int>(apvts.getRawParameterValue(paramPair)->load()));

    const float* inL = buffer.getReadPointer(0);
    const float* inR = buffer.getReadPointer(1);
    const int blockSamples = buffer.getNumSamples();
    auto firstSamplePosition = somma::invalidSamplePosition;
    if (const auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto hostSamplePosition = position->getTimeInSamples())
                firstSamplePosition = *hostSamplePosition;
        }
    }
    timelinePositionAvailable.store(somma::isValidSamplePosition(firstSamplePosition), std::memory_order_relaxed);

    for (int offset = 0; offset < blockSamples; offset += somma::maxSamplesPerPacket)
    {
        const int packetSamples = juce::jmin(somma::maxSamplesPerPacket, blockSamples - offset);
        const auto packetBlockIndex = blockIndex++;
        auto* packet = beginPacket();
        if (packet == nullptr)
        {
            droppedPackets.fetch_add(1u, std::memory_order_relaxed);
            droppedFrames.fetch_add(static_cast<uint32_t>(packetSamples), std::memory_order_relaxed);
            continue;
        }

        packet->header.magic = somma::protocolMagic;
        packet->header.version = somma::protocolVersion;
        packet->header.packetType = static_cast<uint16_t>(somma::PacketType::senderAudio);
        packet->header.sessionId = 1;
        packet->header.streamId = streamId;
        packet->header.blockIndex = packetBlockIndex;
        packet->header.firstSamplePosition = firstSamplePosition;
        if (somma::isValidSamplePosition(firstSamplePosition)
            && !somma::addSampleFrames(firstSamplePosition, static_cast<uint32_t>(offset), packet->header.firstSamplePosition))
        {
            packet->header.firstSamplePosition = somma::invalidSamplePosition;
        }
        packet->header.sampleRate = currentSampleRate;
        packet->header.numSamples = static_cast<uint16_t>(packetSamples);
        packet->header.pairIndex = static_cast<uint16_t>(pairIndex);

        for (int i = 0; i < packetSamples; ++i)
        {
            const auto sourceLeft = inL[offset + i];
            const auto sourceRight = inR[offset + i];
            packet->interleaved[static_cast<size_t>(i * 2)] = gain == 1.0f ? sourceLeft : sourceLeft * gain;
            packet->interleaved[static_cast<size_t>(i * 2 + 1)] = gain == 1.0f ? sourceRight : sourceRight * gain;
        }

        commitPacket();
    }

    muteOutput();
}

void SenderAudioProcessor::startNetworkThread()
{
    networkThread = std::make_unique<SenderNetworkThread>(*this);
    networkThread->startThread();
}

void SenderAudioProcessor::stopNetworkThread()
{
    if (networkThread == nullptr)
        return;

    networkThread->signalThreadShouldExit();
    networkThread->stopThread(2000);
    networkThread.reset();
}

somma::StereoAudioPacket* SenderAudioProcessor::beginPacket() noexcept
{
    const auto writePosition = packetWritePosition.load(std::memory_order_relaxed);
    const auto readPosition = packetReadPosition.load(std::memory_order_acquire);
    if (writePosition - readPosition >= packetQueueCapacity)
        return nullptr;

    return &packetQueue[writePosition % packetQueueCapacity];
}

void SenderAudioProcessor::commitPacket() noexcept
{
    const auto writePosition = packetWritePosition.load(std::memory_order_relaxed);
    packetWritePosition.store(writePosition + 1u, std::memory_order_release);

    const auto readPosition = packetReadPosition.load(std::memory_order_acquire);
    const auto depth = writePosition + 1u - readPosition;
    auto highWater = queueHighWater.load(std::memory_order_relaxed);
    while (depth > highWater
           && !queueHighWater.compare_exchange_weak(highWater, depth, std::memory_order_relaxed))
    {
    }
}

bool SenderAudioProcessor::popPacket(somma::StereoAudioPacket& packet) noexcept
{
    const auto readPosition = packetReadPosition.load(std::memory_order_relaxed);
    const auto writePosition = packetWritePosition.load(std::memory_order_acquire);
    if (readPosition == writePosition)
        return false;

    packet = packetQueue[readPosition % packetQueueCapacity];
    packetReadPosition.store(readPosition + 1u, std::memory_order_release);
    return true;
}

SenderTransportStats SenderAudioProcessor::getTransportStats() const noexcept
{
    const auto read = packetReadPosition.load(std::memory_order_acquire);
    const auto write = packetWritePosition.load(std::memory_order_acquire);
    return {
        juce::jmin(write - read, packetQueueCapacity),
        queueHighWater.load(std::memory_order_relaxed),
        droppedPackets.load(std::memory_order_relaxed),
        droppedFrames.load(std::memory_order_relaxed),
        packetsSent.load(std::memory_order_relaxed),
        sendErrors.load(std::memory_order_relaxed),
        static_cast<uint16_t>(targetPort.load(std::memory_order_relaxed)),
        workerRunning.load(std::memory_order_acquire),
        timelinePositionAvailable.load(std::memory_order_relaxed),
    };
}

juce::AudioProcessorEditor* SenderAudioProcessor::createEditor()
{
    return new SenderAudioProcessorEditor(*this);
}

bool SenderAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String SenderAudioProcessor::getName() const { return "sommacampagna_sender"; }
bool SenderAudioProcessor::acceptsMidi() const { return false; }
bool SenderAudioProcessor::producesMidi() const { return false; }
bool SenderAudioProcessor::isMidiEffect() const { return false; }
double SenderAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int SenderAudioProcessor::getNumPrograms() { return 1; }
int SenderAudioProcessor::getCurrentProgram() { return 0; }
void SenderAudioProcessor::setCurrentProgram(int) {}
const juce::String SenderAudioProcessor::getProgramName(int) { return {}; }
void SenderAudioProcessor::changeProgramName(int, const juce::String&) {}

void SenderAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SenderAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorValueTreeState::ParameterLayout SenderAudioProcessor::createParameterLayout() const
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterChoice>(paramPair,
                                                             "Target Pair",
                                                             juce::StringArray { "Pair 1 (1/2)", "Pair 2 (3/4)", "Pair 3 (5/6)", "Pair 4 (7/8)",
                                                                                 "Pair 5 (9/10)", "Pair 6 (11/12)", "Pair 7 (13/14)", "Pair 8 (15/16)" },
                                                             0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramGain,
                                                            "Pre Send Gain (dB)",
                                                            juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f),
                                                            0.0f));
    p.push_back(std::make_unique<juce::AudioParameterBool>(paramBypass, "Bypass Send", false));
    return { p.begin(), p.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SenderAudioProcessor();
}
