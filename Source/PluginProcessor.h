#pragma once
#include <JuceHeader.h>
#include "NeveEQ.h"
#include "Api560EQ.h"

//==============================================================================
// FleshRenderProcessor  (v2)
//
// Signal chain (all minimum-phase IIR / per-sample waveshaping — ZERO latency,
// no oversampling, every section skipped entirely when it would do nothing):
//
//   input
//     --> NEVE 1073/74-STYLE EQ   (HPF + low shelf + mid bell + 12k shelf)
//     --> MULTIBAND SATURATION    (LR4 split, SWEEPABLE crossovers,
//                                  sat -> drive -> fuzz per band, v2 realistic
//                                  level-compensated curves, wet/dry MIX)
//     --> API-560 GRAPHIC EQ      (10 octave bands, +-12 dB, proportional Q)
//     --> post HI-PASS / LO-PASS
//     --> master OUTPUT LEVEL (smoothed) --> peak meter tap
//
// v2 stage voicings (ported from NECRONAM MAX, each level-compensated so
// engaging a stage doesn't jump the volume):
//   SAT   — tape / transformer: tanh with a small bias for gentle even
//           harmonics; soft, warm, compresses peaks.
//   DRIVE — plain soft clipping (arctangent): smooth odd-harmonic overdrive.
//           (parameter ids remain *_dist for session compatibility)
//   FUZZ  — Big Muff style: two cascaded high-gain clipping stages.
//==============================================================================
class FleshRenderProcessor : public juce::AudioProcessor
{
public:
    FleshRenderProcessor();
    ~FleshRenderProcessor() override;

    //==========================================================================
    void prepareToPlay   (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock    (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

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

    // Output meter tap (peak since last read, linear). Read+reset by the editor.
    float fetchOutputPeak() { return outputPeak.exchange (0.0f); }

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==========================================================================
    // Waveshaping (v2 voicing, level-compensated) — shared with the editor for
    // any future curve displays.
    //==========================================================================
    static inline float applySaturation (float x, float amount) noexcept
    {
        if (amount < 1e-4f) return x;
        const float drive = 1.0f + amount * 7.0f;             // 1 .. 8 (tape range)
        const float bias  = amount * 0.18f;                    // even-harmonic tilt
        const float norm  = 1.0f / std::tanh (drive);
        float y = (std::tanh (drive * x + bias) - std::tanh (bias)) * norm;
        return y * std::pow (drive, -0.45f) * (1.0f + amount * 0.35f);
    }

    static inline float applyDistortion (float x, float amount) noexcept
    {
        if (amount < 1e-4f) return x;
        const float drive = std::pow (30.0f, amount);          // 1 .. 30
        float y = (2.0f / juce::MathConstants<float>::pi) * std::atan (x * drive);
        return y * std::pow (drive, -0.5f) * (1.0f + amount * 0.6f);
    }

    static inline float applyFuzz (float x, float amount) noexcept
    {
        if (amount < 1e-4f) return x;
        const float g = 1.0f + amount * 60.0f;                 // 1 .. 61
        float y = std::tanh (x * g);                           // stage 1: soft
        y = std::tanh (y * 2.2f);                              // stage 2: driven again
        y = juce::jlimit (-0.88f, 0.88f, y * 1.35f);           // diode pair to the rails
        return y * std::pow (g, -0.40f) * (1.0f + amount * 0.8f);
    }

    static inline float processChain (float x, float sat, float dist, float fuzz) noexcept
    {
        x = applySaturation (x, sat);
        x = applyDistortion (x, dist);
        x = applyFuzz        (x, fuzz);
        return x;
    }

private:
    //==========================================================================
    using Filter    = juce::dsp::IIR::Filter<float>;
    using Coeffs    = juce::dsp::IIR::Coefficients<float>;
    using FilterDup = juce::dsp::ProcessorDuplicator<Filter, Coeffs>;

    void updateCrossovers (float lowMidHz, float midHighHz);

    // ---- Pre EQ (Neve 1073/74 style) ----------------------------------------
    NeveEQ neveEQ;

    // ---- Multiband crossovers (LR4, sweepable) ------------------------------
    FilterDup lowLP1, lowLP2;
    FilterDup midHP1, midHP2;
    FilterDup midLP1, midLP2;
    FilterDup highHP1, highHP2;
    float xoverLowCached  = -1.0f;
    float xoverHighCached = -1.0f;
    bool  bandsWereActive = false;   // reset the split filters on re-engage

    juce::AudioBuffer<float> lowBuf, midBuf, highBuf;
    juce::AudioBuffer<float> dryBuf;                 // wet/dry MIX scratch

    // ---- Post EQ (API-560 style, one instance per channel) ------------------
    Api560EQ geq[2];

    // ---- Post filters ---------------------------------------------------------
    FilterDup postHPF, postLPF;
    float postHpfCached = -1.0f;
    float postLpfCached = -1.0f;

    double currentSampleRate = 44100.0;

    std::atomic<float>* outputLevelParam = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGainSmoothed;

    // Output peak accumulator (read+reset by the editor's meter timer).
    std::atomic<float> outputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FleshRenderProcessor)
};
