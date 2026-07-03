#include "PluginEditor.h"
#include "BinaryData.h"

using namespace horror;
using ID = NecronamAudioProcessor::ParamID;

namespace
{
    // All editor-painted text runs ~15% larger for legibility (v2.1).
    juce::Font monoFont (float h, bool bold = true)
    {
        return HorrorLookAndFeel::monoFont (h * 1.15f, bold);
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
    tunerBuffer.resize (16384, 0.0f);

    // ---- Preset bar ----
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

    // ---- Tab bar ----
    struct TabDef { juce::TextButton* btn; int tab; };
    for (auto& t : { TabDef { &tabPreButton, TabPre },     TabDef { &tabAmpCabButton, TabAmpCab },
                     TabDef { &tabPostButton, TabPost },   TabDef { &tabFxButton, TabFx },
                     TabDef { &tabTunerButton, TabTuner } })
    {
        addAndMakeVisible (*t.btn);
        t.btn->setClickingTogglesState (false);
        const int tab = t.tab;
        t.btn->onClick = [this, tab] { showTab (tab); };
    }

    // Solo Amp/Cab switch — lives in the tab bar, lights up vivid red when on.
    soloButton.setClickingTogglesState (true);
    soloButton.getProperties().set ("bright", true);
    addAndMakeVisible (soloButton);
    soloAttach = std::make_unique<ButtonAttach> (processor.apvts, ID::soloAmpCab, soloButton);

    // =====================================================================
    // MASTER STRIP (persistent — untagged so every tab shows it)
    // =====================================================================
    inputKnob = &addKnob (-1, ID::inputLevel, "INPUT");

    masterFader.setSliderStyle (juce::Slider::LinearVertical);
    masterFader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 17);
    masterFader.setColour (juce::Slider::textBoxTextColourId, c (COL_BONE));
    addAndMakeVisible (masterFader);
    sliderAttachments.push_back (std::make_unique<SliderAttach> (processor.apvts, ID::outputLevel, masterFader));

    cleanKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    cleanKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    cleanKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 16);
    cleanKnob.setColour (juce::Slider::textBoxTextColourId, c (COL_BONE));
    addAndMakeVisible (cleanKnob);
    sliderAttachments.push_back (std::make_unique<SliderAttach> (processor.apvts, ID::cleanBlend, cleanKnob));

    addAndMakeVisible (outputModeBox);
    outputModeBox.addItemList ({ "Raw", "Normalized", "Calibrated" }, 1);
    outputModeAttach = std::make_unique<ComboAttach> (processor.apvts, ID::outputMode, outputModeBox);

    inMeter.caption     = "IN";
    namOutMeter.caption = "OUT";
    masterMeter.caption = "OUT";
    addAndMakeVisible (inMeter);                       // master strip (persistent)
    addAndMakeVisible (masterMeter);
    addAndMakeVisible (namOutMeter); assignTab (namOutMeter, TabAmpCab);

    // =====================================================================
    // PRE tab — gate, Flesh Render pre, drive, low cut
    // =====================================================================
    gateKnob   = &addKnob (TabPre, ID::gateThresh, "GATE THR");
    gateToggle = &addToggle (TabPre, ID::gateActive, "On");
    addAndMakeVisible (gatePosBox);
    assignTab (gatePosBox, TabPre);
    gatePosBox.addItemList ({ "Pre Amp", "Post Amp" }, 1);
    gatePosAttach = std::make_unique<ComboAttach> (processor.apvts, ID::gatePosition, gatePosBox);

    frontSatToggle = &addToggle (TabPre, ID::frontSatActive, "On");
    frontXLowKnob  = &addKnob (TabPre, ID::frontSatXLow,  "X-LOW");
    frontXHighKnob = &addKnob (TabPre, ID::frontSatXHigh, "X-HIGH");

    driveToggle = &addToggle (TabPre, ID::driveActive, "On");
    addAndMakeVisible (driveCircuitBox);
    assignTab (driveCircuitBox, TabPre);
    driveCircuitBox.addItemList ({ "TC Preamp", "Tube Screamer" }, 1);
    driveCircuitAttach = std::make_unique<ComboAttach> (processor.apvts, ID::driveCircuit, driveCircuitBox);

    auto tagCircuit = [this] (juce::Slider& s, int circ)
    {
        s.getProperties().set ("circuit", circ);
        if (! labels.empty())
            labels.back()->getProperties().set ("circuit", circ);
        (circ == 0 ? tcComps : tsComps).push_back (&s);
        if (! labels.empty())
            (circ == 0 ? tcComps : tsComps).push_back (labels.back().get());
    };
    tcGainKnob   = &addKnob (TabPre, ID::tcGain,   "GAIN");   tagCircuit (*tcGainKnob, 0);
    tcBassKnob   = &addKnob (TabPre, ID::tcBass,   "BASS");   tagCircuit (*tcBassKnob, 0);
    tcMidKnob    = &addKnob (TabPre, ID::tcMid,    "MID");    tagCircuit (*tcMidKnob, 0);
    tcTrebleKnob = &addKnob (TabPre, ID::tcTreble, "TREBLE"); tagCircuit (*tcTrebleKnob, 0);
    tcLevelKnob  = &addKnob (TabPre, ID::tcLevel,  "LEVEL");  tagCircuit (*tcLevelKnob, 0);
    tsDriveKnob  = &addKnob (TabPre, ID::tsDrive,  "DRIVE");  tagCircuit (*tsDriveKnob, 1);
    tsToneKnob   = &addKnob (TabPre, ID::tsTone,   "TONE");   tagCircuit (*tsToneKnob, 1);
    tsLevelKnob  = &addKnob (TabPre, ID::tsLevel,  "LEVEL");  tagCircuit (*tsLevelKnob, 1);

    lowCutKnob   = &addKnob (TabPre, ID::lowCutFreq, "LOW CUT");
    lowCutToggle = &addToggle (TabPre, ID::lowCutActive, "On");

    // Front FR stage knobs (SAT / DRIVE / FUZZ per band).
    const char* bandIds[3]  = { "low", "mid", "high" };
    const char* stageIds[3] = { "sat", "dist", "fuzz" };
    const char* stageNm[3]  = { "SAT", "DRIVE", "FUZZ" };
    for (int b = 0; b < 3; ++b)
        for (int s = 0; s < 3; ++s)
            frontSatKnobs[(size_t) (b * 3 + s)] =
                &addKnob (TabPre, NecronamAudioProcessor::satParamID (true, bandIds[b], stageIds[s]), stageNm[s]);

    // =====================================================================
    // AMP / CAB tab
    // =====================================================================
    addAndMakeVisible (inputModeBox);
    assignTab (inputModeBox, TabAmpCab);
    inputModeBox.addItemList ({ "Mono", "Stereo (Dual Mono)" }, 1);
    inputModeAttach = std::make_unique<ComboAttach> (processor.apvts, ID::inputMode, inputModeBox);

    for (auto* b : { &loadModelAButton, &clearModelAButton, &loadModelBButton, &clearModelBButton,
                     &prevAButton, &nextAButton, &prevBButton, &nextBButton })
    {
        addAndMakeVisible (*b);
        assignTab (*b, TabAmpCab);
    }
    loadModelAButton.onClick  = [this] { chooseModel (0); };
    clearModelAButton.onClick = [this] { processor.clearNamModel (0); };
    loadModelBButton.onClick  = [this] { chooseModel (1); };
    clearModelBButton.onClick = [this] { processor.clearNamModel (1); };
    prevAButton.onClick = [this] { stepModel (0, -1); };
    nextAButton.onClick = [this] { stepModel (0,  1); };
    prevBButton.onClick = [this] { stepModel (1, -1); };
    nextBButton.onClick = [this] { stepModel (1,  1); };

    for (auto* l : { &modelANameLabel, &modelBNameLabel })
    {
        l->setFont (monoFont (11.0f, false));
        l->setColour (juce::Label::textColourId, c (COL_BONE));
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
        assignTab (*l, TabAmpCab);
    }

    addAndMakeVisible (routingBox);
    assignTab (routingBox, TabAmpCab);
    routingBox.addItemList ({ "Single", "Series", "Parallel" }, 1);
    routingAttach = std::make_unique<ComboAttach> (processor.apvts, ID::ampRouting, routingBox);

    spreadKnob    = &addKnob (TabAmpCab, ID::ampSpread,  "SPREAD");
    ampALevelKnob = &addKnob (TabAmpCab, ID::ampALevel,  "AMP A");
    ampBLevelKnob = &addKnob (TabAmpCab, ID::ampBLevel,  "AMP B");
    inputCalKnob  = &addKnob (TabAmpCab, ID::inputCal,   "IN CAL");
    ampOutKnob    = &addKnob (TabAmpCab, ID::namOutput,  "AMP OUT");
    ampAToggle    = &addToggle (TabAmpCab, ID::ampAActive, "On");
    ampBToggle    = &addToggle (TabAmpCab, ID::ampBActive, "On");
    // Mute = kill switch (lights vivid red like the solo button).
    ampAMuteToggle = &addToggle (TabAmpCab, ID::ampAMute, "M");
    ampBMuteToggle = &addToggle (TabAmpCab, ID::ampBMute, "M");
    ampAMuteToggle->getProperties().set ("bright", true);
    ampBMuteToggle->getProperties().set ("bright", true);

    qualitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    qualitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 104, 18);
    qualitySlider.setColour (juce::Slider::textBoxTextColourId, c (COL_BONE));
    addAndMakeVisible (qualitySlider);
    assignTab (qualitySlider, TabAmpCab);
    sliderAttachments.push_back (std::make_unique<SliderAttach> (processor.apvts, ID::quality, qualitySlider));

    sagKnob = &addKnob (TabAmpCab, ID::sagAmount, "SAG");

    for (auto* b : { &loadIRAButton, &clearIRAButton, &loadIRBButton, &clearIRBButton })
    {
        addAndMakeVisible (*b);
        assignTab (*b, TabAmpCab);
    }
    loadIRAButton.onClick  = [this] { chooseIR (0); };
    clearIRAButton.onClick = [this] { processor.clearImpulseResponse (0); };
    loadIRBButton.onClick  = [this] { chooseIR (1); };
    clearIRBButton.onClick = [this] { processor.clearImpulseResponse (1); };

    for (auto* l : { &irANameLabel, &irBNameLabel })
    {
        l->setFont (monoFont (11.0f, false));
        l->setColour (juce::Label::textColourId, c (COL_BONE));
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
        assignTab (*l, TabAmpCab);
    }
    cabAToggle    = &addToggle (TabAmpCab, ID::cabAActive, "On");
    cabBToggle    = &addToggle (TabAmpCab, ID::cabBActive, "On");
    cabAMuteToggle = &addToggle (TabAmpCab, ID::cabAMute, "M");
    cabBMuteToggle = &addToggle (TabAmpCab, ID::cabBMute, "M");
    cabAMuteToggle->getProperties().set ("bright", true);
    cabBMuteToggle->getProperties().set ("bright", true);
    cabALevelKnob = &addHSlider (TabAmpCab, ID::cabALevel);
    cabBLevelKnob = &addHSlider (TabAmpCab, ID::cabBLevel);

    // =====================================================================
    // POST tab — EQ, compressor, Flesh Render post, filters
    // =====================================================================
    eqToggle = &addToggle (TabPost, ID::eqActive, "EQ On");
    for (int i = 0; i < Api560EQ::kNumBands; ++i)
    {
        const float f = Api560EQ::kFrequencies[(size_t) i];
        const juce::String name = f >= 1000.0f ? juce::String (f / 1000.0f, (f == 16000.0f ? 0 : 1)) + "k"
                                               : juce::String (juce::roundToInt (f));
        eqSliders[(size_t) i] = &addVSlider (TabPost, NecronamAudioProcessor::eqParamID (i), name);
    }

    compToggle   = &addToggle (TabPost, ID::compActive, "On");
    compKnob     = &addKnob (TabPost, ID::compAmount, "PEAK REDUCTION");
    compGainKnob = &addKnob (TabPost, ID::compGain,   "GAIN");
    addAndMakeVisible (grMeter);
    assignTab (grMeter, TabPost);

    satToggle   = &addToggle (TabPost, ID::satActive, "On");
    satXLowKnob  = &addKnob (TabPost, ID::satXLow,  "X-LOW");
    satXHighKnob = &addKnob (TabPost, ID::satXHigh, "X-HIGH");
    for (int b = 0; b < 3; ++b)
        for (int s = 0; s < 3; ++s)
            satKnobs[(size_t) (b * 3 + s)] =
                &addKnob (TabPost, NecronamAudioProcessor::satParamID (false, bandIds[b], stageIds[s]), stageNm[s]);

    hpfKnob   = &addKnob (TabPost, ID::hpfFreq, "HI-PASS");
    lpfKnob   = &addKnob (TabPost, ID::lpfFreq, "LOW-PASS");
    hpfToggle = &addToggle (TabPost, ID::hpfActive, "On");
    lpfToggle = &addToggle (TabPost, ID::lpfActive, "On");

    // =====================================================================
    // FX tab
    // =====================================================================
    delayTimeKnob  = &addKnob (TabFx, ID::delayTime,     "TIME");
    delayFbKnob    = &addKnob (TabFx, ID::delayFeedback, "FEEDBACK");
    delayMixKnob   = &addKnob (TabFx, ID::delayMix,      "MIX");
    reverbSizeKnob = &addKnob (TabFx, ID::reverbSize,    "SIZE");
    reverbDampKnob = &addKnob (TabFx, ID::reverbDamp,    "DAMP");
    reverbMixKnob  = &addKnob (TabFx, ID::reverbMix,     "MIX");
    delayToggle    = &addToggle (TabFx, ID::delayActive,  "Delay On");
    reverbToggle   = &addToggle (TabFx, ID::reverbActive, "Reverb On");

    addAndMakeVisible (reverbTypeBox);
    assignTab (reverbTypeBox, TabFx);
    reverbTypeBox.addItemList ({ "Plate", "Spring" }, 1);
    reverbTypeAttach = std::make_unique<ComboAttach> (processor.apvts, ID::reverbType, reverbTypeBox);

    addAndMakeVisible (fxOrderBox);
    assignTab (fxOrderBox, TabFx);
    fxOrderBox.addItemList ({ "Delay -> Reverb", "Reverb -> Delay" }, 1);
    fxOrderAttach = std::make_unique<ComboAttach> (processor.apvts, ID::fxOrder, fxOrderBox);

    // =====================================================================
    // TUNER tab
    // =====================================================================
    tunerToggle = &addToggle (TabTuner, ID::tunerActive, "TUNER  (mutes output)");
    addAndMakeVisible (strobeTuner);
    assignTab (strobeTuner, TabTuner);

    startTimerHz (30);
    setSize (1140, 800);
    showTab (TabAmpCab);          // always open on AMP / CAB
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
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 17);
    s->setColour (juce::Slider::textBoxTextColourId, c (COL_BONE));
    addAndMakeVisible (*s);
    if (tab >= 0) assignTab (*s, tab);

    auto lab = std::make_unique<juce::Label> (juce::String(), labelText);
    lab->setJustificationType (juce::Justification::centred);
    lab->setFont (monoFont (10.0f));
    lab->setColour (juce::Label::textColourId, c (COL_BONE));
    addAndMakeVisible (*lab);
    lab->attachToComponent (s.get(), false);
    if (tab >= 0) assignTab (*lab, tab);

    sliderAttachments.push_back (std::make_unique<SliderAttach> (processor.apvts, paramID, *s));

    auto& ref = *s;
    sliders.push_back (std::move (s));
    labels.push_back (std::move (lab));
    return ref;
}

juce::Slider& NecronamAudioProcessorEditor::addVSlider (int tab, const juce::String& paramID, const juce::String& labelText)
{
    auto s = std::make_unique<juce::Slider> (juce::Slider::LinearVertical, juce::Slider::TextBoxBelow);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
    s->setColour (juce::Slider::textBoxTextColourId, c (COL_BONE));
    addAndMakeVisible (*s);
    if (tab >= 0) assignTab (*s, tab);

    auto lab = std::make_unique<juce::Label> (juce::String(), labelText);
    lab->setJustificationType (juce::Justification::centred);
    lab->setFont (monoFont (9.5f));
    lab->setColour (juce::Label::textColourId, c (COL_BONE));
    addAndMakeVisible (*lab);
    lab->attachToComponent (s.get(), false);
    if (tab >= 0) assignTab (*lab, tab);

    sliderAttachments.push_back (std::make_unique<SliderAttach> (processor.apvts, paramID, *s));

    auto& ref = *s;
    sliders.push_back (std::move (s));
    labels.push_back (std::move (lab));
    return ref;
}

juce::Slider& NecronamAudioProcessorEditor::addHSlider (int tab, const juce::String& paramID)
{
    auto s = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 68, 18);
    s->setColour (juce::Slider::textBoxTextColourId, c (COL_BONE));
    addAndMakeVisible (*s);
    if (tab >= 0) assignTab (*s, tab);
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
    if (tab >= 0) assignTab (*b, tab);
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
        return;

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
    const int circuit = (int) processor.apvts.getRawParameterValue (ID::driveCircuit)->load();

    for (auto* ch : getChildren())
    {
        const auto& props = ch->getProperties();
        if (! props.contains ("tab"))
            continue;
        bool vis = (int) props["tab"] == currentTab;
        if (vis && props.contains ("circuit"))
            vis = (int) props["circuit"] == circuit;
        ch->setVisible (vis);
    }
    lastDriveCircuit = circuit;

    tabPreButton.setToggleState    (tab == TabPre,    juce::dontSendNotification);
    tabAmpCabButton.setToggleState (tab == TabAmpCab, juce::dontSendNotification);
    tabPostButton.setToggleState   (tab == TabPost,   juce::dontSendNotification);
    tabFxButton.setToggleState     (tab == TabFx,     juce::dontSendNotification);
    tabTunerButton.setToggleState  (tab == TabTuner,  juce::dontSendNotification);
    repaint();
}

void NecronamAudioProcessorEditor::refreshDriveVisibility()
{
    const int circuit = (int) processor.apvts.getRawParameterValue (ID::driveCircuit)->load();
    if (circuit == lastDriveCircuit)
        return;
    showTab (currentTab);
}

// ===========================================================================
void NecronamAudioProcessorEditor::timerCallback()
{
    inMeter.update     (processor.fetchInputPeak());
    namOutMeter.update (processor.fetchNamPeak());
    masterMeter.update (processor.fetchMasterPeak());
    grMeter.update     (processor.fetchGainReductionDb());

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

    // Grey out what the current input mode / routing doesn't use.
    const bool stereoIn = ((int) processor.apvts.getRawParameterValue (ID::inputMode)->load()) == 1;
    const int  routing  = (int) processor.apvts.getRawParameterValue (ID::ampRouting)->load();
    const bool bUsed      = stereoIn || routing != 0;
    const bool spreadUsed = ! stereoIn && routing == 2;
    const bool routingUsed = ! stereoIn;

    auto setEn = [] (juce::Component& comp, bool en)
    {
        if (comp.isEnabled() != en)
        {
            comp.setEnabled (en);
            comp.setAlpha (en ? 1.0f : 0.5f);
        }
    };
    for (juce::Component* comp : { (juce::Component*) ampBLevelKnob, (juce::Component*) &loadModelBButton,
                                   (juce::Component*) &clearModelBButton, (juce::Component*) &modelBNameLabel,
                                   (juce::Component*) ampBToggle, (juce::Component*) ampBMuteToggle,
                                   (juce::Component*) &prevBButton, (juce::Component*) &nextBButton })
        setEn (*comp, bUsed);
    setEn (*spreadKnob, spreadUsed);
    setEn (routingBox, routingUsed);

    // Drive circuit switch (from automation or the combo).
    refreshDriveVisibility();

    // Solo Amp/Cab: grey out the bypassed tabs and jump to AMP/CAB on engage.
    const bool solo = processor.apvts.getRawParameterValue (ID::soloAmpCab)->load() > 0.5f;
    if (solo != lastSolo)
    {
        lastSolo = solo;
        for (auto* tb : { &tabPreButton, &tabPostButton, &tabFxButton, &tabTunerButton })
        {
            tb->setEnabled (! solo);
            tb->setAlpha (solo ? 0.35f : 1.0f);
        }
        if (solo)
            showTab (TabAmpCab);
    }

    // Tuner.
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
    g.setColour (c (COL_BONE));
    g.setFont (monoFont (11.0f));
    g.drawText ("[ DUAL NEURAL AMP  //  DRIVE + SAG + LA-2A  //  STEREO DUAL MONO  //  PLATE / SPRING  //  STROBE TUNER ]",
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
    auto caption = [&] (juce::Rectangle<int> area, const juce::String& t)
    {
        g.setColour (c (COL_BONE));
        g.setFont (monoFont (9.0f));
        g.drawText (t, area, juce::Justification::centred);
    };
    // Band captions over a Flesh Render grid (gridW = width of the 3x3 area).
    auto drawSatBands = [&] (juce::Rectangle<int> area, int gridW)
    {
        if (area.isEmpty()) return;
        auto s = area.reduced (14);
        s.removeFromTop (24);
        auto bandRow = s.removeFromTop (16).removeFromLeft (gridW);
        const char* names[3] = { "LOW", "MID", "HIGH" };
        const int cw = bandRow.getWidth() / 3;
        g.setColour (c (COL_BLOOD));
        g.setFont (monoFont (12.0f));
        for (int b = 0; b < 3; ++b) g.drawText (names[b], bandRow.removeFromLeft (cw), juce::Justification::centred);
    };

    // Preset caption.
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
        const auto ck = cleanKnob.getBounds();
        if (! ck.isEmpty())
            caption (juce::Rectangle<int> (masterArea.getX() + 4, ck.getY() - 13, masterArea.getWidth() - 8, 12),
                     "CLEAN BLEND");
        const auto ob = outputModeBox.getBounds();
        if (! ob.isEmpty())
            caption (juce::Rectangle<int> (masterArea.getX() + 4, ob.getY() - 13, masterArea.getWidth() - 8, 12),
                     "OUTPUT MODE");
    }

    if (currentTab == TabPre)
    {
        for (auto* r : { &frontSatArea, &driveArea, &gateArea, &lowCutArea })
            HorrorLookAndFeel::drawPanelBackground (g, r->toFloat());
        title (frontSatArea, "FLESH RENDER PRE  -  MULTIBAND SATURATION");
        title (driveArea,    "DRIVE");
        title (gateArea,     "GATE");
        title (lowCutArea,   "LOW CUT");
        drawSatBands (frontSatArea, frontSatArea.getWidth() - 28 - 220);

        const auto db = driveCircuitBox.getBounds();
        if (! db.isEmpty())
            caption (juce::Rectangle<int> (driveArea.getX() + 14, db.getY() - 13, 160, 12), "CIRCUIT");
        const auto gp = gatePosBox.getBounds();
        if (! gp.isEmpty())
            caption (juce::Rectangle<int> (gateArea.getX() + 14, gp.getY() - 13, 160, 12), "POSITION");
    }
    else if (currentTab == TabAmpCab)
    {
        for (auto* r : { &ampModelsArea, &sagArea, &cabArea })
            HorrorLookAndFeel::drawPanelBackground (g, r->toFloat());
        title (ampModelsArea, "AMPS / MODELS");
        title (sagArea,       "SAG  -  POWER AMP");
        title (cabArea,       "CAB / DUAL IMPULSE RESPONSE");

        const auto rb = routingBox.getBounds();
        if (! rb.isEmpty())
            caption (juce::Rectangle<int> (rb.getX(), rb.getY() - 13, rb.getWidth(), 12), "ROUTING");
        const auto im = inputModeBox.getBounds();
        if (! im.isEmpty())
            caption (juce::Rectangle<int> (im.getX(), im.getY() - 13, im.getWidth(), 12), "INPUT MODE");

        g.setColour (c (COL_BLOOD));
        g.setFont (monoFont (10.0f));
        if (! loadIRAButton.getBounds().isEmpty())
            g.drawText ("CAB A", juce::Rectangle<int> (loadIRAButton.getX(), loadIRAButton.getY() - 14, 120, 12),
                        juce::Justification::centredLeft);
        if (! loadIRBButton.getBounds().isEmpty())
            g.drawText ("CAB B", juce::Rectangle<int> (loadIRBButton.getX(), loadIRBButton.getY() - 14, 120, 12),
                        juce::Justification::centredLeft);

        // Quality hints.
        if (! qualityLabelArea.isEmpty())
        {
            const auto track = qualityLabelArea.withTrimmedRight (96);
            const bool slim = processor.isAnyModelSlimmable();
            const float dim = slim ? 1.0f : 0.4f;
            g.setFont (monoFont (9.0f));
            g.setColour (c (COL_BONE).withAlpha (dim));
            g.drawText ("MAX EFFICIENCY", track, juce::Justification::centredLeft);
            g.drawText ("MAX QUALITY",    track, juce::Justification::centredRight);
            g.setColour (slim ? c (COL_BLOOD_BRIGHT) : c (COL_BONE).withAlpha (dim));
            g.drawText (slim ? "QUALITY (A2)" : "QUALITY (A1 - FIXED)", track, juce::Justification::centred);
        }
    }
    else if (currentTab == TabPost)
    {
        for (auto* r : { &eqArea, &compArea, &postSatArea, &filterArea })
            HorrorLookAndFeel::drawPanelBackground (g, r->toFloat());
        title (eqArea,      "GRAPHIC EQ  -  API 560 STYLE");
        title (compArea,    "COMPRESSOR  -  LA-2A STYLE");
        title (postSatArea, "FLESH RENDER POST  -  MULTIBAND SATURATION");
        title (filterArea,  "FILTERS");
        drawSatBands (postSatArea, postSatArea.getWidth() - 28 - 220);
    }
    else if (currentTab == TabFx)
    {
        for (auto* r : { &delayArea, &reverbArea })
            HorrorLookAndFeel::drawPanelBackground (g, r->toFloat());
        title (delayArea,  "DELAY");
        title (reverbArea, "REVERB");
        const auto rt = reverbTypeBox.getBounds();
        if (! rt.isEmpty())
            caption (juce::Rectangle<int> (rt.getX(), rt.getY() - 13, rt.getWidth(), 12), "TYPE");
        if (! fxOrderArea.isEmpty())
        {
            auto fo = fxOrderArea;
            g.setColour (c (COL_BLOOD_BRIGHT));
            g.setFont (monoFont (12.0f));
            g.drawText ("SIGNAL ORDER", fo.removeFromLeft (120), juce::Justification::centredLeft);
            g.setColour (c (COL_BONE));
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
    g.setColour (c (COL_BONE));
    g.setFont (monoFont (10.0f));
    g.drawText ("CP SOFTWARE  -  NECRONAM MAX v2.0  -  NEURAL AMP NECROMANCY",
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

    // ---- Preset bar ----
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

    // ---- Tab bar (+ the Solo Amp/Cab switch on the right) ----
    tabBarArea = area.removeFromTop (30);
    {
        auto tb = tabBarArea;
        soloButton.setBounds (tb.removeFromRight (170));
        tb.removeFromRight (10);
        const int bw = 120;
        tabPreButton.setBounds    (tb.removeFromLeft (bw)); tb.removeFromLeft (4);
        tabAmpCabButton.setBounds (tb.removeFromLeft (bw)); tb.removeFromLeft (4);
        tabPostButton.setBounds   (tb.removeFromLeft (bw)); tb.removeFromLeft (4);
        tabFxButton.setBounds     (tb.removeFromLeft (bw)); tb.removeFromLeft (4);
        tabTunerButton.setBounds  (tb.removeFromLeft (bw));
    }
    area.removeFromTop (10);

    // ---- Master strip (persistent) ----
    masterArea = area.removeFromRight (112);
    area.removeFromRight (12);
    {
        auto m = masterArea.reduced (10);
        m.removeFromTop (20);                        // title

        // Master input level (chain start) + IN meter.
        m.removeFromTop (14);                        // knob label room
        auto inRow = m.removeFromTop (78);
        inMeter.setBounds (inRow.removeFromRight (24));
        inRow.removeFromRight (4);
        inputKnob->setBounds (inRow);

        // Bottom: output mode + clean blend.
        outputModeBox.setBounds (m.removeFromBottom (24));
        m.removeFromBottom (14);                     // caption
        auto cleanArea = m.removeFromBottom (72);
        cleanKnob.setBounds (cleanArea.reduced (8, 0));
        m.removeFromBottom (14);                     // caption
        m.removeFromBottom (4);

        // Middle: master meter + fader.
        m.removeFromTop (6);
        masterMeter.setBounds (m.removeFromLeft (26));
        m.removeFromLeft (6);
        masterFader.setBounds (m);
    }

    bodyArea = area;

    // Lays out a Flesh Render panel: 3x3 stage grid left, crossover pair right.
    auto layoutFR = [] (juce::Rectangle<int> a, const std::array<juce::Slider*, 9>& knobs,
                        juce::TextButton& toggle, juce::Slider& xlow, juce::Slider& xhigh)
    {
        auto s = a.reduced (14);
        toggle.setBounds (s.removeFromTop (24).removeFromRight (70));
        s.removeFromTop (16);                        // band captions (painted)
        s.removeFromTop (18);                        // knob label room
        auto xover = s.removeFromRight (220);
        const int cw = s.getWidth() / 3;
        for (int b = 0; b < 3; ++b)
        {
            auto col = s.removeFromLeft (cw);
            const int kw = col.getWidth() / 3;
            for (int st = 0; st < 3; ++st)
                knobs[(size_t) (b * 3 + st)]->setBounds (col.removeFromLeft (kw).reduced (4));
        }
        xover.removeFromLeft (16);
        const int xw = xover.getWidth() / 2;
        xlow.setBounds  (xover.removeFromLeft (xw).reduced (4));
        xhigh.setBounds (xover.reduced (4));
    };

    // ===== PRE tab =====
    {
        auto a = bodyArea;
        frontSatArea = a.removeFromTop (216);
        a.removeFromTop (12);
        auto row2 = a.removeFromTop (216);
        driveArea = row2.removeFromLeft (600);
        row2.removeFromLeft (12);
        gateArea = row2.removeFromLeft (196);
        row2.removeFromLeft (12);
        lowCutArea = row2;

        layoutFR (frontSatArea, frontSatKnobs, *frontSatToggle, *frontXLowKnob, *frontXHighKnob);

        {
            auto d = driveArea.reduced (14);
            driveToggle->setBounds (d.removeFromTop (24).removeFromRight (70));
            d.removeFromTop (14);                    // CIRCUIT caption (painted)
            driveCircuitBox.setBounds (d.removeFromTop (26).removeFromLeft (190));
            d.removeFromTop (18);                    // knob label room
            auto knobRow = d.removeFromTop (92);
            // TC: 5 knobs; TS: 3 knobs — both laid out in the same row space.
            {
                auto r = knobRow;
                const int kw = r.getWidth() / 5;
                tcGainKnob->setBounds   (r.removeFromLeft (kw).reduced (4, 0));
                tcBassKnob->setBounds   (r.removeFromLeft (kw).reduced (4, 0));
                tcMidKnob->setBounds    (r.removeFromLeft (kw).reduced (4, 0));
                tcTrebleKnob->setBounds (r.removeFromLeft (kw).reduced (4, 0));
                tcLevelKnob->setBounds  (r.reduced (4, 0));
            }
            {
                auto r = knobRow;
                const int kw = r.getWidth() / 3;
                tsDriveKnob->setBounds (r.removeFromLeft (kw).reduced (10, 0));
                tsToneKnob->setBounds  (r.removeFromLeft (kw).reduced (10, 0));
                tsLevelKnob->setBounds (r.reduced (10, 0));
            }
        }
        {
            auto gt = gateArea.reduced (14);
            gt.removeFromTop (22);
            gt.removeFromTop (16);                   // knob label room
            gateKnob->setBounds (gt.removeFromTop (84).reduced (28, 0));
            gt.removeFromTop (4);
            gateToggle->setBounds (gt.removeFromTop (24).reduced (30, 0));
            gt.removeFromTop (16);                   // POSITION caption (painted)
            gatePosBox.setBounds (gt.removeFromTop (24));
        }
        {
            auto lc = lowCutArea.reduced (14);
            lc.removeFromTop (22);
            lc.removeFromTop (16);
            lowCutKnob->setBounds (lc.removeFromTop (84).reduced (28, 0));
            lc.removeFromTop (4);
            lowCutToggle->setBounds (lc.removeFromTop (24).reduced (30, 0));
        }
    }

    // ===== AMP / CAB tab =====
    {
        auto a = bodyArea;
        auto row1 = a.removeFromTop (330);
        ampModelsArea = row1.removeFromLeft (676);
        row1.removeFromLeft (12);
        sagArea = row1;
        a.removeFromTop (12);
        cabArea = a.removeFromTop (200);

        {
            auto m = ampModelsArea.reduced (14);
            m.removeFromTop (22);

            auto meterStrip = m.removeFromRight (34);
            meterStrip.removeFromTop (2);
            namOutMeter.setBounds (meterStrip.removeFromTop (150));
            m.removeFromRight (10);

            auto rowA = m.removeFromTop (26);
            loadModelAButton.setBounds (rowA.removeFromLeft (86)); rowA.removeFromLeft (4);
            prevAButton.setBounds (rowA.removeFromLeft (22)); rowA.removeFromLeft (2);
            nextAButton.setBounds (rowA.removeFromLeft (22)); rowA.removeFromLeft (6);
            clearModelAButton.setBounds (rowA.removeFromRight (24)); rowA.removeFromRight (4);
            ampAMuteToggle->setBounds (rowA.removeFromRight (30)); rowA.removeFromRight (4);
            ampAToggle->setBounds (rowA.removeFromRight (40)); rowA.removeFromRight (4);
            modelANameLabel.setBounds (rowA);
            m.removeFromTop (5);
            auto rowB = m.removeFromTop (26);
            loadModelBButton.setBounds (rowB.removeFromLeft (86)); rowB.removeFromLeft (4);
            prevBButton.setBounds (rowB.removeFromLeft (22)); rowB.removeFromLeft (2);
            nextBButton.setBounds (rowB.removeFromLeft (22)); rowB.removeFromLeft (6);
            clearModelBButton.setBounds (rowB.removeFromRight (24)); rowB.removeFromRight (4);
            ampBMuteToggle->setBounds (rowB.removeFromRight (30)); rowB.removeFromRight (4);
            ampBToggle->setBounds (rowB.removeFromRight (40)); rowB.removeFromRight (4);
            modelBNameLabel.setBounds (rowB);

            m.removeFromTop (18);                    // combo captions (painted)
            auto comboRow = m.removeFromTop (26);
            routingBox.setBounds (comboRow.removeFromLeft (160));
            comboRow.removeFromLeft (14);
            inputModeBox.setBounds (comboRow.removeFromLeft (190));

            m.removeFromTop (18);                    // knob label room
            auto knobRow = m.removeFromTop (86);
            const int kw = knobRow.getWidth() / 5;
            spreadKnob->setBounds    (knobRow.removeFromLeft (kw).reduced (4, 0));
            ampALevelKnob->setBounds (knobRow.removeFromLeft (kw).reduced (4, 0));
            ampBLevelKnob->setBounds (knobRow.removeFromLeft (kw).reduced (4, 0));
            inputCalKnob->setBounds  (knobRow.removeFromLeft (kw).reduced (4, 0));
            ampOutKnob->setBounds    (knobRow.reduced (4, 0));

            m.removeFromTop (6);
            qualityLabelArea = m.removeFromTop (14);
            qualitySlider.setBounds (m.removeFromTop (22));
        }
        {
            // SAG fills its whole block — one big, substantial knob.
            auto sg = sagArea.reduced (14);
            sg.removeFromTop (22);               // section title
            sg.removeFromTop (16);               // knob label room
            sagKnob->setBounds (sg.reduced (4));
        }
        {
            auto cb = cabArea.reduced (14);
            cb.removeFromTop (20);
            const int half = (cb.getWidth() - 20) / 2;
            auto blockA = cb.removeFromLeft (half);
            cb.removeFromLeft (20);
            auto blockB = cb;

            auto irBlock = [] (juce::Rectangle<int> block, juce::TextButton& load, juce::TextButton& clear,
                               juce::TextButton& on, juce::TextButton& mute, juce::Label& name, juce::Slider& level)
            {
                block.removeFromTop (14);            // CAB A/B caption (painted)
                auto top = block.removeFromTop (26);
                load.setBounds (top.removeFromLeft (100)); top.removeFromLeft (6);
                clear.setBounds (top.removeFromLeft (26)); top.removeFromLeft (8);
                on.setBounds (top.removeFromLeft (44));    top.removeFromLeft (4);
                mute.setBounds (top.removeFromLeft (30));  top.removeFromLeft (8);
                name.setBounds (top);
                block.removeFromTop (8);
                level.setBounds (block.removeFromTop (22));
            };
            irBlock (blockA, loadIRAButton, clearIRAButton, *cabAToggle, *cabAMuteToggle, irANameLabel, *cabALevelKnob);
            irBlock (blockB, loadIRBButton, clearIRBButton, *cabBToggle, *cabBMuteToggle, irBNameLabel, *cabBLevelKnob);
        }
    }

    // ===== POST tab =====
    // Strict chain order: EQ -> Flesh Render (full width, big knobs like the
    // PRE version) -> compressor + filters sharing the bottom row.
    {
        auto a = bodyArea;
        eqArea = a.removeFromTop (170);
        a.removeFromTop (12);
        postSatArea = a.removeFromTop (216);
        a.removeFromTop (12);
        auto row3 = a;
        compArea = row3.removeFromLeft (380);
        row3.removeFromLeft (12);
        filterArea = row3;

        {
            auto e = eqArea.reduced (14);
            auto etop = e.removeFromTop (24);
            eqToggle->setBounds (etop.removeFromRight (90));
            e.removeFromTop (14);
            const int n = Api560EQ::kNumBands;
            const int sw = e.getWidth() / n;
            for (int i = 0; i < n; ++i)
                eqSliders[(size_t) i]->setBounds (e.removeFromLeft (sw).reduced (6, 2));
        }
        layoutFR (postSatArea, satKnobs, *satToggle, *satXLowKnob, *satXHighKnob);
        {
            auto cp = compArea.reduced (14);
            compToggle->setBounds (cp.removeFromTop (22).removeFromRight (70));
            cp.removeFromTop (16);
            grMeter.setBounds (cp.removeFromRight (44).reduced (0, 6));
            cp.removeFromRight (10);
            auto knobRow = cp.removeFromTop (100);
            const int kw = knobRow.getWidth() / 2;
            compKnob->setBounds     (knobRow.removeFromLeft (kw).reduced (4, 0));
            compGainKnob->setBounds (knobRow.reduced (4, 0));
        }
        {
            auto fa = filterArea.reduced (14);
            fa.removeFromTop (20);
            const int half = fa.getWidth() / 2;
            auto left = fa.removeFromLeft (half);
            auto right = fa;
            left.removeFromTop (14);
            hpfKnob->setBounds (left.removeFromTop (72).reduced (juce::jmax (0, (left.getWidth() - 100) / 2), 0));
            hpfToggle->setBounds (left.removeFromTop (24).withSizeKeepingCentre (100, 24));
            right.removeFromTop (14);
            lpfKnob->setBounds (right.removeFromTop (72).reduced (juce::jmax (0, (right.getWidth() - 100) / 2), 0));
            lpfToggle->setBounds (right.removeFromTop (24).withSizeKeepingCentre (100, 24));
        }
    }

    // ===== FX tab =====
    {
        auto a = bodyArea;
        auto row = a.removeFromTop (240);
        const int half = (row.getWidth() - 14) / 2;
        delayArea = row.removeFromLeft (half);
        row.removeFromLeft (14);
        reverbArea = row;
        a.removeFromTop (14);
        fxOrderArea = a.removeFromTop (30);

        {
            auto d = delayArea.reduced (14);
            delayToggle->setBounds (d.removeFromTop (24).removeFromRight (96));
            d.removeFromTop (26 + 14);                // spacer to align with reverb's type row
            d.removeFromTop (18);
            auto knobRow = d.removeFromTop (96);
            const int kw = knobRow.getWidth() / 3;
            delayTimeKnob->setBounds (knobRow.removeFromLeft (kw).reduced (8, 0));
            delayFbKnob->setBounds   (knobRow.removeFromLeft (kw).reduced (8, 0));
            delayMixKnob->setBounds  (knobRow.reduced (8, 0));
        }
        {
            auto r = reverbArea.reduced (14);
            reverbToggle->setBounds (r.removeFromTop (24).removeFromRight (96));
            r.removeFromTop (14);                     // TYPE caption (painted)
            reverbTypeBox.setBounds (r.removeFromTop (26).removeFromLeft (160));
            r.removeFromTop (18);
            auto knobRow = r.removeFromTop (96);
            const int kw = knobRow.getWidth() / 3;
            reverbSizeKnob->setBounds (knobRow.removeFromLeft (kw).reduced (8, 0));
            reverbDampKnob->setBounds (knobRow.removeFromLeft (kw).reduced (8, 0));
            reverbMixKnob->setBounds  (knobRow.reduced (8, 0));
        }

        auto o = fxOrderArea;
        o.removeFromLeft (120);
        fxOrderBox.setBounds (o.removeFromLeft (200).reduced (0, 2));
    }

    // ===== TUNER tab =====
    {
        tunerArea = bodyArea;
        auto t = tunerArea.reduced (10);
        t.removeFromTop (24);
        tunerToggle->setBounds (t.removeFromTop (36).withSizeKeepingCentre (220, 30));
        t.removeFromTop (10);
        strobeTuner.setBounds (t);
    }
}
