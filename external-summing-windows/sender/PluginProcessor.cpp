#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "Protocol.h"
#include "PortDiscovery.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace
{
constexpr auto paramPair = "pairIndex";
constexpr auto paramGain = "preSendGainDb";
constexpr auto paramBypass = "bypassSend";
}

SenderAudioProcessor::SenderAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    socket = std::make_unique<juce::DatagramSocket>();
    socket->bindToPort(0);
    streamId = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt());
}

SenderAudioProcessor::~SenderAudioProcessor() = default;

void SenderAudioProcessor::prepareToPlay(double, int)
{
}

void SenderAudioProcessor::releaseResources()
{
}

bool SenderAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SenderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto muteOutput = [&buffer]()
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
    };

    if (buffer.getNumChannels() < 2)
    {
        muteOutput();
        return;
    }

    const bool bypass = apvts.getRawParameterValue(paramBypass)->load() > 0.5f;
    if (bypass)
    {
        muteOutput();
        return;
    }

    const float gainDb = apvts.getRawParameterValue(paramGain)->load();
    const float gain = juce::Decibels::decibelsToGain(gainDb);
    const int pairIndex = juce::jlimit(0, 7, static_cast<int>(apvts.getRawParameterValue(paramPair)->load()));
    const int numSamples = juce::jmin(buffer.getNumSamples(), somma::maxSamplesPerPacket);

    somma::StereoAudioPacket packet {};
    packet.header.magic = somma::protocolMagic;
    packet.header.version = somma::protocolVersion;
    packet.header.packetType = static_cast<uint16_t>(somma::PacketType::senderAudio);
    packet.header.sessionId = 1;
    packet.header.streamId = streamId;
    packet.header.blockIndex = blockIndex++;
    packet.header.sampleRate = static_cast<uint32_t>(getSampleRate());
    packet.header.numSamples = static_cast<uint16_t>(numSamples);
    packet.header.pairIndex = static_cast<uint16_t>(pairIndex);

    const auto now = juce::Time::getMillisecondCounter();
    if ((now - lastPortPollMs) > 500u)
    {
        targetEnginePort = somma::readPorts().senderToEngine;
        lastPortPollMs = now;
    }

    const float* inL = buffer.getReadPointer(0);
    const float* inR = buffer.getReadPointer(1);
    for (int i = 0; i < numSamples; ++i)
    {
        packet.interleaved[static_cast<size_t>(i * 2)] = inL[i] * gain;
        packet.interleaved[static_cast<size_t>(i * 2 + 1)] = inR[i] * gain;
    }

    const int bytesToSend = static_cast<int>(sizeof(somma::PacketHeader) + static_cast<size_t>(numSamples * 2) * sizeof(float));
    socket->write("127.0.0.1", static_cast<int>(targetEnginePort), reinterpret_cast<const char*>(&packet), bytesToSend);

    muteOutput();
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
