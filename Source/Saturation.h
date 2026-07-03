#pragma once

// ============================================================================
//  Saturation  —  Flesh Render multiband saturator (v2 voicing).
//  Linkwitz-Riley 4th-order crossovers split the signal into three bands; the
//  crossover frequencies are SWEEPABLE (low/mid and mid/high knobs). Each band
//  runs saturation -> drive -> fuzz, then the bands are summed back
//  (complementary LR4 -> flat magnitude at unity settings).
//
//  v2 stage voicings (each level-compensated so engaging a stage doesn't jump
//  the volume):
//   * SATURATION — tape / transformer: tanh transfer with a small signal-
//     dependent bias for gentle even harmonics; soft, warm, compresses peaks.
//   * DRIVE — plain soft clipping (arctangent transfer): smooth odd-harmonic
//     overdrive, no hard edge.
//   * FUZZ — Big Muff style: two cascaded high-gain clipping stages
//     (soft stage into a harder limit) -> heavily sustained, wall-of-fuzz.
//
//  Mono per instance, no oversampling (zero latency). Extreme drive can alias
//  on bright material — same trade-off as the original Flesh Render.
// ============================================================================

#include <cmath>
#include <juce_dsp/juce_dsp.h>

class Saturation
{
public:
    // ----- Waveshapers (v2 voicing, level-compensated) ------------------------
    static inline float applySaturation (float x, float amount) noexcept
    {
        if (amount < 1.0e-4f) return x;
        const float drive = 1.0f + amount * 7.0f;             // 1 .. 8 (tape range)
        const float bias  = amount * 0.18f;                    // even-harmonic tilt
        const float norm  = 1.0f / std::tanh (drive);
        // Asymmetric tanh, re-centred so silence stays at zero (no DC).
        float y = (std::tanh (drive * x + bias) - std::tanh (bias)) * norm;
        return y * std::pow (drive, -0.45f) * (1.0f + amount * 0.35f);
    }

    static inline float applyDistortion (float x, float amount) noexcept
    {
        if (amount < 1.0e-4f) return x;
        const float drive = std::pow (30.0f, amount);          // 1 .. 30
        float y = (2.0f / juce::MathConstants<float>::pi) * std::atan (x * drive);
        return y * std::pow (drive, -0.5f) * (1.0f + amount * 0.6f);
    }

    static inline float applyFuzz (float x, float amount) noexcept
    {
        if (amount < 1.0e-4f) return x;
        const float g = 1.0f + amount * 60.0f;                 // 1 .. 61
        // Stage 1: soft transistor stage.
        float y = std::tanh (x * g);
        // Stage 2: driven again and limited harder (diode pair to the rails).
        y = std::tanh (y * 2.2f);
        y = juce::jlimit (-0.88f, 0.88f, y * 1.35f);
        return y * std::pow (g, -0.40f) * (1.0f + amount * 0.8f);
    }

    static inline float processChain (float x, float sat, float dist, float fuzz) noexcept
    {
        x = applySaturation (x, sat);
        x = applyDistortion (x, dist);
        x = applyFuzz        (x, fuzz);
        return x;                                              // order: sat -> drive -> fuzz
    }

    // ----- Lifecycle ---------------------------------------------------------
    void prepare (double sampleRate, int maxBlockSize)
    {
        mSampleRate = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        for (auto* f : { &lowLP1, &lowLP2, &midHP1, &midHP2, &midLP1, &midLP2, &highHP1, &highHP2 })
            f->prepare (spec);

        mXoverLowMid = mXoverMidHigh = -1.0f;   // force update
        setCrossovers (250.0f, 2000.0f);

        lowBuf.setSize  (1, maxBlockSize);
        midBuf.setSize  (1, maxBlockSize);
        highBuf.setSize (1, maxBlockSize);

        reset();
    }

    void reset()
    {
        for (auto* f : { &lowLP1, &lowLP2, &midHP1, &midHP2, &midLP1, &midLP2, &highHP1, &highHP2 })
            f->reset();
    }

    // Sweepable band split. lowMid and midHigh in Hz; midHigh is kept at least
    // an octave above lowMid.
    void setCrossovers (float lowMidHz, float midHighHz)
    {
        midHighHz = juce::jmax (midHighHz, lowMidHz * 2.0f);
        if (std::abs (lowMidHz - mXoverLowMid) < 0.5f && std::abs (midHighHz - mXoverMidHigh) < 0.5f)
            return;

        mXoverLowMid  = lowMidHz;
        mXoverMidHigh = midHighHz;

        const double fLowMid  = juce::jlimit (40.0,  1000.0, (double) lowMidHz);
        const double fMidHigh = juce::jlimit (500.0, 9000.0, (double) midHighHz);

        *lowLP1.coefficients  = *Coeffs::makeLowPass  (mSampleRate, fLowMid);
        *lowLP2.coefficients  = *Coeffs::makeLowPass  (mSampleRate, fLowMid);

        *midHP1.coefficients  = *Coeffs::makeHighPass (mSampleRate, fLowMid);
        *midHP2.coefficients  = *Coeffs::makeHighPass (mSampleRate, fLowMid);
        *midLP1.coefficients  = *Coeffs::makeLowPass  (mSampleRate, fMidHigh);
        *midLP2.coefficients  = *Coeffs::makeLowPass  (mSampleRate, fMidHigh);

        *highHP1.coefficients = *Coeffs::makeHighPass (mSampleRate, fMidHigh);
        *highHP2.coefficients = *Coeffs::makeHighPass (mSampleRate, fMidHigh);
    }

    struct BandParams { float sat = 0.0f, dist = 0.0f, fuzz = 0.0f; };

    void setParams (const BandParams& low, const BandParams& mid, const BandParams& high)
    {
        lowParams = low; midParams = mid; highParams = high;
    }

    void process (float* data, int numSamples)
    {
        auto* lo = lowBuf.getWritePointer (0);
        auto* md = midBuf.getWritePointer (0);
        auto* hi = highBuf.getWritePointer (0);

        for (int i = 0; i < numSamples; ++i)
            lo[i] = md[i] = hi[i] = data[i];

        // LR4 = two cascaded Butterworth stages per edge.
        filter (lowLP1, lo, numSamples);  filter (lowLP2, lo, numSamples);

        filter (midHP1, md, numSamples);  filter (midHP2, md, numSamples);
        filter (midLP1, md, numSamples);  filter (midLP2, md, numSamples);

        filter (highHP1, hi, numSamples); filter (highHP2, hi, numSamples);

        for (int i = 0; i < numSamples; ++i)
            data[i] = processChain (lo[i], lowParams.sat,  lowParams.dist,  lowParams.fuzz)
                    + processChain (md[i], midParams.sat,  midParams.dist,  midParams.fuzz)
                    + processChain (hi[i], highParams.sat, highParams.dist, highParams.fuzz);
    }

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    static void filter (Filter& f, float* data, int numSamples)
    {
        float* channels[1] = { data };
        juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        f.process (ctx);
    }

    double mSampleRate = 44100.0;
    float  mXoverLowMid = -1.0f, mXoverMidHigh = -1.0f;
    Filter lowLP1, lowLP2, midHP1, midHP2, midLP1, midLP2, highHP1, highHP2;
    juce::AudioBuffer<float> lowBuf, midBuf, highBuf;

    BandParams lowParams, midParams, highParams;
};
