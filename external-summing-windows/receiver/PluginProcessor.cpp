#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "Protocol.h"
#include "PortDiscovery.h"

#include <cmath>
#include <memory>
#include <vector>

namespace
{
constexpr auto paramTrim = "outputTrimDb";
}

class ReceiverNetworkThread final : public juce::Thread
{
public:
    explicit ReceiverNetworkThread(ReceiverAudioProcessor& p)
        : juce::Thread("SommaReceiverNetwork"), processor(p)
    {
    }

    void run() override
    {
        std::unique_ptr<juce::DatagramSocket> socket;
        uint16_t receivePort = 0;
        uint32_t lastPortPollMs = 0;
        std::array<char, somma::maximumInspectedDatagramSize> datagram;
        somma::StereoAudioPacket packet;

        while (!threadShouldExit())
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (lastPortPollMs == 0 || (now - lastPortPollMs) > 500u)
            {
                const auto config = somma::readPorts();
                processor.transmissionBufferMs.store(config.transmissionBufferMs, std::memory_order_relaxed);
                if (socket == nullptr || config.engineToReceiver != receivePort)
                {
                    auto replacement = std::make_unique<juce::DatagramSocket>();
                    replacement->setEnablePortReuse(true);
                    if (replacement->bindToPort(static_cast<int>(config.engineToReceiver), "127.0.0.1"))
                    {
                        socket = std::move(replacement);
                        receivePort = config.engineToReceiver;
                    }
                }
                lastPortPollMs = now;
            }

            int packetsRead = 0;
            if (socket != nullptr && socket->waitUntilReady(true, 10) > 0)
            {
                while (packetsRead < 64 && !threadShouldExit())
                {
                    const int received = socket->read(datagram.data(), static_cast<int>(datagram.size()), false);
                    if (received <= 0)
                        break;

                    if (somma::decodeStereoAudioPacket(datagram.data(),
                                                       static_cast<size_t>(received),
                                                       somma::PacketType::mainStereoSum,
                                                       packet)
                        && packet.header.sampleRate == processor.currentSampleRate)
                    {
                        processor.pushPacket(packet);
                        processor.lastBlockReceived.store(packet.header.blockIndex, std::memory_order_release);
                        processor.lastReceiveTimeMs.store(now, std::memory_order_release);
                        processor.connected.store(true, std::memory_order_release);
                    }
                    ++packetsRead;
                }
            }
            else if (socket == nullptr)
            {
                juce::Thread::sleep(10);
            }

            const auto lastReceive = processor.lastReceiveTimeMs.load(std::memory_order_acquire);
            if (lastReceive == 0 || (juce::Time::getMillisecondCounter() - lastReceive) >= 1500u)
                processor.connected.store(false, std::memory_order_release);
        }

        processor.connected.store(false, std::memory_order_release);
    }

private:
    ReceiverAudioProcessor& processor;
};

ReceiverAudioProcessor::ReceiverAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
}

ReceiverAudioProcessor::~ReceiverAudioProcessor()
{
    stopNetworkThread();
}

void ReceiverAudioProcessor::prepareToPlay(double sampleRate, int)
{
    stopNetworkThread();
    writePosition.store(0, std::memory_order_relaxed);
    readPosition.store(0, std::memory_order_relaxed);
    playbackPrimed = false;
    const auto roundedSampleRate = std::isfinite(sampleRate) && sampleRate > 0.0
                                 ? static_cast<uint32_t>(std::lround(sampleRate))
                                 : 0u;
    currentSampleRate = somma::isSupportedSampleRate(roundedSampleRate) ? roundedSampleRate : 48000u;
    lastOutL = 0.0f;
    lastOutR = 0.0f;
    lastBlockReceived.store(0, std::memory_order_relaxed);
    lastReceiveTimeMs.store(0, std::memory_order_relaxed);
    connected.store(false, std::memory_order_relaxed);
    resyncRequested.store(false, std::memory_order_relaxed);
    startNetworkThread();
}

void ReceiverAudioProcessor::releaseResources()
{
    stopNetworkThread();
    writePosition.store(0, std::memory_order_relaxed);
    readPosition.store(0, std::memory_order_relaxed);
    playbackPrimed = false;
    connected.store(false, std::memory_order_relaxed);
    resyncRequested.store(false, std::memory_order_relaxed);
}

bool ReceiverAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ReceiverAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    buffer.clear();
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getWritePointer(1);
    const float trim = juce::Decibels::decibelsToGain(apvts.getRawParameterValue(paramTrim)->load());
    const auto targetBufferFrames = getTargetBufferFrames(numSamples);
    const bool isConnected = connected.load(std::memory_order_acquire);
    const auto write = writePosition.load(std::memory_order_acquire);
    auto read = resyncRequested.exchange(false, std::memory_order_acq_rel)
              ? write
              : readPosition.load(std::memory_order_relaxed);
    auto availableFrames = write - read;
    if (!isConnected)
    {
        read = write;
        availableFrames = 0;
        playbackPrimed = false;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float l = 0.0f;
        float r = 0.0f;

        if (isConnected && !playbackPrimed && availableFrames >= targetBufferFrames)
            playbackPrimed = true;

        if (isConnected && playbackPrimed && availableFrames > 0)
        {
            const auto ringIndex = static_cast<size_t>(read % static_cast<uint32_t>(ringFrames));
            l = ringL[ringIndex];
            r = ringR[ringIndex];
            ++read;
            --availableFrames;
            lastOutL = l;
            lastOutR = r;
        }
        else
        {
            if (!isConnected || availableFrames == 0)
                playbackPrimed = false;

            lastOutL *= 0.985f;
            lastOutR *= 0.985f;
            l = lastOutL;
            r = lastOutR;
        }

        outL[i] = l * trim;
        outR[i] = r * trim;
    }

    readPosition.store(read, std::memory_order_release);
}

juce::AudioProcessorEditor* ReceiverAudioProcessor::createEditor()
{
    return new ReceiverAudioProcessorEditor(*this);
}

bool ReceiverAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String ReceiverAudioProcessor::getName() const { return "sommacampagna_receiver"; }
bool ReceiverAudioProcessor::acceptsMidi() const { return false; }
bool ReceiverAudioProcessor::producesMidi() const { return false; }
bool ReceiverAudioProcessor::isMidiEffect() const { return false; }
double ReceiverAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int ReceiverAudioProcessor::getNumPrograms() { return 1; }
int ReceiverAudioProcessor::getCurrentProgram() { return 0; }
void ReceiverAudioProcessor::setCurrentProgram(int) {}
const juce::String ReceiverAudioProcessor::getProgramName(int) { return {}; }
void ReceiverAudioProcessor::changeProgramName(int, const juce::String&) {}

void ReceiverAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void ReceiverAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorValueTreeState::ParameterLayout ReceiverAudioProcessor::createParameterLayout() const
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramTrim,
                                                            "Output Trim (dB)",
                                                            juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f),
                                                            0.0f));
    return { p.begin(), p.end() };
}

float ReceiverAudioProcessor::getConnectedValue() const noexcept
{
    return connected.load(std::memory_order_acquire) ? 1.0f : 0.0f;
}

void ReceiverAudioProcessor::startNetworkThread()
{
    networkThread = std::make_unique<ReceiverNetworkThread>(*this);
    networkThread->startThread();
}

void ReceiverAudioProcessor::stopNetworkThread()
{
    if (networkThread == nullptr)
        return;

    networkThread->signalThreadShouldExit();
    networkThread->stopThread(2000);
    networkThread.reset();
}

bool ReceiverAudioProcessor::pushPacket(const somma::StereoAudioPacket& packet) noexcept
{
    const auto write = writePosition.load(std::memory_order_relaxed);
    const auto read = readPosition.load(std::memory_order_acquire);
    const auto packetFrames = static_cast<uint32_t>(packet.header.numSamples);
    if (write - read + packetFrames > ringFrames)
    {
        resyncRequested.store(true, std::memory_order_release);
        return false;
    }

    for (uint32_t i = 0; i < packetFrames; ++i)
    {
        const auto ringIndex = static_cast<size_t>((write + i) % static_cast<uint32_t>(ringFrames));
        ringL[ringIndex] = packet.interleaved[static_cast<size_t>(i * 2u)];
        ringR[ringIndex] = packet.interleaved[static_cast<size_t>(i * 2u + 1u)];
    }

    writePosition.store(write + packetFrames, std::memory_order_release);
    return true;
}

size_t ReceiverAudioProcessor::getTargetBufferFrames(int blockSamples) const noexcept
{
    const auto safeBufferMs = somma::sanitizeTransmissionBufferMs(transmissionBufferMs.load(std::memory_order_relaxed));
    const auto frames = static_cast<size_t>((static_cast<double>(safeBufferMs) * static_cast<double>(currentSampleRate)) / 1000.0);
    const auto minFrames = juce::jmin(ringFrames / 2u, static_cast<size_t>(juce::jmax(1, blockSamples)));
    return juce::jlimit(minFrames, ringFrames - minFrames, frames);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReceiverAudioProcessor();
}
