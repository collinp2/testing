#include "HorrorLookAndFeel.h"
#include "BinaryData.h"

using namespace horror;

namespace
{
    juce::Typeface::Ptr plexRegular()
    {
        static const juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor (
            BinaryData::IBMPlexMonoRegular_ttf, (size_t) BinaryData::IBMPlexMonoRegular_ttfSize);
        return t;
    }
    juce::Typeface::Ptr plexBold()
    {
        static const juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor (
            BinaryData::IBMPlexMonoBold_ttf, (size_t) BinaryData::IBMPlexMonoBold_ttfSize);
        return t;
    }
}

juce::Font HorrorLookAndFeel::monoFont (float height, bool bold)
{
    return juce::Font (bold ? plexBold() : plexRegular()).withHeight (height);
}

HorrorLookAndFeel::HorrorLookAndFeel()
{
    // Font-lock all default-drawn text (textboxes, combo, popups) to Plex Mono.
    setDefaultSansSerifTypeface (plexRegular());

    setColour (juce::Slider::textBoxTextColourId,      c (COL_BONE));
    setColour (juce::Slider::textBoxOutlineColourId,   juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId,              c (COL_BONE));
    setColour (juce::ComboBox::backgroundColourId,     c (COL_PANEL_BG));
    setColour (juce::ComboBox::textColourId,           c (COL_BONE));
    setColour (juce::ComboBox::outlineColourId,        c (COL_PANEL_BORDER));
    setColour (juce::ComboBox::arrowColourId,          c (COL_BLOOD_BRIGHT));
    setColour (juce::PopupMenu::backgroundColourId,    c (COL_PANEL_BG));
    setColour (juce::PopupMenu::textColourId,          c (COL_BONE));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, c (COL_BLOOD_DARK));
    setColour (juce::TextButton::buttonColourId,       c (COL_PANEL_BG));
    setColour (juce::TextButton::textColourOnId,       c (COL_BLOOD_BRIGHT));
    setColour (juce::TextButton::textColourOffId,      c (COL_BONE));
}

// ---------------------------------------------------------------------------
void HorrorLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                          juce::Slider&)
{
    // Use a centred square so the knob is always circular, never oval, even
    // when the slider's bounds aren't square.
    const auto area  = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float side = juce::jmin (area.getWidth(), area.getHeight()) - 8.0f;
    const auto bounds = juce::Rectangle<float> (side, side).withCentre (area.getCentre());
    const auto centre = bounds.getCentre();
    const float radius = side * 0.5f;
    const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Drop shadow.
    g.setColour (c (COL_KNOB_SHADOW).withAlpha (0.8f));
    g.fillEllipse (bounds.translated (0.0f, 2.0f));

    // Outer metal ring.
    juce::ColourGradient ring (c (COL_KNOB_SHINE), centre.x, centre.y - radius,
                               c (COL_KNOB_SHADOW), centre.x, centre.y + radius, false);
    g.setGradientFill (ring);
    g.fillEllipse (bounds);

    // Inner body.
    const auto inner = bounds.reduced (radius * 0.22f);
    juce::ColourGradient body (c (0xFF222222), centre.x, centre.y - radius * 0.6f,
                               c (0xFF0E0E0E), centre.x, centre.y + radius * 0.6f, false);
    g.setGradientFill (body);
    g.fillEllipse (inner);

    g.setColour (c (COL_KNOB_SHADOW));
    g.drawEllipse (inner, 1.0f);

    // Jagged "claw" tick marks around the dial.
    const int numTicks = 13;
    for (int i = 0; i < numTicks; ++i)
    {
        const float t = (float) i / (float) (numTicks - 1);
        const float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
        const float r1 = radius * 1.02f;
        const float r2 = radius * (1.16f + 0.04f * std::sin (i * 2.3f));   // ragged length
        const juce::Point<float> p1 (centre.x + r1 * std::cos (a - juce::MathConstants<float>::halfPi),
                                     centre.y + r1 * std::sin (a - juce::MathConstants<float>::halfPi));
        const juce::Point<float> p2 (centre.x + r2 * std::cos (a - juce::MathConstants<float>::halfPi),
                                     centre.y + r2 * std::sin (a - juce::MathConstants<float>::halfPi));
        g.setColour (c (COL_BLOOD_DARK).withAlpha (0.7f));
        g.drawLine ({ p1, p2 }, 1.4f);
    }

    // Active arc (how far the knob is turned).
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius * 1.10f, radius * 1.10f, 0.0f,
                       rotaryStartAngle, angle, true);
    g.setColour (c (COL_BLOOD).withAlpha (0.6f));
    g.strokePath (arc, juce::PathStrokeType (2.4f));

    // Leading-edge dot.
    const juce::Point<float> lead (centre.x + radius * 1.10f * std::cos (angle - juce::MathConstants<float>::halfPi),
                                   centre.y + radius * 1.10f * std::sin (angle - juce::MathConstants<float>::halfPi));
    g.setColour (c (COL_BLOOD_BRIGHT));
    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (lead));

    // Indicator line (dim glow + bright core).
    const juce::Point<float> tip (centre.x + radius * 0.72f * std::cos (angle - juce::MathConstants<float>::halfPi),
                                  centre.y + radius * 0.72f * std::sin (angle - juce::MathConstants<float>::halfPi));
    g.setColour (c (COL_BLOOD_DARK).withAlpha (0.6f));
    g.drawLine ({ centre, tip }, 4.0f);
    g.setColour (c (COL_BLOOD_BRIGHT));
    g.drawLine ({ centre, tip }, 2.0f);

    // Centre cap.
    g.setColour (c (COL_BLOOD_BRIGHT));
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (centre));
}

// ---------------------------------------------------------------------------
void HorrorLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float, float,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        const float cy = (float) y + height * 0.5f;
        const juce::Rectangle<float> track ((float) x, cy - 2.0f, (float) width, 4.0f);
        g.setColour (c (COL_KNOB_SHADOW));
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (c (COL_PANEL_BORDER).withAlpha (0.6f));
        g.drawRoundedRectangle (track, 2.0f, 1.0f);

        // Filled portion from the left to the thumb.
        g.setColour (c (COL_BLOOD).withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> ((float) x, cy - 2.0f, sliderPos - (float) x, 4.0f));

        // Thumb.
        const auto thumb = juce::Rectangle<float> (8.0f, (float) height * 0.7f)
                               .withCentre ({ sliderPos, cy });
        g.setColour (c (COL_KNOB_BODY));
        g.fillRoundedRectangle (thumb, 2.0f);
        g.setColour (slider.isEnabled() ? c (COL_BLOOD_BRIGHT) : c (COL_BONE_DIM));
        g.drawRoundedRectangle (thumb, 2.0f, 1.2f);
        return;
    }

    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, slider);
        return;
    }

    const auto track = juce::Rectangle<float> ((float) x + width * 0.5f - 2.0f, (float) y,
                                               4.0f, (float) height);
    // Track groove.
    g.setColour (c (COL_KNOB_SHADOW));
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (c (COL_PANEL_BORDER).withAlpha (0.6f));
    g.drawRoundedRectangle (track, 2.0f, 1.0f);

    // Centre (0 dB) marker.
    const float mid = (float) y + height * 0.5f;
    g.setColour (c (COL_BONE_DIM).withAlpha (0.5f));
    g.drawLine ((float) x + 2.0f, mid, (float) x + width - 2.0f, mid, 1.0f);

    // Fill from centre to the thumb.
    g.setColour (c (COL_BLOOD).withAlpha (0.55f));
    if (sliderPos < mid)
        g.fillRect (juce::Rectangle<float> (track.getX(), sliderPos, track.getWidth(), mid - sliderPos));
    else
        g.fillRect (juce::Rectangle<float> (track.getX(), mid, track.getWidth(), sliderPos - mid));

    // Thumb (bone cap with a blood edge).
    const auto thumb = juce::Rectangle<float> ((float) width * 0.8f, 8.0f)
                           .withCentre ({ (float) x + width * 0.5f, sliderPos });
    g.setColour (c (COL_KNOB_BODY));
    g.fillRoundedRectangle (thumb, 2.0f);
    g.setColour (c (COL_BLOOD_BRIGHT));
    g.drawRoundedRectangle (thumb, 2.0f, 1.2f);
}

// ---------------------------------------------------------------------------
juce::Font HorrorLookAndFeel::getLabelFont (juce::Label&)
{
    return monoFont (10.5f, true);
}

void HorrorLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.setColour (label.findColour (juce::Label::textColourId));
    g.setFont (getLabelFont (label));
    g.drawFittedText (label.getText().toUpperCase(), label.getLocalBounds(),
                      label.getJustificationType(), 1, 0.9f);
}

// ---------------------------------------------------------------------------
void HorrorLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                              const juce::Colour&, bool highlighted, bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();

    juce::Colour fill = on ? c (COL_BLOOD_DARK) : c (COL_PANEL_BG);
    if (down)        fill = fill.brighter (0.15f);
    else if (highlighted) fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (on ? c (COL_BLOOD_BRIGHT) : c (COL_PANEL_BORDER));
    g.drawRoundedRectangle (bounds, 3.0f, 1.2f);
}

void HorrorLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? c (COL_BONE_LIGHT) : c (COL_BONE));
    g.setFont (monoFont (11.0f, true));
    g.drawFittedText (b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred, 1, 0.85f);
}

// ---------------------------------------------------------------------------
void HorrorLookAndFeel::drawGrainTexture (juce::Graphics& g, juce::Rectangle<int> area, float alpha)
{
    juce::Random rng (0xDEADBEEF);
    // Spatters dark dried blood flecks over the light bone surface instead of white dust
    for (int i = 0; i < area.getWidth() * area.getHeight() / 12; ++i)
    {
        const int px = area.getX() + rng.nextInt (juce::jmax (1, area.getWidth()));
        const int py = area.getY() + rng.nextInt (juce::jmax (1, area.getHeight()));
        const float a = juce::jmap (rng.nextFloat(), 0.0f, 1.0f, 0.0f, alpha * 4.0f);

        // Randomly alternate between dark dried clots and raw red spots
        g.setColour (rng.nextBool() ? c (COL_BLOOD_DARK).withAlpha (a) : c (COL_BLOOD).withAlpha (a * 0.5f));
        g.fillRect (px, py, rng.nextInt (2) + 1, rng.nextInt (2) + 1);
    }
}

void HorrorLookAndFeel::drawBloodDrips (juce::Graphics& g, juce::Rectangle<float> area)
{
    juce::Random rng (1337);
    const int stems = 14;
    for (int i = 0; i < stems; ++i)
    {
        const float sx = area.getX() + area.getWidth() * ((float) i + 0.5f) / (float) stems
                       + rng.nextFloat() * 6.0f - 3.0f;
        const float len = area.getHeight() * (0.25f + rng.nextFloat() * 0.6f);
        const float wob = rng.nextFloat() * 8.0f - 4.0f;

        juce::Path p;
        p.startNewSubPath (sx, area.getY());
        p.cubicTo (sx + wob, area.getY() + len * 0.4f,
                   sx - wob * 0.5f, area.getY() + len * 0.7f,
                   sx, area.getY() + len);
        g.setColour (c (COL_BLOOD_DARK).withAlpha (0.55f));
        g.strokePath (p, juce::PathStrokeType (1.6f + rng.nextFloat()));

        // Drip bulb at the end.
        g.setColour (c (COL_BLOOD).withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (3.0f, 4.0f).withCentre ({ sx, area.getY() + len }));
    }
}

void HorrorLookAndFeel::drawPanelBackground (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Soft drop shadow so the panel reads as raised off the parchment. Clipped
    // to the panel's exterior so the (translucent) interior stays clean.
    {
        juce::Graphics::ScopedSaveState save (g);
        g.excludeClipRegion (bounds.toNearestInt());
        juce::Path sp;
        sp.addRoundedRectangle (bounds, 4.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.32f), 13, { 0, 4 }).drawForPath (g, sp);
    }

    g.setColour (c (COL_PANEL_BG));
    g.fillRoundedRectangle (bounds, 4.0f);

    // Vignette - transitioned from black to a deep, bruising red-brown blend
    juce::ColourGradient vig (juce::Colours::transparentBlack, bounds.getCentre(),
                              c (COL_RUST).withAlpha (0.25f), bounds.getTopLeft(), true);
    g.setGradientFill (vig);
    g.fillRoundedRectangle (bounds, 4.0f);

    // Deep flesh scratches (Dark instead of white so they tear into the bone plate)
    juce::Random rng ((int) (bounds.getX() * 7.0f + bounds.getY() * 13.0f) + 99);
    g.setColour (c (COL_BLOOD_DARK).withAlpha (0.15f));
    for (int i = 0; i < 7; ++i)
    {
        const float yy = bounds.getY() + rng.nextFloat() * bounds.getHeight();
        g.drawLine (bounds.getX() + 4.0f, yy, bounds.getRight() - 4.0f, yy + rng.nextFloat() * 6.0f - 3.0f, 0.8f);
    }

    // Outer framing tissue boundaries
    g.setColour (c (COL_PANEL_BORDER));
    g.drawRoundedRectangle (bounds, 4.0f, 1.2f);
    g.setColour (c (COL_BLOOD_DARK).withAlpha (0.4f));
    g.drawLine (bounds.getX() + 2.0f, bounds.getY() + 2.0f, bounds.getRight() - 2.0f, bounds.getY() + 2.0f, 0.8f);
}
