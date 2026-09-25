#pragma once

#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class ReceiverAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit ReceiverAudioProcessorEditor(ReceiverAudioProcessor&);
    ~ReceiverAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ReceiverAudioProcessor& processor;

    juce::Slider trimSlider;
    juce::Label statusLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trimAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReceiverAudioProcessorEditor)
};
