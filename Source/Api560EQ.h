#pragma once
#include <JuceHeader.h>

//==============================================================================
// Api560EQ — ten-band, octave-spaced graphic EQ in the spirit of the API 560,
// used here as the POST-saturation EQ. Ported from NECRONAM MAX.
//
// Bands are bell (peak) filters on ISO centre frequencies with a
// "proportional Q" characteristic: gentle boosts/cuts are broad, larger moves
// narrow up — the behaviour that gives the 560 its musical feel.
//
// Mono per instance (use one per channel). Zero added latency (minimum-phase
// IIR biquads), and bands sitting at 0 dB are skipped entirely so a mostly-flat
// curve costs almost nothing.
//==============================================================================
class Api560EQ
{
public:
    static constexpr int kNumBands = 10;

    // ISO octave centres, 31 Hz .. 16 kHz (classic 560 layout).
    static constexpr std::array<float, kNumBands> kFrequencies {
        31.25f, 62.5f, 125.0f, 250.0f, 500.0f,
        1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };

    void prepare (double sampleRate, int maxBlockSize)
    {
        mSampleRate = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        for (int i = 0; i < kNumBands; ++i)
        {
            mBands[(size_t) i].prepare (spec);
            mCachedGainDb[(size_t) i] = std::numeric_limits<float>::lowest();
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
        if (std::abs (gainDb - mCachedGainDb[(size_t) index]) > 1.0e-3f)
            updateBand (index, gainDb);
    }

    void process (float* data, int numSamples)
    {
        float* channels[1] = { data };
        juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        for (int i = 0; i < kNumBands; ++i)
            if (std::abs (mCachedGainDb[(size_t) i]) > 0.05f)   // skip flat bands
                mBands[(size_t) i].process (ctx);
    }

private:
    void updateBand (int index, float gainDb)
    {
        mCachedGainDb[(size_t) index] = gainDb;

        // Proportional Q: ~0.7 (broad) at 0 dB up to ~2.0 (focused) at +-12 dB.
        const float q = 0.7f + 1.3f * (std::abs (gainDb) / 12.0f);
        const float gainLin = juce::Decibels::decibelsToGain (gainDb);

        const float freq = juce::jmin (kFrequencies[(size_t) index],
                                       (float) (mSampleRate * 0.49));

        *mBands[(size_t) index].coefficients =
            *juce::dsp::IIR::Coefficients<float>::makePeakFilter (mSampleRate, freq, q, gainLin);
    }

    double mSampleRate = 44100.0;
    std::array<juce::dsp::IIR::Filter<float>, kNumBands> mBands;
    std::array<float, kNumBands> mCachedGainDb {};
};
