#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <filesystem>

#include "NAM/get_dsp.h"
#include "NAM/model_config.h"
#include "NAM/wavenet/model.h"
#include "NAM/convnet.h"
#include "NAM/lstm.h"
#include "NAM/linear.h"
#include "NAM/container.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
    // NAM architectures self-register via anonymous static objects, which get
    // dead-stripped when the core is linked through JUCE's SharedCode static
    // library. Register them explicitly (also references the symbols so their
    // translation units aren't dropped). Without this, get_dsp() throws and
    // the model never loads.
    void ensureNamArchitecturesRegistered()
    {
        static const bool done = []
        {
            auto& reg = nam::ConfigParserRegistry::instance();
            auto tryReg = [&reg] (const char* name, nam::ConfigParserFunction fn)
            {
                try { reg.registerParser (name, std::move (fn)); }
                catch (const std::exception&) { /* already self-registered */ }
            };
            tryReg ("WaveNet",            nam::wavenet::create_config);
            tryReg ("ConvNet",            nam::convnet::create_config);
            tryReg ("LSTM",               nam::lstm::create_config);
            tryReg ("Linear",             nam::linear::create_config);
            tryReg ("SlimmableContainer", nam::container::create_config);
            return true;
        }();
        juce::ignoreUnused (done);
    }

    float blockPeak (const float* x, int n)
    {
        float m = 0.0f;
        for (int i = 0; i < n; ++i) m = juce::jmax (m, std::abs (x[i]));
        return m;
    }

    void accumulatePeak (std::atomic<float>& dst, float v)
    {
        float cur = dst.load (std::memory_order_relaxed);
        while (v > cur && ! dst.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }

    // Equal-power pan. pan in [-1, +1]: -1 = hard left, 0 = centre (-3 dB both
    // sides), +1 = hard right.
    void equalPowerPan (float pan, float& gainL, float& gainR) noexcept
    {
        pan = juce::jlimit (-1.0f, 1.0f, pan);
        const float t = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi; // 0..pi/2
        gainL = std::cos (t);
        gainR = std::sin (t);
    }

    juce::String dbToText (float v, int)  { return juce::String (v, 1) + " dB"; }
    juce::String hzToText (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    }
    juce::String pctToText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
}

// ===========================================================================
NecronamAudioProcessor::NecronamAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createLayout())
{
    // Watch the A2 quality control and the amp routing so we can apply them off
    // the audio thread (SetSlimmableSize / setLatencySamples are not RT-safe).
    apvts.addParameterListener (ParamID::quality,    this);
    apvts.addParameterListener (ParamID::ampRouting, this);
}

NecronamAudioProcessor::~NecronamAudioProcessor()
{
    apvts.removeParameterListener (ParamID::quality,    this);
    apvts.removeParameterListener (ParamID::ampRouting, this);
    cancelPendingUpdate();
}

// ===========================================================================
APVTS::ParameterLayout NecronamAudioProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto fParam = [] (const juce::String& id, const juce::String& name, Range range,
                      float def, std::function<juce::String (float, int)> fmt)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (fmt)));
    };
    auto bParam = [] (const juce::String& id, const juce::String& name, bool def)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
    };

    // ---- Levels / IO ----
    params.push_back (fParam (ParamID::inputLevel,  "Input Level",  Range (-20.0f, 20.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::namOutput,   "Amp Output",   Range (-40.0f, 40.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::outputLevel, "Output Level", Range (-40.0f, 40.0f, 0.1f), 0.0f, dbToText));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::outputMode, 1 }, "Output Mode",
        juce::StringArray { "Raw", "Normalized", "Calibrated" }, 0));
    params.push_back (fParam (ParamID::inputCal, "Input Calibration", Range (0.0f, 30.0f, 0.1f), 12.0f,
                              [] (float v, int) { return juce::String (v, 1) + " dBu"; }));
    params.push_back (fParam (ParamID::gateThresh, "Gate Threshold", Range (-100.0f, 0.0f, 0.5f), -80.0f, dbToText));
    params.push_back (bParam (ParamID::gateActive, "Gate", false));

    // A2 quality / efficiency. 0 = max efficiency (lite), 1 = max quality (full).
    // Shared across both amps; only affects A2 "slimmable" models.
    params.push_back (fParam (ParamID::quality, "Quality", Range (0.0f, 1.0f, 0.01f), 1.0f,
                              [] (float v, int)
                              {
                                  if (v >= 0.999f) return juce::String ("Max Quality");
                                  if (v <= 0.001f) return juce::String ("Max Efficiency");
                                  return juce::String (juce::roundToInt (v * 100.0f)) + "%";
                              }));

    // ---- Dual amp ----
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::ampRouting, 1 }, "Amp Routing",
        juce::StringArray { "Single", "Series", "Parallel" }, 0));
    params.push_back (fParam (ParamID::ampALevel, "Amp A Level", Range (-40.0f, 24.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::ampBLevel, "Amp B Level", Range (-40.0f, 24.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::ampSpread, "Amp Spread", Range (0.0f, 1.0f, 0.01f), 0.5f, pctToText));

    // ---- Dual cab IR mixer ----
    params.push_back (bParam (ParamID::cabAActive, "Cab A", true));
    params.push_back (bParam (ParamID::cabBActive, "Cab B", false));
    params.push_back (fParam (ParamID::cabALevel, "Cab A Level", Range (-40.0f, 12.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::cabBLevel, "Cab B Level", Range (-40.0f, 12.0f, 0.1f), 0.0f, dbToText));

    // ---- Filters ----
    params.push_back (fParam (ParamID::hpfFreq, "Hi-Pass", Range (20.0f, 2000.0f, 1.0f, 0.3f), 20.0f, hzToText));
    params.push_back (bParam (ParamID::hpfActive, "Hi-Pass On", false));
    params.push_back (fParam (ParamID::lpfFreq, "Low-Pass", Range (1000.0f, 20000.0f, 1.0f, 0.3f), 20000.0f, hzToText));
    params.push_back (bParam (ParamID::lpfActive, "Low-Pass On", false));

    // ---- API-560 EQ ----
    params.push_back (bParam (ParamID::eqActive, "EQ", false));
    for (int i = 0; i < Api560EQ::kNumBands; ++i)
    {
        const float f = Api560EQ::kFrequencies[(size_t) i];
        const juce::String name = (f >= 1000.0f ? juce::String (f / 1000.0f, (f == 16000.0f ? 0 : 1)) + "k"
                                                : juce::String (juce::roundToInt (f))) + " Hz";
        params.push_back (fParam (eqParamID (i), "EQ " + name, Range (-12.0f, 12.0f, 0.1f), 0.0f, dbToText));
    }

    // ---- Saturation (Flesh Render): front (pre-amp) + post (output) ----
    const char* bands[3]    = { "low", "mid", "high" };
    const char* bandsUp[3]  = { "Low", "Mid", "High" };
    const char* stages[3]   = { "sat", "dist", "fuzz" };
    const char* stagesUp[3] = { "Saturation", "Distortion", "Fuzz" };

    params.push_back (bParam (ParamID::frontSatActive, "Front Saturation", false));
    for (int b = 0; b < 3; ++b)
        for (int s = 0; s < 3; ++s)
            params.push_back (fParam (satParamID (true, bands[b], stages[s]),
                                      "Front " + juce::String (bandsUp[b]) + " " + stagesUp[s],
                                      Range (0.0f, 1.0f, 0.001f), 0.0f, pctToText));

    params.push_back (bParam (ParamID::satActive, "Saturation", false));
    for (int b = 0; b < 3; ++b)
        for (int s = 0; s < 3; ++s)
            params.push_back (fParam (satParamID (false, bands[b], stages[s]),
                                      juce::String (bandsUp[b]) + " " + stagesUp[s],
                                      Range (0.0f, 1.0f, 0.001f), 0.0f, pctToText));

    // ---- Tuner ----
    params.push_back (bParam (ParamID::tunerActive, "Tuner", false));

    // ---- Delay ----
    params.push_back (bParam (ParamID::delayActive, "Delay", false));
    params.push_back (fParam (ParamID::delayTime, "Delay Time", Range (1.0f, 2000.0f, 1.0f, 0.4f), 350.0f,
                              [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; }));
    params.push_back (fParam (ParamID::delayFeedback, "Delay Feedback", Range (0.0f, 0.95f, 0.001f), 0.35f, pctToText));
    params.push_back (fParam (ParamID::delayMix, "Delay Mix", Range (0.0f, 1.0f, 0.001f), 0.30f, pctToText));

    // ---- Reverb ----
    params.push_back (bParam (ParamID::reverbActive, "Reverb", false));
    params.push_back (fParam (ParamID::reverbSize, "Reverb Size", Range (0.0f, 1.0f, 0.001f), 0.5f, pctToText));
    params.push_back (fParam (ParamID::reverbDamp, "Reverb Damp", Range (0.0f, 1.0f, 0.001f), 0.5f, pctToText));
    params.push_back (fParam (ParamID::reverbMix,  "Reverb Mix",  Range (0.0f, 1.0f, 0.001f), 0.25f, pctToText));

    // ---- FX order ----
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::fxOrder, 1 }, "FX Order",
        juce::StringArray { "Delay -> Reverb", "Reverb -> Delay" }, 0));

    return { params.begin(), params.end() };
}

// ===========================================================================
void NecronamAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    mSampleRate = sampleRate;
    mMaxBlock   = samplesPerBlock;

    mMonoIn.setSize     (1, samplesPerBlock);
    mAmpAOut.setSize    (1, samplesPerBlock);
    mAmpBOut.setSize    (1, samplesPerBlock);
    mBus.setSize        (2, samplesPerBlock);
    mCabScratch.setSize (2, samplesPerBlock);
    mCabSum.setSize     (2, samplesPerBlock);

    juce::dsp::ProcessSpec monoSpec   { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    juce::dsp::ProcessSpec stereoSpec { sampleRate, (juce::uint32) samplesPerBlock, 2 };

    for (int ch = 0; ch < 2; ++ch)
    {
        mEQ[ch].prepare (sampleRate, samplesPerBlock);
        mSaturation[ch].prepare (sampleRate, samplesPerBlock);
        mHPF[ch].prepare (monoSpec);
        mLPF[ch].prepare (monoSpec);
        *mDCBlocker[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 10.0);
        mDCBlocker[ch].reset();
    }
    mHpfCachedFreq = mLpfCachedFreq = -1.0f;

    for (int c = 0; c < 2; ++c)
        mCab[c].conv.prepare (stereoSpec);

    // Front saturation (mono) + end-of-chain stereo FX.
    mFrontSat.prepare (sampleRate, samplesPerBlock);
    mReverb.prepare (stereoSpec);
    mReverb.reset();
    mDelay.prepare (stereoSpec);
    mDelay.reset();
    mFrontSatWasActive = mDelayWasActive = mReverbWasActive = false;

    // Tuner capture ring.
    mTunerRing.fill (0.0f);
    mTunerWrite.store (0);

    mGateEnv  = 0.0f;
    mGateGain = 1.0f;

    // Reset the active AND staged models for both amps. The staged one matters:
    // a model restored from state before prepareToPlay was staged without a
    // Reset, and must be sized here before it can be swapped in and processed.
    for (int a = 0; a < 2; ++a)
    {
        const juce::SpinLock::ScopedLockType l (mAmp[a].swapLock);
        if (mAmp[a].model  != nullptr) mAmp[a].model->Reset  (sampleRate, samplesPerBlock);
        if (mAmp[a].staged != nullptr) mAmp[a].staged->Reset (sampleRate, samplesPerBlock);
    }

    mInPeak.store (0.0f);
    mNamPeak.store (0.0f);
    mMasterPeak.store (0.0f);

    mPrepared.store (true);
    updateLatency();
}

bool NecronamAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono   = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();

    if (out != mono && out != stereo)                    return false;
    if (in != mono && in != stereo && ! in.isDisabled()) return false;
    return true;
}

// ===========================================================================
float NecronamAudioProcessor::computeOutputGain() const
{
    const int   mode     = (int) apvts.getRawParameterValue (ParamID::outputMode)->load();
    const float outDb    = apvts.getRawParameterValue (ParamID::outputLevel)->load();
    const float inputCal = apvts.getRawParameterValue (ParamID::inputCal)->load();

    float gain = juce::Decibels::decibelsToGain (outDb);

    // Normalized / Calibrated reference Amp A's model metadata (the primary amp).
    const auto& m = mAmp[0].model;
    if (mode == 1 && m != nullptr && m->HasLoudness())          // Normalized -> -18 dBFS
        gain *= juce::Decibels::decibelsToGain (-18.0f - (float) m->GetLoudness());
    else if (mode == 2 && m != nullptr && m->HasOutputLevel())  // Calibrated -> real dBu
        gain *= juce::Decibels::decibelsToGain ((float) m->GetOutputLevel() - inputCal);

    return gain;
}

void NecronamAudioProcessor::updateLatency()
{
    auto slotLatency = [this] (int a) -> int
    {
        const juce::SpinLock::ScopedLockType l (mAmp[a].swapLock);
        if (mAmp[a].staged != nullptr) return mAmp[a].staged->GetLatency();
        if (mAmp[a].model  != nullptr) return mAmp[a].model->GetLatency();
        return 0;
    };

    const int la = slotLatency (0);
    const int lb = slotLatency (1);
    const auto routing = (Routing) (int) apvts.getRawParameterValue (ParamID::ampRouting)->load();

    int latency = 0;
    switch (routing)
    {
        case Routing::Single:   latency = la;                  break;
        case Routing::Series:   latency = la + lb;             break;
        case Routing::Parallel: latency = juce::jmax (la, lb); break;
    }
    // NB: in Parallel, mismatched amp latencies are not internally compensated;
    // this only arises when the two models have different native sample rates.
    setLatencySamples (latency);
}

// ===========================================================================
void NecronamAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();

    // ---- Hot-swap staged models; honour clear requests (per amp) ----
    for (int a = 0; a < 2; ++a)
    {
        const juce::SpinLock::ScopedTryLockType l (mAmp[a].swapLock);
        if (l.isLocked() && mAmp[a].staged != nullptr)
            mAmp[a].model = std::move (mAmp[a].staged);
    }
    for (int a = 0; a < 2; ++a)
        if (mAmp[a].clear.exchange (false))
            mAmp[a].model.reset();

    // ---- Sum input to a mono amp-input buffer ----
    float* mono = mMonoIn.getWritePointer (0);
    if (numIn >= 2)
    {
        const float* L = buffer.getReadPointer (0);
        const float* R = buffer.getReadPointer (1);
        for (int i = 0; i < numSamples; ++i)
            mono[i] = 0.5f * (L[i] + R[i]);
    }
    else if (numIn == 1)
        juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0), numSamples);
    else
        juce::FloatVectorOperations::clear (mono, numSamples);

    // ---- Input gain (+ calibrated input alignment, referencing Amp A) ----
    const int   mode     = (int) apvts.getRawParameterValue (ParamID::outputMode)->load();
    const float inputCal = apvts.getRawParameterValue (ParamID::inputCal)->load();
    float inputGainDb    = apvts.getRawParameterValue (ParamID::inputLevel)->load();
    if (mode == 2 && mAmp[0].model != nullptr && mAmp[0].model->HasInputLevel())
        inputGainDb += inputCal - (float) mAmp[0].model->GetInputLevel();
    juce::FloatVectorOperations::multiply (mono, juce::Decibels::decibelsToGain (inputGainDb), numSamples);

    // NAM input meter (post input gain — what Amp A actually sees).
    accumulatePeak (mInPeak, blockPeak (mono, numSamples));

    // ---- Capture the dry input into the tuner ring (power-of-two, lock-free) ----
    {
        int w = mTunerWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            mTunerRing[(size_t) w] = mono[i];
            w = (w + 1) & (kTunerRing - 1);
        }
        mTunerWrite.store (w, std::memory_order_release);
    }

    // ---- Tuner: when engaged, mute the output entirely (silent tuning) ----
    if (apvts.getRawParameterValue (ParamID::tunerActive)->load() > 0.5f)
    {
        buffer.clear();
        mNamPeak.store (0.0f);
        mMasterPeak.store (0.0f);
        return;
    }

    // ---- Noise gate (simple downward gate on the pre-amp signal) ----
    if (apvts.getRawParameterValue (ParamID::gateActive)->load() > 0.5f)
    {
        const float threshLin = juce::Decibels::decibelsToGain (
            apvts.getRawParameterValue (ParamID::gateThresh)->load());
        const float envRel = std::exp (-1.0f / (0.050f * (float) mSampleRate)); // 50 ms
        const float openC  = std::exp (-1.0f / (0.005f * (float) mSampleRate)); // 5 ms
        const float closeC = std::exp (-1.0f / (0.100f * (float) mSampleRate)); // 100 ms
        for (int i = 0; i < numSamples; ++i)
        {
            const float a = std::abs (mono[i]);
            mGateEnv = juce::jmax (a, mGateEnv * envRel);
            const float target = mGateEnv >= threshLin ? 1.0f : 0.0f;
            const float c = (target < mGateGain) ? closeC : openC;
            mGateGain = target + (mGateGain - target) * c;
            mono[i] *= mGateGain;
        }
    }

    // ---- Front saturation (Flesh Render, mono) — immediately before the amps.
    //      True-bypass: not processed at all while off; reset on the off-edge.
    {
        const bool on = apvts.getRawParameterValue (ParamID::frontSatActive)->load() > 0.5f;
        if (on)
        {
            auto band = [this] (const char* b)
            {
                Saturation::BandParams p;
                p.sat  = apvts.getRawParameterValue (satParamID (true, b, "sat"))->load();
                p.dist = apvts.getRawParameterValue (satParamID (true, b, "dist"))->load();
                p.fuzz = apvts.getRawParameterValue (satParamID (true, b, "fuzz"))->load();
                return p;
            };
            mFrontSat.setParams (band ("low"), band ("mid"), band ("high"));
            mFrontSat.process (mono, numSamples);
        }
        else if (mFrontSatWasActive)
            mFrontSat.reset();
        mFrontSatWasActive = on;
    }

    // ---- DUAL AMP STAGE -> stereo bus (mBus ch0 = L, ch1 = R) ----
    const auto  routing = (Routing) (int) apvts.getRawParameterValue (ParamID::ampRouting)->load();
    const float levelA  = juce::Decibels::decibelsToGain (apvts.getRawParameterValue (ParamID::ampALevel)->load());
    const float levelB  = juce::Decibels::decibelsToGain (apvts.getRawParameterValue (ParamID::ampBLevel)->load());

    float* aOut = mAmpAOut.getWritePointer (0);
    float* bOut = mAmpBOut.getWritePointer (0);
    float* busL = mBus.getWritePointer (0);
    float* busR = mBus.getWritePointer (1);

    auto runAmp = [numSamples] (AmpSlot& slot, float* in, float* out)
    {
        float* inPtr[1]  = { in };
        float* outPtr[1] = { out };
        if (slot.model != nullptr) slot.model->process (inPtr, outPtr, numSamples);
        else if (out != in)        juce::FloatVectorOperations::copy (out, in, numSamples);
    };

    if (routing == Routing::Single)
    {
        runAmp (mAmp[0], mono, aOut);
        juce::FloatVectorOperations::multiply (aOut, levelA, numSamples);
        juce::FloatVectorOperations::copy (busL, aOut, numSamples);
        juce::FloatVectorOperations::copy (busR, aOut, numSamples);
    }
    else if (routing == Routing::Series)
    {
        runAmp (mAmp[0], mono, aOut);
        juce::FloatVectorOperations::multiply (aOut, levelA, numSamples);  // drive into Amp B
        runAmp (mAmp[1], aOut, bOut);
        juce::FloatVectorOperations::multiply (bOut, levelB, numSamples);
        juce::FloatVectorOperations::copy (busL, bOut, numSamples);
        juce::FloatVectorOperations::copy (busR, bOut, numSamples);
    }
    else // Parallel
    {
        runAmp (mAmp[0], mono, aOut);
        runAmp (mAmp[1], mono, bOut);
        juce::FloatVectorOperations::multiply (aOut, levelA, numSamples);
        juce::FloatVectorOperations::multiply (bOut, levelB, numSamples);

        const float spread = apvts.getRawParameterValue (ParamID::ampSpread)->load();
        float gAL, gAR, gBL, gBR;
        equalPowerPan (-spread, gAL, gAR);   // Amp A toward the left
        equalPowerPan ( spread, gBL, gBR);   // Amp B toward the right
        for (int i = 0; i < numSamples; ++i)
        {
            busL[i] = aOut[i] * gAL + bOut[i] * gBL;
            busR[i] = aOut[i] * gAR + bOut[i] * gBR;
        }
    }

    // ---- Overall amp-bus output trim ----
    const float ampOutGain = juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (ParamID::namOutput)->load());
    juce::FloatVectorOperations::multiply (busL, ampOutGain, numSamples);
    juce::FloatVectorOperations::multiply (busR, ampOutGain, numSamples);

    // NAM output meter (max of L/R, post amp-bus trim).
    accumulatePeak (mNamPeak, juce::jmax (blockPeak (busL, numSamples), blockPeak (busR, numSamples)));

    // ---- DUAL CAB IR mixer (stereo): convolve the bus through each active cab
    //      and blend by per-cab level. Bypassed entirely if no cab contributes.
    {
        const bool aOn = apvts.getRawParameterValue (ParamID::cabAActive)->load() > 0.5f && mCab[0].loaded.load();
        const bool bOn = apvts.getRawParameterValue (ParamID::cabBActive)->load() > 0.5f && mCab[1].loaded.load();
        if (aOn || bOn)
        {
            mCabSum.clear();
            float* sumL = mCabSum.getWritePointer (0);
            float* sumR = mCabSum.getWritePointer (1);

            auto runCab = [&] (int c, const char* levelID)
            {
                const float gain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue (levelID)->load());
                float* sc0 = mCabScratch.getWritePointer (0);
                float* sc1 = mCabScratch.getWritePointer (1);
                juce::FloatVectorOperations::copy (sc0, busL, numSamples);
                juce::FloatVectorOperations::copy (sc1, busR, numSamples);

                float* ch[2] = { sc0, sc1 };
                juce::dsp::AudioBlock<float> block (ch, 2, (size_t) numSamples);
                juce::dsp::ProcessContextReplacing<float> ctx (block);
                mCab[c].conv.process (ctx);

                juce::FloatVectorOperations::addWithMultiply (sumL, sc0, gain, numSamples);
                juce::FloatVectorOperations::addWithMultiply (sumR, sc1, gain, numSamples);
            };

            if (aOn) runCab (0, ParamID::cabALevel);
            if (bOn) runCab (1, ParamID::cabBLevel);

            juce::FloatVectorOperations::copy (busL, sumL, numSamples);
            juce::FloatVectorOperations::copy (busR, sumR, numSamples);
        }
    }

    // ---- Per-channel stereo post chain ----
    float* busCh[2] = { busL, busR };

    auto processMono = [numSamples] (juce::dsp::IIR::Filter<float>& f, float* data)
    {
        float* ch[1] = { data };
        juce::dsp::AudioBlock<float> block (ch, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        f.process (ctx);
    };

    // DC blocker (~10 Hz, always).
    for (int ch = 0; ch < 2; ++ch)
        processMono (mDCBlocker[ch], busCh[ch]);

    // EQ
    if (apvts.getRawParameterValue (ParamID::eqActive)->load() > 0.5f)
    {
        for (int i = 0; i < Api560EQ::kNumBands; ++i)
        {
            const float g = apvts.getRawParameterValue (eqParamID (i))->load();
            mEQ[0].setBandGain (i, g);
            mEQ[1].setBandGain (i, g);
        }
        mEQ[0].process (busL, numSamples);
        mEQ[1].process (busR, numSamples);
    }

    // Saturation
    if (apvts.getRawParameterValue (ParamID::satActive)->load() > 0.5f)
    {
        auto band = [this] (const char* prefix)
        {
            Saturation::BandParams p;
            p.sat  = apvts.getRawParameterValue (juce::String (prefix) + "_sat")->load();
            p.dist = apvts.getRawParameterValue (juce::String (prefix) + "_dist")->load();
            p.fuzz = apvts.getRawParameterValue (juce::String (prefix) + "_fuzz")->load();
            return p;
        };
        const auto lo = band ("low");
        const auto md = band ("mid");
        const auto hi = band ("high");
        mSaturation[0].setParams (lo, md, hi);
        mSaturation[1].setParams (lo, md, hi);
        mSaturation[0].process (busL, numSamples);
        mSaturation[1].process (busR, numSamples);
    }

    // Hi-pass
    if (apvts.getRawParameterValue (ParamID::hpfActive)->load() > 0.5f)
    {
        const float f = apvts.getRawParameterValue (ParamID::hpfFreq)->load();
        if (std::abs (f - mHpfCachedFreq) > 0.5f)
        {
            mHpfCachedFreq = f;
            auto co = juce::dsp::IIR::Coefficients<float>::makeHighPass (mSampleRate, f);
            *mHPF[0].coefficients = *co;
            *mHPF[1].coefficients = *co;
        }
        for (int ch = 0; ch < 2; ++ch)
            processMono (mHPF[ch], busCh[ch]);
    }

    // Low-pass
    if (apvts.getRawParameterValue (ParamID::lpfActive)->load() > 0.5f)
    {
        const float f = apvts.getRawParameterValue (ParamID::lpfFreq)->load();
        if (std::abs (f - mLpfCachedFreq) > 0.5f)
        {
            mLpfCachedFreq = f;
            auto co = juce::dsp::IIR::Coefficients<float>::makeLowPass (mSampleRate, f);
            *mLPF[0].coefficients = *co;
            *mLPF[1].coefficients = *co;
        }
        for (int ch = 0; ch < 2; ++ch)
            processMono (mLPF[ch], busCh[ch]);
    }

    // ---- End-of-chain FX: Delay & Reverb (order-switchable, true-bypass) ----
    //      Each module is skipped entirely while off and reset on its off-edge,
    //      so a bypassed module adds no tail, no colour and no CPU.
    {
        const int  order    = (int) apvts.getRawParameterValue (ParamID::fxOrder)->load();
        const bool delayOn  = apvts.getRawParameterValue (ParamID::delayActive)->load()  > 0.5f;
        const bool reverbOn = apvts.getRawParameterValue (ParamID::reverbActive)->load() > 0.5f;

        auto runDelay = [&]
        {
            if (delayOn)
            {
                const float timeMs = apvts.getRawParameterValue (ParamID::delayTime)->load();
                const float fb     = apvts.getRawParameterValue (ParamID::delayFeedback)->load();
                const float mix    = apvts.getRawParameterValue (ParamID::delayMix)->load();
                const float dsamp  = juce::jlimit (1.0f, (float) ((1 << 17) - 2),
                                                   (float) (timeMs * 0.001 * mSampleRate));
                mDelay.setDelay (dsamp);
                float* chs[2] = { busL, busR };
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* d = chs[ch];
                    for (int i = 0; i < numSamples; ++i)
                    {
                        const float dry = d[i];
                        const float wet = mDelay.popSample (ch, dsamp, true);
                        mDelay.pushSample (ch, dry + wet * fb);
                        d[i] = dry * (1.0f - mix) + wet * mix;
                    }
                }
            }
            else if (mDelayWasActive)
                mDelay.reset();
            mDelayWasActive = delayOn;
        };

        auto runReverb = [&]
        {
            if (reverbOn)
            {
                const float mix = apvts.getRawParameterValue (ParamID::reverbMix)->load();
                juce::dsp::Reverb::Parameters rp;
                rp.roomSize   = apvts.getRawParameterValue (ParamID::reverbSize)->load();
                rp.damping    = apvts.getRawParameterValue (ParamID::reverbDamp)->load();
                rp.wetLevel   = mix;
                rp.dryLevel   = 1.0f - mix;
                rp.width      = 1.0f;
                rp.freezeMode = 0.0f;
                mReverb.setParameters (rp);

                float* chs[2] = { busL, busR };
                juce::dsp::AudioBlock<float> block (chs, 2, (size_t) numSamples);
                juce::dsp::ProcessContextReplacing<float> ctx (block);
                mReverb.process (ctx);
            }
            else if (mReverbWasActive)
                mReverb.reset();
            mReverbWasActive = reverbOn;
        };

        if (order == 0) { runDelay(); runReverb(); }
        else            { runReverb(); runDelay(); }
    }

    // ---- Master output gain / mode ----
    const float outGain = computeOutputGain();
    juce::FloatVectorOperations::multiply (busL, outGain, numSamples);
    juce::FloatVectorOperations::multiply (busR, outGain, numSamples);

    accumulatePeak (mMasterPeak, juce::jmax (blockPeak (busL, numSamples), blockPeak (busR, numSamples)));

    // ---- Write the stereo result to the outputs ----
    if (numOut >= 2)
    {
        juce::FloatVectorOperations::copy (buffer.getWritePointer (0), busL, numSamples);
        juce::FloatVectorOperations::copy (buffer.getWritePointer (1), busR, numSamples);
        for (int ch = 2; ch < numOut; ++ch)
        {
            float* o = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i) o[i] = 0.5f * (busL[i] + busR[i]);
        }
    }
    else if (numOut == 1)
    {
        float* o = buffer.getWritePointer (0);
        for (int i = 0; i < numSamples; ++i) o[i] = 0.5f * (busL[i] + busR[i]);
    }
}

// ===========================================================================
void NecronamAudioProcessor::loadNamModel (const juce::File& file, int ampIndex)
{
    const int a = idx (ampIndex);
    ensureNamArchitecturesRegistered();
    try
    {
        const auto p = std::filesystem::u8path (file.getFullPathName().toStdString());
        std::unique_ptr<nam::DSP> dsp = nam::get_dsp (p);
        if (dsp == nullptr)
        {
            mAmp[a].name = "LOAD FAILED (null)";
            return;
        }

        auto wrapped = std::make_unique<ResamplingNAM> (std::move (dsp));
        if (mPrepared.load())
            wrapped->Reset (mSampleRate, mMaxBlock);

        const bool slimmable = wrapped->IsSlimmable();
        // Apply the current quality before the model goes live (not RT-safe).
        wrapped->SetQuality (apvts.getRawParameterValue (ParamID::quality)->load());

        {
            const juce::SpinLock::ScopedLockType l (mAmp[a].swapLock);
            mAmp[a].staged = std::move (wrapped);
        }

        mAmp[a].slimmable.store (slimmable);
        mAmp[a].name = file.getFileNameWithoutExtension();
        apvts.state.setProperty (kNamPathKey[a], file.getFullPathName(), nullptr);
        updateLatency();
    }
    catch (const std::exception& e)
    {
        mAmp[a].name = "LOAD FAILED";
        juce::Logger::writeToLog (juce::String ("NECRONAM MAX: failed to load model: ") + e.what());
    }
}

void NecronamAudioProcessor::clearNamModel (int ampIndex)
{
    const int a = idx (ampIndex);
    mAmp[a].clear.store (true);
    {
        const juce::SpinLock::ScopedLockType l (mAmp[a].swapLock);
        mAmp[a].staged.reset();
    }
    mAmp[a].name = {};
    mAmp[a].slimmable.store (false);
    apvts.state.setProperty (kNamPathKey[a], "", nullptr);
    updateLatency();
}

void NecronamAudioProcessor::loadImpulseResponse (const juce::File& file, int cabIndex)
{
    const int c = idx (cabIndex);
    if (! file.existsAsFile())
        return;

    // juce::dsp::Convolution loads on its own background thread and swaps the IR
    // in atomically; it resamples the IR to the current spec automatically. The
    // mono IR (Stereo::no) is applied identically to both channels of the bus.
    mCab[c].conv.loadImpulseResponse (file,
                                      juce::dsp::Convolution::Stereo::no,
                                      juce::dsp::Convolution::Trim::no,
                                      0);
    mCab[c].loaded.store (true);
    mCab[c].name = file.getFileNameWithoutExtension();
    apvts.state.setProperty (kIrPathKey[c], file.getFullPathName(), nullptr);
}

void NecronamAudioProcessor::clearImpulseResponse (int cabIndex)
{
    const int c = idx (cabIndex);
    mCab[c].loaded.store (false);   // bypass; the convolver simply isn't run
    mCab[c].name = {};
    apvts.state.setProperty (kIrPathKey[c], "", nullptr);
}

// ===========================================================================
void NecronamAudioProcessor::parameterChanged (const juce::String&, float)
{
    // May fire on the audio thread during automation; defer the non-RT-safe
    // SetSlimmableSize / setLatencySamples work to the message thread.
    triggerAsyncUpdate();
}

void NecronamAudioProcessor::handleAsyncUpdate()
{
    applyQuality();
    updateLatency();
}

void NecronamAudioProcessor::applyQuality()
{
    const double v = apvts.getRawParameterValue (ParamID::quality)->load();
    for (int a = 0; a < 2; ++a)
    {
        // Hold the swap lock so the audio thread can't reassign the model mid-apply.
        const juce::SpinLock::ScopedLockType l (mAmp[a].swapLock);
        if (mAmp[a].model  != nullptr) mAmp[a].model->SetQuality (v);
        if (mAmp[a].staged != nullptr) mAmp[a].staged->SetQuality (v);
    }
}

void NecronamAudioProcessor::reloadReferencedFiles()
{
    for (int a = 0; a < 2; ++a)
    {
        const auto path = apvts.state.getProperty (kNamPathKey[a]).toString();
        if (path.isNotEmpty() && juce::File (path).existsAsFile())
            loadNamModel (juce::File (path), a);
        else
            clearNamModel (a);
    }
    for (int c = 0; c < 2; ++c)
    {
        const auto path = apvts.state.getProperty (kIrPathKey[c]).toString();
        if (path.isNotEmpty() && juce::File (path).existsAsFile())
            loadImpulseResponse (juce::File (path), c);
        else
            clearImpulseResponse (c);
    }
}

void NecronamAudioProcessor::setStateTree (const juce::ValueTree& tree)
{
    apvts.replaceState (tree);
    reloadReferencedFiles();
    updateLatency();
}

// ===========================================================================
juce::AudioProcessorEditor* NecronamAudioProcessor::createEditor()
{
    return new NecronamAudioProcessorEditor (*this);
}

void NecronamAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void NecronamAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            setStateTree (juce::ValueTree::fromXml (*xml));
}

// ===========================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NecronamAudioProcessor();
}
