#include "BandPanel.h"
#include "HorrorLookAndFeel.h"

//==============================================================================
BandPanel::BandPanel()
{
    // Configure all three knobs identically — only the APVTS attachment differs
    auto configureKnob = [&] (juce::Slider& k)
    {
        k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f, true);
        addAndMakeVisible (k);
    };

    configureKnob (satKnob);
    configureKnob (distKnob);
    configureKnob (fuzzKnob);

    auto configureLabel = [&] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    configureLabel (satLabel,  "SATURATION");
    configureLabel (distLabel, "DISTORTION");
    configureLabel (fuzzLabel, "FUZZ");
}

//==============================================================================
void BandPanel::init (juce::AudioProcessorValueTreeState& apvts,
                       const juce::String& name,
                       const juce::String& range,
                       const juce::String& satId,
                       const juce::String& distId,
                       const juce::String& fuzzId)
{
    bandName  = name;
    freqRange = range;

    satAttach  = std::make_unique<Attachment> (apvts, satId,  satKnob);
    distAttach = std::make_unique<Attachment> (apvts, distId, distKnob);
    fuzzAttach = std::make_unique<Attachment> (apvts, fuzzId, fuzzKnob);
}

//==============================================================================
void BandPanel::paint (juce::Graphics& g)
{
    // Panel background
    HorrorLookAndFeel::drawPanelBackground (g, getLocalBounds());

    // Band name
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold));
    g.setColour (juce::Colour (HorrorLookAndFeel::COL_BLOOD_BRIGHT));
    g.drawText (bandName.toUpperCase(), 0, 10, getWidth(), 18, juce::Justification::centred);

    // Frequency range
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
    g.setColour (juce::Colour (HorrorLookAndFeel::COL_BONE_DIM));
    g.drawText (freqRange, 0, 28, getWidth(), 13, juce::Justification::centred);

    // Horizontal divider under header
    g.setColour (juce::Colour (HorrorLookAndFeel::COL_BLOOD_DARK).withAlpha (0.6f));
    g.drawHorizontalLine (46, 8.0f, float (getWidth() - 8));

    // Small decorative skull-teeth notches on divider
    g.setColour (juce::Colour (HorrorLookAndFeel::COL_BLOOD_DARK));
    for (int i = 0; i < 5; ++i)
    {
        const float nx = 20.0f + i * (float (getWidth() - 40) / 4.0f);
        g.fillRect (juce::Rectangle<float> (nx - 1.0f, 43.0f, 2.0f, 4.0f));
    }
}

//==============================================================================
void BandPanel::resized()
{
    // Layout: header = 50px, then three equal knob sections below
    const int headerH  = 50;
    const int available = getHeight() - headerH;
    const int sectionH  = available / 3;

    auto layoutSection = [&] (juce::Slider& knob, juce::Label& label, int sectionY)
    {
        const int knobSize  = juce::jmin (getWidth() - 16, sectionH - 24);
        const int knobX     = (getWidth() - knobSize) / 2;
        const int knobY     = sectionY + 8;
        const int labelY    = sectionY + sectionH - 20;

        knob.setBounds  (knobX, knobY, knobSize, knobSize);
        label.setBounds (0, labelY, getWidth(), 16);
    };

    layoutSection (satKnob,  satLabel,  headerH);
    layoutSection (distKnob, distLabel, headerH + sectionH);
    layoutSection (fuzzKnob, fuzzLabel, headerH + sectionH * 2);
}
