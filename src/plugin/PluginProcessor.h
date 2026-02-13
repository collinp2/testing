#pragma once

#include "../dsp/GraphicEQ.h"
#include "PluginParameters.h"

/// Core plugin processor — format-agnostic audio processing logic.
/// This is used by the AAX wrapper (and could also be wrapped for VST3, AU, etc.).
class PluginProcessor {
public:
    PluginProcessor() = default;

    void prepare(double sampleRate, int maxBlockSize) {
        mEQ.prepare(sampleRate, maxBlockSize);
        mBypassed = false;
    }

    void reset() {
        mEQ.reset();
    }

    /// Update a parameter by index.
    void setParameter(int paramIndex, double value) {
        if (paramIndex == PluginParams::kBypass) {
            mBypassed = (value >= 0.5);
            return;
        }
        int band = paramIndex - PluginParams::kBand1;
        if (band >= 0 && band < GraphicEQ::kNumBands) {
            mEQ.setBandGain(band, value);
        }
    }

    double getParameter(int paramIndex) const {
        if (paramIndex == PluginParams::kBypass)
            return mBypassed ? 1.0 : 0.0;
        int band = paramIndex - PluginParams::kBand1;
        if (band >= 0 && band < GraphicEQ::kNumBands)
            return mEQ.getBandGain(band);
        return 0.0;
    }

    /// Process audio buffers.
    void processBlock(float** buffers, int numChannels, int numSamples) {
        if (mBypassed)
            return;
        mEQ.processBlock(buffers, numChannels, numSamples);
    }

    bool isBypassed() const { return mBypassed; }
    const GraphicEQ& getEQ() const { return mEQ; }

private:
    GraphicEQ mEQ;
    bool mBypassed = false;
};
