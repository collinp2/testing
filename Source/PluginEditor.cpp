#include "PluginEditor.h"

//==============================================================================
static constexpr int HEADER_H = 90;
static constexpr int FOOTER_H = 40;
static constexpr int MARGIN   = 12;

static juce::Colour hcol (juce::uint32 u) { return juce::Colour (u); }

//==============================================================================
FleshRenderEditor::FleshRenderEditor (FleshRenderProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&laf);

    // ---- Neve pre EQ -------------------------------------------------------
    hpfBox       = &makeCombo ("neve_hpf", { "Off", "50 Hz", "80 Hz", "160 Hz", "300 Hz" });
    lowFreqBox   = &makeCombo ("neve_low_freq", { "35 Hz", "60 Hz", "110 Hz", "220 Hz" });
    midFreqBox   = &makeCombo ("neve_mid_freq",
                               { "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" });
    lowGainKnob  = &makeKnob ("neve_low_gain",  &neveLaf);
    midGainKnob  = &makeKnob ("neve_mid_gain",  &neveLaf);
    highGainKnob = &makeKnob ("neve_high_gain", &neveLaf);
    neveOnButton = &makeToggle ("neve_active", "EQ IN");

    // ---- Bands (parameter ids unchanged; middle stage reads DRIVE) ---------
    lowPanel.init  (p.apvts, "LOW",  "below X-LOW",
                    "low_sat",  "low_dist",  "low_fuzz");
    midPanel.init  (p.apvts, "MID",  "X-LOW \xe2\x80\x94 X-HIGH",
                    "mid_sat",  "mid_dist",  "mid_fuzz");
    highPanel.init (p.apvts, "HIGH", "above X-HIGH",
                    "high_sat", "high_dist", "high_fuzz");
    content.addAndMakeVisible (lowPanel);
    content.addAndMakeVisible (midPanel);
    content.addAndMakeVisible (highPanel);

    xoverLowKnob  = &makeKnob ("xover_low",  nullptr);
    xoverHighKnob = &makeKnob ("xover_high", nullptr);
    mixKnob       = &makeKnob ("sat_mix",    nullptr);

    // ---- Graphic EQ ---------------------------------------------------------
    for (int i = 0; i < Api560EQ::kNumBands; ++i)
    {
        auto s = std::make_unique<juce::Slider> (juce::Slider::LinearVertical,
                                                 juce::Slider::TextBoxBelow);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 14);
        content.addAndMakeVisible (*s);
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (
            p.apvts, "geq_" + juce::String (i), *s));
        geqSliders[(size_t) i] = s.get();
        sliders.push_back (std::move (s));

        const float f = Api560EQ::kFrequencies[(size_t) i];
        auto& lab = makeLabel (f >= 1000.0f ? juce::String (f / 1000.0f, (f == 16000.0f ? 0 : 1)) + "k"
                                            : juce::String (juce::roundToInt (f)));
        lab.attachToComponent (geqSliders[(size_t) i], false);
    }
    geqOnButton = &makeToggle ("geq_active", "EQ IN");

    // ---- Filters / output ----------------------------------------------------
    postHpfKnob = &makeKnob ("post_hpf_freq", nullptr);
    postLpfKnob = &makeKnob ("post_lpf_freq", nullptr);
    postHpfOn   = &makeToggle ("post_hpf_active", "ON");
    postLpfOn   = &makeToggle ("post_lpf_active", "ON");

    outputKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    outputKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    outputKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                    juce::MathConstants<float>::pi * 2.8f, true);
    outputKnob.setLookAndFeel (&neveLaf);
    content.addAndMakeVisible (outputKnob);
    outputAttach = std::make_unique<SliderAttachment> (p.apvts, "output_level", outputKnob);

    outputMeter.caption = "OUT";
    content.addAndMakeVisible (outputMeter);

    // ---- Scalable canvas -----------------------------------------------------
    addAndMakeVisible (content);
    content.setBounds (0, 0, kBaseW, kBaseH);
    layoutContent();
    buildBackgroundTexture();

    setResizable (true, true);
    if (auto* cs = getConstrainer())
    {
        cs->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
        cs->setSizeLimits (kBaseW * 3 / 4, kBaseH * 3 / 4, kBaseW * 2, kBaseH * 2);
    }
    const int savedW = juce::jlimit (kBaseW * 3 / 4, kBaseW * 2,
                                     (int) p.apvts.state.getProperty ("editor_w", kBaseW));
    setSize (savedW, juce::roundToInt (savedW * (double) kBaseH / (double) kBaseW));

    startTimerHz (30);
}

FleshRenderEditor::~FleshRenderEditor()
{
    outputKnob.setLookAndFeel (nullptr);
    for (auto& s : sliders)
        s->setLookAndFeel (nullptr);
    for (auto& l : labels)
        l->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
juce::Slider& FleshRenderEditor::makeKnob (const juce::String& paramId, juce::LookAndFeel* lnfToUse)
{
    auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag,
                                             juce::Slider::TextBoxBelow);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 15);
    s->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                            juce::MathConstants<float>::pi * 2.8f, true);
    if (lnfToUse != nullptr)
        s->setLookAndFeel (lnfToUse);
    content.addAndMakeVisible (*s);
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.apvts, paramId, *s));

    auto& ref = *s;
    sliders.push_back (std::move (s));
    return ref;
}

juce::Label& FleshRenderEditor::makeLabel (const juce::String& text)
{
    auto l = std::make_unique<juce::Label> (juce::String(), text);
    l->setJustificationType (juce::Justification::centred);
    l->setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
    l->setColour (juce::Label::textColourId, hcol (HorrorLookAndFeel::COL_BONE));
    content.addAndMakeVisible (*l);

    auto& ref = *l;
    labels.push_back (std::move (l));
    return ref;
}

juce::ComboBox& FleshRenderEditor::makeCombo (const juce::String& paramId, const juce::StringArray& items)
{
    auto c = std::make_unique<juce::ComboBox>();
    c->addItemList (items, 1);
    content.addAndMakeVisible (*c);
    comboAttachments.push_back (std::make_unique<ComboAttachment> (processor.apvts, paramId, *c));

    auto& ref = *c;
    combos.push_back (std::move (c));
    return ref;
}

juce::TextButton& FleshRenderEditor::makeToggle (const juce::String& paramId, const juce::String& text)
{
    auto b = std::make_unique<juce::TextButton> (text);
    b->setClickingTogglesState (true);
    b->setColour (juce::TextButton::buttonOnColourId, hcol (HorrorLookAndFeel::COL_BLOOD));
    content.addAndMakeVisible (*b);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.apvts, paramId, *b));

    auto& ref = *b;
    buttons.push_back (std::move (b));
    return ref;
}

//==============================================================================
void FleshRenderEditor::timerCallback()
{
    outputMeter.update (processor.fetchOutputPeak());
}

//==============================================================================
// Pre-render the canvas background once (drawn by paintContent).
void FleshRenderEditor::buildBackgroundTexture()
{
    const int w = kBaseW, h = kBaseH;
    backgroundTexture = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics bg (backgroundTexture);

    bg.fillAll (hcol (HorrorLookAndFeel::COL_BACKGROUND));

    {
        juce::ColourGradient v (juce::Colours::transparentBlack, w * 0.5f, h * 0.5f,
                                juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true);
        bg.setGradientFill (v);
        bg.fillRect (0, 0, w, h);
    }

    HorrorLookAndFeel::drawGrainTexture (bg, { 0, 0, w, h }, 0.018f);

    // Header.
    bg.setColour (hcol (HorrorLookAndFeel::COL_HEADER_BG));
    bg.fillRect (0, 0, w, HEADER_H + 4);
    {
        juce::ColourGradient hg (juce::Colour (0xFF280000), 0.0f, 0.0f,
                                 hcol (HorrorLookAndFeel::COL_HEADER_BG), 0.0f, (float) HEADER_H, false);
        bg.setGradientFill (hg);
        bg.fillRect (0, 0, w, HEADER_H);
    }

    // Footer.
    bg.setColour (hcol (HorrorLookAndFeel::COL_HEADER_BG));
    bg.fillRect (0, h - FOOTER_H, w, FOOTER_H);

    bg.setColour (hcol (HorrorLookAndFeel::COL_BLOOD_DARK));
    bg.drawHorizontalLine (HEADER_H,     2.0f, (float) (w - 2));
    bg.drawHorizontalLine (h - FOOTER_H, 2.0f, (float) (w - 2));
    bg.setColour (hcol (HorrorLookAndFeel::COL_BLOOD).withAlpha (0.5f));
    bg.drawHorizontalLine (HEADER_H + 1, 2.0f, (float) (w - 2));

    // Title.
    bg.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 42.0f,
                            juce::Font::bold | juce::Font::italic));
    bg.setColour (juce::Colours::black.withAlpha (0.8f));
    bg.drawText ("FLESH RENDER", 3, 10, w, 46, juce::Justification::centred);
    bg.setColour (juce::Colour (0xFF4A0000));
    bg.drawText ("FLESH RENDER", 1, 11, w, 46, juce::Justification::centred);
    bg.setColour (hcol (HorrorLookAndFeel::COL_BLOOD_BRIGHT));
    bg.drawText ("FLESH RENDER", 0, 9, w, 46, juce::Justification::centred);
    bg.setColour (juce::Colour (0xFFFF4040).withAlpha (0.25f));
    bg.drawText ("FLESH RENDER", 0, 8, w, 46, juce::Justification::centred);

    bg.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
    bg.setColour (hcol (HorrorLookAndFeel::COL_BONE_DIM));
    bg.drawText ("[ NEVE PRE-EQ  //  MULTIBAND SATURATION - DRIVE - FUZZ  //  API 560 POST-EQ  //  FILTERS ]",
                 0, 57, w, 14, juce::Justification::centred);

    bg.setColour (hcol (HorrorLookAndFeel::COL_BLOOD_DARK).withAlpha (0.5f));
    bg.drawHorizontalLine (71, (float) w * 0.04f, (float) w * 0.24f);
    bg.drawHorizontalLine (71, (float) w * 0.76f, (float) w * 0.96f);

    HorrorLookAndFeel::drawBloodDrips (bg, 10.0f, (float) (w - 10), (float) (HEADER_H - 2), 28.0f);

    // Footer text.
    bg.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain));
    bg.setColour (hcol (HorrorLookAndFeel::COL_BONE_DIM).withAlpha (0.7f));
    bg.drawText ("VOIDCRAFT AUDIO  \xe2\x80\xa2  FLESH RENDER v2.0  \xe2\x80\xa2  HORROR-GRADE MULTIBAND PROCESSING",
                 0, h - FOOTER_H + 12, w, 16, juce::Justification::centred);
    bg.setColour (hcol (HorrorLookAndFeel::COL_BLOOD_DARK).withAlpha (0.8f));
    bg.setFont (juce::Font (14.0f));
    bg.drawText ("\xe2\x98\xa0", 20, h - FOOTER_H + 10, 20, 18, juce::Justification::centred);
    bg.drawText ("\xe2\x98\xa0", w - 40, h - FOOTER_H + 10, 20, 18, juce::Justification::centred);
}

//==============================================================================
void FleshRenderEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);   // letterbox behind the scaled canvas
}

void FleshRenderEditor::paintContent (juce::Graphics& g)
{
    g.drawImageAt (backgroundTexture, 0, 0);

    auto title = [&] (juce::Rectangle<int> area, const juce::String& t)
    {
        if (area.isEmpty()) return;
        HorrorLookAndFeel::drawPanelBackground (g, area);
        g.setColour (hcol (HorrorLookAndFeel::COL_BLOOD_BRIGHT));
        g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
        g.drawText (t, area.reduced (12, 6).removeFromTop (14), juce::Justification::centredLeft);
    };

    title (neveArea,   "NEVE 1073 / 74  -  PRE EQ");
    title (bandArea,   "FLESH RENDER  -  MULTIBAND  (SAT / DRIVE / FUZZ)");
    title (xoverArea,  "X-OVERS / MIX");
    title (geqArea,    "API 560  -  POST GRAPHIC EQ");
    title (bottomArea, "FILTERS / OUTPUT");

    // Small captions for the Neve columns and bottom controls.
    g.setColour (hcol (HorrorLookAndFeel::COL_BONE_DIM));
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
    auto captionAbove = [&] (juce::Component* comp, const juce::String& t)
    {
        if (comp != nullptr && ! comp->getBounds().isEmpty())
            g.drawText (t, comp->getX() - 20, comp->getY() - 15, comp->getWidth() + 40, 13,
                        juce::Justification::centred);
    };
    captionAbove (hpfBox,        "HPF");
    captionAbove (lowGainKnob,   "LOW");
    captionAbove (midGainKnob,   "MID");
    captionAbove (highGainKnob,  "HIGH 12K");
    captionAbove (xoverLowKnob,  "X-LOW");
    captionAbove (xoverHighKnob, "X-HIGH");
    captionAbove (mixKnob,       "MIX");
    captionAbove (postHpfKnob,   "HI-PASS");
    captionAbove (postLpfKnob,   "LOW-PASS");
    captionAbove (&outputKnob,   "OUTPUT");
}

//==============================================================================
void FleshRenderEditor::resized()
{
    // Uniform scale: layout stays at the fixed base size; the canvas is
    // stretched to fill the window (aspect ratio locked by the constrainer).
    const float scale = getWidth() / (float) kBaseW;
    content.setTransform (juce::AffineTransform::scale (scale));
    processor.apvts.state.setProperty ("editor_w", getWidth(), nullptr);
}

void FleshRenderEditor::layoutContent()
{
    auto area = juce::Rectangle<int> (0, 0, kBaseW, kBaseH);
    area.removeFromTop (HEADER_H + 4);
    area.removeFromBottom (FOOTER_H + 2);
    area.reduce (MARGIN, 4);

    // ---- NEVE pre EQ row -----------------------------------------------------
    neveArea = area.removeFromTop (150);
    {
        auto n = neveArea.reduced (12);
        n.removeFromTop (16);                       // section title
        n.removeFromTop (15);                       // captions (painted)

        auto cols = n;
        const int colW = cols.getWidth() / 5;

        auto hpfCol = cols.removeFromLeft (colW);
        hpfBox->setBounds (hpfCol.withSizeKeepingCentre (colW - 30, 24));

        auto lowCol = cols.removeFromLeft (colW);
        lowGainKnob->setBounds (lowCol.removeFromTop (76).withSizeKeepingCentre (72, 76));
        lowFreqBox->setBounds (lowCol.removeFromTop (24).withSizeKeepingCentre (colW - 40, 22));

        auto midCol = cols.removeFromLeft (colW);
        midGainKnob->setBounds (midCol.removeFromTop (76).withSizeKeepingCentre (72, 76));
        midFreqBox->setBounds (midCol.removeFromTop (24).withSizeKeepingCentre (colW - 40, 22));

        auto highCol = cols.removeFromLeft (colW);
        highGainKnob->setBounds (highCol.removeFromTop (76).withSizeKeepingCentre (72, 76));

        auto onCol = cols;
        neveOnButton->setBounds (onCol.withSizeKeepingCentre (78, 26));
    }
    area.removeFromTop (8);

    // ---- Bands + crossover/mix column -----------------------------------------
    auto bandsRow = area.removeFromTop (312);
    xoverArea = bandsRow.removeFromRight (160);
    bandsRow.removeFromRight (8);
    bandArea = bandsRow;
    {
        auto b = bandArea.reduced (10);
        b.removeFromTop (18);                       // section title
        const int panelW = (b.getWidth() - 2 * 8) / 3;
        lowPanel .setBounds (b.removeFromLeft (panelW)); b.removeFromLeft (8);
        midPanel .setBounds (b.removeFromLeft (panelW)); b.removeFromLeft (8);
        highPanel.setBounds (b);
    }
    {
        auto x = xoverArea.reduced (10);
        x.removeFromTop (18);
        const int kh = x.getHeight() / 3;
        auto place = [&] (juce::Slider* s)
        {
            auto cell = x.removeFromTop (kh);
            cell.removeFromTop (14);                // caption (painted)
            s->setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth() - 8, 86),
                                                      juce::jmin (cell.getHeight(), 78)));
        };
        place (xoverLowKnob);
        place (xoverHighKnob);
        place (mixKnob);
    }
    area.removeFromTop (8);

    // ---- Graphic EQ row ---------------------------------------------------------
    geqArea = area.removeFromTop (178);
    {
        auto e = geqArea.reduced (12);
        auto top = e.removeFromTop (16);
        geqOnButton->setBounds (top.removeFromRight (78).withHeight (22));
        e.removeFromTop (16);                       // band labels (attached)
        const int sw = e.getWidth() / Api560EQ::kNumBands;
        for (int i = 0; i < Api560EQ::kNumBands; ++i)
            geqSliders[(size_t) i]->setBounds (e.removeFromLeft (sw).reduced (6, 0));
    }
    area.removeFromTop (8);

    // ---- Filters / output row ------------------------------------------------
    bottomArea = area;
    {
        auto f = bottomArea.reduced (12);
        f.removeFromTop (16);                       // section title
        f.removeFromTop (15);                       // captions (painted)

        outputMeter.setBounds (f.removeFromRight (30).reduced (0, 4));
        f.removeFromRight (10);

        const int cellW = f.getWidth() / 3;
        auto hp = f.removeFromLeft (cellW);
        postHpfKnob->setBounds (hp.removeFromLeft (86).withHeight (juce::jmin (hp.getHeight(), 84)));
        postHpfOn->setBounds (hp.withSizeKeepingCentre (juce::jmin (hp.getWidth() - 8, 56), 24));

        auto lp = f.removeFromLeft (cellW);
        postLpfKnob->setBounds (lp.removeFromLeft (86).withHeight (juce::jmin (lp.getHeight(), 84)));
        postLpfOn->setBounds (lp.withSizeKeepingCentre (juce::jmin (lp.getWidth() - 8, 56), 24));

        auto out = f;
        outputKnob.setBounds (out.withSizeKeepingCentre (84, juce::jmin (out.getHeight(), 88)));
    }
}
