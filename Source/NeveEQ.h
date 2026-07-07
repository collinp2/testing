#pragma once
#include <JuceHeader.h>

//==============================================================================
// NeveEQ — a 1073/1074-family channel EQ, placed BEFORE the saturation stage.
//
// Exact hardware control set:
//   HPF        : Off / 50 / 80 / 160 / 300 Hz     (18 dB/oct, 3rd-order)
//   LOW shelf  : 35 / 60 / 110 / 220 Hz            +-16 dB
//   MID bell   : 360 / 700 / 1.6k / 3.2k / 4.8k / 7.2k Hz   +-18 dB
//                (proportional Q — broad at small boosts, focused when pushed,
//                 the musical behaviour of the inductor mid band)
//   HIGH shelf : fixed 12 kHz                      +-16 dB
//   EQ in/out button
//
// Efficiency: every stage is a minimum-phase IIR (zero latency), coefficients
// are cached and only recomputed when a control actually moves, and stages at
// 0 dB (or HPF Off) are skipped entirely. Stereo via ProcessorDuplicator.
//==============================================================================
class NeveEQ
{
public:
    static constexpr float kHpfFreqs[4] = { 50.0f, 80.0f, 160.0f, 300.0f };
    static constexpr float kLowFreqs[4] = { 35.0f, 60.0f, 110.0f, 220.0f };
    static constexpr float kMidFreqs[6] = { 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };
    static constexpr float kHighFreq    = 12000.0f;

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        mSampleRate = spec.sampleRate;
        for (auto* f : { &mHpf2, &mHpf1, &mLow, &mMid, &mHigh })
            f->prepare (spec);
        // Force a full coefficient rebuild on the next setParams().
        mCache = Cache { -2, -2, -99.0f, -2, -99.0f, -99.0f };
        reset();
    }

    void reset()
    {
        for (auto* f : { &mHpf2, &mHpf1, &mLow, &mMid, &mHigh })
            f->reset();
    }

    // hpfIdx: -1 = Off, 0..3 into kHpfFreqs. Gains in dB.
    void setParams (int hpfIdx, int lowIdx, float lowDb, int midIdx, float midDb, float highDb)
    {
        auto changed = [] (float a, float b) { return std::abs (a - b) > 0.01f; };

        if (hpfIdx != mCache.hpfIdx && hpfIdx >= 0)
        {
            const double f = kHpfFreqs[juce::jlimit (0, 3, hpfIdx)];
            // 3rd-order Butterworth (18 dB/oct) = 2nd-order (Q = 1) + 1st-order.
            *mHpf2.state = *Coeffs::makeHighPass (mSampleRate, f, 1.0f);
            *mHpf1.state = *Coeffs::makeFirstOrderHighPass (mSampleRate, f);
        }
        mCache.hpfIdx = hpfIdx;

        if (lowIdx != mCache.lowIdx || changed (lowDb, mCache.lowDb))
        {
            const double f = kLowFreqs[juce::jlimit (0, 3, lowIdx)];
            *mLow.state = *Coeffs::makeLowShelf (mSampleRate, f, 0.707f,
                                                 juce::Decibels::decibelsToGain (lowDb));
            mCache.lowIdx = lowIdx;
            mCache.lowDb  = lowDb;
        }

        if (midIdx != mCache.midIdx || changed (midDb, mCache.midDb))
        {
            const double f = kMidFreqs[juce::jlimit (0, 5, midIdx)];
            // Proportional Q: ~0.7 broad at low gain, up to ~2.0 when pushed.
            const float q = 0.7f + 1.3f * (std::abs (midDb) / 18.0f);
            *mMid.state = *Coeffs::makePeakFilter (mSampleRate, f, q,
                                                   juce::Decibels::decibelsToGain (midDb));
            mCache.midIdx = midIdx;
            mCache.midDb  = midDb;
        }

        if (changed (highDb, mCache.highDb))
        {
            *mHigh.state = *Coeffs::makeHighShelf (mSampleRate, kHighFreq, 0.707f,
                                                   juce::Decibels::decibelsToGain (highDb));
            mCache.highDb = highDb;
        }
    }

    // True when the current settings would change the signal at all.
    bool wouldProcess() const
    {
        return mCache.hpfIdx >= 0
            || std::abs (mCache.lowDb)  > 0.05f
            || std::abs (mCache.midDb)  > 0.05f
            || std::abs (mCache.highDb) > 0.05f;
    }

    void process (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples)
    {
        juce::dsp::AudioBlock<float> blk (buffer.getArrayOfWritePointers(),
                                          (size_t) numChannels, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);

        // Each stage runs only when it does something (zero-cost when flat).
        if (mCache.hpfIdx >= 0)               { mHpf2.process (ctx); mHpf1.process (ctx); }
        if (std::abs (mCache.lowDb)  > 0.05f)   mLow.process (ctx);
        if (std::abs (mCache.midDb)  > 0.05f)   mMid.process (ctx);
        if (std::abs (mCache.highDb) > 0.05f)   mHigh.process (ctx);
    }

private:
    using Filter    = juce::dsp::IIR::Filter<float>;
    using Coeffs    = juce::dsp::IIR::Coefficients<float>;
    using FilterDup = juce::dsp::ProcessorDuplicator<Filter, Coeffs>;

    struct Cache
    {
        int   hpfIdx = -2;
        int   lowIdx = -2;
        float lowDb  = -99.0f;
        int   midIdx = -2;
        float midDb  = -99.0f;
        float highDb = -99.0f;
    };

    double    mSampleRate = 44100.0;
    FilterDup mHpf2, mHpf1, mLow, mMid, mHigh;
    Cache     mCache;
};
