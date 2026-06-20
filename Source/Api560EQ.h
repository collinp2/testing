#pragma once

// ============================================================================
//  Api560EQ
//  Ten-band, octave-spaced graphic EQ in the spirit of the API 560.
//  Bands are bell (peak) filters on ISO centre frequencies with a
//  "proportional Q" characteristic: gentle boosts/cuts are broad, larger
//  moves narrow up — the behaviour that gives the 560 its musical feel.
//
//  Mono, zero added latency (minimum-phase IIR biquads).
// ============================================================================

#include <array>
#include <cmath>
#include <limits>
#include <juce_dsp/juce_dsp.h>

class Api560EQ
{
public:
    static constexpr int kNumBands = 10;

    // ISO octave centres, 31 Hz .. 16 kHz (classic 560 layout).
    static constexpr std::array<float, kNumBands> kFrequencies {
        31.25f, 62.5f, 125.0f, 250.0f, 500.0f,
        1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };

    void prepare (double sampleRate, int maxBlockSize, int /*numChannels*/ = 1)
    {
        mSampleRate = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        for (int i = 0; i < kNumBands; ++i)
        {
            mBands[i].prepare (spec);
            mCachedGainDb[i] = std::numeric_limits<float>::lowest(); // force update
            updateBand (i, 0.0f);
        }
    }

    void reset()
    {
        for (auto& b : mBands)
            b.reset();
    }

    // gainDb in [-12, +12].
    void setBandGain (int index, float gainDb)
    {
        if (std::abs (gainDb - mCachedGainDb[index]) > 1.0e-3f)
            updateBand (index, gainDb);
    }

    void process (float* data, int numSamples)
    {
        float* channels[1] = { data };
        juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        for (auto& b : mBands)
            b.process (ctx);
    }

private:
    void updateBand (int index, float gainDb)
    {
        mCachedGainDb[index] = gainDb;

        // Proportional Q: ~0.7 (broad) at 0 dB up to ~2.0 (focused) at ±12 dB.
        const float q = 0.7f + 1.3f * (std::abs (gainDb) / 12.0f);
        const float gainLin = juce::Decibels::decibelsToGain (gainDb);

        const float freq = juce::jmin (kFrequencies[(size_t) index],
                                       (float) (mSampleRate * 0.49));

        *mBands[index].coefficients =
            *juce::dsp::IIR::Coefficients<float>::makePeakFilter (mSampleRate, freq, q, gainLin);
    }

    double mSampleRate = 44100.0;
    std::array<juce::dsp::IIR::Filter<float>, kNumBands> mBands;
    std::array<float, kNumBands> mCachedGainDb {};
};
