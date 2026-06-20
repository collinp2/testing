#pragma once

// ============================================================================
//  ResamplingNAM
//  Wraps a nam::DSP model and transparently resamples host audio to/from the
//  model's native sample rate using JUCE's windowed-sinc interpolators. When
//  host SR matches the model SR (the common, lowest-latency case) the resampler
//  is bypassed and reported latency is zero.
//
//  Pure JUCE — no AudioDSPTools/iPlug2 dependency.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "NAM/dsp.h"
#include "NAM/slimmable.h"

class ResamplingNAM
{
public:
    explicit ResamplingNAM (std::unique_ptr<nam::DSP> model)
        : mModel (std::move (model))
    {
        if (mModel != nullptr)
            mExpectedSampleRate = mModel->GetExpectedSampleRate();
    }

    // Off-thread only (allocates). Call after loading, or in prepareToPlay.
    void Reset (double hostSampleRate, int maxBlockSize)
    {
        mHostSR   = hostSampleRate;
        mMaxBlock = maxBlockSize;
        mModelSR  = (mExpectedSampleRate > 0.0) ? mExpectedSampleRate : hostSampleRate;
        mBypass   = std::abs (mModelSR - mHostSR) < 1.0;

        mUp.reset();
        mDown.reset();
        mInFifo.clear();
        mOutFifo.clear();
        mModelAccum = 0.0;
        mMargin = (int) std::ceil (juce::WindowedSincInterpolator::getBaseLatency()) + 2;

        const int maxModel = (int) std::ceil (maxBlockSize * mModelSR / mHostSR) + 2 * mMargin + 4;
        mModelIn.assign  ((size_t) maxModel, 0.0f);
        mModelOut.assign ((size_t) maxModel, 0.0f);

        if (mBypass)
        {
            mModel->Reset (mHostSR, maxBlockSize);
            mLatency = 0;
        }
        else
        {
            mModel->Reset (mModelSR, maxModel);
            // Prime the output FIFO so the down-sampler never underruns.
            const int prime = (int) std::ceil (maxBlockSize * mModelSR / mHostSR) + 2 * mMargin;
            mOutFifo.assign ((size_t) prime, 0.0f);
            // Reported latency: the priming, expressed at the host rate.
            mLatency = (int) std::ceil ((double) prime * mHostSR / mModelSR);
        }
    }

    // Mono in / mono out, channel-pointer arrays of size 1 (NAM_SAMPLE == float).
    void process (NAM_SAMPLE** input, NAM_SAMPLE** output, int numFrames)
    {
        if (mModel == nullptr)
        {
            if (output[0] != input[0])
                std::copy (input[0], input[0] + numFrames, output[0]);
            return;
        }

        if (mBypass)
        {
            mModel->process (input, output, numFrames);
            return;
        }

        const float* in  = input[0];
        float*       out = output[0];

        // 1) Buffer host input.
        mInFifo.insert (mInFifo.end(), in, in + numFrames);

        // 2) Down host -> model: produce as many model samples as the buffered
        //    input safely allows (leaving the sinc margin so we never read OOB).
        const double upRatio = mHostSR / mModelSR;          // speedRatio (input/output)
        int numModel = (int) std::floor (((double) mInFifo.size() - mMargin) / upRatio);
        numModel = juce::jlimit (0, (int) mModelIn.size(), numModel);

        if (numModel > 0)
        {
            const int used = mUp.process (upRatio, mInFifo.data(), mModelIn.data(), numModel);
            mInFifo.erase (mInFifo.begin(), mInFifo.begin() + juce::jmin (used, (int) mInFifo.size()));

            // 3) Run the model at its native rate.
            NAM_SAMPLE* mi[1] = { mModelIn.data() };
            NAM_SAMPLE* mo[1] = { mModelOut.data() };
            mModel->process (mi, mo, numModel);

            // 4) Buffer model output.
            mOutFifo.insert (mOutFifo.end(), mModelOut.data(), mModelOut.data() + numModel);
        }

        // 5) Up model -> host: produce exactly numFrames. Guard against underrun.
        const double downRatio = mModelSR / mHostSR;
        const int need = (int) std::ceil (numFrames * downRatio) + mMargin;
        if ((int) mOutFifo.size() < need)
            mOutFifo.insert (mOutFifo.begin(), (size_t) (need - mOutFifo.size()), 0.0f);

        const int used2 = mDown.process (downRatio, mOutFifo.data(), out, numFrames);
        mOutFifo.erase (mOutFifo.begin(), mOutFifo.begin() + juce::jmin (used2, (int) mOutFifo.size()));
    }

    int    GetLatency() const          { return mLatency; }
    bool   HasLoudness() const         { return mModel && mModel->HasLoudness(); }
    double GetLoudness() const         { return mModel->GetLoudness(); }
    bool   HasInputLevel() const       { return mModel && mModel->HasInputLevel(); }
    double GetInputLevel() const       { return mModel->GetInputLevel(); }
    bool   HasOutputLevel() const      { return mModel && mModel->HasOutputLevel(); }
    double GetOutputLevel() const      { return mModel->GetOutputLevel(); }
    double GetExpectedSampleRate() const { return mExpectedSampleRate; }

    // ----- A2 "slimmable" quality control (no-op for A1; not RT-safe) --------
    nam::SlimmableModel* GetSlimmableModel()
    {
        return dynamic_cast<nam::SlimmableModel*> (mModel.get());
    }
    bool IsSlimmable() { return GetSlimmableModel() != nullptr; }
    void SetQuality (double size)
    {
        if (auto* s = GetSlimmableModel())
            s->SetSlimmableSize (size);
    }

private:
    std::unique_ptr<nam::DSP> mModel;

    juce::WindowedSincInterpolator mUp, mDown;
    std::vector<float> mInFifo, mOutFifo, mModelIn, mModelOut;

    double mExpectedSampleRate = -1.0;
    double mHostSR = 48000.0, mModelSR = 48000.0, mModelAccum = 0.0;
    int    mMaxBlock = 512, mMargin = 64, mLatency = 0;
    bool   mBypass = true;
};
