#pragma once
#include <JuceHeader.h>

//==============================================================================
// FleshRenderProcessor
//
// Multiband Saturation / Distortion / Fuzz VST3 plugin.
//
// Signal path per band:
//   input --> LR4 crossover filters --> saturation --> distortion --> fuzz --> sum
//
// Crossover frequencies:
//   Low  band :     DC … 250 Hz
//   Mid  band : 250 Hz … 2 kHz
//   High band : 2 kHz … 20 kHz
//
// Linkwitz-Riley 4th-order (LR4) crossovers are implemented as two cascaded
// 2nd-order Butterworth IIR sections at the same corner frequency.
//==============================================================================
class FleshRenderProcessor : public juce::AudioProcessor
{
public:
    FleshRenderProcessor();
    ~FleshRenderProcessor() override;

    //==========================================================================
    // AudioProcessor overrides
    //==========================================================================
    void prepareToPlay   (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock    (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock; // avoid hiding the default no-op overload

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool   acceptsMidi() const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Parameter tree
    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    //==========================================================================
    // DSP types
    //==========================================================================
    using Filter    = juce::dsp::IIR::Filter<float>;
    using Coeffs    = juce::dsp::IIR::Coefficients<float>;
    using FilterDup = juce::dsp::ProcessorDuplicator<Filter, Coeffs>;

    // LR4 low-pass at 250 Hz  (stage1 × stage2 = 4th-order Butterworth LP)
    FilterDup lowLP1, lowLP2;

    // LR4 high-pass at 250 Hz for mid/high bands
    FilterDup midHP1, midHP2;

    // LR4 low-pass at 2 kHz for low/mid bands
    FilterDup midLP1, midLP2;

    // LR4 high-pass at 2 kHz for high band
    FilterDup highHP1, highHP2;

    // Temporary buffers — one per band, filled each block
    juce::AudioBuffer<float> lowBuf, midBuf, highBuf;

    double currentSampleRate = 44100.0;

    std::atomic<float>* outputLevelParam = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGainSmoothed;

    //==========================================================================
    // Waveshaping functions
    //==========================================================================

    // Soft saturation — tanh waveshaper normalised to unity at full drive.
    // drive range maps amount 0…1 to 1x…20x pre-gain.
    static inline float applySaturation (float x, float amount) noexcept
    {
        if (amount < 1e-4f) return x;
        const float drive  = 1.0f + amount * 19.0f;          // 1 … 20
        const float norm   = 1.0f / std::tanh (drive);
        return std::tanh (x * drive) * norm;
    }

    // Distortion — atan waveshaper, approaches square-wave at high drive.
    // drive range maps amount 0…1 to 1x…100x.
    static inline float applyDistortion (float x, float amount) noexcept
    {
        if (amount < 1e-4f) return x;
        const float drive = std::pow (100.0f, amount);        // 1 … 100
        return (2.0f / juce::MathConstants<float>::pi) * std::atan (x * drive);
    }

    // Fuzz — asymmetric hard-clip with DC bias for one-sided velcro character.
    // drive range maps amount 0…1 to 1x…200x.
    static inline float applyFuzz (float x, float amount) noexcept
    {
        if (amount < 1e-4f) return x;
        const float drive = std::pow (200.0f, amount);        // 1 … 200
        const float bias  = amount * 0.25f;                   // slight asymmetric push

        float driven = x * drive + bias;

        // Hard positive clip, slightly softer negative clip
        if (driven >  1.0f)  driven =  1.0f;
        if (driven < -0.75f) driven = -0.75f;

        // Remove most of the added bias from output
        return driven - bias * 0.6f;
    }

    // Apply the full chain (sat → dist → fuzz) to a single sample
    static inline float processChain (float x,
                                       float sat, float dist, float fuzz) noexcept
    {
        x = applySaturation (x, sat);
        x = applyDistortion (x, dist);
        x = applyFuzz        (x, fuzz);
        return x;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FleshRenderProcessor)
};
