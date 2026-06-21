#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "PresetManager.h"
#include "PitchDetector.h"
#include "StrobeTuner.h"
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

    enum Tab { TabAmp = 0, TabTone, TabFx, TabTuner, NumTabs };

    void timerCallback() override;
    void showTab (int tab);

    // Tab-tagged control factories (tag both the widget and its label so they
    // hide/show with the tab they belong to).
    juce::Slider& addKnob (int tab, const juce::String& paramID, const juce::String& labelText);
    juce::Slider& addVSlider (int tab, const juce::String& paramID, const juce::String& labelText);
    juce::Slider& addHSlider (int tab, const juce::String& paramID);
    juce::TextButton& addToggle (int tab, const juce::String& paramID, const juce::String& text);
    static void assignTab (juce::Component& c, int tab) { c.getProperties().set ("tab", tab); }

    void chooseModel (int ampIndex);
    void chooseIR (int cabIndex);
    void refreshPresetBox();
    void savePresetDialog();

    NecronamAudioProcessor& processor;
    PresetManager           presetManager;
    HorrorLookAndFeel       lnf;

    // ---- Preset bar (persistent) ----
    juce::ComboBox   presetBox;
    juce::TextButton presetPrevButton { "<" }, presetNextButton { ">" }, presetSaveButton { "SAVE" };

    // ---- Tab bar (persistent) ----
    juce::TextButton tabAmpButton { "AMP" }, tabToneButton { "TONE" },
                     tabFxButton { "FX" },   tabTunerButton { "TUNER" };
    int currentTab = TabAmp;

    // ---- Amp A / B model loaders (AMP tab) ----
    juce::TextButton loadModelAButton { "Load Amp A" }, clearModelAButton { "X" };
    juce::TextButton loadModelBButton { "Load Amp B" }, clearModelBButton { "X" };
    juce::Label      modelANameLabel  { {}, "(no model)" };
    juce::Label      modelBNameLabel  { {}, "(no model)" };
    juce::ComboBox   routingBox;
    std::unique_ptr<ComboAttach> routingAttach;

    // ---- Cab A / B IR loaders (TONE tab) ----
    juce::TextButton loadIRAButton { "Load IR A" }, clearIRAButton { "X" };
    juce::TextButton loadIRBButton { "Load IR B" }, clearIRBButton { "X" };
    juce::Label      irANameLabel  { {}, "(no IR)" };
    juce::Label      irBNameLabel  { {}, "(no IR)" };

    // ---- Master (persistent) ----
    juce::ComboBox   outputModeBox;
    std::unique_ptr<ComboAttach> outputModeAttach;
    juce::Slider     qualitySlider;   // AMP tab
    juce::Slider     masterFader;     // persistent
    LevelMeter       inMeter, namOutMeter, masterMeter;

    // ---- FX order (FX tab) ----
    juce::ComboBox   fxOrderBox;
    std::unique_ptr<ComboAttach> fxOrderAttach;

    // ---- Tuner (TUNER tab) ----
    StrobeTuner        strobeTuner;
    PitchDetector      pitchDetector;
    std::vector<float> tunerBuffer;

    // Owned widgets + labels + attachments.
    std::vector<std::unique_ptr<juce::Slider>>     sliders;
    std::vector<std::unique_ptr<juce::Label>>      labels;
    std::vector<std::unique_ptr<juce::TextButton>> toggles;
    std::vector<std::unique_ptr<SliderAttach>>     sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttach>>     buttonAttachments;

    // Direct references for layout.
    juce::Slider* inputKnob = nullptr;
    juce::Slider* gateKnob = nullptr;
    juce::Slider* ampOutKnob = nullptr;
    juce::Slider* inputCalKnob = nullptr;
    juce::Slider* spreadKnob = nullptr;
    juce::Slider* ampALevelKnob = nullptr;
    juce::Slider* ampBLevelKnob = nullptr;
    juce::Slider* hpfKnob = nullptr;
    juce::Slider* lpfKnob = nullptr;
    juce::Slider* cabALevelKnob = nullptr;
    juce::Slider* cabBLevelKnob = nullptr;
    juce::Slider* delayTimeKnob = nullptr;
    juce::Slider* delayFbKnob = nullptr;
    juce::Slider* delayMixKnob = nullptr;
    juce::Slider* reverbSizeKnob = nullptr;
    juce::Slider* reverbDampKnob = nullptr;
    juce::Slider* reverbMixKnob = nullptr;
    juce::TextButton* gateToggle = nullptr;
    juce::TextButton* frontSatToggle = nullptr;
    juce::TextButton* cabAToggle = nullptr;
    juce::TextButton* cabBToggle = nullptr;
    juce::TextButton* eqToggle = nullptr;
    juce::TextButton* satToggle = nullptr;
    juce::TextButton* hpfToggle = nullptr;
    juce::TextButton* lpfToggle = nullptr;
    juce::TextButton* delayToggle = nullptr;
    juce::TextButton* reverbToggle = nullptr;
    juce::TextButton* tunerToggle = nullptr;
    std::array<juce::Slider*, Api560EQ::kNumBands> eqSliders {};
    std::array<juce::Slider*, 9> satKnobs {};        // post / output saturation
    std::array<juce::Slider*, 9> frontSatKnobs {};   // front / pre-amp saturation

    // Section rectangles (filled in resized(), painted in paint()).
    juce::Rectangle<int> presetArea, tabBarArea, masterArea, bodyArea, qualityLabelArea;
    juce::Rectangle<int> ampModelsArea, inputArea, frontSatArea;     // AMP tab
    juce::Rectangle<int> cabArea, filterArea, eqArea, postSatArea;   // TONE tab
    juce::Rectangle<int> delayArea, reverbArea, fxOrderArea;         // FX tab
    juce::Rectangle<int> tunerArea;                                  // TUNER tab

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> saveDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NecronamAudioProcessorEditor)
};
