#include "test_framework.h"
#include "plugin/PluginProcessor.h"
#include <cmath>
#include <vector>

TEST(processor_bypass) {
    PluginProcessor proc;
    proc.prepare(44100.0, 512);

    // Enable bypass
    proc.setParameter(PluginParams::kBypass, 1.0);
    ASSERT_TRUE(proc.isBypassed());

    // When bypassed, audio should pass through unchanged
    const int N = 256;
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[i] = static_cast<float>(std::sin(2.0 * M_PI * 440.0 * i / 44100.0));
    std::vector<float> orig(buf);

    float* bufs[1] = { buf.data() };
    proc.processBlock(bufs, 1, N);

    for (int i = 0; i < N; ++i)
        ASSERT_NEAR(buf[i], orig[i], 1e-10);
}

TEST(processor_bypass_toggle) {
    PluginProcessor proc;
    proc.prepare(44100.0, 512);

    proc.setParameter(PluginParams::kBypass, 1.0);
    ASSERT_TRUE(proc.isBypassed());
    ASSERT_NEAR(proc.getParameter(PluginParams::kBypass), 1.0, 1e-10);

    proc.setParameter(PluginParams::kBypass, 0.0);
    ASSERT_FALSE(proc.isBypassed());
    ASSERT_NEAR(proc.getParameter(PluginParams::kBypass), 0.0, 1e-10);
}

TEST(processor_set_band_gains) {
    PluginProcessor proc;
    proc.prepare(44100.0, 512);

    for (int b = 0; b < GraphicEQ::kNumBands; ++b) {
        double gain = (b % 2 == 0) ? 6.0 : -6.0;
        proc.setParameter(PluginParams::kBand1 + b, gain);
        ASSERT_NEAR(proc.getParameter(PluginParams::kBand1 + b), gain, 1e-10);
    }
}

TEST(processor_processes_audio) {
    PluginProcessor proc;
    proc.prepare(44100.0, 512);

    // Boost 1 kHz band
    proc.setParameter(PluginParams::kBand1 + 8, 12.0);

    const int N = 8820; // 200ms
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i)
        buf[i] = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * i / 44100.0));

    float* bufs[1] = { buf.data() };
    proc.processBlock(bufs, 1, N);

    // Measure RMS of last half (after settle)
    double sumSq = 0.0;
    int start = N / 2;
    for (int i = start; i < N; ++i)
        sumSq += buf[i] * buf[i];
    double rmsOut = std::sqrt(sumSq / (N - start));

    // RMS of a unit sine is ~0.707; with +12 dB boost it should be ~2.8
    ASSERT_TRUE(rmsOut > 2.0);
}

TEST(processor_stereo) {
    PluginProcessor proc;
    proc.prepare(44100.0, 256);
    proc.setParameter(PluginParams::kBand1 + 4, 6.0);

    const int N = 256;
    std::vector<float> left(N), right(N);
    for (int i = 0; i < N; ++i) {
        float val = static_cast<float>(std::sin(2.0 * M_PI * 160.0 * i / 44100.0));
        left[i] = val;
        right[i] = val;
    }

    float* bufs[2] = { left.data(), right.data() };
    proc.processBlock(bufs, 2, N);

    // Both channels should have the same output
    double maxDiff = 0.0;
    for (int i = 0; i < N; ++i) {
        double d = std::fabs(left[i] - right[i]);
        if (d > maxDiff) maxDiff = d;
    }
    ASSERT_TRUE(maxDiff < 1e-10);
}

TEST(processor_default_flat) {
    // All parameters should default to 0 (flat)
    PluginProcessor proc;
    proc.prepare(44100.0, 512);

    for (int b = 0; b < GraphicEQ::kNumBands; ++b)
        ASSERT_NEAR(proc.getParameter(PluginParams::kBand1 + b), 0.0, 1e-10);
    ASSERT_FALSE(proc.isBypassed());
}
