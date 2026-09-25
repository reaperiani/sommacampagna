#include "PluginEditor.h"

namespace
{
constexpr int numRows = 4;
constexpr int numCols = 4;
constexpr int meterPanelHeight = 220;
constexpr float floorDb = -100.0f;
constexpr float offThresholdDb = -60.0f;
constexpr float greenThresholdDb = -8.0f;
constexpr float yellowThresholdDb = -1.0f;
}

HotSummerAudioProcessorEditor::HotSummerAudioProcessorEditor(HotSummerAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    genericEditor = std::make_unique<juce::GenericAudioProcessorEditor>(processor);
    addAndMakeVisible(*genericEditor);

    smoothedMetersDb.fill(floorDb);
    const int width = juce::jmax(520, genericEditor->getWidth());
    const int height = juce::jmax(620, genericEditor->getHeight() + meterPanelHeight);
    setSize(width, height);
    startTimerHz(60);
}

void HotSummerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff13161a));

    if (meterArea.isEmpty())
        return;

    const auto meterBounds = meterArea.toFloat();
    juce::ColourGradient gradient(juce::Colour(0xff1b1f24), meterBounds.getX(), meterBounds.getY(),
                                  juce::Colour(0xff0a0b0d), meterBounds.getX(), meterBounds.getBottom(), false);
    g.setGradientFill(gradient);
    g.fillRect(meterArea);

    auto area = meterBounds.reduced(20.0f);
    const float gap = 10.0f;
    const float ledWidth = (area.getWidth() - gap * (numCols - 1)) / (float) numCols;
    const float ledHeight = (area.getHeight() - gap * (numRows - 1)) / (float) numRows;

    for (int row = 0; row < numRows; ++row)
    {
        for (int col = 0; col < numCols; ++col)
        {
            const int index = row * numCols + col;
            auto ledBounds = juce::Rectangle<float>(area.getX() + col * (ledWidth + gap),
                                                    area.getY() + row * (ledHeight + gap),
                                                    ledWidth,
                                                    ledHeight);

            const float db = smoothedMetersDb[(size_t) index];
            const float brightness = brightnessForDb(db);
            const auto ledColor = colorForDb(db);

            g.setColour(juce::Colour(0xff171a1d));
            g.fillRoundedRectangle(ledBounds, 8.0f);

            auto inner = ledBounds.reduced(3.0f);
            auto litColor = ledColor.withMultipliedSaturation(1.1f).withMultipliedBrightness(0.35f + 0.65f * brightness);
            auto darkColor = juce::Colour(0xff1d2227);
            auto fillColor = db < offThresholdDb ? darkColor : litColor;

            juce::ColourGradient ledGradient(fillColor.brighter(0.35f), inner.getX(), inner.getY(),
                                             fillColor.darker(0.6f), inner.getRight(), inner.getBottom(), false);
            g.setGradientFill(ledGradient);
            g.fillRoundedRectangle(inner, 6.0f);

            if (db >= offThresholdDb)
            {
                g.setColour(ledColor.withAlpha(0.08f + brightness * 0.35f));
                g.drawRoundedRectangle(inner.expanded(1.5f), 7.0f, 2.5f);
                g.setColour(ledColor.withAlpha(0.06f + brightness * 0.22f));
                g.fillRoundedRectangle(inner.expanded(3.0f), 9.0f);
            }
        }
    }
}

void HotSummerAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    if (genericEditor != nullptr)
    {
        const int topHeight = juce::jmin(genericEditor->getHeight(), juce::jmax(200, bounds.getHeight() - meterPanelHeight));
        genericEditor->setBounds(bounds.removeFromTop(topHeight));
    }

    meterArea = bounds;
}

void HotSummerAudioProcessorEditor::timerCallback()
{
    for (int i = 0; i < HotSummerAudioProcessor::meterChannelCount; ++i)
    {
        const float targetDb = juce::jlimit(floorDb, 6.0f, processor.getInputMeterDb(i));
        const float currentDb = smoothedMetersDb[(size_t) i];

        constexpr float attackCoeff = 0.62f;
        constexpr float releaseCoeff = 0.11f;
        const float coeff = targetDb > currentDb ? attackCoeff : releaseCoeff;

        smoothedMetersDb[(size_t) i] = currentDb + (targetDb - currentDb) * coeff;
    }

    repaint();
}

juce::Colour HotSummerAudioProcessorEditor::colorForDb(float db)
{
    if (db >= yellowThresholdDb)
        return juce::Colour(0xffff4545);
    if (db >= greenThresholdDb)
        return juce::Colour(0xffffd249);
    return juce::Colour(0xff6dff72);
}

float HotSummerAudioProcessorEditor::brightnessForDb(float db)
{
    if (db < offThresholdDb)
        return 0.0f;

    if (db < greenThresholdDb)
        return juce::jmap(db, offThresholdDb, greenThresholdDb, 0.15f, 0.85f);
    if (db < yellowThresholdDb)
        return juce::jmap(db, greenThresholdDb, yellowThresholdDb, 0.60f, 0.95f);

    return juce::jlimit(0.75f, 1.0f, juce::jmap(db, yellowThresholdDb, 0.0f, 0.88f, 1.0f));
}
