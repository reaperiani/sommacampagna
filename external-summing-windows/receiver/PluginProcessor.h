#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Protocol.h"

class ReceiverNetworkThread;

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
    friend class ReceiverNetworkThread;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() const;
    void startNetworkThread();
    void stopNetworkThread();
    bool pushPacket(const somma::StereoAudioPacket& packet) noexcept;

    std::unique_ptr<ReceiverNetworkThread> networkThread;
    std::atomic<float> transmissionBufferMs { somma::defaultTransmissionBufferMs };
    std::atomic<uint32_t> lastBlockReceived { 0 };
    std::atomic<uint32_t> lastReceiveTimeMs { 0 };
    std::atomic<bool> connected { false };
    std::atomic<bool> resyncRequested { false };

    static constexpr size_t ringFrames = somma::maxBufferedFrames;
    static_assert(ringFrames < (1u << 31u));
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);

    std::array<float, ringFrames> ringL {};
    std::array<float, ringFrames> ringR {};
    alignas(64) std::atomic<uint32_t> writePosition { 0 };
    alignas(64) std::atomic<uint32_t> readPosition { 0 };
    bool playbackPrimed = false;
    uint32_t currentSampleRate = 48000;
    float lastOutL = 0.0f;
    float lastOutR = 0.0f;

    size_t getTargetBufferFrames(int blockSamples) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReceiverAudioProcessor)
};
