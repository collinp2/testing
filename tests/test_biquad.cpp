#include "test_framework.h"
#include "Biquad.h"
#include <cmath>
#include <vector>

TEST(biquad_passthrough_at_zero_gain) {
    Biquad bq;
    bq.setParams(Biquad::Type::Peak, 1000.0, 1.0, 0.0, 44100.0);

    const int N = 4410;
    double maxError = 0.0;
    for (int i = 0; i < N; ++i) {
        double input = std::sin(2.0 * M_PI * 1000.0 * i / 44100.0);
        double output = bq.process(input);
        double err = std::fabs(output - input);
        if (err > maxError) maxError = err;
    }
    ASSERT_TRUE(maxError < 1e-10);
}

TEST(biquad_peak_boosts_center_frequency) {
    Biquad bq;
    bq.setParams(Biquad::Type::Peak, 1000.0, 4.318, 12.0, 44100.0);

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

    ASSERT_NEAR(gainDB, 12.0, 0.5);
}

TEST(biquad_peak_cuts_center_frequency) {
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

    for (int i = 0; i < 100; ++i)
        bq.process(static_cast<double>(i) / 100.0);

    bq.reset();

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

    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) {
        double err = std::fabs(buf[i] - expected[i]);
        if (err > maxErr) maxErr = err;
    }
    ASSERT_TRUE(maxErr < 1e-5);
}

TEST(biquad_off_frequency_unaffected) {
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

    ASSERT_NEAR(gainDB, 0.0, 1.0);
}
