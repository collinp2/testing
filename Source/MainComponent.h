#pragma once

#include <JuceHeader.h>
#include "IrCapture.h"
#include "LevelMeter.h"

class MainComponent : public juce::Component,
                      public juce::AudioIODeviceCallback,
                      public juce::Timer
{
public:
    explicit MainComponent (juce::AudioDeviceManager& deviceManager);
    ~MainComponent() override;

    // Component
    void paint (juce::Graphics& g) override;
    void resized() override;

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // Timer (30 Hz meter refresh)
    void timerCallback() override;

private:
    void startCapture();
    void onCaptureComplete();
    void openAudioSetup();
    void chooseSaveDir();
    void setStatus (const juce::String& text);

    juce::AudioDeviceManager& deviceManager;

    // UI
    juce::TextButton audioSetupButton { "Audio Setup" };
    juce::Label      titleLabel;

    juce::Label      gainLabel;
    juce::Slider     gainSlider;

    juce::ToggleButton normalizeButton { "Normalize output" };

    juce::Label      baseNameLabel;
    juce::TextEditor baseNameEditor;

    juce::TextButton chooseDirButton { "Save To..." };
    juce::Label      dirLabel;

    juce::TextButton captureButton { "Capture" };
    juce::Label      statusLabel;

    LevelMeter inMeter;
    LevelMeter outMeter;

    // State
    IrCapture    irCapture;
    int          captureIndex { 1 };
    juce::String lastBaseName;
    juce::File   outputDir;

    std::unique_ptr<juce::FileChooser> dirChooser;

    // Scratch block for audio thread (avoid heap alloc in callback)
    juce::AudioBuffer<float> ioBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
