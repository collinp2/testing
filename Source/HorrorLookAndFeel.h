#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace horror
{
    // ----- Light "Stained Marble" palette, colour-matched to the reference image -----
    inline constexpr juce::uint32 COL_BACKGROUND   = 0xFFE6E0D5; // Aged marble / parchment base
    inline constexpr juce::uint32 COL_HEADER_BG    = 0xFFCEC3B8; // Darker stained marble (header tier)
    inline constexpr juce::uint32 COL_PANEL_BG     = 0xFFEFEAE0; // Cleaner marble for panels
    inline constexpr juce::uint32 COL_PANEL_BORDER = 0xFF3E110E; // Oxblood outline

    inline constexpr juce::uint32 COL_BLOOD_DARK   = 0xFF330907; // Deep dried oxblood
    inline constexpr juce::uint32 COL_BLOOD        = 0xFF4C0A08; // Oxidized blood red
    inline constexpr juce::uint32 COL_BLOOD_BRIGHT = 0xFF5D0B09; // Brightest accent present (a muted brick)

    inline constexpr juce::uint32 COL_BONE         = 0xFF2A1612; // Near-black crimson for readable text
    inline constexpr juce::uint32 COL_BONE_DIM     = 0xFF7A6A60; // Faded stain for inactive text/markers

    inline constexpr juce::uint32 COL_KNOB_BODY    = 0xFF3A0E0C; // Dark clotted knob body
    inline constexpr juce::uint32 COL_KNOB_SHINE   = 0xFF6A2420; // Muted wet shine
    inline constexpr juce::uint32 COL_KNOB_SHADOW  = 0xFFC9BCA9; // Soft marble-tinted drop shadow
    inline constexpr juce::uint32 COL_RUST         = 0xFF7D5A50; // Dried flesh / rust accent

    // Light tone for text/marks drawn on top of dark blood fills.
    inline constexpr juce::uint32 COL_BONE_LIGHT   = 0xFFEFEAE0;

    // Warm grey for marble veins (matches reference vein tone).
    inline constexpr juce::uint32 COL_VEIN         = 0xFFA09990;

    inline juce::Colour c (juce::uint32 argb) { return juce::Colour (argb); }
}

class HorrorLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HorrorLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    static void drawGrainTexture (juce::Graphics&, juce::Rectangle<int> area, float alpha = 0.04f);
    static void drawBloodDrips   (juce::Graphics&, juce::Rectangle<float> area);
    static void drawPanelBackground (juce::Graphics&, juce::Rectangle<float> bounds);

    // Bundled body typeface (IBM Plex Mono), shared by the editor and L&F.
    static juce::Font monoFont (float height, bool bold = true);
};
