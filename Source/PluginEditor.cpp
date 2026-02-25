#include "PluginEditor.h"

//==============================================================================
static constexpr int EDITOR_W        = 600;
static constexpr int EDITOR_H        = 620;
static constexpr int HEADER_H        = 90;
static constexpr int FOOTER_H        = 40;
static constexpr int OUTPUT_SECTION_H = 90;
static constexpr int PANEL_MARGIN    = 12;

//==============================================================================
FleshRenderEditor::FleshRenderEditor (FleshRenderProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&laf);

    // Wire up each band panel to the APVTS
    lowPanel.init  (p.apvts, "LOW",  "DC \xe2\x80\x94 250 Hz",
                    "low_sat",  "low_dist",  "low_fuzz");
    midPanel.init  (p.apvts, "MID",  "250 Hz \xe2\x80\x94 2 kHz",
                    "mid_sat",  "mid_dist",  "mid_fuzz");
    highPanel.init (p.apvts, "HIGH", "2 kHz \xe2\x80\x94 20 kHz",
                    "high_sat", "high_dist", "high_fuzz");

    addAndMakeVisible (lowPanel);
    addAndMakeVisible (midPanel);
    addAndMakeVisible (highPanel);

    // Master output knob — Neve 1073 style
    outputKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    outputKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    outputKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                     juce::MathConstants<float>::pi * 2.8f, true);
    outputKnob.setLookAndFeel (&neveLaf);
    addAndMakeVisible (outputKnob);

    outputLabel.setText ("Output", juce::dontSendNotification);
    outputLabel.setJustificationType (juce::Justification::centred);
    outputLabel.setLookAndFeel (&neveLaf);
    addAndMakeVisible (outputLabel);

    outputAttach = std::make_unique<Attachment> (p.apvts, "output_level", outputKnob);

    setSize (EDITOR_W, EDITOR_H);
    buildBackgroundTexture (EDITOR_W, EDITOR_H);
}

FleshRenderEditor::~FleshRenderEditor()
{
    outputKnob.setLookAndFeel (nullptr);
    outputLabel.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
// Pre-render a background texture image so paint() is fast.
void FleshRenderEditor::buildBackgroundTexture (int w, int h)
{
    backgroundTexture = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics bg (backgroundTexture);

    // Base fill
    bg.fillAll (juce::Colour (HorrorLookAndFeel::COL_BACKGROUND));

    // Vignette — dark edges
    {
        juce::ColourGradient v (juce::Colours::transparentBlack, w * 0.5f, h * 0.5f,
                                 juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
        bg.setGradientFill (v);
        bg.fillRect (0, 0, w, h);
    }

    // Grain texture
    HorrorLookAndFeel::drawGrainTexture (bg, {0, 0, w, h}, 0.018f);

    // Header background
    bg.setColour (juce::Colour (HorrorLookAndFeel::COL_HEADER_BG));
    bg.fillRect (0, 0, w, HEADER_H + 4);

    // Header gradient overlay (slightly lighter top)
    {
        juce::ColourGradient hg (juce::Colour (0xFF280000), 0.0f, 0.0f,
                                  juce::Colour (HorrorLookAndFeel::COL_HEADER_BG), 0.0f, float (HEADER_H), false);
        bg.setGradientFill (hg);
        bg.fillRect (0, 0, w, HEADER_H);
    }

    // Footer background
    bg.setColour (juce::Colour (HorrorLookAndFeel::COL_HEADER_BG));
    bg.fillRect (0, h - FOOTER_H, w, FOOTER_H);

    // Header / footer / output section separator lines
    bg.setColour (juce::Colour (HorrorLookAndFeel::COL_BLOOD_DARK));
    bg.drawHorizontalLine (HEADER_H,                          2.0f, float (w - 2));
    bg.drawHorizontalLine (h - FOOTER_H,                      2.0f, float (w - 2));
    bg.drawHorizontalLine (h - FOOTER_H - OUTPUT_SECTION_H,  2.0f, float (w - 2));

    // Bright border on the header top separator
    bg.setColour (juce::Colour (HorrorLookAndFeel::COL_BLOOD).withAlpha (0.5f));
    bg.drawHorizontalLine (HEADER_H + 1, 2.0f, float (w - 2));

    // ---- Title "FLESH RENDER" -----------------------------------------------
    // Shadow layer
    bg.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 42.0f,
                             juce::Font::bold | juce::Font::italic));
    bg.setColour (juce::Colours::black.withAlpha (0.8f));
    bg.drawText ("FLESH RENDER", 3, 10, w, 46, juce::Justification::centred);

    // Dark blood under-layer
    bg.setColour (juce::Colour (0xFF4A0000));
    bg.drawText ("FLESH RENDER", 1, 11, w, 46, juce::Justification::centred);

    // Main title
    bg.setColour (juce::Colour (HorrorLookAndFeel::COL_BLOOD_BRIGHT));
    bg.drawText ("FLESH RENDER", 0, 9, w, 46, juce::Justification::centred);

    // Title highlight (thin bright stroke — approximate with a slightly offset copy)
    bg.setColour (juce::Colour (0xFFFF4040).withAlpha (0.25f));
    bg.drawText ("FLESH RENDER", 0, 8, w, 46, juce::Justification::centred);

    // ---- Subtitle -----------------------------------------------------------
    bg.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
    bg.setColour (juce::Colour (HorrorLookAndFeel::COL_BONE_DIM));
    bg.drawText ("[ MULTIBAND DECIMATOR  //  SATURATION  //  DISTORTION  //  FUZZ ]",
                  0, 57, w, 14, juce::Justification::centred);

    // ---- Thin decorative lines flanking the subtitle -----------------------
    bg.setColour (juce::Colour (HorrorLookAndFeel::COL_BLOOD_DARK).withAlpha (0.5f));
    bg.drawHorizontalLine (71, float (w) * 0.04f, float (w) * 0.28f);
    bg.drawHorizontalLine (71, float (w) * 0.72f, float (w) * 0.96f);

    // ---- Blood drips from header --------------------------------------------
    HorrorLookAndFeel::drawBloodDrips (bg, 10.0f, float (w - 10), float (HEADER_H - 2), 28.0f);

    // ---- Footer text --------------------------------------------------------
    bg.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain));
    bg.setColour (juce::Colour (HorrorLookAndFeel::COL_BONE_DIM).withAlpha (0.7f));
    bg.drawText ("VOIDCRAFT AUDIO  \xe2\x80\xa2  FLESH RENDER v1.0  \xe2\x80\xa2  HORROR-GRADE MULTIBAND PROCESSING",
                  0, h - FOOTER_H + 12, w, 16, juce::Justification::centred);

    // Small skull dingbats (unicode) in footer
    bg.setColour (juce::Colour (HorrorLookAndFeel::COL_BLOOD_DARK).withAlpha (0.8f));
    bg.setFont (juce::Font (14.0f));
    bg.drawText ("\xe2\x98\xa0", 20, h - FOOTER_H + 10, 20, 18, juce::Justification::centred);
    bg.drawText ("\xe2\x98\xa0", w - 40, h - FOOTER_H + 10, 20, 18, juce::Justification::centred);
}

//==============================================================================
void FleshRenderEditor::paint (juce::Graphics& g)
{
    // Draw pre-rendered background
    g.drawImageAt (backgroundTexture, 0, 0);
}

//==============================================================================
void FleshRenderEditor::resized()
{
    const int outputSectionY  = getHeight() - FOOTER_H - OUTPUT_SECTION_H;
    const int panelAreaTop    = HEADER_H + 6;
    const int panelAreaBottom = outputSectionY - 6;
    const int panelH          = panelAreaBottom - panelAreaTop;

    // Three equal-width panels with margins
    const int totalMargins = PANEL_MARGIN * 4;
    const int panelW       = (getWidth() - totalMargins) / 3;

    const int p1x = PANEL_MARGIN;
    const int p2x = PANEL_MARGIN * 2 + panelW;
    const int p3x = PANEL_MARGIN * 3 + panelW * 2;

    lowPanel .setBounds (p1x, panelAreaTop, panelW, panelH);
    midPanel .setBounds (p2x, panelAreaTop, panelW, panelH);
    highPanel.setBounds (p3x, panelAreaTop, panelW, panelH);

    // Output knob — centred in the output section
    const int knobSize = 56;
    const int knobX    = (getWidth() - knobSize) / 2;
    const int knobY    = outputSectionY + 6;
    outputKnob .setBounds (knobX, knobY, knobSize, knobSize);
    outputLabel.setBounds (0, knobY + knobSize + 2, getWidth(), 14);
}
