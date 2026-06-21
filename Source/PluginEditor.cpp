#include "PluginEditor.h"
#include "BinaryData.h"

using namespace horror;
using ID = NecronamAudioProcessor::ParamID;

namespace
{
    juce::Font monoFont (float h, bool bold = true)
    {
        return HorrorLookAndFeel::monoFont (h, bold);
    }

    juce::Font logoFont (float h)
    {
        static const juce::Typeface::Ptr tf =
            juce::Typeface::createSystemTypefaceFor (BinaryData::AntonRegular_ttf,
                                                     (size_t) BinaryData::AntonRegular_ttfSize);
        return juce::Font (tf).withHeight (h);
    }
}

// ===========================================================================
NecronamAudioProcessorEditor::NecronamAudioProcessorEditor (NecronamAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), presetManager (p)
{
    setLookAndFeel (&lnf);
    tunerBuffer.resize (4096, 0.0f);

    // ---- Preset bar (persistent) ----
    addAndMakeVisible (presetBox);
    presetBox.setTextWhenNothingSelected ("(no preset)");
    presetBox.onChange = [this]
    {
        const auto name = presetBox.getText();
        if (name.isNotEmpty() && name != presetManager.getCurrentName())
            presetManager.load (name);
    };
    addAndMakeVisible (presetPrevButton);
    addAndMakeVisible (presetNextButton);
    addAndMakeVisible (presetSaveButton);
    presetPrevButton.onClick = [this] { presetManager.step (-1); refreshPresetBox(); };
    presetNextButton.onClick = [this] { presetManager.step ( 1); refreshPresetBox(); };
    presetSaveButton.onClick = [this] { savePresetDialog(); };
    refreshPresetBox();

    // ---- Tab bar (persistent) ----
    struct TabDef { juce::TextButton* btn; int tab; };
    for (auto& t : { TabDef { &tabAmpButton, TabAmp }, TabDef { &tabToneButton, TabTone },
                     TabDef { &tabFxButton, TabFx },   TabDef { &tabTunerButton, TabTuner } })
    {
        addAndMakeVisible (*t.btn);
        t.btn->setClickingTogglesState (false);
        const int tab = t.tab;
        t.btn->onClick = [this, tab] { showTab (tab); };
    }

    // ---- Amp A / B model loaders (AMP tab) ----
    for (auto* b : { &loadModelAButton, &clearModelAButton, &loadModelBButton, &clearModelBButton })
    {
        addAndMakeVisible (*b);
        assignTab (*b, TabAmp);
    }
    loadModelAButton.onClick  = [this] { chooseModel (0); };
    clearModelAButton.onClick = [this] { processor.clearNamModel (0); };
    loadModelBButton.onClick  = [this] { chooseModel (1); };
    clearModelBButton.onClick = [this] { processor.clearNamModel (1); };

    // Scroll through the .nam files in the loaded model's folder (per amp).
    for (auto* b : { &prevAButton, &nextAButton, &prevBButton, &nextBButton })
    {
        addAndMakeVisible (*b);
        assignTab (*b, TabAmp);
    }
    prevAButton.onClick = [this] { stepModel (0, -1); };
    nextAButton.onClick = [this] { stepModel (0,  1); };
    prevBButton.onClick = [this] { stepModel (1, -1); };
    nextBButton.onClick = [this] { stepModel (1,  1); };

    // ---- Cab A / B IR loaders (TONE tab) ----
    for (auto* b : { &loadIRAButton, &clearIRAButton, &loadIRBButton, &clearIRBButton })
    {
        addAndMakeVisible (*b);
        assignTab (*b, TabTone);
    }
    loadIRAButton.onClick  = [this] { chooseIR (0); };
    clearIRAButton.onClick = [this] { processor.clearImpulseResponse (0); };
    loadIRBButton.onClick  = [this] { chooseIR (1); };
    clearIRBButton.onClick = [this] { processor.clearImpulseResponse (1); };

    for (auto* l : { &modelANameLabel, &modelBNameLabel })
    {
        l->setFont (monoFont (11.0f, false));
        l->setColour (juce::Label::textColourId, c (COL_BONE_DIM));
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
        assignTab (*l, TabAmp);
    }
    for (auto* l : { &irANameLabel, &irBNameLabel })
    {
        l->setFont (monoFont (11.0f, false));
        l->setColour (juce::Label::textColourId, c (COL_BONE_DIM));
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
        assignTab (*l, TabTone);
    }

    // ---- Routing selector (AMP tab) ----
    addAndMakeVisible (routingBox);
    assignTab (routingBox, TabAmp);
    routingBox.addItemList ({ "Single", "Series", "Parallel" }, 1);
    routingAttach = std::make_unique<ComboAttach> (processor.apvts, ID::ampRouting, routingBox);

    // ---- Output mode selector (persistent / master) ----
    addAndMakeVisible (outputModeBox);
    outputModeBox.addItemList ({ "Raw", "Normalized", "Calibrated" }, 1);
    outputModeAttach = std::make_unique<ComboAttach> (processor.apvts, ID::outputMode, outputModeBox);

    // ---- FX order selector (FX tab) ----
    addAndMakeVisible (fxOrderBox);
    assignTab (fxOrderBox, TabFx);
    fxOrderBox.addItemList ({ "Delay -> Reverb", "Reverb -> Delay" }, 1);
    fxOrderAttach = std::make_unique<ComboAttach> (processor.apvts, ID::fxOrder, fxOrderBox);

    // ---- Strobe tuner display (TUNER tab) ----
    addAndMakeVisible (strobeTuner);
    assignTab (strobeTuner, TabTuner);

    // A2 quality slider (AMP tab).
    qualitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    qualitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 96, 16);
    qualitySlider.setColour (juce::Slider::textBoxTextColourId, c (COL_BONE));
    addAndMakeVisible (qualitySlider);
    assignTab (qualitySlider, TabAmp);
    sliderAttachments.push_back (std::make_unique<SliderAttach> (processor.apvts, ID::quality, qualitySlider));

    // Master output fader (persistent).
    masterFader.setSliderStyle (juce::Slider::LinearVertical);
    masterFader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 15);
    masterFader.setColour (juce::Slider::textBoxTextColourId, c (COL_BONE));
    addAndMakeVisible (masterFader);
    sliderAttachments.push_back (std::make_unique<SliderAttach> (processor.apvts, ID::outputLevel, masterFader));

    // Clean DI blend knob (persistent — output section).
    cleanKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    cleanKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    cleanKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
    cleanKnob.setColour (juce::Slider::textBoxTextColourId, c (COL_BONE));
    addAndMakeVisible (cleanKnob);
    sliderAttachments.push_back (std::make_unique<SliderAttach> (processor.apvts, ID::cleanBlend, cleanKnob));

    // Level meters.
    inMeter.caption     = "IN";
    namOutMeter.caption = "OUT";
    masterMeter.caption = "OUT";
    addAndMakeVisible (inMeter);     assignTab (inMeter, TabAmp);
    addAndMakeVisible (namOutMeter); assignTab (namOutMeter, TabAmp);
    addAndMakeVisible (masterMeter);

    // ---- Knobs ----
    inputKnob     = &addKnob (TabAmp, ID::inputLevel, "INPUT");
    gateKnob      = &addKnob (TabAmp, ID::gateThresh, "GATE THR");
    ampOutKnob    = &addKnob (TabAmp, ID::namOutput,  "AMP OUT");
    inputCalKnob  = &addKnob (TabAmp, ID::inputCal,   "IN CAL");
    spreadKnob    = &addKnob (TabAmp, ID::ampSpread,  "SPREAD");
    ampALevelKnob = &addKnob (TabAmp, ID::ampALevel,  "AMP A");
    ampBLevelKnob = &addKnob (TabAmp, ID::ampBLevel,  "AMP B");

    hpfKnob       = &addKnob (TabTone, ID::hpfFreq, "HI-PASS");
    lpfKnob       = &addKnob (TabTone, ID::lpfFreq, "LOW-PASS");
    frontHpfKnob  = &addKnob (TabAmp,  ID::frontHpfFreq, "HI-PASS");
    frontLpfKnob  = &addKnob (TabAmp,  ID::frontLpfFreq, "LOW-PASS");
    cabALevelKnob = &addHSlider (TabTone, ID::cabALevel);
    cabBLevelKnob = &addHSlider (TabTone, ID::cabBLevel);

    delayTimeKnob  = &addKnob (TabFx, ID::delayTime,     "TIME");
    delayFbKnob    = &addKnob (TabFx, ID::delayFeedback, "FEEDBACK");
    delayMixKnob   = &addKnob (TabFx, ID::delayMix,      "MIX");
    reverbSizeKnob = &addKnob (TabFx, ID::reverbSize,    "SIZE");
    reverbDampKnob = &addKnob (TabFx, ID::reverbDamp,    "DAMP");
    reverbMixKnob  = &addKnob (TabFx, ID::reverbMix,     "MIX");

    // ---- Toggles ----
    gateToggle     = &addToggle (TabAmp,   ID::gateActive,     "Gate");
    frontSatToggle = &addToggle (TabAmp,   ID::frontSatActive, "Front Sat On");
    ampAToggle     = &addToggle (TabAmp,   ID::ampAActive,     "On");
    ampBToggle     = &addToggle (TabAmp,   ID::ampBActive,     "On");
    frontHpfToggle = &addToggle (TabAmp,   ID::frontHpfActive, "On");
    frontLpfToggle = &addToggle (TabAmp,   ID::frontLpfActive, "On");
    cabAToggle     = &addToggle (TabTone,  ID::cabAActive,     "A On");
    cabBToggle     = &addToggle (TabTone,  ID::cabBActive,     "B On");
    eqToggle       = &addToggle (TabTone,  ID::eqActive,       "EQ On");
    satToggle      = &addToggle (TabTone,  ID::satActive,      "Sat On");
    hpfToggle      = &addToggle (TabTone,  ID::hpfActive,      "On");
    lpfToggle      = &addToggle (TabTone,  ID::lpfActive,      "On");
    delayToggle    = &addToggle (TabFx,    ID::delayActive,    "Delay On");
    reverbToggle   = &addToggle (TabFx,    ID::reverbActive,   "Reverb On");
    tunerToggle    = &addToggle (TabTuner, ID::tunerActive,    "TUNER  (mutes output)");

    // ---- Graphic EQ sliders (TONE tab) ----
    for (int i = 0; i < Api560EQ::kNumBands; ++i)
    {
        const float f = Api560EQ::kFrequencies[(size_t) i];
        const juce::String name = f >= 1000.0f ? juce::String (f / 1000.0f, (f == 16000.0f ? 0 : 1)) + "k"
                                               : juce::String (juce::roundToInt (f));
        eqSliders[(size_t) i] = &addVSlider (TabTone, NecronamAudioProcessor::eqParamID (i), name);
    }

    // ---- Saturation knobs: front (AMP) + post (TONE) ----
    const char* bandIds[3]  = { "low", "mid", "high" };
    const char* stageIds[3] = { "sat", "dist", "fuzz" };
    const char* stageNm[3]  = { "SAT", "DIST", "FUZZ" };
    for (int b = 0; b < 3; ++b)
        for (int s = 0; s < 3; ++s)
        {
            frontSatKnobs[(size_t) (b * 3 + s)] =
                &addKnob (TabAmp,  NecronamAudioProcessor::satParamID (true,  bandIds[b], stageIds[s]), stageNm[s]);
            satKnobs[(size_t) (b * 3 + s)] =
                &addKnob (TabTone, NecronamAudioProcessor::satParamID (false, bandIds[b], stageIds[s]), stageNm[s]);
        }

    startTimerHz (30);
    setSize (1140, 800);
    showTab (TabAmp);
}

NecronamAudioProcessorEditor::~NecronamAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

// ===========================================================================
juce::Slider& NecronamAudioProcessorEditor::addKnob (int tab, const juce::String& paramID, const juce::String& labelText)
{
    auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag,
                                             juce::Slider::TextBoxBelow);
    s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                            juce::MathConstants<float>::pi * 2.75f, true);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 15);
    addAndMakeVisible (*s);
    assignTab (*s, tab);

    auto lab = std::make_unique<juce::Label> (juce::String(), labelText);
    lab->setJustificationType (juce::Justification::centred);
    lab->setFont (monoFont (10.0f));
    lab->setColour (juce::Label::textColourId, c (COL_BONE));
    addAndMakeVisible (*lab);
    lab->attachToComponent (s.get(), false);
    assignTab (*lab, tab);

    sliderAttachments.push_back (std::make_unique<SliderAttach> (processor.apvts, paramID, *s));

    auto& ref = *s;
    sliders.push_back (std::move (s));
    labels.push_back (std::move (lab));
    return ref;
}

juce::Slider& NecronamAudioProcessorEditor::addVSlider (int tab, const juce::String& paramID, const juce::String& labelText)
{
    auto s = std::make_unique<juce::Slider> (juce::Slider::LinearVertical, juce::Slider::TextBoxBelow);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 14);
    addAndMakeVisible (*s);
    assignTab (*s, tab);

    auto lab = std::make_unique<juce::Label> (juce::String(), labelText);
    lab->setJustificationType (juce::Justification::centred);
    lab->setFont (monoFont (9.5f));
    lab->setColour (juce::Label::textColourId, c (COL_BONE_DIM));
    addAndMakeVisible (*lab);
    lab->attachToComponent (s.get(), false);
    assignTab (*lab, tab);

    sliderAttachments.push_back (std::make_unique<SliderAttach> (processor.apvts, paramID, *s));

    auto& ref = *s;
    sliders.push_back (std::move (s));
    labels.push_back (std::move (lab));
    return ref;
}

juce::Slider& NecronamAudioProcessorEditor::addHSlider (int tab, const juce::String& paramID)
{
    auto s = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 16);
    s->setColour (juce::Slider::textBoxTextColourId, c (COL_BONE));
    addAndMakeVisible (*s);
    assignTab (*s, tab);
    sliderAttachments.push_back (std::make_unique<SliderAttach> (processor.apvts, paramID, *s));

    auto& ref = *s;
    sliders.push_back (std::move (s));
    return ref;
}

juce::TextButton& NecronamAudioProcessorEditor::addToggle (int tab, const juce::String& paramID, const juce::String& text)
{
    auto b = std::make_unique<juce::TextButton> (text);
    b->setClickingTogglesState (true);
    addAndMakeVisible (*b);
    assignTab (*b, tab);
    buttonAttachments.push_back (std::make_unique<ButtonAttach> (processor.apvts, paramID, *b));

    auto& ref = *b;
    toggles.push_back (std::move (b));
    return ref;
}

// ===========================================================================
void NecronamAudioProcessorEditor::chooseModel (int ampIndex)
{
    juce::File dir;
    const auto last = processor.apvts.state.getProperty (
        NecronamAudioProcessor::kNamPathKey[juce::jlimit (0, 1, ampIndex)]).toString();
    if (last.isNotEmpty()) dir = juce::File (last).getParentDirectory();
    if (! dir.isDirectory()) dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    chooser = std::make_unique<juce::FileChooser> (
        "Select a NAM model (.nam) for Amp " + juce::String (ampIndex == 0 ? "A" : "B"), dir, "*.nam");

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (flags, [this, ampIndex] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile()) processor.loadNamModel (f, ampIndex);
    });
}

void NecronamAudioProcessorEditor::chooseIR (int cabIndex)
{
    juce::File dir;
    const auto last = processor.apvts.state.getProperty (
        NecronamAudioProcessor::kIrPathKey[juce::jlimit (0, 1, cabIndex)]).toString();
    if (last.isNotEmpty()) dir = juce::File (last).getParentDirectory();
    if (! dir.isDirectory()) dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    chooser = std::make_unique<juce::FileChooser> (
        "Select an impulse response (.wav) for Cab " + juce::String (cabIndex == 0 ? "A" : "B"), dir, "*.wav");

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (flags, [this, cabIndex] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile()) processor.loadImpulseResponse (f, cabIndex);
    });
}

void NecronamAudioProcessorEditor::stepModel (int ampIndex, int dir)
{
    const int a = juce::jlimit (0, 1, ampIndex);
    const auto cur = processor.apvts.state.getProperty (NecronamAudioProcessor::kNamPathKey[a]).toString();
    const juce::File curFile (cur);
    const juce::File folder = curFile.getParentDirectory();
    if (! folder.isDirectory())
        return;   // a model must be loaded first so we know which folder to scroll

    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.nam");
    if (files.isEmpty())
        return;

    struct NameSort
    {
        int compareElements (const juce::File& x, const juce::File& y) const
        {
            return x.getFileName().compareNatural (y.getFileName());
        }
    } sorter;
    files.sort (sorter);

    int idx = files.indexOf (curFile);
    if (idx < 0) idx = (dir > 0 ? -1 : 0);
    idx = (idx + dir + files.size()) % files.size();
    processor.loadNamModel (files[idx], a);
}

// ===========================================================================
void NecronamAudioProcessorEditor::refreshPresetBox()
{
    presetManager.refresh();
    presetBox.clear (juce::dontSendNotification);
    const auto& list = presetManager.getPresets();
    for (int i = 0; i < list.size(); ++i)
        presetBox.addItem (list[i], i + 1);
    const int sel = list.indexOf (presetManager.getCurrentName());
    presetBox.setSelectedId (sel >= 0 ? sel + 1 : 0, juce::dontSendNotification);
}

void NecronamAudioProcessorEditor::savePresetDialog()
{
    saveDialog = std::make_unique<juce::AlertWindow> ("SAVE PRESET", "Name this preset:",
                                                      juce::MessageBoxIconType::NoIcon);
    const auto initial = presetManager.getCurrentName().isNotEmpty() ? presetManager.getCurrentName()
                                                                     : juce::String ("Preset");
    saveDialog->addTextEditor ("name", initial);
    saveDialog->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [this] (int result)
        {
            if (result == 1 && saveDialog != nullptr)
                if (presetManager.save (saveDialog->getTextEditorContents ("name")))
                    refreshPresetBox();
            saveDialog.reset();
        }), false);
}

// ===========================================================================
void NecronamAudioProcessorEditor::showTab (int tab)
{
    currentTab = tab;
    for (auto* ch : getChildren())
    {
        const auto& props = ch->getProperties();
        if (props.contains ("tab"))
            ch->setVisible ((int) props["tab"] == currentTab);
    }
    tabAmpButton.setToggleState   (tab == TabAmp,   juce::dontSendNotification);
    tabToneButton.setToggleState  (tab == TabTone,  juce::dontSendNotification);
    tabFxButton.setToggleState    (tab == TabFx,    juce::dontSendNotification);
    tabTunerButton.setToggleState (tab == TabTuner, juce::dontSendNotification);
    repaint();
}

// ===========================================================================
void NecronamAudioProcessorEditor::timerCallback()
{
    inMeter.update     (processor.fetchInputPeak());
    namOutMeter.update (processor.fetchNamPeak());
    masterMeter.update (processor.fetchMasterPeak());

    auto setName = [] (juce::Label& l, const juce::String& n, const char* empty)
    {
        l.setText (n.isEmpty() ? empty : n, juce::dontSendNotification);
    };
    setName (modelANameLabel, processor.getLoadedModelName (0), "(no model)");
    setName (modelBNameLabel, processor.getLoadedModelName (1), "(no model)");
    setName (irANameLabel,    processor.getLoadedIRName (0),    "(no IR)");
    setName (irBNameLabel,    processor.getLoadedIRName (1),    "(no IR)");

    const bool slim = processor.isAnyModelSlimmable();
    if (qualitySlider.isEnabled() != slim)
    {
        qualitySlider.setEnabled (slim);
        qualitySlider.setAlpha (slim ? 1.0f : 0.45f);
        repaint (qualityLabelArea);
        repaint (getWidth() - 90, 0, 90, 84);
    }

    // Grey out controls the current routing doesn't use.
    const int routing = (int) processor.apvts.getRawParameterValue (ID::ampRouting)->load();
    const bool bUsed      = routing != 0;
    const bool spreadUsed = routing == 2;
    if (ampBLevelKnob->isEnabled() != bUsed)
        for (juce::Component* comp : { (juce::Component*) ampBLevelKnob,
                                       (juce::Component*) &loadModelBButton,
                                       (juce::Component*) &clearModelBButton,
                                       (juce::Component*) &modelBNameLabel,
                                       (juce::Component*) ampBToggle })
        {
            comp->setEnabled (bUsed);
            comp->setAlpha (bUsed ? 1.0f : 0.5f);
        }
    if (spreadKnob->isEnabled() != spreadUsed)
    {
        spreadKnob->setEnabled (spreadUsed);
        spreadKnob->setAlpha (spreadUsed ? 1.0f : 0.5f);
    }

    // Tuner: detect pitch on the message thread from the captured DI ring.
    if (processor.apvts.getRawParameterValue (ID::tunerActive)->load() > 0.5f)
    {
        const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
        pitchDetector.prepare (sr);
        const int n = processor.readTunerWindow (tunerBuffer.data(), (int) tunerBuffer.size());
        strobeTuner.update (pitchDetector.detect (tunerBuffer.data(), n), 1.0f / 30.0f);
    }
    else
    {
        strobeTuner.update (0.0f, 1.0f / 30.0f);
    }
}

// ===========================================================================
void NecronamAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto w = getWidth();
    g.fillAll (c (COL_BACKGROUND));

    // Header.
    g.setColour (c (COL_HEADER_BG));
    g.fillRect (juce::Rectangle<int> (0, 0, w, 84));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.18f), 0.0f, 84.0f,
                                             juce::Colours::transparentBlack, 0.0f, 97.0f, false));
    g.fillRect (0, 84, w, 13);
    HorrorLookAndFeel::drawBloodDrips (g, juce::Rectangle<float> (0.0f, 72.0f, (float) w, 14.0f));

    g.setColour (c (COL_BLOOD_BRIGHT));
    g.setFont (logoFont (40.0f));
    g.drawText ("NECRONAM MAX", juce::Rectangle<int> (16, 2, w - 220, 46), juce::Justification::centredLeft);
    g.setColour (c (COL_BONE_DIM));
    g.setFont (monoFont (11.0f));
    g.drawText ("[ DUAL NEURAL AMP  //  FRONT + OUTPUT SATURATION  //  STEREO CAB  //  DELAY / REVERB  //  STROBE TUNER ]",
                juce::Rectangle<int> (18, 50, w - 36, 16), juce::Justification::centredLeft);

    // "A2" badge.
    {
        const bool slim = processor.isAnyModelSlimmable();
        auto badge = juce::Rectangle<float> (w - 78.0f, 16.0f, 60.0f, 32.0f);
        g.setColour (slim ? c (COL_BLOOD_DARK) : c (COL_PANEL_BG));
        g.fillRoundedRectangle (badge, 4.0f);
        if (slim) { g.setColour (c (COL_BLOOD_BRIGHT).withAlpha (0.25f)); g.drawRoundedRectangle (badge.expanded (2.0f), 5.0f, 2.0f); }
        g.setColour (slim ? c (COL_BLOOD_BRIGHT) : c (COL_BONE_DIM).withAlpha (0.5f));
        g.drawRoundedRectangle (badge, 4.0f, 1.4f);
        g.setColour (slim ? c (COL_BONE_LIGHT) : c (COL_BONE_DIM).withAlpha (0.5f));
        g.setFont (logoFont (18.0f));
        g.drawText ("A2", badge.withTrimmedBottom (9.0f), juce::Justification::centred);
        g.setFont (monoFont (6.5f));
        g.drawText ("ARCHITECTURE", badge.removeFromBottom (10.0f), juce::Justification::centred);
    }

    auto title = [&] (juce::Rectangle<int> area, const juce::String& t)
    {
        if (area.isEmpty()) return;
        g.setColour (c (COL_BLOOD_BRIGHT));
        g.setFont (monoFont (12.0f));
        g.drawText (t, area.reduced (14, 6).removeFromTop (16), juce::Justification::centredLeft);
    };
    auto drawSatBands = [&] (juce::Rectangle<int> area)
    {
        if (area.isEmpty()) return;
        auto s = area.reduced (14);
        s.removeFromTop (24);
        auto bandRow = s.removeFromTop (16);
        const char* names[3] = { "LOW", "MID", "HIGH" };
        const int cw = bandRow.getWidth() / 3;
        g.setColour (c (COL_BLOOD));
        g.setFont (monoFont (12.0f));
        for (int b = 0; b < 3; ++b) g.drawText (names[b], bandRow.removeFromLeft (cw), juce::Justification::centred);
    };

    // Preset caption (persistent).
    if (! presetArea.isEmpty())
    {
        g.setColour (c (COL_BLOOD_BRIGHT));
        g.setFont (monoFont (12.0f));
        g.drawText ("PRESET", presetArea.reduced (6, 0).removeFromLeft (64), juce::Justification::centredLeft);
    }

    // Master strip (persistent).
    if (! masterArea.isEmpty())
    {
        HorrorLookAndFeel::drawPanelBackground (g, masterArea.toFloat());
        title (masterArea, "MASTER");
        g.setColour (c (COL_BONE_DIM));
        g.setFont (monoFont (8.5f));
        auto lbl = juce::Rectangle<int> (masterArea.getX() + 10, masterArea.getBottom() - 10 - 24 - 13,
                                         masterArea.getWidth() - 20, 12);
        g.drawText ("OUTPUT MODE", lbl, juce::Justification::centred);

        const auto ck = cleanKnob.getBounds();
        if (! ck.isEmpty())
            g.drawText ("CLEAN BLEND", juce::Rectangle<int> (masterArea.getX() + 4, ck.getY() - 13,
                                                             masterArea.getWidth() - 8, 12), juce::Justification::centred);
    }

    // Current-tab panels.
    if (currentTab == TabAmp)
    {
        for (auto* r : { &ampModelsArea, &inputArea, &frontSatArea, &frontFilterArea })
            HorrorLookAndFeel::drawPanelBackground (g, r->toFloat());
        title (ampModelsArea,   "AMPS / MODELS");
        title (inputArea,       "INPUT / OUTPUT");
        title (frontSatArea,    "FRONT SATURATION  -  FLESH RENDER (PRE-AMP)");
        title (frontFilterArea, "FRONT FILTERS");
        drawSatBands (frontSatArea);

        // Routing caption.
        const auto rb = routingBox.getBounds();
        if (! rb.isEmpty())
        {
            g.setColour (c (COL_BONE_DIM));
            g.setFont (monoFont (9.5f));
            g.drawText ("ROUTING", juce::Rectangle<int> (ampModelsArea.getX() + 14, rb.getY(),
                                                         rb.getX() - ampModelsArea.getX() - 18, rb.getHeight()),
                        juce::Justification::centredLeft);
        }

        // Quality hints.
        if (! qualityLabelArea.isEmpty())
        {
            const auto track = qualityLabelArea.withTrimmedRight (96);
            const float dim = processor.isAnyModelSlimmable() ? 1.0f : 0.4f;
            g.setFont (monoFont (9.0f));
            g.setColour (c (COL_BONE_DIM).withAlpha (dim));
            g.drawText ("MAX EFFICIENCY", track, juce::Justification::centredLeft);
            g.drawText ("MAX QUALITY",    track, juce::Justification::centredRight);
            g.setColour (processor.isAnyModelSlimmable() ? c (COL_BLOOD_BRIGHT) : c (COL_BONE_DIM).withAlpha (dim));
            g.drawText (processor.isAnyModelSlimmable() ? "QUALITY (A2)" : "QUALITY (A1 - FIXED)", track, juce::Justification::centred);
        }
    }
    else if (currentTab == TabTone)
    {
        for (auto* r : { &cabArea, &filterArea, &eqArea, &postSatArea })
            HorrorLookAndFeel::drawPanelBackground (g, r->toFloat());
        title (cabArea,     "CAB / DUAL IMPULSE RESPONSE");
        title (filterArea,  "FILTERS");
        title (eqArea,      "GRAPHIC EQ  -  API 560 STYLE");
        title (postSatArea, "OUTPUT SATURATION  -  FLESH RENDER");
        drawSatBands (postSatArea);

        g.setColour (c (COL_BLOOD));
        g.setFont (monoFont (10.0f));
        if (! loadIRAButton.getBounds().isEmpty())
            g.drawText ("CAB A", juce::Rectangle<int> (cabArea.getX() + 14, loadIRAButton.getY() - 14, 120, 12), juce::Justification::centredLeft);
        if (! loadIRBButton.getBounds().isEmpty())
            g.drawText ("CAB B", juce::Rectangle<int> (cabArea.getX() + 14, loadIRBButton.getY() - 14, 120, 12), juce::Justification::centredLeft);
    }
    else if (currentTab == TabFx)
    {
        for (auto* r : { &delayArea, &reverbArea })
            HorrorLookAndFeel::drawPanelBackground (g, r->toFloat());
        title (delayArea,  "DELAY");
        title (reverbArea, "REVERB");
        if (! fxOrderArea.isEmpty())
        {
            auto fo = fxOrderArea;
            g.setColour (c (COL_BLOOD_BRIGHT));
            g.setFont (monoFont (12.0f));
            g.drawText ("SIGNAL ORDER", fo.removeFromLeft (120), juce::Justification::centredLeft);
            g.setColour (c (COL_BONE_DIM));
            g.setFont (monoFont (9.0f));
            g.drawText ("(bypassed modules are fully removed from the chain)",
                        fo.withTrimmedLeft (210), juce::Justification::centredLeft);
        }
    }
    else // TabTuner
    {
        if (! tunerArea.isEmpty())
        {
            g.setColour (c (COL_BLOOD_BRIGHT));
            g.setFont (monoFont (12.0f));
            g.drawText ("STROBE TUNER", tunerArea.reduced (10, 6).removeFromTop (18), juce::Justification::centredLeft);
        }
    }

    // Footer.
    g.setColour (c (COL_BONE_DIM));
    g.setFont (monoFont (10.0f));
    g.drawText ("CP SOFTWARE  -  NECRONAM MAX v1.0  -  NEURAL AMP NECROMANCY",
                juce::Rectangle<int> (0, getHeight() - 26, w, 22), juce::Justification::centred);

    HorrorLookAndFeel::drawGrainTexture (g, getLocalBounds());
}

// ===========================================================================
void NecronamAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (84);
    area.removeFromBottom (28);
    area.reduce (12, 8);

    // ---- Preset bar (persistent) ----
    presetArea = area.removeFromTop (30);
    {
        auto pb = presetArea.reduced (4, 2);
        pb.removeFromLeft (66);
        presetPrevButton.setBounds (pb.removeFromLeft (30));
        pb.removeFromLeft (4);
        presetSaveButton.setBounds (pb.removeFromRight (90));
        pb.removeFromRight (8);
        presetNextButton.setBounds (pb.removeFromRight (30));
        pb.removeFromRight (4);
        presetBox.setBounds (pb);
    }
    area.removeFromTop (8);

    // ---- Tab bar (persistent) ----
    tabBarArea = area.removeFromTop (30);
    {
        auto tb = tabBarArea;
        const int bw = 110;
        tabAmpButton.setBounds   (tb.removeFromLeft (bw)); tb.removeFromLeft (4);
        tabToneButton.setBounds  (tb.removeFromLeft (bw)); tb.removeFromLeft (4);
        tabFxButton.setBounds    (tb.removeFromLeft (bw)); tb.removeFromLeft (4);
        tabTunerButton.setBounds (tb.removeFromLeft (bw));
    }
    area.removeFromTop (10);

    // ---- Master strip (persistent, full body height) ----
    masterArea = area.removeFromRight (112);
    area.removeFromRight (12);
    {
        auto m = masterArea.reduced (10);
        m.removeFromTop (20);
        outputModeBox.setBounds (m.removeFromBottom (24));
        m.removeFromBottom (14);              // OUTPUT MODE caption
        m.removeFromBottom (10);
        auto cleanArea = m.removeFromBottom (74);
        cleanKnob.setBounds (cleanArea.reduced (10, 0).removeFromBottom (66));
        m.removeFromBottom (14);              // CLEAN BLEND caption (painted)
        masterMeter.setBounds (m.removeFromLeft (28));
        m.removeFromLeft (8);
        masterFader.setBounds (m);
    }

    bodyArea = area;

    auto layoutSat = [] (juce::Rectangle<int> a, const std::array<juce::Slider*, 9>& knobs, juce::TextButton& toggle)
    {
        auto s = a.reduced (14);
        toggle.setBounds (s.removeFromTop (24).removeFromRight (90));
        s.removeFromTop (16);   // band names (painted)
        s.removeFromTop (20);   // gap
        const int cw = s.getWidth() / 3;
        for (int b = 0; b < 3; ++b)
        {
            auto col = s.removeFromLeft (cw);
            const int kw = col.getWidth() / 3;
            for (int st = 0; st < 3; ++st)
                knobs[(size_t) (b * 3 + st)]->setBounds (col.removeFromLeft (kw).reduced (6));
        }
    };

    // ===== AMP tab =====
    {
        // Top-to-bottom in signal order: front sat + front filters, then amps + I/O.
        auto a = bodyArea;
        auto rowTop = a.removeFromTop (170);
        frontSatArea = rowTop.removeFromLeft (740);
        rowTop.removeFromLeft (14);
        frontFilterArea = rowTop;
        a.removeFromTop (12);
        auto rowBot = a.removeFromTop (280);
        ampModelsArea = rowBot.removeFromLeft (580);
        rowBot.removeFromLeft (14);
        inputArea = rowBot;

        {
            auto m = ampModelsArea.reduced (14);
            m.removeFromTop (22);
            auto rowA = m.removeFromTop (26);
            loadModelAButton.setBounds (rowA.removeFromLeft (100)); rowA.removeFromLeft (5);
            prevAButton.setBounds (rowA.removeFromLeft (22)); rowA.removeFromLeft (2);
            nextAButton.setBounds (rowA.removeFromLeft (22)); rowA.removeFromLeft (6);
            clearModelAButton.setBounds (rowA.removeFromRight (24)); rowA.removeFromRight (5);
            ampAToggle->setBounds (rowA.removeFromRight (40)); rowA.removeFromRight (5);
            modelANameLabel.setBounds (rowA);
            m.removeFromTop (6);
            auto rowB = m.removeFromTop (26);
            loadModelBButton.setBounds (rowB.removeFromLeft (100)); rowB.removeFromLeft (5);
            prevBButton.setBounds (rowB.removeFromLeft (22)); rowB.removeFromLeft (2);
            nextBButton.setBounds (rowB.removeFromLeft (22)); rowB.removeFromLeft (6);
            clearModelBButton.setBounds (rowB.removeFromRight (24)); rowB.removeFromRight (5);
            ampBToggle->setBounds (rowB.removeFromRight (40)); rowB.removeFromRight (5);
            modelBNameLabel.setBounds (rowB);
            m.removeFromTop (8);
            auto routingRow = m.removeFromTop (26);
            routingRow.removeFromLeft (74);
            routingBox.setBounds (routingRow.removeFromLeft (170));
            m.removeFromTop (16);
            auto knobRow = m.removeFromTop (80);
            const int kw = knobRow.getWidth() / 3;
            spreadKnob->setBounds    (knobRow.removeFromLeft (kw).reduced (6, 2));
            ampALevelKnob->setBounds (knobRow.removeFromLeft (kw).reduced (6, 2));
            ampBLevelKnob->setBounds (knobRow.reduced (6, 2));
            m.removeFromTop (6);
            qualityLabelArea = m.removeFromTop (14);
            qualitySlider.setBounds (m.removeFromTop (22));
        }
        {
            auto ip = inputArea.reduced (14);
            ip.removeFromTop (22);
            auto meterStrip = ip.removeFromRight (60);
            meterStrip.removeFromTop (4);
            inMeter.setBounds (meterStrip.removeFromLeft (26));
            meterStrip.removeFromLeft (8);
            namOutMeter.setBounds (meterStrip);
            ip.removeFromRight (14);
            ip.removeFromTop (16);
            auto knobRow = ip.removeFromTop (86);
            const int kw = knobRow.getWidth() / 4;
            inputKnob->setBounds    (knobRow.removeFromLeft (kw).reduced (5, 2));
            gateKnob->setBounds     (knobRow.removeFromLeft (kw).reduced (5, 2));
            ampOutKnob->setBounds   (knobRow.removeFromLeft (kw).reduced (5, 2));
            inputCalKnob->setBounds (knobRow.reduced (5, 2));
            ip.removeFromTop (10);
            gateToggle->setBounds (ip.removeFromTop (26).removeFromLeft (100));
        }
        layoutSat (frontSatArea, frontSatKnobs, *frontSatToggle);

        {
            auto fa = frontFilterArea.reduced (14);
            fa.removeFromTop (22);
            const int half = fa.getWidth() / 2;
            auto left = fa.removeFromLeft (half);
            auto right = fa;
            left.removeFromTop (18);
            frontHpfKnob->setBounds (left.removeFromTop (84).reduced (20, 2));
            frontHpfToggle->setBounds (left.removeFromTop (24).reduced (22, 2));
            right.removeFromTop (18);
            frontLpfKnob->setBounds (right.removeFromTop (84).reduced (20, 2));
            frontLpfToggle->setBounds (right.removeFromTop (24).reduced (22, 2));
        }
    }

    // ===== TONE tab =====
    {
        auto a = bodyArea;
        auto row2 = a.removeFromTop (165);
        cabArea = row2.removeFromLeft (580);
        row2.removeFromLeft (14);
        filterArea = row2;
        a.removeFromTop (12);
        eqArea = a.removeFromTop (190);
        a.removeFromTop (12);
        postSatArea = a.removeFromTop (170);

        {
            auto cb = cabArea.reduced (14);
            cb.removeFromTop (20);
            auto irBlock = [] (juce::Rectangle<int> block, juce::TextButton& load, juce::TextButton& clear,
                               juce::TextButton& on, juce::Label& name, juce::Slider& level)
            {
                block.removeFromTop (14);   // CAB A/B caption (painted)
                auto top = block.removeFromTop (26);
                load.setBounds (top.removeFromLeft (120)); top.removeFromLeft (6);
                clear.setBounds (top.removeFromLeft (26)); top.removeFromLeft (8);
                on.setBounds (top.removeFromLeft (54));    top.removeFromLeft (8);
                name.setBounds (top);
                block.removeFromTop (4);
                level.setBounds (block.removeFromTop (22));
            };
            const int blockH = cb.getHeight() / 2;
            irBlock (cb.removeFromTop (blockH), loadIRAButton, clearIRAButton, *cabAToggle, irANameLabel, *cabALevelKnob);
            irBlock (cb, loadIRBButton, clearIRBButton, *cabBToggle, irBNameLabel, *cabBLevelKnob);
        }
        {
            auto fa = filterArea.reduced (14);
            fa.removeFromTop (22);
            const int half = fa.getWidth() / 2;
            auto left = fa.removeFromLeft (half);
            auto right = fa;
            left.removeFromTop (18);
            hpfKnob->setBounds (left.removeFromTop (84).reduced (24, 2));
            hpfToggle->setBounds (left.removeFromTop (24).reduced (34, 2));
            right.removeFromTop (18);
            lpfKnob->setBounds (right.removeFromTop (84).reduced (24, 2));
            lpfToggle->setBounds (right.removeFromTop (24).reduced (34, 2));
        }
        {
            auto e = eqArea.reduced (14);
            auto etop = e.removeFromTop (24);
            eqToggle->setBounds (etop.removeFromRight (90));
            e.removeFromTop (16);
            const int n = Api560EQ::kNumBands;
            const int sw = e.getWidth() / n;
            for (int i = 0; i < n; ++i)
                eqSliders[(size_t) i]->setBounds (e.removeFromLeft (sw).reduced (6, 2));
        }
        layoutSat (postSatArea, satKnobs, *satToggle);
    }

    // ===== FX tab =====
    {
        auto a = bodyArea;
        auto row = a.removeFromTop (210);
        const int half = (row.getWidth() - 14) / 2;
        delayArea = row.removeFromLeft (half);
        row.removeFromLeft (14);
        reverbArea = row;
        a.removeFromTop (14);
        fxOrderArea = a.removeFromTop (30);

        auto fxPanel = [] (juce::Rectangle<int> panel, juce::TextButton& toggle,
                           juce::Slider& k1, juce::Slider& k2, juce::Slider& k3)
        {
            auto d = panel.reduced (14);
            toggle.setBounds (d.removeFromTop (24).removeFromRight (96));
            d.removeFromTop (18);
            auto knobRow = d.removeFromTop (96);
            const int kw = knobRow.getWidth() / 3;
            k1.setBounds (knobRow.removeFromLeft (kw).reduced (8, 2));
            k2.setBounds (knobRow.removeFromLeft (kw).reduced (8, 2));
            k3.setBounds (knobRow.reduced (8, 2));
        };
        fxPanel (delayArea,  *delayToggle,  *delayTimeKnob,  *delayFbKnob,   *delayMixKnob);
        fxPanel (reverbArea, *reverbToggle, *reverbSizeKnob, *reverbDampKnob, *reverbMixKnob);

        auto o = fxOrderArea;
        o.removeFromLeft (120);
        fxOrderBox.setBounds (o.removeFromLeft (200).reduced (0, 2));
    }

    // ===== TUNER tab =====
    {
        tunerArea = bodyArea;
        auto t = tunerArea.reduced (10);
        t.removeFromTop (24);   // title (painted)
        tunerToggle->setBounds (t.removeFromTop (36).withSizeKeepingCentre (220, 30));
        t.removeFromTop (10);
        strobeTuner.setBounds (t);
    }
}
