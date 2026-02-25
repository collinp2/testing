#include "LevelMeter.h"

LevelMeter::LevelMeter()
{
    setOpaque (false);
}

void LevelMeter::setLevel (float newLevel)
{
    currentLevel.store (newLevel);
}

void LevelMeter::timerTick()
{
    const float newLevel = currentLevel.load();
    displayLevel = newLevel;

    if (newLevel >= 1.0f)
        clipped = true;

    if (newLevel >= peakLevel)
    {
        peakLevel      = newLevel;
        peakHoldFrames = kPeakHoldFrames;
    }
    else
    {
        if (peakHoldFrames > 0)
            --peakHoldFrames;
        else
        {
            // Decay peak at ~6 dB/s = 0.5/30 per frame
            peakLevel = std::max (0.0f, peakLevel - 0.5f / static_cast<float> (kTimerHz));
        }
    }

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();

    // Background
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillRoundedRectangle (bounds, 3.0f);

    // Clip indicator strip at the top
    const float clipH = 12.0f;
    juce::Rectangle<float> clipRect (0.0f, 0.0f, w, clipH);
    g.setColour (clipped ? juce::Colours::red : juce::Colours::darkgrey);
    g.fillRect (clipRect);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (8.0f);
    g.drawText ("CLIP", clipRect, juce::Justification::centred, false);

    const float meterTop    = clipH + 2.0f;
    const float meterBottom = h - 2.0f;
    const float meterH      = meterBottom - meterTop;

    if (meterH <= 0.0f) return;

    // Convert linear level to dB and map to bar height
    // dB range: -60 to 0
    auto levelToProportion = [] (float linLevel) -> float
    {
        if (linLevel <= 0.0f) return 0.0f;
        const float dB = juce::Decibels::gainToDecibels (linLevel, -60.0f);
        return juce::jlimit (0.0f, 1.0f, (dB + 60.0f) / 60.0f);
    };

    const float barProp  = levelToProportion (displayLevel);
    const float barH     = barProp * meterH;
    const float barY     = meterBottom - barH;

    // Color zones (proportions from bottom):
    // Green:  0 – 50%   (-60 to -6 dBFS)
    // Amber: 50 – 89%   (-6  to -1 dBFS)
    // Red:   89 – 100%  (-1  to  0 dBFS)
    const float greenTop  = meterBottom - levelToProportion (kAmberThresh) * meterH;
    const float amberTop  = meterBottom - levelToProportion (kRedThresh)   * meterH;

    // Draw the bar in segments
    if (barH > 0.0f)
    {
        // Green segment
        if (barY < greenTop)
        {
            const float segY = std::max (barY, meterTop);
            const float segBottom = greenTop;
            if (segBottom > segY)
            {
                g.setColour (juce::Colour (0xff22cc44));
                g.fillRect (2.0f, segY, w - 4.0f, segBottom - segY);
            }
        }

        // Amber segment
        if (barY < amberTop)
        {
            const float segY = std::max (barY, greenTop);
            const float segBottom = amberTop;
            if (segBottom > segY)
            {
                g.setColour (juce::Colour (0xffddaa00));
                g.fillRect (2.0f, segY, w - 4.0f, segBottom - segY);
            }
        }

        // Red segment
        if (barY < meterTop + (meterH * (1.0f - levelToProportion (kRedThresh))))
        {
            const float segY = std::max (barY, amberTop);
            const float segBottom = meterBottom - levelToProportion (kRedThresh) * meterH;
            // Actually draw from amberTop upward
            if (barY < amberTop)
            {
                const float rY = std::max (barY, meterTop);
                const float rBottom = amberTop;
                g.setColour (juce::Colour (0xffdd2222));
                g.fillRect (2.0f, rY, w - 4.0f, rBottom - rY);
            }
        }
    }

    // Peak hold line
    const float peakProp = levelToProportion (peakLevel);
    if (peakProp > 0.0f)
    {
        const float peakY = meterBottom - peakProp * meterH;
        juce::Colour peakColour;
        if (peakLevel >= kRedThresh)        peakColour = juce::Colours::red;
        else if (peakLevel >= kAmberThresh) peakColour = juce::Colours::yellow;
        else                                peakColour = juce::Colours::lightgreen;

        g.setColour (peakColour);
        g.drawLine (2.0f, peakY, w - 2.0f, peakY, 2.0f);
    }

    // Border
    g.setColour (juce::Colours::grey.withAlpha (0.5f));
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    clipped   = false;
    peakLevel = 0.0f;
    peakHoldFrames = 0;
    repaint();
}
