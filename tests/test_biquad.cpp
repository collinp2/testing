#include "test_framework.h"
#include "dsp/Biquad.h"
#include <cmath>
#include <vector>

TEST(biquad_passthrough_at_zero_gain) {
    // A peaking filter at 0 dB gain should pass audio through unchanged.
    Biquad bq;
    bq.setParams(Biquad::Type::Peak, 1000.0, 1.0, 0.0, 44100.0);

    // Process a 1 kHz sine at 44.1 kHz
    const int N = 4410; // 100ms
    double maxError = 0.0;
    for (int i = 0; i < N; ++i) {
        double input = std::sin(2.0 * M_PI * 1000.0 * i / 44100.0);
        double output = bq.process(input);
        double err = std::fabs(output - input);
        if (err > maxError) maxError = err;
    }
    // At 0 dB gain the filter is unity — error should be negligible
    ASSERT_TRUE(maxError < 1e-10);
}

TEST(biquad_peak_boosts_center_frequency) {
    // A +12 dB peak at 1 kHz should amplify a 1 kHz signal significantly.
    Biquad bq;
    bq.setParams(Biquad::Type::Peak, 1000.0, 4.318, 12.0, 44100.0);

    // Let it settle, then measure RMS of last 1000 samples
    const int settle = 4410;
    const int measure = 4410;
    for (int i = 0; i < settle; ++i) {
        double input = std::sin(2.0 * M_PI * 1000.0 * i / 44100.0);
        bq.process(input);
    }

    double sumSqIn = 0.0, sumSqOut = 0.0;
    for (int i = 0; i < measure; ++i) {
        int t = settle + i;
        double input = std::sin(2.0 * M_PI * 1000.0 * t / 44100.0);
        double output = bq.process(input);
        sumSqIn += input * input;
        sumSqOut += output * output;
    }

    double rmsIn  = std::sqrt(sumSqIn / measure);
    double rmsOut = std::sqrt(sumSqOut / measure);
    double gainDB = 20.0 * std::log10(rmsOut / rmsIn);

    // Should be close to +12 dB
    ASSERT_NEAR(gainDB, 12.0, 0.5);
}

TEST(biquad_peak_cuts_center_frequency) {
    // A -12 dB peak at 1 kHz should attenuate a 1 kHz signal.
    Biquad bq;
    bq.setParams(Biquad::Type::Peak, 1000.0, 4.318, -12.0, 44100.0);

    const int settle = 4410;
    const int measure = 4410;
    for (int i = 0; i < settle; ++i) {
        double input = std::sin(2.0 * M_PI * 1000.0 * i / 44100.0);
        bq.process(input);
    }

    double sumSqIn = 0.0, sumSqOut = 0.0;
    for (int i = 0; i < measure; ++i) {
        int t = settle + i;
        double input = std::sin(2.0 * M_PI * 1000.0 * t / 44100.0);
        double output = bq.process(input);
        sumSqIn += input * input;
        sumSqOut += output * output;
    }

    double rmsIn  = std::sqrt(sumSqIn / measure);
    double rmsOut = std::sqrt(sumSqOut / measure);
    double gainDB = 20.0 * std::log10(rmsOut / rmsIn);

    ASSERT_NEAR(gainDB, -12.0, 0.5);
}

TEST(biquad_reset_clears_state) {
    Biquad bq;
    bq.setParams(Biquad::Type::Peak, 1000.0, 1.0, 6.0, 44100.0);

    // Push some signal through
    for (int i = 0; i < 100; ++i)
        bq.process(static_cast<double>(i) / 100.0);

    bq.reset();

    // After reset, processing silence should give silence
    double out = bq.process(0.0);
    ASSERT_NEAR(out, 0.0, 1e-15);
}

TEST(biquad_process_block) {
    Biquad bq;
    bq.setParams(Biquad::Type::Peak, 1000.0, 1.0, 0.0, 44100.0);

    const int N = 256;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[i] = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * i / 44100.0));

    std::vector<float> expected(buf);
    bq.processBlock(buf.data(), N);

    // At 0 dB, output should match input
    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) {
        double err = std::fabs(buf[i] - expected[i]);
        if (err > maxErr) maxErr = err;
    }
    ASSERT_TRUE(maxErr < 1e-5);
}

TEST(biquad_off_frequency_unaffected) {
    // A narrow peak at 1 kHz should leave a 100 Hz signal mostly unchanged.
    Biquad bq;
    bq.setParams(Biquad::Type::Peak, 1000.0, 4.318, 12.0, 44100.0);

    const int settle = 4410;
    const int measure = 4410;
    for (int i = 0; i < settle; ++i) {
        double input = std::sin(2.0 * M_PI * 100.0 * i / 44100.0);
        bq.process(input);
    }

    double sumSqIn = 0.0, sumSqOut = 0.0;
    for (int i = 0; i < measure; ++i) {
        int t = settle + i;
        double input = std::sin(2.0 * M_PI * 100.0 * t / 44100.0);
        double output = bq.process(input);
        sumSqIn += input * input;
        sumSqOut += output * output;
    }

    double rmsIn  = std::sqrt(sumSqIn / measure);
    double rmsOut = std::sqrt(sumSqOut / measure);
    double gainDB = 20.0 * std::log10(rmsOut / rmsIn);

    // 100 Hz is far from the 1 kHz peak, gain should be near 0 dB
    ASSERT_NEAR(gainDB, 0.0, 1.0);
}
