#pragma once

// ============================================================================
//  SagProcessor — tube power-amp "sag" simulation, placed immediately before
//  the cab IR.
//
//  In a real tube amp, hitting the power section hard makes the rectifier /
//  power supply voltage droop: the amp compresses, transients punch through
//  before the droop catches up, and notes "bloom" as the supply recovers.
//
//  Model: an envelope follower with a deliberately SLOW attack (the droop
//  lags the signal) and a musical release (the recovery bloom) drives a gain
//  reduction:   gain = 1 / (1 + depth * env).
//  Amount 0..10 maps to depth 0 (none) .. ~2.2 (extreme brown-out squish).
//
//  Stereo; the two channels can be linked (max of both envelopes drives both
//  gains — keeps a stereo image stable) or independent (dual-mono mode).
// ============================================================================

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

class SagProcessor
{
public:
    void prepare (double sampleRate)
    {
        mSR = sampleRate;
        // Droop onset ~35 ms, recovery ~280 ms.
        mAtk = std::exp (-1.0 / (0.035 * sampleRate));
        mRel = std::exp (-1.0 / (0.280 * sampleRate));
        reset();
    }

    void reset() { mEnv[0] = mEnv[1] = 0.0f; }

    // amount 0..10; linked: both channels share the louder envelope.
    void process (float* L, float* R, int n, float amount, bool linked)
    {
        if (amount <= 0.01f)
            return;

        const float depth = (amount / 10.0f) * 2.2f;
        float* ch[2] = { L, R };

        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float a = std::abs (ch[c][i]);
                const float coef = a > mEnv[c] ? (float) mAtk : (float) mRel;
                mEnv[c] = a + (mEnv[c] - a) * coef;
            }

            if (linked)
            {
                const float e = juce::jmax (mEnv[0], mEnv[1]);
                const float g = 1.0f / (1.0f + depth * e);
                L[i] *= g;
                R[i] *= g;
            }
            else
            {
                L[i] *= 1.0f / (1.0f + depth * mEnv[0]);
                R[i] *= 1.0f / (1.0f + depth * mEnv[1]);
            }
        }
    }

private:
    double mSR = 48000.0, mAtk = 0.999, mRel = 0.9999;
    float  mEnv[2] { 0.0f, 0.0f };
};
