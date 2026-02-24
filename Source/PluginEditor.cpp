#include "PluginEditor.h"

static constexpr int kSliderWidth  = 50;
static constexpr int kSliderHeight = 250;
static constexpr int kLabelHeight  = 20;
static constexpr int kHeaderHeight = 40;
static constexpr int kPadding      = 10;

GEQ12AudioProcessorEditor::GEQ12AudioProcessorEditor(GEQ12AudioProcessor& proc)
    : AudioProcessorEditor(proc), mProcessor(proc)
{
    for (int b = 0; b < kNumBands; ++b) {
        // Slider setup — vertical fader style, classic graphic EQ look
        auto& slider = mBandSliders[static_cast<size_t>(b)];
        slider.setSliderStyle(juce::Slider::LinearVertical);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
        slider.setRange(GraphicEQ::kMinGainDB, GraphicEQ::kMaxGainDB, 0.1);
        slider.setDoubleClickReturnValue(true, 0.0); // double-click resets to 0 dB
        addAndMakeVisible(slider);

        // APVTS attachment — keeps slider synced with host automation
        mAttachments[static_cast<size_t>(b)] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                mProcessor.getAPVTS(),
                GEQ12AudioProcessor::getBandParamID(b),
                slider);

        // Label below slider
        auto& label = mBandLabels[static_cast<size_t>(b)];
        label.setText(GEQ12AudioProcessor::getBandParamName(b),
                      juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(11.0f));
        addAndMakeVisible(label);
    }

    setSize(kNumBands * kSliderWidth + (kNumBands + 1) * kPadding,
            kHeaderHeight + kSliderHeight + kLabelHeight + kPadding * 3);
}

void GEQ12AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1E1E2E));   // dark background

    // Header
    g.setColour(juce::Colour(0xFFCDD6F4)); // light text
    g.setFont(juce::Font(18.0f).boldened());
    g.drawText("GEQ-12", getLocalBounds().removeFromTop(kHeaderHeight),
               juce::Justification::centred);

    // 0 dB reference line across the sliders
    g.setColour(juce::Colour(0x40CDD6F4)); // translucent
    int yCenter = kHeaderHeight + kPadding + kSliderHeight / 2;
    g.drawHorizontalLine(yCenter, static_cast<float>(kPadding),
                         static_cast<float>(getWidth() - kPadding));
}

void GEQ12AudioProcessorEditor::resized()
{
    int x = kPadding;
    int sliderTop = kHeaderHeight + kPadding;

    for (int b = 0; b < kNumBands; ++b) {
        mBandSliders[static_cast<size_t>(b)].setBounds(x, sliderTop,
                                                        kSliderWidth, kSliderHeight);
        mBandLabels[static_cast<size_t>(b)].setBounds(x, sliderTop + kSliderHeight,
                                                       kSliderWidth, kLabelHeight);
        x += kSliderWidth + kPadding;
    }
}
