#pragma once

// ============================================================================
//  SpringReverb — a lightweight spring-tank model for the FX section's
//  "Spring" reverb type (the "Plate" type is the tuned juce::dsp::Reverb).
//
//  Structure per channel (two slightly detuned "springs" for stereo):
//    input band-pass (springs only carry ~150 Hz .. 4.5 kHz)
//      -> feedback loop [ modulated delay ~40 ms
//                         -> 6 first-order allpasses (chirpy dispersion)
//                         -> damping low-pass ]
//    wet = loop output, mixed against dry by the caller's mix parameter.
//
//  The allpass cascade inside the loop is what gives the characteristic
//  spring "boing"/chirp; the slow +-0.4 ms modulation keeps it from ringing
//  metallically. No allocation in process(); everything is per-sample IIR.
// ============================================================================

#include <cmath>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

class SpringReverb
{
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        mSR = sampleRate;
        for (int c = 0; c < 2; ++c)
        {
            auto& s = mSpring[c];
            s.delayLen = (int) ((c == 0 ? 0.041 : 0.037) * sampleRate);
            s.buf.assign ((size_t) s.delayLen + 64, 0.0f);
            s.w = 0;
            for (auto& z : s.ap) z = 0.0f;
            s.lpState = 0.0f;
            s.hpState = 0.0f;
            s.inLp = 0.0f;
            s.lfoPhase = c == 0 ? 0.0f : 0.5f;
        }
    }

    void reset()
    {
        for (auto& s : mSpring)
        {
            std::fill (s.buf.begin(), s.buf.end(), 0.0f);
            for (auto& z : s.ap) z = 0.0f;
            s.lpState = s.hpState = s.inLp = 0.0f;
        }
    }

    // size 0..1 (loop feedback / decay), damp 0..1, mix 0..1 (equal-mix wet add).
    void process (float* L, float* R, int n, float size, float damp, float mix)
    {
        const float fb    = 0.45f + size * 0.42f;                 // decay
        const float lpCo  = 0.15f + (1.0f - damp) * 0.55f;        // loop damping
        const float lfoInc = (float) (0.9 / mSR);                 // ~0.9 Hz
        float* ch[2] = { L, R };

        for (int c = 0; c < 2; ++c)
        {
            auto& s = mSpring[c];
            float* d = ch[c];

            for (int i = 0; i < n; ++i)
            {
                // Input colouring: HPF ~150 Hz + LPF ~4.5 kHz (band-passed drive).
                const float x = d[i];
                s.hpState += 0.02f * (x - s.hpState);
                float inx = x - s.hpState;
                s.inLp += 0.45f * (inx - s.inLp);
                inx = s.inLp;

                // Modulated tap (+-0.4 ms) out of the delay.
                s.lfoPhase += lfoInc;
                if (s.lfoPhase >= 1.0f) s.lfoPhase -= 1.0f;
                const float modSamps = std::sin (s.lfoPhase * juce::MathConstants<float>::twoPi)
                                       * 0.0004f * (float) mSR;
                float rp = (float) s.w - (float) s.delayLen + modSamps;
                while (rp < 0.0f) rp += (float) s.buf.size();
                const int   r0 = (int) rp;
                const float fr = rp - (float) r0;
                const int   r1 = (r0 + 1) % (int) s.buf.size();
                float loop = s.buf[(size_t) r0] * (1.0f - fr) + s.buf[(size_t) r1] * fr;

                // Chirpy dispersion: 6 first-order allpasses.
                for (auto& z : s.ap)
                {
                    const float v = loop - 0.62f * z;
                    loop = z + 0.62f * v;
                    z = v;
                }

                // Loop damping.
                s.lpState += lpCo * (loop - s.lpState);
                loop = s.lpState;

                // Write back into the loop.
                s.buf[(size_t) s.w] = inx + loop * fb;
                s.w = (s.w + 1) % (int) s.buf.size();

                d[i] = x * (1.0f - mix) + loop * mix * 1.6f;
            }
        }
    }

private:
    struct Spring
    {
        std::vector<float> buf;
        int   delayLen = 1;
        int   w = 0;
        float ap[6] {};
        float lpState = 0.0f, hpState = 0.0f, inLp = 0.0f;
        float lfoPhase = 0.0f;
    };

    double mSR = 48000.0;
    Spring mSpring[2];
};
