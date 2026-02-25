#pragma once
#include <JuceHeader.h>

//==============================================================================
// Horror-themed LookAndFeel: industrial metal knobs, blood-red indicators,
// bone-white labels — designed to look brutal and horrifying.
//==============================================================================
class HorrorLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HorrorLookAndFeel();
    ~HorrorLookAndFeel() override = default;

    //==========================================================================
    // Rotary knob
    //==========================================================================
    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider& slider) override;

    //==========================================================================
    // Label
    //==========================================================================
    void drawLabel (juce::Graphics& g, juce::Label& label) override;
    juce::Font getLabelFont (juce::Label& label) override;

    //==========================================================================
    // Helpers used by the editor paint()
    //==========================================================================
    static void drawBloodDrips (juce::Graphics& g,
                                float leftX, float rightX,
                                float y,
                                float maxDripLength);

    static void drawGrainTexture (juce::Graphics& g,
                                  juce::Rectangle<int> bounds,
                                  float alpha = 0.018f);

    static void drawPanelBackground (juce::Graphics& g,
                                     juce::Rectangle<int> bounds);

    //==========================================================================
    // Colour palette
    //==========================================================================
    static constexpr juce::uint32 COL_BACKGROUND   = 0xFF080208;
    static constexpr juce::uint32 COL_HEADER_BG    = 0xFF1A0000;
    static constexpr juce::uint32 COL_PANEL_BG     = 0xFF0D0808;
    static constexpr juce::uint32 COL_PANEL_BORDER = 0xFF5A0000;
    static constexpr juce::uint32 COL_BLOOD_DARK   = 0xFF5A0000;
    static constexpr juce::uint32 COL_BLOOD        = 0xFF8B0000;
    static constexpr juce::uint32 COL_BLOOD_BRIGHT = 0xFFBB1515;
    static constexpr juce::uint32 COL_BONE         = 0xFFCCBDA5;
    static constexpr juce::uint32 COL_BONE_DIM     = 0xFF7A6A55;
    static constexpr juce::uint32 COL_KNOB_BODY    = 0xFF1A1A1A;
    static constexpr juce::uint32 COL_KNOB_SHINE   = 0xFF2E2E2E;
    static constexpr juce::uint32 COL_KNOB_SHADOW  = 0xFF090909;
    static constexpr juce::uint32 COL_RUST         = 0xFF3D2020;
};

//==============================================================================
// Neve 1073-style LookAndFeel: chunky matte-black knobs with white marker line,
// knurled outer ring, scale dots — classic British console aesthetic.
//==============================================================================
class NeveLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeveLookAndFeel();
    ~NeveLookAndFeel() override = default;

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider& slider) override;

    void drawLabel  (juce::Graphics& g, juce::Label& label) override;
    juce::Font getLabelFont (juce::Label& label) override;
};
