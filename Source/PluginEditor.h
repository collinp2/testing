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

// ---------------------------------------------------------------------------
// Small gain-reduction meter for the LA-2A section: fills DOWN from the top
// as the compressor works (0 .. ~20 dB scale), with a slow-decay hold.
// ---------------------------------------------------------------------------
class GainReductionMeter : public juce::Component
{
public:
    void update (float grDb)
    {
        mGr = juce::jmax (grDb, mGr * 0.85f);   // decay toward zero
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace horror;
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (c (COL_KNOB_SHADOW));
        g.fillRoundedRectangle (b, 2.0f);

        const float frac = juce::jlimit (0.0f, 1.0f, mGr / 20.0f);
        auto fill = b.withHeight (b.getHeight() * frac);
        g.setColour (c (COL_BLOOD));
        g.fillRoundedRectangle (fill, 2.0f);

        g.setColour (c (COL_PANEL_BORDER));
        g.drawRoundedRectangle (b, 2.0f, 1.0f);

        g.setColour (c (COL_BONE));
        g.setFont (HorrorLookAndFeel::monoFont (8.5f, true));
        g.drawText ("GR", getLocalBounds().removeFromBottom (12), juce::Justification::centred);
        g.drawText (juce::String (juce::roundToInt (mGr)) + "dB",
                    getLocalBounds().removeFromTop (12), juce::Justification::centred);
    }

private:
    float mGr = 0.0f;
};

// ---------------------------------------------------------------------------
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

    // Tabs follow the signal chain strictly.
    enum Tab { TabPre = 0, TabAmpCab, TabPost, TabFx, TabTuner, NumTabs };

    void timerCallback() override;
    void showTab (int tab);
    void refreshDriveVisibility();
    void paintContent (juce::Graphics&);
    void layoutContent();

    juce::Slider& addKnob (int tab, const juce::String& paramID, const juce::String& labelText);
    juce::Slider& addVSlider (int tab, const juce::String& paramID, const juce::String& labelText);
    juce::Slider& addHSlider (int tab, const juce::String& paramID);
    juce::TextButton& addToggle (int tab, const juce::String& paramID, const juce::String& text);
    static void assignTab (juce::Component& c, int tab) { c.getProperties().set ("tab", tab); }

    void chooseModel (int ampIndex);
    void chooseIR (int cabIndex);
    void stepModel (int ampIndex, int dir);
    void refreshPresetBox();
    void savePresetDialog();

    NecronamAudioProcessor& processor;
    PresetManager           presetManager;
    HorrorLookAndFeel       lnf;

    // ---- Scalable UI ----
    // Everything lives on this fixed-size canvas; the editor window is
    // resizable (aspect locked, 75%..200%) and stretches the canvas
    // uniformly, so the whole UI just gets bigger on larger monitors.
    static constexpr int kBaseW = 1140, kBaseH = 800;
    struct ContentComp : public juce::Component
    {
        explicit ContentComp (NecronamAudioProcessorEditor& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        NecronamAudioProcessorEditor& owner;
    };
    ContentComp content { *this };

    // ---- Preset bar (persistent) ----
    juce::ComboBox   presetBox;
    juce::TextButton presetPrevButton { "<" }, presetNextButton { ">" }, presetSaveButton { "SAVE" };

    // ---- Tab bar (persistent) ----
    juce::TextButton tabPreButton   { "PRE" },  tabAmpCabButton { "AMP / CAB" },
                     tabPostButton  { "POST" }, tabFxButton     { "FX" },
                     tabTunerButton { "TUNER" };
    int currentTab = TabAmpCab;

    // Solo Amp/Cab: bright switch in the tab bar; while on, every effect
    // module except the amp/sag/cab is bypassed and the other tabs grey out.
    juce::TextButton soloButton { "SOLO AMP/CAB" };
    std::unique_ptr<ButtonAttach> soloAttach;
    bool lastSolo = false;

    // ---- Master strip (persistent, all pages) ----
    juce::Slider     masterFader;
    juce::Slider     cleanKnob;
    juce::ComboBox   outputModeBox;
    std::unique_ptr<ComboAttach> outputModeAttach;
    LevelMeter       inMeter, namOutMeter, masterMeter;
    juce::Slider*    inputKnob = nullptr;          // master input level (chain start)
    juce::Slider*    diAlignKnob = nullptr;        // clean-blend DI alignment

    // ---- PRE tab ----
    juce::Slider* gateKnob = nullptr;
    juce::Slider* gateReleaseKnob = nullptr;
    juce::TextButton* gateToggle = nullptr;
    juce::ComboBox   gatePosBox;
    std::unique_ptr<ComboAttach> gatePosAttach;

    juce::TextButton* frontSatToggle = nullptr;
    juce::Slider* frontXLowKnob = nullptr;
    juce::Slider* frontXHighKnob = nullptr;
    juce::Slider* frontSatMixKnob = nullptr;
    std::array<juce::Slider*, 9> frontSatKnobs {};

    juce::TextButton* driveToggle = nullptr;
    juce::ComboBox   driveCircuitBox;
    std::unique_ptr<ComboAttach> driveCircuitAttach;
    juce::Slider* tcGainKnob = nullptr;
    juce::Slider* tcBassKnob = nullptr;
    juce::Slider* tcMidKnob = nullptr;
    juce::Slider* tcTrebleKnob = nullptr;
    juce::Slider* tcLevelKnob = nullptr;
    juce::Slider* tsDriveKnob = nullptr;
    juce::Slider* tsToneKnob = nullptr;
    juce::Slider* tsLevelKnob = nullptr;
    int lastDriveCircuit = -1;

    juce::Slider* lowCutKnob = nullptr;
    juce::TextButton* lowCutToggle = nullptr;

    // ---- AMP / CAB tab ----
    juce::ComboBox   inputModeBox;
    std::unique_ptr<ComboAttach> inputModeAttach;
    juce::TextButton loadModelAButton { "Load A" }, clearModelAButton { "X" };
    juce::TextButton loadModelBButton { "Load B" }, clearModelBButton { "X" };
    juce::TextButton prevAButton { "<" }, nextAButton { ">" };
    juce::TextButton prevBButton { "<" }, nextBButton { ">" };
    juce::Label      modelANameLabel { {}, "(no model)" };
    juce::Label      modelBNameLabel { {}, "(no model)" };
    juce::ComboBox   routingBox;
    std::unique_ptr<ComboAttach> routingAttach;
    juce::Slider* spreadKnob = nullptr;
    juce::Slider* ampALevelKnob = nullptr;
    juce::Slider* ampBLevelKnob = nullptr;
    juce::Slider* inputCalKnob = nullptr;
    juce::Slider* ampOutKnob = nullptr;
    juce::TextButton* ampAToggle = nullptr;
    juce::TextButton* ampBToggle = nullptr;
    juce::TextButton* ampAMuteToggle = nullptr;
    juce::TextButton* ampBMuteToggle = nullptr;
    juce::TextButton* ampAPhaseToggle = nullptr;
    juce::TextButton* ampBPhaseToggle = nullptr;
    juce::Slider* ampAAlignKnob = nullptr;
    juce::Slider* ampBAlignKnob = nullptr;
    juce::Slider qualitySlider;

    juce::Slider* sagKnob = nullptr;

    juce::TextButton loadIRAButton { "Load IR A" }, clearIRAButton { "X" };
    juce::TextButton loadIRBButton { "Load IR B" }, clearIRBButton { "X" };
    juce::Label      irANameLabel { {}, "(no IR)" };
    juce::Label      irBNameLabel { {}, "(no IR)" };
    juce::TextButton* cabAToggle = nullptr;
    juce::TextButton* cabBToggle = nullptr;
    juce::TextButton* cabAMuteToggle = nullptr;
    juce::TextButton* cabBMuteToggle = nullptr;
    juce::TextButton* cabAPhaseToggle = nullptr;
    juce::TextButton* cabBPhaseToggle = nullptr;
    juce::Slider* cabALevelKnob = nullptr;
    juce::Slider* cabBLevelKnob = nullptr;
    juce::Slider* cabAAlignSlider = nullptr;
    juce::Slider* cabBAlignSlider = nullptr;

    // ---- POST tab ----
    juce::TextButton* eqToggle = nullptr;
    std::array<juce::Slider*, Api560EQ::kNumBands> eqSliders {};

    juce::TextButton* compToggle = nullptr;
    juce::Slider* compKnob = nullptr;
    juce::Slider* compGainKnob = nullptr;
    GainReductionMeter grMeter;

    juce::TextButton* satToggle = nullptr;
    juce::Slider* satXLowKnob = nullptr;
    juce::Slider* satXHighKnob = nullptr;
    juce::Slider* satMixKnob = nullptr;
    std::array<juce::Slider*, 9> satKnobs {};

    juce::Slider* hpfKnob = nullptr;
    juce::Slider* lpfKnob = nullptr;
    juce::TextButton* hpfToggle = nullptr;
    juce::TextButton* lpfToggle = nullptr;

    // ---- FX tab ----
    juce::Slider* delayTimeKnob = nullptr;
    juce::Slider* delayFbKnob = nullptr;
    juce::Slider* delayMixKnob = nullptr;
    juce::Slider* reverbSizeKnob = nullptr;
    juce::Slider* reverbDampKnob = nullptr;
    juce::Slider* reverbMixKnob = nullptr;
    juce::TextButton* delayToggle = nullptr;
    juce::TextButton* reverbToggle = nullptr;
    juce::ComboBox   reverbTypeBox;
    std::unique_ptr<ComboAttach> reverbTypeAttach;
    juce::ComboBox   fxOrderBox;
    std::unique_ptr<ComboAttach> fxOrderAttach;

    // ---- TUNER tab ----
    juce::TextButton*  tunerToggle = nullptr;
    StrobeTuner        strobeTuner;
    PitchDetector      pitchDetector;
    std::vector<float> tunerBuffer;

    // Owned widgets + attachments.
    std::vector<std::unique_ptr<juce::Slider>>     sliders;
    std::vector<std::unique_ptr<juce::Label>>      labels;
    std::vector<std::unique_ptr<juce::TextButton>> toggles;
    std::vector<std::unique_ptr<SliderAttach>>     sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttach>>     buttonAttachments;

    // Drive circuit sub-panels (visibility follows the circuit selector).
    std::vector<juce::Component*> tcComps, tsComps;

    // Section rectangles.
    juce::Rectangle<int> presetArea, tabBarArea, masterArea, bodyArea;
    juce::Rectangle<int> frontSatArea, driveArea, lowCutArea, gateArea;            // PRE
    juce::Rectangle<int> ampModelsArea, sagArea, cabArea, qualityLabelArea;        // AMP/CAB
    juce::Rectangle<int> eqArea, compArea, postSatArea, filterArea;                // POST
    juce::Rectangle<int> delayArea, reverbArea, fxOrderArea;                       // FX
    juce::Rectangle<int> tunerArea;                                                // TUNER

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> saveDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NecronamAudioProcessorEditor)
};
