#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto paramFlavor = "consoleFlavor";
constexpr auto paramDrive = "compensatedDriveDb";
constexpr auto paramPsu = "powerSupplyType";
constexpr auto paramOut = "masterOutputDb";
constexpr auto paramGravity = "gravityPct";
constexpr auto paramBypass = "bypass";
constexpr auto paramBusStress = "busStressPct";
}

HotSummerAudioProcessor::HotSummerAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::discreteChannels(16), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    for (auto& meter : inputMeterDb)
        meter.store(-100.0f, std::memory_order_relaxed);
}

void HotSummerAudioProcessor::prepareToPlay(double sampleRate, int)
{
    dsp.prepare(sampleRate);
}

void HotSummerAudioProcessor::releaseResources()
{
}

bool HotSummerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in.size() == 16 && out == juce::AudioChannelSet::stereo();
}

void HotSummerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < meterChannelCount; ++ch)
    {
        float meterDb = -100.0f;

        if (ch < buffer.getNumChannels() && numSamples > 0)
        {
            const float* channelData = buffer.getReadPointer(ch);
            float peak = 0.0f;

            for (int n = 0; n < numSamples; ++n)
                peak = juce::jmax(peak, std::abs(channelData[n]));

            meterDb = juce::Decibels::gainToDecibels(peak, -100.0f);
        }

        inputMeterDb[(size_t) ch].store(meterDb, std::memory_order_relaxed);
    }

    if (buffer.getNumChannels() < 16)
    {
        buffer.clear();
        return;
    }

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getWritePointer(1);

    std::array<float*, 16> inputs {};
    for (int ch = 0; ch < 16; ++ch)
        inputs[(size_t) ch] = buffer.getWritePointer(ch);

    HotSummerParams params;
    params.consoleFlavor = (int) apvts.getRawParameterValue(paramFlavor)->load();
    if (params.consoleFlavor == 4)
        params.consoleFlavor = 0;
    params.compensatedDriveDb = apvts.getRawParameterValue(paramDrive)->load();
    params.powerSupplyType = (int) apvts.getRawParameterValue(paramPsu)->load();
    params.masterOutputDb = apvts.getRawParameterValue(paramOut)->load();
    params.gravityPct = apvts.getRawParameterValue(paramGravity)->load();
    params.bypass = apvts.getRawParameterValue(paramBypass)->load() > 0.5f;

    dsp.process(inputs.data(), outL, outR, numSamples, params);

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);

    if (auto* busStress = apvts.getParameter(paramBusStress))
    {
        const float normalized = juce::jlimit(0.0f, 1.0f, dsp.getBusStressPercent() / 100.0f);
        busStress->setValueNotifyingHost(normalized);
    }
}

juce::AudioProcessorEditor* HotSummerAudioProcessor::createEditor()
{
    return new HotSummerAudioProcessorEditor(*this);
}

bool HotSummerAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String HotSummerAudioProcessor::getName() const
{
    return "sommacampagna";
}

bool HotSummerAudioProcessor::acceptsMidi() const { return false; }
bool HotSummerAudioProcessor::producesMidi() const { return false; }
bool HotSummerAudioProcessor::isMidiEffect() const { return false; }
double HotSummerAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int HotSummerAudioProcessor::getNumPrograms() { return 1; }
int HotSummerAudioProcessor::getCurrentProgram() { return 0; }
void HotSummerAudioProcessor::setCurrentProgram(int) {}
const juce::String HotSummerAudioProcessor::getProgramName(int) { return {}; }
void HotSummerAudioProcessor::changeProgramName(int, const juce::String&) {}

void HotSummerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void HotSummerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

float HotSummerAudioProcessor::getInputMeterDb(int channel) const noexcept
{
    if (channel < 0 || channel >= meterChannelCount)
        return -100.0f;

    return inputMeterDb[(size_t) channel].load(std::memory_order_relaxed);
}

juce::AudioProcessorValueTreeState::ParameterLayout HotSummerAudioProcessor::createParameterLayout() const
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back(std::make_unique<juce::AudioParameterChoice>(paramFlavor, "Console Flavor", juce::StringArray{ "Signature", "Vintage British", "Discrete American", "Modern Ultra-Linear", "Master Controlled" }, 0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramDrive, "Compensated Drive (dB)", juce::NormalisableRange<float>(-12.0f, 18.0f, 0.1f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(paramPsu, "Power Supply Type", juce::StringArray{ "Custom", "Vintage", "Modern", "Mastering" }, 0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramOut, "Master Output Gain (dB)", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramGravity, "Gravity (%)", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterBool>(paramBypass, "Summing Bypass", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramBusStress, "BUS STRESS (RAIL SAG) %", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 0.0f));

    return { p.begin(), p.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HotSummerAudioProcessor();
}
