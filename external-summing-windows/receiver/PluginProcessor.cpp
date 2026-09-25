#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "Protocol.h"
#include "PortDiscovery.h"

#include <cstring>
#include <memory>
#include <vector>

namespace
{
constexpr auto paramTrim = "outputTrimDb";
}

ReceiverAudioProcessor::ReceiverAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    socket = std::make_unique<juce::DatagramSocket>();
    socket->setEnablePortReuse(true);
    socket->bindToPort(static_cast<int>(receivePort));
}

ReceiverAudioProcessor::~ReceiverAudioProcessor() = default;

void ReceiverAudioProcessor::prepareToPlay(double sampleRate, int)
{
    writePos = 0;
    readPos = 0;
    availableFrames = 0;
    playbackPrimed = false;
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    lastOutL = 0.0f;
    lastOutR = 0.0f;
}

void ReceiverAudioProcessor::releaseResources()
{
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

    for (;;)
    {
        const auto now = juce::Time::getMillisecondCounter();
        if ((now - lastPortPollMs) > 500u)
        {
            const auto discoveredConfig = somma::readPorts();
            const auto discovered = discoveredConfig.engineToReceiver;
            transmissionBufferMs = discoveredConfig.transmissionBufferMs;
            if (discovered != receivePort)
            {
                auto newSocket = std::make_unique<juce::DatagramSocket>();
                newSocket->setEnablePortReuse(true);
                if (newSocket->bindToPort(static_cast<int>(discovered)))
                {
                    socket = std::move(newSocket);
                    receivePort = discovered;
                }
            }
            lastPortPollMs = now;
        }

        somma::StereoAudioPacket packet {};
        const int received = socket->read(reinterpret_cast<char*>(&packet), sizeof(packet), false);
        if (received < static_cast<int>(sizeof(somma::PacketHeader)))
            break;

        if (!somma::isValidHeader(packet.header)
            || packet.header.packetType != static_cast<uint16_t>(somma::PacketType::mainStereoSum))
        {
            continue;
        }

        const int copySamples = static_cast<int>(packet.header.numSamples);
        for (int i = 0; i < copySamples; ++i)
        {
            const float l = packet.interleaved[static_cast<size_t>(i * 2)];
            const float r = packet.interleaved[static_cast<size_t>(i * 2 + 1)];
            pushFrame(l, r);
        }

        lastBlockReceived.store(packet.header.blockIndex, std::memory_order_release);
        lastReceiveTimeMs.store(juce::Time::getMillisecondCounter(), std::memory_order_release);
    }

    buffer.clear();
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getWritePointer(1);
    const float trim = juce::Decibels::decibelsToGain(apvts.getRawParameterValue(paramTrim)->load());
    const auto targetBufferFrames = getTargetBufferFrames(numSamples);

    const bool connected = getConnectedValue() > 0.5f;
    for (int i = 0; i < numSamples; ++i)
    {
        float l = 0.0f;
        float r = 0.0f;

        if (connected && !playbackPrimed && availableFrames >= targetBufferFrames)
            playbackPrimed = true;

        if (connected && playbackPrimed && popFrame(l, r))
        {
            lastOutL = l;
            lastOutR = r;
        }
        else
        {
            if (!connected || availableFrames == 0)
                playbackPrimed = false;

            lastOutL *= 0.985f;
            lastOutR *= 0.985f;
            l = lastOutL;
            r = lastOutR;
        }

        outL[i] = l * trim;
        outR[i] = r * trim;
    }
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
    const auto now = juce::Time::getMillisecondCounter();
    const auto last = lastReceiveTimeMs.load(std::memory_order_acquire);
    return (last > 0 && (now - last) < 1500u) ? 1.0f : 0.0f;
}

void ReceiverAudioProcessor::pushFrame(float l, float r) noexcept
{
    ringL[writePos] = l;
    ringR[writePos] = r;
    writePos = (writePos + 1u) % ringFrames;

    if (availableFrames < ringFrames)
    {
        ++availableFrames;
    }
    else
    {
        readPos = (readPos + 1u) % ringFrames;
    }
}

bool ReceiverAudioProcessor::popFrame(float& l, float& r) noexcept
{
    if (availableFrames == 0)
        return false;

    l = ringL[readPos];
    r = ringR[readPos];
    readPos = (readPos + 1u) % ringFrames;
    --availableFrames;
    return true;
}

size_t ReceiverAudioProcessor::getTargetBufferFrames(int blockSamples) const noexcept
{
    const auto safeBufferMs = somma::sanitizeTransmissionBufferMs(transmissionBufferMs);
    const auto frames = static_cast<size_t>((static_cast<double>(safeBufferMs) * currentSampleRate) / 1000.0);
    const auto minFrames = static_cast<size_t>(juce::jmax(1, blockSamples));
    return juce::jlimit(minFrames, ringFrames - minFrames, frames);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReceiverAudioProcessor();
}
