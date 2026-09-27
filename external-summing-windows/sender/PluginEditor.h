#pragma once

#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class SenderAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit SenderAudioProcessorEditor(SenderAudioProcessor&);
    ~SenderAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    SenderAudioProcessor& processor;

    juce::ComboBox pairBox;
    juce::Slider gainSlider;
    juce::ToggleButton bypassButton;
    juce::Label statusLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pairAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SenderAudioProcessorEditor)
};
