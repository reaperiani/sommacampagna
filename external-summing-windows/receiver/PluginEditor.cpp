#include "PluginEditor.h"

ReceiverAudioProcessorEditor::ReceiverAudioProcessorEditor(ReceiverAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(trimSlider);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(diagnosticsLabel);

    trimSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    trimSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    statusLabel.setText("Status: disconnected", juce::dontSendNotification);
    diagnosticsLabel.setJustificationType(juce::Justification::topLeft);

    trimAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "outputTrimDb", trimSlider);

    setSize(440, 200);
    startTimerHz(10);
}

void ReceiverAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181a1e));
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawFittedText("sommacampagna_receiver", getLocalBounds().removeFromTop(28), juce::Justification::centred, 1);
    g.setColour(juce::Colours::lightgrey);
    g.setFont(10.0f);
    g.drawFittedText("AGPL-3.0 | No warranty | github.com/reaperiani/sommacampagna",
                     10, getHeight() - 20, getWidth() - 20, 14, juce::Justification::centred, 1);
}

void ReceiverAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(10);
    r.removeFromTop(28);
    trimSlider.setBounds(r.removeFromTop(32));
    statusLabel.setBounds(r.removeFromTop(24));
    diagnosticsLabel.setBounds(r.removeFromTop(54));
}

void ReceiverAudioProcessorEditor::timerCallback()
{
    const auto stats = processor.getTransportStats();
    statusLabel.setText(stats.connected ? "Status: connected (main stereo sum)" : "Status: disconnected", juce::dontSendNotification);
    diagnosticsLabel.setText("Ring " + juce::String(stats.occupancyFrames) + "/" + juce::String(stats.targetFrames)
                                 + " fr | " + (stats.primed ? "primed" : "priming")
                                 + " | clock " + juce::String(stats.clockCorrectionPpm) + " ppm\n"
                                 + "discontinuities/stale " + juce::String(stats.discontinuities) + "/" + juce::String(stats.stalePackets)
                                 + " | under/overflow " + juce::String(stats.underflows) + "/" + juce::String(stats.overflowPackets)
                                 + " | resync " + juce::String(stats.resyncs)
                                 + " | invalid/rate " + juce::String(stats.invalidPackets) + "/" + juce::String(stats.wrongRatePackets),
                             juce::dontSendNotification);
}
