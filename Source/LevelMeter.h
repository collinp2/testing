#pragma once
#include <JuceHeader.h>
#include "HorrorLookAndFeel.h"

//==============================================================================
// LevelMeter — peak meter with peak-hold, in the Flesh Render horror palette.
// Driven from the editor timer via update(linearPeak): instant attack, smooth
// release. Draws an optional caption along the bottom. Ported from
// NECRONAM MAX and adapted to this plugin's colour constants.
//==============================================================================
class LevelMeter : public juce::Component
{
public:
    juce::String caption;

    // Called from the editor's timer with the accumulated peak since last read.
    void update (float linearPeak)
    {
        const float db = juce::Decibels::gainToDecibels (linearPeak, kMinDb);

        displayDb = (db >= displayDb) ? db                                   // instant attack
                                      : juce::jmax (db, displayDb - kDecay); // smooth release

        if (db >= peakDb) { peakDb = db; peakHold = kPeakHoldTicks; }
        else if (peakHold > 0) --peakHold;
        else peakDb = juce::jmax (kMinDb, peakDb - kDecay);

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto col = [] (juce::uint32 u) { return juce::Colour (u); };

        auto b = getLocalBounds().toFloat();
        auto capArea = b.removeFromBottom (caption.isEmpty() ? 0.0f : 13.0f);
        auto r = b.reduced (1.0f);

        g.setColour (col (HorrorLookAndFeel::COL_KNOB_SHADOW));
        g.fillRect (r);

        // Filled bar from the bottom up to the current level.
        const float y = dbToY (displayDb, r);
        juce::ColourGradient grad (col (HorrorLookAndFeel::COL_BLOOD_DARK), r.getX(), r.getBottom(),
                                   col (HorrorLookAndFeel::COL_BLOOD_BRIGHT), r.getX(), r.getY(), false);
        grad.addColour (0.8, col (HorrorLookAndFeel::COL_BLOOD));
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> (r.getX(), y, r.getWidth(), r.getBottom() - y));

        // Over-0 dB portion painted bone as a clip warning.
        if (displayDb > 0.0f)
        {
            const float zy = dbToY (0.0f, r);
            g.setColour (col (HorrorLookAndFeel::COL_BONE));
            g.fillRect (juce::Rectangle<float> (r.getX(), y, r.getWidth(), zy - y));
        }

        // 0 dB reference line.
        const float zy = dbToY (0.0f, r);
        g.setColour (col (HorrorLookAndFeel::COL_BONE_DIM).withAlpha (0.6f));
        g.drawLine (r.getX(), zy, r.getRight(), zy, 1.0f);

        // Peak-hold marker.
        const float py = dbToY (peakDb, r);
        g.setColour (peakDb > 0.0f ? col (HorrorLookAndFeel::COL_BONE)
                                   : col (HorrorLookAndFeel::COL_BLOOD_BRIGHT));
        g.drawLine (r.getX(), py, r.getRight(), py, 1.4f);

        g.setColour (col (HorrorLookAndFeel::COL_PANEL_BORDER).withAlpha (0.7f));
        g.drawRect (r, 1.0f);

        if (! caption.isEmpty())
        {
            g.setColour (col (HorrorLookAndFeel::COL_BONE_DIM));
            g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
            g.drawText (caption, capArea, juce::Justification::centred);
        }
    }

private:
    static constexpr float kMinDb = -60.0f;
    static constexpr float kMaxDb = 6.0f;
    static constexpr float kDecay = 1.8f;       // dB per timer tick (~30 Hz)
    static constexpr int   kPeakHoldTicks = 20;

    float dbToY (float db, juce::Rectangle<float> r) const
    {
        return juce::jmap (juce::jlimit (kMinDb, kMaxDb, db), kMinDb, kMaxDb,
                           r.getBottom(), r.getY());
    }

    float displayDb = kMinDb;
    float peakDb    = kMinDb;
    int   peakHold  = 0;
};
