#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class GEQ12AudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit GEQ12AudioProcessorEditor(GEQ12AudioProcessor&);
    ~GEQ12AudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    GEQ12AudioProcessor& mProcessor;

    static constexpr int kNumBands = GEQ12AudioProcessor::kNumBands;

    std::array<juce::Slider, kNumBands> mBandSliders;
    std::array<juce::Label, kNumBands>  mBandLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
               kNumBands> mAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GEQ12AudioProcessorEditor)
};
