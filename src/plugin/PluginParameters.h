#pragma once

#include "../dsp/GraphicEQ.h"
#include <string>
#include <array>

/// Parameter IDs for the 12-band graphic EQ plugin.
/// AAX parameters are identified by an integer index.
namespace PluginParams {

    static constexpr int kNumParams = GraphicEQ::kNumBands + 1; // bands + bypass

    // Parameter indices
    static constexpr int kBypass = 0;
    // Bands are indices 1..12
    static constexpr int kBand1  = 1;
    static constexpr int kBand2  = 2;
    static constexpr int kBand3  = 3;
    static constexpr int kBand4  = 4;
    static constexpr int kBand5  = 5;
    static constexpr int kBand6  = 6;
    static constexpr int kBand7  = 7;
    static constexpr int kBand8  = 8;
    static constexpr int kBand9  = 9;
    static constexpr int kBand10 = 10;
    static constexpr int kBand11 = 11;
    static constexpr int kBand12 = 12;

    /// Human-readable band labels.
    inline const char* getBandLabel(int band) {
        static const char* labels[GraphicEQ::kNumBands] = {
            "25 Hz",  "40 Hz",  "63 Hz",  "100 Hz",
            "160 Hz", "250 Hz", "400 Hz", "630 Hz",
            "1 kHz",  "2.5 kHz","6.3 kHz","16 kHz"
        };
        if (band >= 0 && band < GraphicEQ::kNumBands)
            return labels[band];
        return "???";
    }

    /// Get the parameter name for display.
    inline const char* getParamName(int paramIndex) {
        if (paramIndex == kBypass) return "Bypass";
        if (paramIndex >= kBand1 && paramIndex <= kBand12)
            return getBandLabel(paramIndex - kBand1);
        return "Unknown";
    }

    /// Default values.
    inline double getDefaultValue(int paramIndex) {
        if (paramIndex == kBypass) return 0.0; // off
        return 0.0; // 0 dB gain
    }

    /// Min values.
    inline double getMinValue(int paramIndex) {
        if (paramIndex == kBypass) return 0.0;
        return GraphicEQ::kMinGainDB;
    }

    /// Max values.
    inline double getMaxValue(int paramIndex) {
        if (paramIndex == kBypass) return 1.0;
        return GraphicEQ::kMaxGainDB;
    }

} // namespace PluginParams
