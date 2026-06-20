#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "HorrorLookAndFeel.h"
#include "LevelMeter.h"

class NecronamAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit NecronamAudioProcessorEditor (NecronamAudioProcessor&);
    ~NecronamAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;

    juce::Slider& addKnob (const juce::String& paramID, const juce::String& labelText);
    juce::Slider& addVSlider (const juce::String& paramID, const juce::String& labelText);
    juce::TextButton& addToggle (const juce::String& paramID, const juce::String& text);
    void chooseFile (bool isModel);

    NecronamAudioProcessor& processor;
    HorrorLookAndFeel lnf;

    // Model / IR
    juce::TextButton loadModelButton { "Load Model" }, clearModelButton { "X" };
    juce::TextButton loadIRButton    { "Load IR" },    clearIRButton    { "X" };
    juce::Label      modelNameLabel  { {}, "(no model)" };
    juce::Label      irNameLabel     { {}, "(no IR)" };
    juce::ComboBox   outputModeBox;
    std::unique_ptr<ComboAttach> outputModeAttach;

    // A2 quality / efficiency slider.
    juce::Slider qualitySlider;

    // Master output level fader + level meters.
    juce::Slider masterFader;
    LevelMeter   inMeter, namOutMeter, masterMeter;

    // Owned widgets (knobs / sliders / toggles) and their labels + attachments.
    std::vector<std::unique_ptr<juce::Slider>>     sliders;
    std::vector<std::unique_ptr<juce::Label>>      labels;
    std::vector<std::unique_ptr<juce::TextButton>> toggles;
    std::vector<std::unique_ptr<SliderAttach>>     sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttach>>     buttonAttachments;

    // Direct references for layout.
    juce::Slider* inputKnob = nullptr;
    juce::Slider* outputKnob = nullptr;
    juce::Slider* gateKnob = nullptr;
    juce::Slider* inputCalKnob = nullptr;
    juce::Slider* hpfKnob = nullptr;
    juce::Slider* lpfKnob = nullptr;
    juce::TextButton* gateToggle = nullptr;
    juce::TextButton* irToggle = nullptr;
    juce::TextButton* eqToggle = nullptr;
    juce::TextButton* satToggle = nullptr;
    juce::TextButton* hpfToggle = nullptr;
    juce::TextButton* lpfToggle = nullptr;
    std::array<juce::Slider*, Api560EQ::kNumBands> eqSliders {};
    std::array<juce::Slider*, 9> satKnobs {};

    // Section rectangles (filled in resized(), painted in paint()).
    juce::Rectangle<int> ampArea, cabArea, filterArea, eqArea, satArea, masterArea;
    juce::Rectangle<int> qualityLabelArea;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NecronamAudioProcessorEditor)
};
