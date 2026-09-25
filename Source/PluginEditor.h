#pragma once

#include <array>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class HotSummerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit HotSummerAudioProcessorEditor(HotSummerAudioProcessor&);
    ~HotSummerAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    static juce::Colour colorForDb(float db);
    static float brightnessForDb(float db);

    HotSummerAudioProcessor& processor;
    std::unique_ptr<juce::GenericAudioProcessorEditor> genericEditor;
    std::array<float, HotSummerAudioProcessor::meterChannelCount> smoothedMetersDb;
    juce::Rectangle<int> meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HotSummerAudioProcessorEditor)
};
