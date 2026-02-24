#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "GraphicEQ.h"

class GEQ12AudioProcessor : public juce::AudioProcessor {
public:
    GEQ12AudioProcessor();
    ~GEQ12AudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    /// Provides access to the APVTS for the editor and host automation.
    juce::AudioProcessorValueTreeState& getAPVTS() { return mAPVTS; }

    /// Band parameter IDs used by both processor and editor.
    static juce::String getBandParamID(int band);
    static juce::String getBandParamName(int band);

    static constexpr int kNumBands = GraphicEQ::kNumBands;

private:
    juce::AudioProcessorValueTreeState mAPVTS;
    GraphicEQ mEQ;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GEQ12AudioProcessor)
};
