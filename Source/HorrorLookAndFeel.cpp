#include "HorrorLookAndFeel.h"

//==============================================================================
HorrorLookAndFeel::HorrorLookAndFeel()
{
    // Slider thumb / track colours
    setColour (juce::Slider::thumbColourId,        juce::Colour (COL_BLOOD_BRIGHT));
    setColour (juce::Slider::trackColourId,        juce::Colour (COL_BLOOD_DARK));
    setColour (juce::Slider::rotarySliderFillColourId,  juce::Colour (COL_BLOOD));
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (COL_RUST));
    setColour (juce::Slider::textBoxTextColourId,  juce::Colour (COL_BONE));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (COL_BLOOD_DARK));
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (COL_PANEL_BG));

    // Label colours
    setColour (juce::Label::textColourId,          juce::Colour (COL_BONE));
    setColour (juce::Label::backgroundColourId,    juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId,       juce::Colours::transparentBlack);
}

//==============================================================================
void HorrorLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                          int x, int y, int width, int height,
                                          float sliderPos,
                                          float startAngle, float endAngle,
                                          juce::Slider& /*slider*/)
{
    const float cx    = x + width  * 0.5f;
    const float cy    = y + height * 0.5f;
    const float rOuter = juce::jmin (width, height) * 0.5f - 4.0f;
    const float rInner = rOuter * 0.72f;

    // ---- Drop shadow --------------------------------------------------------
    {
        juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.7f), cx, cy,
                                     juce::Colours::transparentBlack, cx + rOuter + 4, cy + rOuter + 4, true);
        g.setGradientFill (shadow);
        g.fillEllipse (cx - rOuter - 3, cy - rOuter - 3, (rOuter + 3) * 2, (rOuter + 3) * 2);
    }

    // ---- Outer ring (dark metal) -------------------------------------------
    {
        juce::ColourGradient rim (juce::Colour (COL_KNOB_SHINE), cx - rOuter * 0.5f, cy - rOuter * 0.5f,
                                   juce::Colour (COL_KNOB_SHADOW), cx + rOuter * 0.5f, cy + rOuter * 0.5f, false);
        g.setGradientFill (rim);
        g.fillEllipse (cx - rOuter, cy - rOuter, rOuter * 2, rOuter * 2);
    }

    // ---- Inner body ---------------------------------------------------------
    {
        juce::ColourGradient body (juce::Colour (0xFF222222), cx - rInner * 0.4f, cy - rInner * 0.4f,
                                    juce::Colour (0xFF0E0E0E), cx + rInner * 0.4f, cy + rInner * 0.4f, false);
        g.setGradientFill (body);
        g.fillEllipse (cx - rInner, cy - rInner, rInner * 2, rInner * 2);
    }

    // ---- Detail ring --------------------------------------------------------
    g.setColour (juce::Colour (0xFF1F1F1F));
    g.drawEllipse (cx - rInner + 3, cy - rInner + 3, (rInner - 3) * 2, (rInner - 3) * 2, 0.7f);
    g.setColour (juce::Colour (0xFF2B2B2B));
    g.drawEllipse (cx - rOuter + 2, cy - rOuter + 2, (rOuter - 2) * 2, (rOuter - 2) * 2, 0.5f);

    // ---- Tick marks (claw-like) ---------------------------------------------
    {
        const int numTicks = 13;
        g.setColour (juce::Colour (COL_BLOOD_DARK).withAlpha (0.7f));
        for (int i = 0; i < numTicks; ++i)
        {
            const float angle    = startAngle + (endAngle - startAngle) * i / float (numTicks - 1);
            const float cosA     = std::cos (angle);
            const float sinA     = std::sin (angle);
            const float rTick0   = rOuter - 1.0f;
            const float rTick1   = rOuter + 3.5f;
            // Slightly jagged length for each tick
            const float jag      = (i % 3 == 0) ? 1.6f : 1.0f;
            g.drawLine (cx + rTick0 * cosA, cy + rTick0 * sinA,
                        cx + (rTick1 * jag) * cosA, cy + (rTick1 * jag) * sinA,
                        (i % 3 == 0) ? 1.8f : 1.0f);
        }
    }

    // ---- Filled arc (active range) -----------------------------------------
    {
        const float arcRadius = rOuter - 5.0f;
        juce::Path arc;
        arc.addArc (cx - arcRadius, cy - arcRadius, arcRadius * 2, arcRadius * 2,
                    startAngle, startAngle + (endAngle - startAngle) * sliderPos, true);

        juce::PathStrokeType stroke (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (juce::Colour (COL_BLOOD).withAlpha (0.6f));
        g.strokePath (arc, stroke);

        // Bright leading edge dot
        if (sliderPos > 0.005f)
        {
            const float tipAngle = startAngle + (endAngle - startAngle) * sliderPos;
            g.setColour (juce::Colour (COL_BLOOD_BRIGHT));
            g.fillEllipse (cx + (arcRadius) * std::cos (tipAngle) - 2.5f,
                           cy + (arcRadius) * std::sin (tipAngle) - 2.5f, 5.0f, 5.0f);
        }
    }

    // ---- Indicator line -----------------------------------------------------
    {
        const float indicatorAngle = startAngle + (endAngle - startAngle) * sliderPos;
        const float cosA = std::cos (indicatorAngle);
        const float sinA = std::sin (indicatorAngle);

        // Glow / soft blur approximation: wider dim line under bright line
        g.setColour (juce::Colour (COL_BLOOD).withAlpha (0.35f));
        g.drawLine (cx, cy, cx + rInner * 0.85f * cosA, cy + rInner * 0.85f * sinA, 5.0f);

        g.setColour (juce::Colour (COL_BLOOD_BRIGHT));
        g.drawLine (cx, cy, cx + rInner * 0.85f * cosA, cy + rInner * 0.85f * sinA, 2.0f);
    }

    // ---- Centre dot ---------------------------------------------------------
    g.setColour (juce::Colour (COL_BLOOD_BRIGHT));
    g.fillEllipse (cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);

    // ---- Outer rim highlight ------------------------------------------------
    g.setColour (juce::Colour (0xFF333333));
    g.drawEllipse (cx - rOuter, cy - rOuter, rOuter * 2, rOuter * 2, 1.0f);
}

//==============================================================================
juce::Font HorrorLookAndFeel::getLabelFont (juce::Label& /*label*/)
{
    return juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold);
}

void HorrorLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (! label.isBeingEdited())
    {
        g.setColour (label.findColour (juce::Label::textColourId));
        g.setFont (getLabelFont (label));
        g.drawFittedText (label.getText().toUpperCase(),
                          label.getLocalBounds(),
                          label.getJustificationType(),
                          1, 1.0f);
    }
}

//==============================================================================
void HorrorLookAndFeel::drawBloodDrips (juce::Graphics& g,
                                         float leftX, float rightX,
                                         float y, float maxDripLength)
{
    // Deterministic "random" positions so the UI is stable on every repaint
    const int numDrips = 14;
    const float span   = rightX - leftX;

    for (int i = 0; i < numDrips; ++i)
    {
        // Pseudo-random values derived purely from index
        const float t       = float (i) / float (numDrips - 1);
        const float offset  = ((i * 137 + 29) % 23 - 11) * 1.5f;
        const float px      = leftX + span * t + offset;
        const float len     = maxDripLength * (0.25f + 0.75f * ((i * 11 + 7) % 17) / 17.0f);
        const float wobble  = ((i * 5 + 3) % 7 - 3) * 1.2f;

        const float alpha   = 0.55f + 0.45f * ((i * 3 + 1) % 5) / 5.0f;
        g.setColour (juce::Colour (COL_BLOOD).withAlpha (alpha));

        // Stem
        juce::Path stem;
        stem.startNewSubPath (px, y);
        stem.cubicTo (px - 1.5f,   y + len * 0.35f,
                      px + wobble, y + len * 0.65f,
                      px + wobble, y + len * 0.88f);
        g.strokePath (stem, juce::PathStrokeType (1.8f + (i % 3) * 0.4f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Drip bulb
        const float br = 2.2f + (i % 4) * 0.5f;
        g.fillEllipse (px + wobble - br, y + len * 0.88f, br * 2.0f, br * 2.5f);
    }
}

//==============================================================================
void HorrorLookAndFeel::drawGrainTexture (juce::Graphics& g,
                                           juce::Rectangle<int> bounds,
                                           float alpha)
{
    // Pre-compute a tiny noise tile and tile it across the bounds.
    // Fixed seed keeps it deterministic.
    static constexpr int TILE = 64;
    static juce::Image grainTile;

    if (grainTile.isNull())
    {
        grainTile = juce::Image (juce::Image::ARGB, TILE, TILE, true);
        juce::Random rng (0xDEADBEEF);
        juce::Graphics tg (grainTile);
        for (int py = 0; py < TILE; ++py)
            for (int px = 0; px < TILE; ++px)
                if (rng.nextFloat() < 0.12f)
                    grainTile.setPixelAt (px, py,
                        juce::Colours::white.withAlpha (rng.nextFloat() * 0.08f + 0.02f));
    }

    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (bounds);
    g.setOpacity (alpha / 0.018f); // normalised
    for (int ty = bounds.getY(); ty < bounds.getBottom(); ty += TILE)
        for (int tx = bounds.getX(); tx < bounds.getRight(); tx += TILE)
            g.drawImageAt (grainTile, tx, ty);
}

//==============================================================================
void HorrorLookAndFeel::drawPanelBackground (juce::Graphics& g,
                                              juce::Rectangle<int> bounds)
{
    const float corner = 4.0f;

    // Fill
    g.setColour (juce::Colour (COL_PANEL_BG));
    g.fillRoundedRectangle (bounds.toFloat(), corner);

    // Inner shadow (vignette)
    {
        juce::ColourGradient vignette (juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getCentreY(),
                                        juce::Colours::black.withAlpha (0.35f),
                                        bounds.getX(), bounds.getY(), true);
        g.setGradientFill (vignette);
        g.fillRoundedRectangle (bounds.toFloat(), corner);
    }

    // Subtle scratch lines (deterministic)
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    for (int i = 0; i < 6; ++i)
    {
        const float sy = bounds.getY()  + (bounds.getHeight() * (i * 43 % 97) / 97.0f);
        const float ex = bounds.getX()  + bounds.getWidth()  * ((i * 37 + 11) % 60) / 100.0f;
        const float ey = bounds.getY()  + (bounds.getHeight() * ((i * 29 + 7) % 89) / 89.0f);
        g.drawLine (float (bounds.getX()), sy, ex, ey, 0.6f);
    }

    // Border
    g.setColour (juce::Colour (COL_PANEL_BORDER));
    g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), corner, 1.5f);

    // Inner bright edge (top/left highlight)
    g.setColour (juce::Colour (0xFF3A0000));
    g.drawLine (bounds.getX() + corner, bounds.getY() + 1.0f,
                bounds.getRight() - corner, bounds.getY() + 1.0f, 0.8f);
    g.drawLine (bounds.getX() + 1.0f, bounds.getY() + corner,
                bounds.getX() + 1.0f, bounds.getBottom() - corner, 0.8f);
}
