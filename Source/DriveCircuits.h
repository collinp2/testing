#pragma once

// ============================================================================
//  DriveCircuits — the switchable pre-amp drive section.
//
//  Two circuit models, one active at a time (selected by a parameter):
//
//  * TCPreamp — inspired by the TC Electronic Integrated Preamplifier:
//    a very clean FET front end that only folds over gently at extreme input,
//    followed by an active 3-band EQ (bass / mid / treble) and output level.
//    Character: transparent, punchy, hi-fi — colour comes from the EQ.
//
//  * TubeScreamer — the generic TS topology: the drive stage boosts only the
//    content above ~720 Hz into a symmetric soft (diode-style) clipper — which
//    is where the classic mid-hump comes from — followed by the usual
//    first-order tone (treble roll-off) and output level.
//
//  Both are mono processes with per-lane state (two lanes, so the dual-mono
//  stereo input mode can run left/right independently). No oversampling
//  (matching the plugin's zero-latency goal); drive amounts are moderate, so
//  aliasing stays acceptable.
// ============================================================================

#include <cmath>
#include <juce_dsp/juce_dsp.h>

class DriveCircuits
{
public:
    enum Circuit { TCPreamp = 0, Screamer = 1 };

    struct TCParams
    {
        float gain   = 0.0f;   // 0..1
        float bassDb = 0.0f, midDb = 0.0f, trebleDb = 0.0f;   // ±12 dB
        float levelDb = 0.0f;
    };
    struct TSParams
    {
        float drive = 0.3f;    // 0..1
        float tone  = 0.5f;    // 0..1
        float levelDb = 0.0f;
    };

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        mSR = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, 512, 1 };
        for (int l = 0; l < 2; ++l)
        {
            for (auto* f : { &mBass[l], &mMid[l], &mTreble[l], &mTsHpf[l], &mTsTone[l] })
                f->prepare (spec);
        }
        mTcCached = TCParams { 1e9f, 1e9f, 1e9f, 1e9f, 1e9f };   // force first update
        mTsCachedTone = -1.0f;
        updateTsHpf();
        reset();
    }

    void reset()
    {
        for (int l = 0; l < 2; ++l)
            for (auto* f : { &mBass[l], &mMid[l], &mTreble[l], &mTsHpf[l], &mTsTone[l] })
                f->reset();
    }

    // ------------------------------------------------------------------ TC --
    void processTC (int lane, float* x, int n, const TCParams& p)
    {
        lane = juce::jlimit (0, 1, lane);
        updateTcEq (p);

        // FET input stage: clean gain, folding over only near the rails.
        const float g   = 1.0f + p.gain * 11.0f;            // up to ~+21 dB in
        const float comp = 1.0f / std::sqrt (g);            // keep loudness sane
        for (int i = 0; i < n; ++i)
        {
            float v = x[i] * g;
            // Gentle 3rd-order soft limit (transparent until |v| approaches 1).
            const float a = std::abs (v);
            if (a > 0.5f)
                v = (v > 0 ? 1.0f : -1.0f) * (0.5f + (1.0f - std::exp (-(a - 0.5f) * 1.6f)) / 1.6f);
            x[i] = v * comp;
        }

        // Active EQ.
        filt (mBass[lane],   x, n);
        filt (mMid[lane],    x, n);
        filt (mTreble[lane], x, n);

        juce::FloatVectorOperations::multiply (x, juce::Decibels::decibelsToGain (p.levelDb), n);
    }

    // ------------------------------------------------------------------ TS --
    void processTS (int lane, float* x, int n, const TSParams& p)
    {
        lane = juce::jlimit (0, 1, lane);
        updateTsTone (p.tone);

        const float g    = 1.0f + p.drive * 45.0f;          // clipper drive
        const float comp = 1.0f / std::pow (g, 0.55f);      // level compensation

        for (int i = 0; i < n; ++i)
        {
            const float dry = x[i];
            // Like the real op-amp stage: the full-range signal passes at unity
            // into the clipper and only the >~720 Hz content gets the drive
            // boost (that's the mid-hump). The HPF is FIRST-ORDER (a real RC),
            // so low = dry - hp is its exact complement.
            float hp = dry;
            filt1 (mTsHpf[lane], hp);
            const float low = dry - hp;
            float v = dry + hp * g;
            // Symmetric diode-style soft clip.
            v = std::tanh (v * 0.9f) * 1.1f;
            // Frequency-aware compensation: the auto level comp must not eat
            // the unity-gain lows (the real pedal's LEVEL knob restores them
            // alongside the clipped mids). Add the clean low path back in so
            // bass stays at unity regardless of drive.
            x[i] = v * comp + low * (1.0f - comp);
        }

        // Tone: first-order treble roll-off, swept by the tone knob.
        filt (mTsTone[lane], x, n);

        juce::FloatVectorOperations::multiply (x, juce::Decibels::decibelsToGain (p.levelDb), n);
    }

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    static void filt (Filter& f, float* d, int n)
    {
        float* ch[1] = { d };
        juce::dsp::AudioBlock<float> b (ch, 1, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (b);
        f.process (ctx);
    }
    static void filt1 (Filter& f, float& s) { s = f.processSample (s); }

    void updateTcEq (const TCParams& p)
    {
        auto changed = [] (float a, float b) { return std::abs (a - b) > 0.01f; };
        if (changed (p.bassDb, mTcCached.bassDb))
        {
            auto co = Coeffs::makeLowShelf (mSR, 100.0, 0.707f, juce::Decibels::decibelsToGain (p.bassDb));
            *mBass[0].coefficients = *co;  *mBass[1].coefficients = *co;
            mTcCached.bassDb = p.bassDb;
        }
        if (changed (p.midDb, mTcCached.midDb))
        {
            auto co = Coeffs::makePeakFilter (mSR, 650.0, 0.8f, juce::Decibels::decibelsToGain (p.midDb));
            *mMid[0].coefficients = *co;   *mMid[1].coefficients = *co;
            mTcCached.midDb = p.midDb;
        }
        if (changed (p.trebleDb, mTcCached.trebleDb))
        {
            auto co = Coeffs::makeHighShelf (mSR, 3200.0, 0.707f, juce::Decibels::decibelsToGain (p.trebleDb));
            *mTreble[0].coefficients = *co; *mTreble[1].coefficients = *co;
            mTcCached.trebleDb = p.trebleDb;
        }
    }

    void updateTsHpf()
    {
        // First-order, like the RC network in the real drive stage (and so
        // that dry - hp is an exact complementary low path).
        auto co = Coeffs::makeFirstOrderHighPass (mSR, 720.0);
        *mTsHpf[0].coefficients = *co;
        *mTsHpf[1].coefficients = *co;
    }

    void updateTsTone (float tone)
    {
        if (std::abs (tone - mTsCachedTone) < 0.005f)
            return;
        mTsCachedTone = tone;
        const double f = 500.0 * std::pow (10.0, (double) tone);   // 500 Hz .. 5 kHz
        auto co = Coeffs::makeLowPass (mSR, juce::jmin (f, mSR * 0.45));
        *mTsTone[0].coefficients = *co;
        *mTsTone[1].coefficients = *co;
    }

    double mSR = 48000.0;
    Filter mBass[2], mMid[2], mTreble[2];     // TC active EQ
    Filter mTsHpf[2], mTsTone[2];             // TS drive HPF + tone
    TCParams mTcCached;
    float mTsCachedTone = -1.0f;
};
