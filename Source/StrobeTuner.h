#pragma once

// ============================================================================
//  StrobeTuner
//  A virtual strobe-tuner display. The editor feeds it the detected frequency
//  each timer tick; it derives the nearest note + cents offset and scrolls a
//  strobe band whose drift rate is proportional to how far out of tune you are
//  (sharp drifts one way, flat the other). When the band appears to stand still
//  you are in tune.
// ============================================================================

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HorrorLookAndFeel.h"

class StrobeTuner : public juce::Component
{
public:
    // freqHz = 0 means "no pitch detected". dtSeconds drives the strobe motion.
    void update (float freqHz, float dtSeconds)
    {
        mFreq = freqHz;
        if (freqHz > 20.0f)
        {
            const float midi    = 69.0f + 12.0f * std::log2 (freqHz / 440.0f);
            const int   nearest = juce::roundToInt (midi);
            mCents    = (midi - (float) nearest) * 100.0f;
            mNoteName = noteName (nearest);
            mHasPitch = true;
            mInTune   = std::abs (mCents) <= 3.0f;

            // Drift one full strobe period per second at ~50 cents off.
            mPhase += (mCents / 50.0f) * dtSeconds;
            mPhase -= std::floor (mPhase);
        }
        else
        {
            mHasPitch = false;
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace horror;
        auto b = getLocalBounds().toFloat();
        HorrorLookAndFeel::drawPanelBackground (g, b);

        auto area = getLocalBounds().reduced (16);

        // Note name (large).
        auto noteArea = area.removeFromTop (juce::jmin (96, area.getHeight() / 2));
        g.setColour (mHasPitch ? (mInTune ? c (COL_BLOOD_BRIGHT) : c (COL_BONE)) : c (COL_BONE_DIM));
        g.setFont (HorrorLookAndFeel::monoFont (mHasPitch ? 64.0f : 30.0f, true));
        g.drawText (mHasPitch ? mNoteName : juce::String ("--"), noteArea, juce::Justification::centred);

        area.removeFromTop (8);

        // Strobe band.
        auto strobe = area.removeFromTop (juce::jmax (40, area.getHeight() - 28));
        g.setColour (c (COL_PANEL_BORDER));
        g.drawRect (strobe, 1);
        auto inner = strobe.reduced (3);

        g.saveState();
        g.reduceClipRegion (inner);

        const int   stripes   = 14;
        const float stripeW   = (float) inner.getWidth() / (float) stripes;
        const float offset    = mPhase * stripeW * 2.0f;   // two stripes per period
        const juce::Colour barCol = mHasPitch ? (mInTune ? c (COL_BLOOD_BRIGHT) : c (COL_BLOOD))
                                              : c (COL_BONE_DIM).withAlpha (0.25f);
        g.setColour (barCol);
        for (int i = -1; i <= stripes + 1; ++i)
        {
            const float x = (float) inner.getX() + (float) i * stripeW - offset;
            g.fillRect (juce::Rectangle<float> (x, (float) inner.getY(), stripeW * 0.5f, (float) inner.getHeight()));
        }
        g.restoreState();

        // Centre reference line.
        g.setColour (c (COL_BONE).withAlpha (0.6f));
        g.fillRect ((float) inner.getCentreX() - 1.0f, (float) strobe.getY(), 2.0f, (float) strobe.getHeight());

        // Cents readout.
        area.removeFromTop (6);
        g.setColour (mHasPitch ? (mInTune ? c (COL_BLOOD_BRIGHT) : c (COL_BONE_DIM)) : c (COL_BONE_DIM));
        g.setFont (HorrorLookAndFeel::monoFont (13.0f, true));
        juce::String centsText = mHasPitch
            ? (mInTune ? juce::String ("IN TUNE")
                       : juce::String (mCents > 0 ? "+" : "") + juce::String (juce::roundToInt (mCents)) + " cents"
                         + (mCents > 0 ? "  (sharp)" : "  (flat)"))
            : juce::String ("play a note");
        g.drawText (centsText, area, juce::Justification::centred);
    }

private:
    static juce::String noteName (int midi)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int pc  = ((midi % 12) + 12) % 12;
        const int oct = midi / 12 - 1;
        return juce::String (names[pc]) + juce::String (oct);
    }

    float        mFreq     = 0.0f;
    float        mCents    = 0.0f;
    float        mPhase    = 0.0f;
    bool         mHasPitch = false;
    bool         mInTune   = false;
    juce::String mNoteName;
};
