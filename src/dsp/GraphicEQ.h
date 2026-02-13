#pragma once

#include "Biquad.h"
#include <array>
#include <algorithm>
#include <cassert>

/// 12-band graphic equalizer.
///
/// ISO standard 1/3-octave center frequencies from 25 Hz to 16 kHz:
///   25, 40, 63, 100, 160, 250, 400, 630, 1k, 2.5k, 6.3k, 16k
///
/// Each band is a peaking EQ biquad filter. Bands are processed in
/// series so their responses sum in dB. Per-channel state is maintained
/// for up to kMaxChannels.
class GraphicEQ {
public:
    static constexpr int kNumBands    = 12;
    static constexpr int kMaxChannels = 2;

    /// ISO 1/3-octave center frequencies (Hz).
    static constexpr double kBandFrequencies[kNumBands] = {
        25.0, 40.0, 63.0, 100.0, 160.0, 250.0,
        400.0, 630.0, 1000.0, 2500.0, 6300.0, 16000.0
    };

    /// Default Q for 1/3-octave graphic EQ bands.
    static constexpr double kDefaultQ = 4.318;

    /// Gain range per band.
    static constexpr double kMinGainDB = -12.0;
    static constexpr double kMaxGainDB =  12.0;

    GraphicEQ() {
        for (auto& gains : mBandGains)
            gains.fill(0.0);
    }

    /// Prepare the processor for playback.
    /// @param sampleRate  Host sample rate in Hz
    /// @param maxBlockSize  Maximum expected block size (unused, reserved)
    void prepare(double sampleRate, int maxBlockSize) {
        mSampleRate = sampleRate;
        recalculateAllBands();
        reset();
    }

    /// Set the gain for a specific band.
    /// @param band   Band index [0, kNumBands)
    /// @param gainDB Gain in dB, clamped to [kMinGainDB, kMaxGainDB]
    void setBandGain(int band, double gainDB) {
        assert(band >= 0 && band < kNumBands);
        gainDB = std::clamp(gainDB, kMinGainDB, kMaxGainDB);
        for (int ch = 0; ch < kMaxChannels; ++ch) {
            mBandGains[ch][band] = gainDB;
            mFilters[ch][band].setParams(
                Biquad::Type::Peak,
                kBandFrequencies[band],
                kDefaultQ,
                gainDB,
                mSampleRate
            );
        }
    }

    /// Get the current gain for a band.
    double getBandGain(int band) const {
        assert(band >= 0 && band < kNumBands);
        return mBandGains[0][band];
    }

    /// Set all bands to 0 dB (flat response).
    void flatten() {
        for (int b = 0; b < kNumBands; ++b)
            setBandGain(b, 0.0);
    }

    /// Process a block of interleaved or non-interleaved audio.
    /// @param buffers    Array of channel buffer pointers
    /// @param numChannels  Number of channels (1 or 2)
    /// @param numSamples   Number of samples per channel
    void processBlock(float** buffers, int numChannels, int numSamples) {
        int chCount = std::min(numChannels, kMaxChannels);
        for (int ch = 0; ch < chCount; ++ch) {
            float* buf = buffers[ch];
            for (int b = 0; b < kNumBands; ++b) {
                mFilters[ch][b].processBlock(buf, numSamples);
            }
        }
    }

    /// Clear all filter states.
    void reset() {
        for (auto& channelFilters : mFilters)
            for (auto& filter : channelFilters)
                filter.reset();
    }

    double getSampleRate() const { return mSampleRate; }

private:
    void recalculateAllBands() {
        for (int ch = 0; ch < kMaxChannels; ++ch) {
            for (int b = 0; b < kNumBands; ++b) {
                mFilters[ch][b].setParams(
                    Biquad::Type::Peak,
                    kBandFrequencies[b],
                    kDefaultQ,
                    mBandGains[ch][b],
                    mSampleRate
                );
            }
        }
    }

    double mSampleRate = 44100.0;
    std::array<std::array<Biquad, kNumBands>, kMaxChannels> mFilters;
    std::array<std::array<double, kNumBands>, kMaxChannels> mBandGains;
};
