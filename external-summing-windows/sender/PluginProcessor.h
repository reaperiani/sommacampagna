#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include "Protocol.h"

class SenderNetworkThread;

class SenderAudioProcessor final : public juce::AudioProcessor
{
public:
    SenderAudioProcessor();
    ~SenderAudioProcessor() override;

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

    juce::AudioProcessorValueTreeState apvts;

private:
    friend class SenderNetworkThread;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() const;
    void startNetworkThread();
    void stopNetworkThread();
    somma::StereoAudioPacket* beginPacket() noexcept;
    void commitPacket() noexcept;
    bool popPacket(somma::StereoAudioPacket& packet) noexcept;

    static constexpr uint32_t packetQueueCapacity = 64;
    static_assert(std::atomic<uint32_t>::is_always_lock_free);

    std::array<somma::StereoAudioPacket, packetQueueCapacity> packetQueue {};
    alignas(64) std::atomic<uint32_t> packetWritePosition { 0 };
    alignas(64) std::atomic<uint32_t> packetReadPosition { 0 };
    std::unique_ptr<SenderNetworkThread> networkThread;
    uint32_t streamId = 0;
    uint32_t blockIndex = 0;
    uint32_t currentSampleRate = 48000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SenderAudioProcessor)
};
