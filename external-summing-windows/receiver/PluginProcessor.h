#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Protocol.h"

class ReceiverAudioProcessor final : public juce::AudioProcessor
{
public:
    ReceiverAudioProcessor();
    ~ReceiverAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    float getConnectedValue() const noexcept;

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() const;

    std::unique_ptr<juce::DatagramSocket> socket;
    uint16_t receivePort = somma::engineToReceiverPort;
    uint32_t lastPortPollMs = 0;
    float transmissionBufferMs = somma::defaultTransmissionBufferMs;
    std::atomic<uint32_t> lastBlockReceived { 0 };
    std::atomic<uint32_t> lastReceiveTimeMs { 0 };

    static constexpr size_t ringFrames = somma::maxBufferedFrames;
    std::array<float, ringFrames> ringL {};
    std::array<float, ringFrames> ringR {};
    size_t writePos = 0;
    size_t readPos = 0;
    size_t availableFrames = 0;
    bool playbackPrimed = false;
    double currentSampleRate = 48000.0;
    float lastOutL = 0.0f;
    float lastOutR = 0.0f;

    void pushFrame(float l, float r) noexcept;
    bool popFrame(float& l, float& r) noexcept;
    size_t getTargetBufferFrames(int blockSamples) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReceiverAudioProcessor)
};
