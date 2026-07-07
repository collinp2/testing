#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "BandPanel.h"
#include "HorrorLookAndFeel.h"
#include "LevelMeter.h"

//==============================================================================
// FleshRenderEditor (v2) — the main plugin window.
//
// The whole UI lives on a fixed-size canvas (kBaseW x kBaseH) that the window
// scales uniformly: drag the corner and everything just gets bigger (aspect
// ratio locked, 75%..200%, chosen size persisted with the plugin state).
//
// Layout (top to bottom, strict signal order):
//   Header      : title, subtitle, blood drips
//   NEVE EQ     : 1073/74-style pre EQ (HPF + low/mid/high, stepped freqs)
//   BANDS       : three BandPanels + crossover / mix column
//   GRAPHIC EQ  : API-560 style post EQ (10 bands)
//   FILTERS/OUT : post HP/LP, master output knob, output meter
//   Footer      : branding bar
//==============================================================================
class FleshRenderEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit FleshRenderEditor (FleshRenderProcessor&);
    ~FleshRenderEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void paintContent (juce::Graphics&);
    void layoutContent();
    void buildBackgroundTexture();

    juce::Slider& makeKnob (const juce::String& paramId, juce::LookAndFeel* lnfToUse);
    juce::Label&  makeLabel (const juce::String& text);
    juce::ComboBox& makeCombo (const juce::String& paramId, const juce::StringArray& items);
    juce::TextButton& makeToggle (const juce::String& paramId, const juce::String& text);

    FleshRenderProcessor& processor;

    HorrorLookAndFeel laf;
    NeveLookAndFeel   neveLaf;

    // ---- Scalable canvas ----
    static constexpr int kBaseW = 980, kBaseH = 880;
    struct ContentComp : public juce::Component
    {
        explicit ContentComp (FleshRenderEditor& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        FleshRenderEditor& owner;
    };
    ContentComp content { *this };
    juce::Image backgroundTexture;

    // ---- Neve pre EQ ----
    juce::ComboBox*   hpfBox = nullptr;
    juce::ComboBox*   lowFreqBox = nullptr;
    juce::ComboBox*   midFreqBox = nullptr;
    juce::Slider*     lowGainKnob = nullptr;
    juce::Slider*     midGainKnob = nullptr;
    juce::Slider*     highGainKnob = nullptr;
    juce::TextButton* neveOnButton = nullptr;

    // ---- Bands + crossover / mix ----
    BandPanel lowPanel, midPanel, highPanel;
    juce::Slider* xoverLowKnob = nullptr;
    juce::Slider* xoverHighKnob = nullptr;
    juce::Slider* mixKnob = nullptr;

    // ---- Graphic EQ ----
    std::array<juce::Slider*, Api560EQ::kNumBands> geqSliders {};
    juce::TextButton* geqOnButton = nullptr;

    // ---- Filters / output ----
    juce::Slider*     postHpfKnob = nullptr;
    juce::Slider*     postLpfKnob = nullptr;
    juce::TextButton* postHpfOn = nullptr;
    juce::TextButton* postLpfOn = nullptr;
    juce::Slider      outputKnob;
    std::unique_ptr<SliderAttachment> outputAttach;
    LevelMeter        outputMeter;

    // Owned widgets + attachments.
    std::vector<std::unique_ptr<juce::Slider>>       sliders;
    std::vector<std::unique_ptr<juce::Label>>        labels;
    std::vector<std::unique_ptr<juce::ComboBox>>     combos;
    std::vector<std::unique_ptr<juce::TextButton>>   buttons;
    std::vector<std::unique_ptr<SliderAttachment>>   sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>>   buttonAttachments;
    std::vector<std::unique_ptr<ComboAttachment>>    comboAttachments;

    // Section rectangles (canvas coordinates; painted in paintContent).
    juce::Rectangle<int> neveArea, bandArea, xoverArea, geqArea, bottomArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FleshRenderEditor)
};
