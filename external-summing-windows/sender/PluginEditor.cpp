#include "PluginEditor.h"

SenderAudioProcessorEditor::SenderAudioProcessorEditor(SenderAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(pairBox);
    addAndMakeVisible(gainSlider);
    addAndMakeVisible(bypassButton);

    pairBox.setJustificationType(juce::Justification::centred);
    pairBox.addItemList({ "Pair 1 (1/2)", "Pair 2 (3/4)", "Pair 3 (5/6)", "Pair 4 (7/8)",
                          "Pair 5 (9/10)", "Pair 6 (11/12)", "Pair 7 (13/14)", "Pair 8 (15/16)" },
                        1);
    gainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    bypassButton.setButtonText("Bypass Send");

    pairAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.apvts, "pairIndex", pairBox);
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "preSendGainDb", gainSlider);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "bypassSend", bypassButton);

    setSize(360, 140);
}

void SenderAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1c20));
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawFittedText("sommacampagna_sender", getLocalBounds().removeFromTop(28), juce::Justification::centred, 1);
}

void SenderAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(10);
    r.removeFromTop(26);
    pairBox.setBounds(r.removeFromTop(28));
    r.removeFromTop(6);
    gainSlider.setBounds(r.removeFromTop(32));
    r.removeFromTop(4);
    bypassButton.setBounds(r.removeFromTop(24));
}
