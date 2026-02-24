#include "test_framework.h"
#include "GraphicEQ.h"
#include <cmath>
#include <vector>

TEST(graphiceq_flat_response_passthrough) {
    GraphicEQ eq;
    eq.prepare(44100.0, 512);

    const int N = 512;
    std::vector<float> left(N), right(N);
    std::vector<float> origLeft(N), origRight(N);

    for (int i = 0; i < N; ++i) {
        float val = static_cast<float>(std::sin(2.0 * M_PI * 440.0 * i / 44100.0));
        left[i] = origLeft[i] = val;
        right[i] = origRight[i] = val;
    }

    float* buffers[2] = { left.data(), right.data() };
    eq.processBlock(buffers, 2, N);

    double maxErr = 0.0;
    for (int i = 0; i < N; ++i) {
        double err = std::fabs(left[i] - origLeft[i]);
        if (err > maxErr) maxErr = err;
    }
    ASSERT_TRUE(maxErr < 1e-5);
}

TEST(graphiceq_band_count) {
    ASSERT_EQ(GraphicEQ::kNumBands, 12);
}

TEST(graphiceq_set_and_get_gain) {
    GraphicEQ eq;
    eq.prepare(44100.0, 512);

    eq.setBandGain(0, 6.0);
    ASSERT_NEAR(eq.getBandGain(0), 6.0, 1e-10);

    eq.setBandGain(5, -3.5);
    ASSERT_NEAR(eq.getBandGain(5), -3.5, 1e-10);
}

TEST(graphiceq_gain_clamping) {
    GraphicEQ eq;
    eq.prepare(44100.0, 512);

    eq.setBandGain(0, 99.0);
    ASSERT_NEAR(eq.getBandGain(0), GraphicEQ::kMaxGainDB, 1e-10);

    eq.setBandGain(0, -99.0);
    ASSERT_NEAR(eq.getBandGain(0), GraphicEQ::kMinGainDB, 1e-10);
}

TEST(graphiceq_flatten) {
    GraphicEQ eq;
    eq.prepare(44100.0, 512);

    for (int b = 0; b < GraphicEQ::kNumBands; ++b)
        eq.setBandGain(b, 6.0);

    eq.flatten();

    for (int b = 0; b < GraphicEQ::kNumBands; ++b)
        ASSERT_NEAR(eq.getBandGain(b), 0.0, 1e-10);
}

TEST(graphiceq_boost_audible) {
    GraphicEQ eq;
    eq.prepare(44100.0, 512);
    eq.setBandGain(8, 12.0); // Band 8 = 1 kHz

    const int settle = 4410;
    const int measure = 4410;
    const int total = settle + measure;

    std::vector<float> buf(total);
    for (int i = 0; i < total; ++i)
        buf[i] = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * i / 44100.0));

    std::vector<float> orig(buf);

    float* bufs[1] = { buf.data() };
    eq.processBlock(bufs, 1, total);

    double sumSqIn = 0.0, sumSqOut = 0.0;
    for (int i = settle; i < total; ++i) {
        sumSqIn += orig[i] * orig[i];
        sumSqOut += buf[i] * buf[i];
    }
    double rmsIn  = std::sqrt(sumSqIn / measure);
    double rmsOut = std::sqrt(sumSqOut / measure);
    double gainDB = 20.0 * std::log10(rmsOut / rmsIn);

    ASSERT_NEAR(gainDB, 12.0, 1.0);
}

TEST(graphiceq_mono_processing) {
    GraphicEQ eq;
    eq.prepare(44100.0, 256);
    eq.setBandGain(4, 3.0);

    std::vector<float> buf(256, 0.5f);
    float* bufs[1] = { buf.data() };
    eq.processBlock(bufs, 1, 256);

    bool hasNonZero = false;
    for (float v : buf) {
        if (std::fabs(v) > 1e-10) { hasNonZero = true; break; }
    }
    ASSERT_TRUE(hasNonZero);
}

TEST(graphiceq_reset_clears_state) {
    GraphicEQ eq;
    eq.prepare(44100.0, 512);
    eq.setBandGain(5, 12.0);

    std::vector<float> buf(512);
    for (int i = 0; i < 512; ++i)
        buf[i] = static_cast<float>(std::sin(2.0 * M_PI * 250.0 * i / 44100.0));
    float* bufs[1] = { buf.data() };
    eq.processBlock(bufs, 1, 512);

    eq.reset();

    std::vector<float> silence(512, 0.0f);
    float* silBufs[1] = { silence.data() };
    eq.processBlock(silBufs, 1, 512);

    double maxVal = 0.0;
    for (float v : silence)
        if (std::fabs(v) > maxVal) maxVal = std::fabs(v);
    ASSERT_TRUE(maxVal < 1e-10);
}

TEST(graphiceq_frequency_values) {
    ASSERT_NEAR(GraphicEQ::kBandFrequencies[0], 25.0, 1e-10);
    ASSERT_NEAR(GraphicEQ::kBandFrequencies[8], 1000.0, 1e-10);
    ASSERT_NEAR(GraphicEQ::kBandFrequencies[11], 16000.0, 1e-10);
}
