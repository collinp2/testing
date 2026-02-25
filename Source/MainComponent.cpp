#include "MainComponent.h"

static constexpr int kWindowWidth  = 480;
static constexpr int kWindowHeight = 440;
static constexpr int kHeaderHeight = 40;
static constexpr int kMeterWidth   = 40;

//==============================================================================
MainComponent::MainComponent (juce::AudioDeviceManager& dm)
    : deviceManager (dm)
{
    setSize (kWindowWidth, kWindowHeight);

    // Title
    titleLabel.setText ("IR Capture", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    // Audio setup button
    audioSetupButton.onClick = [this] { openAudioSetup(); };
    addAndMakeVisible (audioSetupButton);

    // Gain row
    gainLabel.setText ("Gain (dB):", juce::dontSendNotification);
    gainLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (gainLabel);

    gainSlider.setRange (-18.0, 18.0, 0.1);
    gainSlider.setValue (0.0);
    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    addAndMakeVisible (gainSlider);

    // Normalize row
    normalizeButton.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (normalizeButton);

    // Base name row
    baseNameLabel.setText ("Base Name:", juce::dontSendNotification);
    baseNameLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (baseNameLabel);

    baseNameEditor.setText ("IR");
    baseNameEditor.setInputRestrictions (64, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
    baseNameEditor.onTextChange = [this]
    {
        // Reset counter whenever the name changes
        captureIndex = 1;
        lastBaseName = baseNameEditor.getText().trim();
    };
    addAndMakeVisible (baseNameEditor);

    // Save directory row
    outputDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                    .getChildFile ("IR Captures");
    lastBaseName = "IR";

    chooseDirButton.onClick = [this] { chooseSaveDir(); };
    addAndMakeVisible (chooseDirButton);

    dirLabel.setText (outputDir.getFullPathName(), juce::dontSendNotification);
    dirLabel.setJustificationType (juce::Justification::centredLeft);
    dirLabel.setMinimumHorizontalScale (0.5f);
    addAndMakeVisible (dirLabel);

    // Capture button
    captureButton.onClick = [this] { startCapture(); };
    addAndMakeVisible (captureButton);

    // Status label
    statusLabel.setText ("Ready", juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    // Meters
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    // Init device manager with default 2-in / 2-out
    const auto err = deviceManager.initialiseWithDefaultDevices (2, 2);
    if (err.isNotEmpty())
        juce::Logger::writeToLog ("AudioDeviceManager init error: " + err);

    deviceManager.addAudioCallback (this);

    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
}

//==============================================================================
void MainComponent::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // Header row
    titleLabel.setBounds (8, 0, 200, kHeaderHeight);
    audioSetupButton.setBounds (w - 110, (kHeaderHeight - 24) / 2, 100, 24);

    // Left / right meter columns
    inMeter.setBounds  (0,               kHeaderHeight, kMeterWidth, h - kHeaderHeight);
    outMeter.setBounds (w - kMeterWidth, kHeaderHeight, kMeterWidth, h - kHeaderHeight);

    // Center area
    const int cx     = kMeterWidth + 8;
    const int cw     = w - kMeterWidth * 2 - 16;
    const int rowH   = 24;
    const int labelW = 80;

    // Gain row
    gainLabel.setBounds  (cx,                 58, labelW, rowH);
    gainSlider.setBounds (cx + labelW + 4,    58, cw - labelW - 4, rowH);

    // Normalize row
    normalizeButton.setBounds (cx + labelW + 4, 94, cw - labelW - 4, rowH);

    // Base name row
    baseNameLabel.setBounds  (cx,                130, labelW, rowH);
    baseNameEditor.setBounds (cx + labelW + 4,   130, cw - labelW - 4, rowH);

    // Save directory row
    chooseDirButton.setBounds (cx,               166, 90, rowH);
    dirLabel.setBounds         (cx + 90 + 6,     166, cw - 90 - 6, rowH);

    // Capture button + status
    captureButton.setBounds (cx,           220, 90, 30);
    statusLabel.setBounds   (cx + 90 + 12, 220, cw - 90 - 12, 30);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    // Header separator
    g.setColour (juce::Colours::grey.withAlpha (0.4f));
    g.drawLine (0.0f, static_cast<float> (kHeaderHeight),
                static_cast<float> (getWidth()), static_cast<float> (kHeaderHeight), 1.0f);

    // Column separators
    g.drawLine (static_cast<float> (kMeterWidth), static_cast<float> (kHeaderHeight),
                static_cast<float> (kMeterWidth), static_cast<float> (getHeight()), 1.0f);
    g.drawLine (static_cast<float> (getWidth() - kMeterWidth), static_cast<float> (kHeaderHeight),
                static_cast<float> (getWidth() - kMeterWidth), static_cast<float> (getHeight()), 1.0f);

    // IN / OUT labels
    g.setColour (juce::Colours::lightgrey);
    g.setFont (10.0f);
    g.drawText ("IN",  0, kHeaderHeight + 4, kMeterWidth, 16, juce::Justification::centred);
    g.drawText ("OUT", getWidth() - kMeterWidth, kHeaderHeight + 4, kMeterWidth, 16, juce::Justification::centred);
}

//==============================================================================
void MainComponent::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double sr        = device->getCurrentSampleRate();
    const int    blockSize = device->getCurrentBufferSizeSamples();
    irCapture.prepare (sr, blockSize);
    ioBuffer.setSize (1, blockSize);
}

void MainComponent::audioDeviceStopped()
{
}

void MainComponent::audioDeviceIOCallbackWithContext (
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    // Silence all outputs first
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            std::fill (outputChannelData[ch], outputChannelData[ch] + numSamples, 0.0f);

    // Use ch0 for in/out
    const float* inPtr  = (numInputChannels > 0 && inputChannelData[0] != nullptr)
                           ? inputChannelData[0] : nullptr;
    float*       outPtr = (numOutputChannels > 0 && outputChannelData[0] != nullptr)
                           ? outputChannelData[0] : nullptr;

    // Route mono output to both channels (left + right) so speakers play
    float* outPtr1 = (numOutputChannels > 1 && outputChannelData[1] != nullptr)
                      ? outputChannelData[1] : nullptr;

    if (inPtr == nullptr || outPtr == nullptr) return;

    ioBuffer.setSize (1, numSamples, false, false, true);
    irCapture.processBlock (inPtr, ioBuffer.getWritePointer (0), numSamples);

    juce::FloatVectorOperations::copy (outPtr, ioBuffer.getReadPointer (0), numSamples);
    if (outPtr1 != nullptr)
        juce::FloatVectorOperations::copy (outPtr1, ioBuffer.getReadPointer (0), numSamples);
}

//==============================================================================
void MainComponent::timerCallback()
{
    inMeter.setLevel  (irCapture.getInputLevel());
    outMeter.setLevel (irCapture.getOutputLevel());
    inMeter.timerTick();
    outMeter.timerTick();

    // Poll for completion / errors
    const auto phase = irCapture.getPhase();
    if (phase == CapturePhase::Processing)
    {
        setStatus ("Processing...");
    }
    else if (phase == CapturePhase::Done)
    {
        onCaptureComplete();
    }
    else if (phase == CapturePhase::Clipped)
    {
        setStatus ("! CLIPPING DETECTED — capture aborted");
        irCapture.resetToIdle();
        captureButton.setEnabled (true);
    }
}

//==============================================================================
void MainComponent::startCapture()
{
    const auto p = irCapture.getPhase();
    if (p != CapturePhase::Idle && p != CapturePhase::Clipped)
        return;

    captureButton.setEnabled (false);
    setStatus ("Capturing sweep...");
    irCapture.startCapture();
}

void MainComponent::onCaptureComplete()
{
    auto ir = irCapture.retrieveIR(); // also resets phase to Idle

    const juce::String baseName = baseNameEditor.getText().trim().isEmpty()
                                    ? "IR"
                                    : baseNameEditor.getText().trim();

    // Apply output gain from slider
    const float gainLinear = juce::Decibels::decibelsToGain ((float) gainSlider.getValue());
    if (gainLinear != 1.0f)
        ir.applyGain (gainLinear);

    // Normalize to 0.99 if requested
    if (normalizeButton.getToggleState())
    {
        float peak = 0.0f;
        for (int i = 0; i < ir.getNumSamples(); ++i)
            peak = std::max (peak, std::abs (ir.getSample (0, i)));
        if (peak > 0.0f)
            ir.applyGain (0.99f / peak);
    }

    // Ensure output directory exists
    if (! outputDir.isDirectory())
    {
        const auto result = outputDir.createDirectory();
        if (result.failed())
        {
            setStatus ("Error: could not create directory");
            captureButton.setEnabled (true);
            return;
        }
    }

    // Find the first available filename starting from captureIndex
    int usedIndex = captureIndex;
    juce::File outputFile;
    for (;;)
    {
        const juce::String filename = baseName + "_"
                                      + juce::String (usedIndex).paddedLeft ('0', 3)
                                      + ".wav";
        outputFile = outputDir.getChildFile (filename);
        if (! outputFile.existsAsFile())
            break;
        ++usedIndex;
    }

    const juce::String err = irCapture.saveToFile (ir, outputFile);

    if (err.isEmpty())
    {
        setStatus ("Saved: " + outputFile.getFileName());
        captureIndex = usedIndex + 1;
    }
    else
    {
        setStatus ("Error: " + err);
    }

    captureButton.setEnabled (true);
}

void MainComponent::setStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}

void MainComponent::chooseSaveDir()
{
    dirChooser = std::make_unique<juce::FileChooser> ("Select output directory", outputDir);

    dirChooser->launchAsync (juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectDirectories,
                             [this] (const juce::FileChooser& fc)
                             {
                                 const auto results = fc.getResults();
                                 if (! results.isEmpty())
                                 {
                                     outputDir = results[0];
                                     dirLabel.setText (outputDir.getFullPathName(),
                                                       juce::dontSendNotification);
                                 }
                             });
}

void MainComponent::openAudioSetup()
{
    juce::AudioDeviceSelectorComponent selector (deviceManager,
                                                  0,   // min input channels
                                                  2,   // max input channels
                                                  0,   // min output channels
                                                  2,   // max output channels
                                                  false, false, false, false);
    selector.setSize (500, 300);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (new juce::AudioDeviceSelectorComponent (
        deviceManager, 0, 2, 0, 2, false, false, false, false));
    opts.content->setSize (500, 300);
    opts.dialogTitle                = "Audio Setup";
    opts.dialogBackgroundColour     = getLookAndFeel().findColour (
        juce::ResizableWindow::backgroundColourId);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar            = true;
    opts.resizable                    = false;
    opts.launchAsync();
}
