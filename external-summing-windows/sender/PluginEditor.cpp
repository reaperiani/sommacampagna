#include "PluginEditor.h"

SenderAudioProcessorEditor::SenderAudioProcessorEditor(SenderAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(pairBox);
    addAndMakeVisible(gainSlider);
    addAndMakeVisible(bypassButton);
    addAndMakeVisible(statusLabel);

    pairBox.setJustificationType(juce::Justification::centred);
    pairBox.addItemList({ "Pair 1 (1/2)", "Pair 2 (3/4)", "Pair 3 (5/6)", "Pair 4 (7/8)",
                          "Pair 5 (9/10)", "Pair 6 (11/12)", "Pair 7 (13/14)", "Pair 8 (15/16)" },
                        1);
    gainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    bypassButton.setButtonText("Bypass Send");
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    pairAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.apvts, "pairIndex", pairBox);
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "preSendGainDb", gainSlider);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "bypassSend", bypassButton);

    setSize(420, 200);
    startTimerHz(10);
}

void SenderAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1c20));
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawFittedText("sommacampagna_sender", getLocalBounds().removeFromTop(28), juce::Justification::centred, 1);
    g.setColour(juce::Colours::lightgrey);
    g.setFont(10.0f);
    g.drawFittedText("AGPL-3.0 | No warranty | github.com/reaperiani/sommacampagna",
                     10, getHeight() - 20, getWidth() - 20, 14, juce::Justification::centred, 1);
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
    r.removeFromTop(4);
    statusLabel.setBounds(r.removeFromTop(38));
}

void SenderAudioProcessorEditor::timerCallback()
{
    const auto stats = processor.getTransportStats();
    const auto worker = stats.workerRunning ? "running" : "stopped";
    statusLabel.setText("UDP " + juce::String(stats.targetPort)
                            + " | worker " + worker
                            + " | queue " + juce::String(stats.queueDepth) + "/64 (peak " + juce::String(stats.queueHighWater) + ")\n"
                            + "sent " + juce::String(stats.packetsSent)
                            + " | drops " + juce::String(stats.droppedPackets)
                            + " / " + juce::String(stats.droppedFrames) + " frames"
                            + " | errors " + juce::String(stats.sendErrors),
                        juce::dontSendNotification);
}
