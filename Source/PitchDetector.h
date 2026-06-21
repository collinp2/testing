#pragma once

// ============================================================================
//  PitchDetector
//  Lightweight autocorrelation pitch tracker for the strobe tuner. Runs on the
//  MESSAGE thread (driven by the editor timer), not the audio thread — the
//  processor only captures a lock-free ring of the dry input for it to read.
//
//  Returns the detected fundamental in Hz, or 0 when the signal is too quiet or
//  not clearly periodic. Tuned for guitar/bass range (~40 Hz .. 1.5 kHz).
// ============================================================================

#include <cmath>
#include <vector>

#include <juce_core/juce_core.h>

class PitchDetector
{
public:
    void prepare (double sampleRate) { mSampleRate = sampleRate; }

    // Analyse a mono window (length n). Returns fundamental in Hz, or 0.
    float detect (const float* x, int n)
    {
        if (n < 1024 || mSampleRate <= 0.0)
            return 0.0f;

        mWork.resize ((size_t) n);

        // DC-remove and measure energy.
        double mean = 0.0;
        for (int i = 0; i < n; ++i) mean += x[i];
        mean /= (double) n;

        double energy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double v = (double) x[i] - mean;
            mWork[(size_t) i] = (float) v;
            energy += v * v;
        }

        const double rms = std::sqrt (energy / (double) n);
        if (rms < 3.0e-4)            // below the noise floor: report "no pitch"
            return 0.0f;

        const int minLag = juce::jmax (2, (int) (mSampleRate / kMaxFreq));
        const int maxLag = juce::jmin (n - 2, (int) (mSampleRate / kMinFreq));
        if (maxLag <= minLag)
            return 0.0f;

        auto autocorr = [this, n] (int lag) -> double
        {
            double s = 0.0;
            for (int i = 0; i + lag < n; ++i) s += (double) mWork[i] * mWork[i + lag];
            return s;
        };

        // Pick the lag with the strongest normalised autocorrelation.
        const double r0 = energy + 1.0e-12;
        double bestVal = 0.0;
        int    bestLag = -1;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const double nac = autocorr (lag) / r0;
            if (nac > bestVal) { bestVal = nac; bestLag = lag; }
        }

        if (bestLag < 1 || bestVal < 0.5)   // weak/ambiguous periodicity
            return 0.0f;

        // Parabolic interpolation around the peak for sub-sample accuracy.
        const double a = autocorr (bestLag - 1);
        const double b = autocorr (bestLag);
        const double cc = autocorr (bestLag + 1);
        const double denom = a - 2.0 * b + cc;
        const double shift = std::abs (denom) > 1.0e-12 ? 0.5 * (a - cc) / denom : 0.0;
        const double lag = (double) bestLag + juce::jlimit (-1.0, 1.0, shift);

        return lag > 0.0 ? (float) (mSampleRate / lag) : 0.0f;
    }

private:
    static constexpr double kMinFreq = 40.0;
    static constexpr double kMaxFreq = 1500.0;

    double mSampleRate = 48000.0;
    std::vector<float> mWork;
};
