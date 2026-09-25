#include "PluginEditor.h"

ReceiverAudioProcessorEditor::ReceiverAudioProcessorEditor(ReceiverAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(trimSlider);
    addAndMakeVisible(statusLabel);

    trimSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    trimSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    statusLabel.setText("Status: disconnected", juce::dontSendNotification);

    trimAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "outputTrimDb", trimSlider);

    setSize(360, 120);
    startTimerHz(10);
}

void ReceiverAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181a1e));
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawFittedText("sommacampagna_receiver", getLocalBounds().removeFromTop(28), juce::Justification::centred, 1);
}

void ReceiverAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(10);
    r.removeFromTop(28);
    trimSlider.setBounds(r.removeFromTop(32));
    statusLabel.setBounds(r.removeFromTop(24));
}

void ReceiverAudioProcessorEditor::timerCallback()
{
    const bool connected = processor.getConnectedValue() > 0.5f;
    statusLabel.setText(connected ? "Status: connected (main stereo sum)" : "Status: disconnected", juce::dontSendNotification);
}
