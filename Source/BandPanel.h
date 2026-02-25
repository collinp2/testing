#pragma once
#include <JuceHeader.h>

class HorrorLookAndFeel;

//==============================================================================
// BandPanel — one column of the plugin UI containing three knobs for
// Saturation, Distortion, and Fuzz for a single frequency band.
//==============================================================================
class BandPanel : public juce::Component
{
public:
    BandPanel();
    ~BandPanel() override = default;

    //==========================================================================
    // Call once after construction to wire up the APVTS
    //==========================================================================
    void init (juce::AudioProcessorValueTreeState& apvts,
               const juce::String& bandName,
               const juce::String& freqRange,
               const juce::String& satParamId,
               const juce::String& distParamId,
               const juce::String& fuzzParamId);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::String bandName, freqRange;

    juce::Slider satKnob,  distKnob,  fuzzKnob;
    juce::Label  satLabel, distLabel, fuzzLabel;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> satAttach, distAttach, fuzzAttach;

    // Avoid "JUCE Leak Detector" warnings from internal components
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPanel)
};
