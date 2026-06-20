#pragma once

// ============================================================================
//  Saturation  —  ported from CP Software "Flesh Render"
//  Three-band multiband saturator. Linkwitz-Riley 4th-order crossovers at
//  250 Hz and 2 kHz split the signal; each band runs the same
//  saturation -> distortion -> fuzz waveshaping chain, then the bands are
//  summed back (complementary LR4 -> flat magnitude).
//
//  Mono, no oversampling (zero latency) — matching the original Flesh Render
//  and honouring NECRONAM's low-latency goal. Bright material can alias on
//  extreme settings; that is the same trade-off as the original.
// ============================================================================

#include <cmath>
#include <juce_dsp/juce_dsp.h>

class Saturation
{
public:
    // ----- Waveshapers (verbatim from Flesh Render) --------------------------
    static inline float applySaturation (float x, float amount) noexcept
    {
        if (amount < 1.0e-4f) return x;
        const float drive = 1.0f + amount * 19.0f;          // 1 .. 20
        const float norm  = 1.0f / std::tanh (drive);
        return std::tanh (x * drive) * norm;
    }

    static inline float applyDistortion (float x, float amount) noexcept
    {
        if (amount < 1.0e-4f) return x;
        const float drive = std::pow (100.0f, amount);      // 1 .. 100
        return (2.0f / juce::MathConstants<float>::pi) * std::atan (x * drive);
    }

    static inline float applyFuzz (float x, float amount) noexcept
    {
        if (amount < 1.0e-4f) return x;
        const float drive = std::pow (200.0f, amount);      // 1 .. 200
        const float bias  = amount * 0.25f;                 // asymmetric push
        float driven = x * drive + bias;
        if (driven >  1.0f)  driven =  1.0f;                // hard positive clip
        if (driven < -0.75f) driven = -0.75f;               // softer negative clip
        return driven - bias * 0.6f;                        // remove most of bias
    }

    static inline float processChain (float x, float sat, float dist, float fuzz) noexcept
    {
        x = applySaturation (x, sat);
        x = applyDistortion (x, dist);
        x = applyFuzz        (x, fuzz);
        return x;                                           // order: sat -> dist -> fuzz
    }

    // ----- Lifecycle ---------------------------------------------------------
    void prepare (double sampleRate, int maxBlockSize)
    {
        mSampleRate = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        for (auto* f : { &lowLP1, &lowLP2, &midHP1, &midHP2, &midLP1, &midLP2, &highHP1, &highHP2 })
            f->prepare (spec);

        updateCrossovers();

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

    void updateCrossovers()
    {
        constexpr double fLowMid = 250.0;
        constexpr double fMidHigh = 2000.0;

        *lowLP1.coefficients  = *Coeffs::makeLowPass  (mSampleRate, fLowMid);
        *lowLP2.coefficients  = *Coeffs::makeLowPass  (mSampleRate, fLowMid);

        *midHP1.coefficients  = *Coeffs::makeHighPass (mSampleRate, fLowMid);
        *midHP2.coefficients  = *Coeffs::makeHighPass (mSampleRate, fLowMid);
        *midLP1.coefficients  = *Coeffs::makeLowPass  (mSampleRate, fMidHigh);
        *midLP2.coefficients  = *Coeffs::makeLowPass  (mSampleRate, fMidHigh);

        *highHP1.coefficients = *Coeffs::makeHighPass (mSampleRate, fMidHigh);
        *highHP2.coefficients = *Coeffs::makeHighPass (mSampleRate, fMidHigh);
    }

    static void filter (Filter& f, float* data, int numSamples)
    {
        float* channels[1] = { data };
        juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        f.process (ctx);
    }

    double mSampleRate = 44100.0;
    Filter lowLP1, lowLP2, midHP1, midHP2, midLP1, midLP2, highHP1, highHP2;
    juce::AudioBuffer<float> lowBuf, midBuf, highBuf;

    BandParams lowParams, midParams, highParams;
};
