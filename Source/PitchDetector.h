#pragma once

// ============================================================================
//  PitchDetector (v2) — YIN pitch tracker, voiced for guitar AND bass.
//
//  Runs on the MESSAGE thread (driven by the editor timer); the processor only
//  captures a lock-free ring of the dry input for it to read.
//
//  v2 changes over the old plain-autocorrelation detector:
//   * YIN (cumulative-mean-normalised difference, absolute-threshold FIRST
//     dip) instead of global-max autocorrelation — this is what kills the
//     octave/harmonic errors that plagued bass.
//   * The input is decimated x4 (with an anti-alias low-pass), so an 8192-
//     sample capture becomes a 2048-sample window at ~12 kHz: long enough to
//     see 5-string low B (30.9 Hz) with several periods, cheap enough to run
//     at UI rate.
//   * Range extended down to 25 Hz (drop-A / low-B safe), up to 1.2 kHz.
//
//  Returns the fundamental in Hz, or 0 when the signal is too quiet or not
//  clearly periodic. getConfidence()/getRms() expose the internals.
// ============================================================================

#include <cmath>
#include <vector>

#include <juce_core/juce_core.h>

class PitchDetector
{
public:
    void prepare (double sampleRate)
    {
        if (std::abs (sampleRate - mHostSR) < 1.0)
            return;
        mHostSR = sampleRate;
        // Decimate to ~8 kHz regardless of host rate: a longer window in
        // samples-of-analysis for the same capture, which is what low bass
        // needs (more periods in view), and cheaper YIN.
        mDecim  = juce::jmax (1, (int) std::round (sampleRate / 8000.0));
        mSR     = sampleRate / (double) mDecim;

        // Three cascaded one-pole low-passes at ~1.8 kHz for anti-aliasing —
        // we only track pitch below 1.2 kHz.
        const double fc = 1800.0;
        mLpCoef = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * fc / sampleRate);
        mLp1 = mLp2 = mLp3 = 0.0f;
    }

    // Analyse a mono window (length n, at the HOST rate). Returns Hz or 0.
    float detect (const float* x, int n)
    {
        if (n < 2048 || mHostSR <= 0.0)
            return fail();

        // ---- Decimate (anti-aliased) ----
        mDec.clear();
        mDec.reserve ((size_t) (n / mDecim) + 1);
        for (int i = 0; i < n; ++i)
        {
            mLp1 = x[i] + (mLp1 - x[i]) * mLpCoef;
            mLp2 = mLp1 + (mLp2 - mLp1) * mLpCoef;
            mLp3 = mLp2 + (mLp3 - mLp2) * mLpCoef;
            if (i % mDecim == 0)
                mDec.push_back (mLp3);
        }

        const int W = (int) mDec.size();
        const int tauMax = juce::jmin (W / 2, (int) (mSR / kMinFreq));
        const int tauMin = juce::jmax (2, (int) (mSR / kMaxFreq));
        if (tauMax <= tauMin + 2)
            return fail();

        // ---- Level check ----
        double energy = 0.0;
        for (float v : mDec) energy += (double) v * v;
        mRms = (float) std::sqrt (energy / (double) W);
        if (mRms < 2.0e-4f)
            return fail();

        // ---- YIN difference function ----
        const int span = W - tauMax;              // integration window
        if (span < 256)
            return fail();

        mDiff.assign ((size_t) tauMax + 1, 0.0f);
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            double s = 0.0;
            const float* a = mDec.data();
            const float* b = mDec.data() + tau;
            for (int i = 0; i < span; ++i)
            {
                const double d = (double) a[i] - (double) b[i];
                s += d * d;
            }
            mDiff[(size_t) tau] = (float) s;
        }

        // ---- Cumulative-mean normalisation (CMNDF) ----
        mCmndf.assign ((size_t) tauMax + 1, 1.0f);
        double runningSum = 0.0;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            runningSum += mDiff[(size_t) tau];
            mCmndf[(size_t) tau] = runningSum > 0.0
                ? (float) (mDiff[(size_t) tau] * (double) (tau - tauMin + 1) / runningSum)
                : 1.0f;
        }

        // ---- Absolute threshold: FIRST dip below threshold, refined to its
        //      local min (classic YIN anti-octave-error rule).
        constexpr float kThreshold = 0.15f;
        int tauEst = -1;
        for (int tau = tauMin + 1; tau < tauMax; ++tau)
        {
            if (mCmndf[(size_t) tau] < kThreshold)
            {
                while (tau + 1 < tauMax && mCmndf[(size_t) (tau + 1)] < mCmndf[(size_t) tau])
                    ++tau;
                tauEst = tau;
                break;
            }
        }

        // Global minimum (used both as fallback and for the bass octave guard).
        int   tauG  = -1;
        float bestG = 1.0f;
        for (int tau = tauMin + 1; tau < tauMax; ++tau)
            if (mCmndf[(size_t) tau] < bestG) { bestG = mCmndf[(size_t) tau]; tauG = tau; }

        if (tauEst < 0)
        {
            if (tauG < 0 || bestG > 0.30f)
                return fail();
            tauEst = tauG;
        }
        // ---- Bass octave guard ----
        // Plucked bass often has a 2nd harmonic strong enough that the FIRST
        // dip sits at T/2 (an octave high). If the deepest dip lies at ~an
        // integer multiple of the first dip's lag AND is clearly deeper, the
        // first dip was a harmonic — take the longer (lower) period.
        else if (tauG > tauEst + 2)
        {
            const double ratio = (double) tauG / (double) tauEst;
            const double frac  = std::abs (ratio - std::round (ratio));
            if (ratio >= 1.8 && ratio <= 4.2 && frac < 0.15
                && bestG + 0.04f < mCmndf[(size_t) tauEst])
                tauEst = tauG;
        }

        mConfidence = 1.0f - mCmndf[(size_t) tauEst];

        // ---- Parabolic interpolation for sub-sample lag ----
        double tau = (double) tauEst;
        if (tauEst > tauMin && tauEst < tauMax)
        {
            const double a = mCmndf[(size_t) (tauEst - 1)];
            const double b = mCmndf[(size_t) tauEst];
            const double c = mCmndf[(size_t) (tauEst + 1)];
            const double den = a - 2.0 * b + c;
            if (std::abs (den) > 1.0e-12)
                tau += juce::jlimit (-1.0, 1.0, 0.5 * (a - c) / den);
        }

        return tau > 0.0 ? (float) (mSR / tau) : fail();
    }

    float getConfidence() const { return mConfidence; }
    float getRms() const        { return mRms; }

private:
    float fail() { mConfidence = 0.0f; return 0.0f; }

    static constexpr double kMinFreq = 25.0;     // below 5-string low B (30.9 Hz)
    static constexpr double kMaxFreq = 1200.0;

    double mHostSR = 0.0, mSR = 8000.0;
    int    mDecim  = 6;
    float  mLpCoef = 0.0f, mLp1 = 0.0f, mLp2 = 0.0f, mLp3 = 0.0f;
    float  mConfidence = 0.0f, mRms = 0.0f;

    std::vector<float> mDec, mDiff, mCmndf;
};
