#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "BandPanel.h"
#include "HorrorLookAndFeel.h"

//==============================================================================
// FleshRenderEditor — the main plugin window.
//
// Layout (600 × 620):
//   Header  (0…90):    title "FLESH RENDER", subtitle, blood drips
//   Panels  (90…480):  three BandPanel components side by side
//   Output  (480…580): master output level knob (Neve 1073 style)
//   Footer  (580…620): branding bar
//==============================================================================
class FleshRenderEditor : public juce::AudioProcessorEditor
{
public:
    explicit FleshRenderEditor (FleshRenderProcessor&);
    ~FleshRenderEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    FleshRenderProcessor& processor;

    HorrorLookAndFeel laf;
    NeveLookAndFeel   neveLaf;

    BandPanel lowPanel, midPanel, highPanel;

    // Master output level knob (Neve 1073 style)
    juce::Slider outputKnob;
    juce::Label  outputLabel;
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> outputAttach;

    // Background texture image — pre-rendered once for performance
    juce::Image backgroundTexture;
    void buildBackgroundTexture (int w, int h);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FleshRenderEditor)
};
