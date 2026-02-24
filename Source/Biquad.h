#pragma once

#include <cmath>

/// Biquad filter implementing standard audio EQ cookbook forms.
/// Reference: Robert Bristow-Johnson's Audio EQ Cookbook.
class Biquad {
public:
    enum class Type {
        Peak,      // Peaking EQ (used for graphic EQ bands)
        LowShelf,
        HighShelf,
        LowPass,
        HighPass
    };

    Biquad() { reset(); }

    /// Recalculate coefficients for the given parameters.
    /// @param type     Filter type
    /// @param freq     Center/corner frequency in Hz
    /// @param q        Q factor (bandwidth)
    /// @param gainDB   Gain in dB (for peak/shelf types)
    /// @param sampleRate  Host sample rate in Hz
    void setParams(Type type, double freq, double q, double gainDB, double sampleRate) {
        double A  = std::pow(10.0, gainDB / 40.0);
        double w0 = 2.0 * M_PI * freq / sampleRate;
        double cosw0 = std::cos(w0);
        double sinw0 = std::sin(w0);
        double alpha = sinw0 / (2.0 * q);

        double b0, b1, b2, a0, a1, a2;

        switch (type) {
        case Type::Peak:
            b0 =  1.0 + alpha * A;
            b1 = -2.0 * cosw0;
            b2 =  1.0 - alpha * A;
            a0 =  1.0 + alpha / A;
            a1 = -2.0 * cosw0;
            a2 =  1.0 - alpha / A;
            break;

        case Type::LowShelf: {
            double sqrtA = std::sqrt(A);
            b0 =        A * ((A + 1.0) - (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha);
            b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
            b2 =        A * ((A + 1.0) - (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha);
            a0 =             (A + 1.0) + (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha;
            a1 =      -2.0 *((A - 1.0) + (A + 1.0) * cosw0);
            a2 =             (A + 1.0) + (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha;
            break;
        }

        case Type::HighShelf: {
            double sqrtA = std::sqrt(A);
            b0 =        A * ((A + 1.0) + (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
            b2 =        A * ((A + 1.0) + (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha);
            a0 =             (A + 1.0) - (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha;
            a1 =       2.0 *((A - 1.0) - (A + 1.0) * cosw0);
            a2 =             (A + 1.0) - (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha;
            break;
        }

        case Type::LowPass:
            b0 = (1.0 - cosw0) / 2.0;
            b1 =  1.0 - cosw0;
            b2 = (1.0 - cosw0) / 2.0;
            a0 =  1.0 + alpha;
            a1 = -2.0 * cosw0;
            a2 =  1.0 - alpha;
            break;

        case Type::HighPass:
            b0 =  (1.0 + cosw0) / 2.0;
            b1 = -(1.0 + cosw0);
            b2 =  (1.0 + cosw0) / 2.0;
            a0 =   1.0 + alpha;
            a1 =  -2.0 * cosw0;
            a2 =   1.0 - alpha;
            break;
        }

        // Normalize by a0
        mB0 = b0 / a0;
        mB1 = b1 / a0;
        mB2 = b2 / a0;
        mA1 = a1 / a0;
        mA2 = a2 / a0;
    }

    /// Process a single sample (Direct Form II Transposed).
    double process(double input) {
        double output = mB0 * input + mZ1;
        mZ1 = mB1 * input - mA1 * output + mZ2;
        mZ2 = mB2 * input - mA2 * output;
        return output;
    }

    /// Process a block of samples in-place.
    void processBlock(float* buffer, int numSamples) {
        for (int i = 0; i < numSamples; ++i) {
            buffer[i] = static_cast<float>(process(static_cast<double>(buffer[i])));
        }
    }

    /// Clear filter state (call on transport reset, etc.).
    void reset() {
        mZ1 = 0.0;
        mZ2 = 0.0;
    }

    // Accessors for testing
    double getB0() const { return mB0; }
    double getB1() const { return mB1; }
    double getB2() const { return mB2; }
    double getA1() const { return mA1; }
    double getA2() const { return mA2; }

private:
    // Coefficients (normalized)
    double mB0 = 1.0, mB1 = 0.0, mB2 = 0.0;
    double mA1 = 0.0, mA2 = 0.0;
    // State (Direct Form II Transposed)
    double mZ1 = 0.0, mZ2 = 0.0;
};
