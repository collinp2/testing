#pragma once

#include <JuceHeader.h>

/** Vertical bar level meter with green/amber/red zones, peak hold, and
    clip latch (click to reset). */
class LevelMeter : public juce::Component
{
public:
    LevelMeter();

    /** Update the current level (0..1 linear amplitude).
        Safe to call from any thread. */
    void setLevel (float newLevel);

    /** Decay the peak hold and refresh.  Call at ~30 Hz from a timer. */
    void timerTick();

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::atomic<float> currentLevel { 0.0f };
    float displayLevel   { 0.0f };
    float peakLevel      { 0.0f };
    int   peakHoldFrames { 0 };
    bool  clipped        { false };

    static constexpr float kAmberThresh = 0.5f;   // -6 dBFS ~= 0.5
    static constexpr float kRedThresh   = 0.891f;  // -1 dBFS
    static constexpr int   kPeakHoldMs  = 3000;
    static constexpr int   kTimerHz     = 30;
    static constexpr int   kPeakHoldFrames = kPeakHoldMs * kTimerHz / 1000; // 90 frames

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};
