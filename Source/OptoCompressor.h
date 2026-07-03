#pragma once

// ============================================================================
//  OptoCompressor — one-knob LA-2A-style optical compressor (post graphic EQ).
//
//  Character notes taken from the LA-2A:
//   * program-dependent optical time constants: ~10 ms attack; a two-stage
//     release (a fast ~60 ms pool plus a slow multi-second "memory" pool)
//   * gentle ~3:1 ratio with a very soft knee
//   * a single PEAK REDUCTION control (drives the threshold down) and
//     automatic make-up gain — no other knobs.
//
//  Stereo-linked (max of both channels feeds one detector, one gain — keeps
//  the image stable). Exposes the current gain reduction in dB for the meter.
// ============================================================================

#include <atomic>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

class OptoCompressor
{
public:
    void prepare (double sampleRate)
    {
        mAtk     = std::exp (-1.0 / (0.010 * sampleRate));   // ~10 ms
        mRelFast = std::exp (-1.0 / (0.060 * sampleRate));   // ~60 ms
        mRelSlow = std::exp (-1.0 / (2.500 * sampleRate));   // opto "memory"
        reset();
    }

    void reset()
    {
        mEnvFast = mEnvSlow = 0.0f;
        mGRdB.store (0.0f);
    }

    // peakReduction 0..100. Auto make-up is derived from the knob.
    void process (float* L, float* R, int n, float peakReduction)
    {
        if (peakReduction <= 0.5f)
        {
            mGRdB.store (0.0f);
            return;
        }

        const float k = peakReduction / 100.0f;
        const float threshDb = -6.0f - k * 30.0f;            // -6 .. -36 dBFS
        const float ratio    = 3.0f;
        const float kneeDb   = 10.0f;
        const float makeup   = juce::Decibels::decibelsToGain (k * 10.0f);  // auto make-up

        float maxGR = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float in = juce::jmax (std::abs (L[i]), std::abs (R[i]));

            // Two-pool optical detector.
            const float aF = in > mEnvFast ? mAtk : mRelFast;
            mEnvFast = in + (mEnvFast - in) * aF;
            const float aS = in > mEnvSlow ? mAtk : mRelSlow;
            mEnvSlow = in + (mEnvSlow - in) * aS;
            const float env = 0.6f * mEnvFast + 0.4f * mEnvSlow;

            const float envDb = juce::Decibels::gainToDecibels (env, -80.0f);

            // Soft-knee 3:1 gain computer.
            float grDb = 0.0f;
            const float over = envDb - threshDb;
            if (over > kneeDb * 0.5f)
                grDb = over * (1.0f - 1.0f / ratio);
            else if (over > -kneeDb * 0.5f)
            {
                const float t = over + kneeDb * 0.5f;
                grDb = (1.0f - 1.0f / ratio) * t * t / (2.0f * kneeDb);
            }

            maxGR = juce::jmax (maxGR, grDb);
            const float g = juce::Decibels::decibelsToGain (-grDb) * makeup;
            L[i] *= g;
            R[i] *= g;
        }

        // Peak GR this block, for the meter (read+decayed by the editor).
        float cur = mGRdB.load (std::memory_order_relaxed);
        if (maxGR > cur) mGRdB.store (maxGR, std::memory_order_relaxed);
    }

    // Read+reset the peak gain reduction (dB) since the last call.
    float fetchGainReductionDb() { return mGRdB.exchange (0.0f); }

private:
    float mAtk = 0.999f, mRelFast = 0.9995f, mRelSlow = 0.99999f;
    float mEnvFast = 0.0f, mEnvSlow = 0.0f;
    std::atomic<float> mGRdB { 0.0f };
};
