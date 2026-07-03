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
    // library. Register them explicitly.
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

    void equalPowerPan (float pan, float& gainL, float& gainR) noexcept
    {
        pan = juce::jlimit (-1.0f, 1.0f, pan);
        const float t = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
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
    apvts.addParameterListener (ParamID::quality,    this);
    apvts.addParameterListener (ParamID::ampRouting, this);
    apvts.addParameterListener (ParamID::inputMode,  this);
}

NecronamAudioProcessor::~NecronamAudioProcessor()
{
    apvts.removeParameterListener (ParamID::quality,    this);
    apvts.removeParameterListener (ParamID::ampRouting, this);
    apvts.removeParameterListener (ParamID::inputMode,  this);
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
    auto cParam = [] (const juce::String& id, const juce::String& name,
                      const juce::StringArray& items, int def)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def);
    };

    // ---- Master / IO ----
    params.push_back (fParam (ParamID::inputLevel,  "Input Level",  Range (-20.0f, 20.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::namOutput,   "Amp Output",   Range (-40.0f, 40.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::outputLevel, "Output Level", Range (-40.0f, 40.0f, 0.1f), 0.0f, dbToText));
    params.push_back (cParam (ParamID::outputMode, "Output Mode", { "Raw", "Normalized", "Calibrated" }, 0));
    params.push_back (fParam (ParamID::inputCal, "Input Calibration", Range (0.0f, 30.0f, 0.1f), 12.0f,
                              [] (float v, int) { return juce::String (v, 1) + " dBu"; }));
    params.push_back (fParam (ParamID::cleanBlend, "Clean Blend", Range (0.0f, 1.0f, 0.001f), 0.0f, pctToText));
    params.push_back (cParam (ParamID::inputMode, "Input Mode", { "Mono", "Stereo (Dual Mono)" }, 0));

    // ---- Gate ----
    params.push_back (fParam (ParamID::gateThresh, "Gate Threshold", Range (-100.0f, 0.0f, 0.5f), -80.0f, dbToText));
    params.push_back (bParam (ParamID::gateActive, "Gate", false));
    params.push_back (cParam (ParamID::gatePosition, "Gate Position", { "Pre Amp", "Post Amp" }, 0));

    // ---- Quality (A2, shared) ----
    params.push_back (fParam (ParamID::quality, "Quality", Range (0.0f, 1.0f, 0.01f), 1.0f,
                              [] (float v, int)
                              {
                                  if (v >= 0.999f) return juce::String ("Max Quality");
                                  if (v <= 0.001f) return juce::String ("Max Efficiency");
                                  return juce::String (juce::roundToInt (v * 100.0f)) + "%";
                              }));

    // ---- Flesh Render stage params (front fs_* + post) ----
    const char* bands[3]    = { "low", "mid", "high" };
    const char* bandsUp[3]  = { "Low", "Mid", "High" };
    const char* stages[3]   = { "sat", "dist", "fuzz" };
    const char* stagesUp[3] = { "Saturation", "Drive", "Fuzz" };

    params.push_back (bParam (ParamID::frontSatActive, "Front Saturation", false));
    params.push_back (fParam (ParamID::frontSatXLow,  "Front Xover Low",  Range (60.0f, 800.0f, 1.0f, 0.4f), 250.0f, hzToText));
    params.push_back (fParam (ParamID::frontSatXHigh, "Front Xover High", Range (800.0f, 8000.0f, 1.0f, 0.4f), 2000.0f, hzToText));
    for (int b = 0; b < 3; ++b)
        for (int s = 0; s < 3; ++s)
            params.push_back (fParam (satParamID (true, bands[b], stages[s]),
                                      "Front " + juce::String (bandsUp[b]) + " " + stagesUp[s],
                                      Range (0.0f, 1.0f, 0.001f), 0.0f, pctToText));

    params.push_back (bParam (ParamID::satActive, "Saturation", false));
    params.push_back (fParam (ParamID::satXLow,  "Xover Low",  Range (60.0f, 800.0f, 1.0f, 0.4f), 250.0f, hzToText));
    params.push_back (fParam (ParamID::satXHigh, "Xover High", Range (800.0f, 8000.0f, 1.0f, 0.4f), 2000.0f, hzToText));
    for (int b = 0; b < 3; ++b)
        for (int s = 0; s < 3; ++s)
            params.push_back (fParam (satParamID (false, bands[b], stages[s]),
                                      juce::String (bandsUp[b]) + " " + stagesUp[s],
                                      Range (0.0f, 1.0f, 0.001f), 0.0f, pctToText));

    // ---- Drive section ----
    params.push_back (bParam (ParamID::driveActive, "Drive", false));
    params.push_back (cParam (ParamID::driveCircuit, "Drive Circuit", { "TC Preamp", "Tube Screamer" }, 0));
    params.push_back (fParam (ParamID::tcGain,   "TC Gain",   Range (0.0f, 1.0f, 0.001f), 0.2f, pctToText));
    params.push_back (fParam (ParamID::tcBass,   "TC Bass",   Range (-12.0f, 12.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::tcMid,    "TC Mid",    Range (-12.0f, 12.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::tcTreble, "TC Treble", Range (-12.0f, 12.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::tcLevel,  "TC Level",  Range (-12.0f, 12.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::tsDrive,  "TS Drive",  Range (0.0f, 1.0f, 0.001f), 0.3f, pctToText));
    params.push_back (fParam (ParamID::tsTone,   "TS Tone",   Range (0.0f, 1.0f, 0.001f), 0.5f, pctToText));
    params.push_back (fParam (ParamID::tsLevel,  "TS Level",  Range (-12.0f, 12.0f, 0.1f), 0.0f, dbToText));

    // ---- Low cut ----
    params.push_back (fParam (ParamID::lowCutFreq, "Low Cut", Range (20.0f, 1000.0f, 1.0f, 0.3f), 80.0f, hzToText));
    params.push_back (bParam (ParamID::lowCutActive, "Low Cut On", false));

    // ---- Dual amp ----
    params.push_back (cParam (ParamID::ampRouting, "Amp Routing", { "Single", "Series", "Parallel" }, 0));
    params.push_back (fParam (ParamID::ampALevel, "Amp A Level", Range (-40.0f, 24.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::ampBLevel, "Amp B Level", Range (-40.0f, 24.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::ampSpread, "Amp Spread", Range (0.0f, 1.0f, 0.01f), 0.5f, pctToText));
    params.push_back (bParam (ParamID::ampAActive, "Amp A", true));
    params.push_back (bParam (ParamID::ampBActive, "Amp B", true));

    // ---- Sag ----
    params.push_back (fParam (ParamID::sagAmount, "Sag", Range (0.0f, 10.0f, 0.1f), 0.0f,
                              [] (float v, int) { return juce::String (v, 1); }));

    // ---- Dual cab ----
    params.push_back (bParam (ParamID::cabAActive, "Cab A", false));
    params.push_back (bParam (ParamID::cabBActive, "Cab B", false));
    params.push_back (fParam (ParamID::cabALevel, "Cab A Level", Range (-40.0f, 12.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::cabBLevel, "Cab B Level", Range (-40.0f, 12.0f, 0.1f), 0.0f, dbToText));

    // ---- EQ ----
    params.push_back (bParam (ParamID::eqActive, "EQ", false));
    for (int i = 0; i < Api560EQ::kNumBands; ++i)
    {
        const float f = Api560EQ::kFrequencies[(size_t) i];
        const juce::String name = (f >= 1000.0f ? juce::String (f / 1000.0f, (f == 16000.0f ? 0 : 1)) + "k"
                                                : juce::String (juce::roundToInt (f))) + " Hz";
        params.push_back (fParam (eqParamID (i), "EQ " + name, Range (-12.0f, 12.0f, 0.1f), 0.0f, dbToText));
    }

    // ---- Compressor ----
    params.push_back (bParam (ParamID::compActive, "Compressor", false));
    params.push_back (fParam (ParamID::compAmount, "Peak Reduction", Range (0.0f, 100.0f, 0.5f), 30.0f,
                              [] (float v, int) { return juce::String (juce::roundToInt (v)); }));

    // ---- Post filters ----
    params.push_back (fParam (ParamID::hpfFreq, "Hi-Pass", Range (20.0f, 2000.0f, 1.0f, 0.3f), 20.0f, hzToText));
    params.push_back (bParam (ParamID::hpfActive, "Hi-Pass On", false));
    params.push_back (fParam (ParamID::lpfFreq, "Low-Pass", Range (1000.0f, 20000.0f, 1.0f, 0.3f), 20000.0f, hzToText));
    params.push_back (bParam (ParamID::lpfActive, "Low-Pass On", false));

    // ---- FX ----
    params.push_back (bParam (ParamID::delayActive, "Delay", false));
    params.push_back (fParam (ParamID::delayTime, "Delay Time", Range (1.0f, 2000.0f, 1.0f, 0.4f), 350.0f,
                              [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; }));
    params.push_back (fParam (ParamID::delayFeedback, "Delay Feedback", Range (0.0f, 0.95f, 0.001f), 0.35f, pctToText));
    params.push_back (fParam (ParamID::delayMix, "Delay Mix", Range (0.0f, 1.0f, 0.001f), 0.30f, pctToText));
    params.push_back (bParam (ParamID::reverbActive, "Reverb", false));
    params.push_back (cParam (ParamID::reverbType, "Reverb Type", { "Plate", "Spring" }, 0));
    params.push_back (fParam (ParamID::reverbSize, "Reverb Size", Range (0.0f, 1.0f, 0.001f), 0.5f, pctToText));
    params.push_back (fParam (ParamID::reverbDamp, "Reverb Damp", Range (0.0f, 1.0f, 0.001f), 0.5f, pctToText));
    params.push_back (fParam (ParamID::reverbMix,  "Reverb Mix",  Range (0.0f, 1.0f, 0.001f), 0.25f, pctToText));
    params.push_back (cParam (ParamID::fxOrder, "FX Order", { "Delay -> Reverb", "Reverb -> Delay" }, 0));

    // ---- Tuner ----
    params.push_back (bParam (ParamID::tunerActive, "Tuner", false));

    return { params.begin(), params.end() };
}

// ===========================================================================
void NecronamAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    mSampleRate = sampleRate;
    mMaxBlock   = samplesPerBlock;

    mLanes.setSize      (2, samplesPerBlock);
    mAmpAOut.setSize    (1, samplesPerBlock);
    mAmpBOut.setSize    (1, samplesPerBlock);
    mBus.setSize        (2, samplesPerBlock);
    mCabScratch.setSize (2, samplesPerBlock);
    mCabSum.setSize     (2, samplesPerBlock);
    mCleanDI.setSize    (2, samplesPerBlock);
    mGateBuf.setSize    (2, samplesPerBlock);

    juce::dsp::ProcessSpec monoSpec   { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    juce::dsp::ProcessSpec stereoSpec { sampleRate, (juce::uint32) samplesPerBlock, 2 };

    for (int ch = 0; ch < 2; ++ch)
    {
        mFrontSat[ch].prepare (sampleRate, samplesPerBlock);
        mSaturation[ch].prepare (sampleRate, samplesPerBlock);
        mEQ[ch].prepare (sampleRate, samplesPerBlock);
        mLowCut[ch].prepare (monoSpec);
        mHPF[ch].prepare (monoSpec);
        mLPF[ch].prepare (monoSpec);
        *mDCBlocker[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 10.0);
        mDCBlocker[ch].reset();
    }
    mLowCutCachedFreq = mHpfCachedFreq = mLpfCachedFreq = -1.0f;

    mDrive.prepare (sampleRate, samplesPerBlock);
    mSag.prepare (sampleRate);
    mComp.prepare (sampleRate);
    mSpring.prepare (sampleRate, samplesPerBlock);
    mReverb.prepare (stereoSpec);
    mReverb.reset();
    mDelay.prepare (stereoSpec);
    mDelay.reset();

    mFrontSatWasActive = mDriveWasActive = mCompWasActive = false;
    mDelayWasActive = mReverbWasActive = mGateWasActive = mPostSatWasActive = false;
    mDriveLastCircuit = mReverbLastType = -1;

    for (int c = 0; c < 2; ++c)
        mCab[c].conv.prepare (stereoSpec);

    mTunerRing.fill (0.0f);
    mTunerWrite.store (0);

    mGateEnv[0] = mGateEnv[1] = 0.0f;
    mGateGain[0] = mGateGain[1] = 1.0f;

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

    const auto& m = mAmp[0].model;   // Normalized / Calibrated reference Amp A
    if (mode == 1 && m != nullptr && m->HasLoudness())
        gain *= juce::Decibels::decibelsToGain (-18.0f - (float) m->GetLoudness());
    else if (mode == 2 && m != nullptr && m->HasOutputLevel())
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
    const auto mode    = (InputMode) (int) apvts.getRawParameterValue (ParamID::inputMode)->load();
    const auto routing = (Routing)   (int) apvts.getRawParameterValue (ParamID::ampRouting)->load();

    int latency = 0;
    if (mode == InputMode::Stereo)
        latency = juce::jmax (la, lb);
    else
        switch (routing)
        {
            case Routing::Single:   latency = la;                  break;
            case Routing::Series:   latency = la + lb;             break;
            case Routing::Parallel: latency = juce::jmax (la, lb); break;
        }
    setLatencySamples (latency);
}

// ===========================================================================
void NecronamAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();

    auto processMono = [numSamples] (juce::dsp::IIR::Filter<float>& f, float* data)
    {
        float* ch[1] = { data };
        juce::dsp::AudioBlock<float> block (ch, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        f.process (ctx);
    };
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    // ---- Hot-swap staged models; honour clear requests ----
    for (int a = 0; a < 2; ++a)
    {
        const juce::SpinLock::ScopedTryLockType l (mAmp[a].swapLock);
        if (l.isLocked() && mAmp[a].staged != nullptr)
            mAmp[a].model = std::move (mAmp[a].staged);
    }
    for (int a = 0; a < 2; ++a)
        if (mAmp[a].clear.exchange (false))
            mAmp[a].model.reset();

    // ================= 1) INPUT LANES + MASTER INPUT LEVEL ==================
    const auto inMode  = (InputMode) (int) raw (ParamID::inputMode);
    const int  nLanes  = inMode == InputMode::Stereo ? 2 : 1;

    float* lane0 = mLanes.getWritePointer (0);
    float* lane1 = mLanes.getWritePointer (1);

    if (inMode == InputMode::Mono)
    {
        if (numIn >= 2)
        {
            const float* L = buffer.getReadPointer (0);
            const float* R = buffer.getReadPointer (1);
            for (int i = 0; i < numSamples; ++i)
                lane0[i] = 0.5f * (L[i] + R[i]);
        }
        else if (numIn == 1)
            juce::FloatVectorOperations::copy (lane0, buffer.getReadPointer (0), numSamples);
        else
            juce::FloatVectorOperations::clear (lane0, numSamples);
    }
    else
    {
        if (numIn >= 1) juce::FloatVectorOperations::copy (lane0, buffer.getReadPointer (0), numSamples);
        else            juce::FloatVectorOperations::clear (lane0, numSamples);
        if (numIn >= 2) juce::FloatVectorOperations::copy (lane1, buffer.getReadPointer (1), numSamples);
        else            juce::FloatVectorOperations::copy (lane1, lane0, numSamples);
    }

    // Input gain (+ calibrated alignment: lane 0 references Amp A; in stereo
    // mode lane 1 references Amp B).
    const int   outMode  = (int) raw (ParamID::outputMode);
    const float inputCal = raw (ParamID::inputCal);
    const float baseInDb = raw (ParamID::inputLevel);
    for (int ln = 0; ln < nLanes; ++ln)
    {
        float db = baseInDb;
        const auto& m = mAmp[inMode == InputMode::Stereo ? ln : 0].model;
        if (outMode == 2 && m != nullptr && m->HasInputLevel())
            db += inputCal - (float) m->GetInputLevel();
        juce::FloatVectorOperations::multiply (ln == 0 ? lane0 : lane1,
                                               juce::Decibels::decibelsToGain (db), numSamples);
    }

    // IN meter (max of lanes) + tuner capture (mono mix of lanes).
    {
        float pk = blockPeak (lane0, numSamples);
        if (nLanes == 2) pk = juce::jmax (pk, blockPeak (lane1, numSamples));
        accumulatePeak (mInPeak, pk);

        int w = mTunerWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            mTunerRing[(size_t) w] = nLanes == 2 ? 0.5f * (lane0[i] + lane1[i]) : lane0[i];
            w = (w + 1) & (kTunerRing - 1);
        }
        mTunerWrite.store (w, std::memory_order_release);
    }

    // Clean DI capture (per lane; mono mode duplicates lane 0).
    juce::FloatVectorOperations::copy (mCleanDI.getWritePointer (0), lane0, numSamples);
    juce::FloatVectorOperations::copy (mCleanDI.getWritePointer (1), nLanes == 2 ? lane1 : lane0, numSamples);

    // ---- Tuner engaged: mute everything ----
    if (raw (ParamID::tunerActive) > 0.5f)
    {
        buffer.clear();
        mNamPeak.store (0.0f);
        mMasterPeak.store (0.0f);
        return;
    }

    // ================= 2) GATE (detector on the direct signal) ==============
    const bool gateOn   = raw (ParamID::gateActive) > 0.5f;
    const int  gatePos  = (int) raw (ParamID::gatePosition);   // 0 = pre, 1 = post
    if (gateOn)
    {
        const float threshLin = juce::Decibels::decibelsToGain (raw (ParamID::gateThresh));
        const float envRel = std::exp (-1.0f / (0.050f * (float) mSampleRate));
        const float openC  = std::exp (-1.0f / (0.005f * (float) mSampleRate));
        const float closeC = std::exp (-1.0f / (0.100f * (float) mSampleRate));

        for (int ln = 0; ln < nLanes; ++ln)
        {
            const float* src = ln == 0 ? lane0 : lane1;   // direct signal keys the gate
            float* gbuf = mGateBuf.getWritePointer (ln);
            for (int i = 0; i < numSamples; ++i)
            {
                const float a = std::abs (src[i]);
                mGateEnv[ln] = juce::jmax (a, mGateEnv[ln] * envRel);
                const float target = mGateEnv[ln] >= threshLin ? 1.0f : 0.0f;
                const float c = (target < mGateGain[ln]) ? closeC : openC;
                mGateGain[ln] = target + (mGateGain[ln] - target) * c;
                gbuf[i] = mGateGain[ln];
            }
        }

        if (gatePos == 0)   // apply pre-amp
            for (int ln = 0; ln < nLanes; ++ln)
                juce::FloatVectorOperations::multiply (ln == 0 ? lane0 : lane1,
                                                       mGateBuf.getReadPointer (ln), numSamples);
    }
    else if (mGateWasActive)
    {
        mGateEnv[0] = mGateEnv[1] = 0.0f;
        mGateGain[0] = mGateGain[1] = 1.0f;
    }
    mGateWasActive = gateOn;

    // ================= 3) FLESH RENDER PRE ===================================
    {
        const bool on = raw (ParamID::frontSatActive) > 0.5f;
        if (on)
        {
            auto band = [&] (const char* b)
            {
                Saturation::BandParams p;
                p.sat  = raw (satParamID (true, b, "sat").toRawUTF8());
                p.dist = raw (satParamID (true, b, "dist").toRawUTF8());
                p.fuzz = raw (satParamID (true, b, "fuzz").toRawUTF8());
                return p;
            };
            const auto lo = band ("low"), md = band ("mid"), hi = band ("high");
            const float xl = raw (ParamID::frontSatXLow), xh = raw (ParamID::frontSatXHigh);
            for (int ln = 0; ln < nLanes; ++ln)
            {
                mFrontSat[ln].setCrossovers (xl, xh);
                mFrontSat[ln].setParams (lo, md, hi);
                mFrontSat[ln].process (ln == 0 ? lane0 : lane1, numSamples);
            }
        }
        else if (mFrontSatWasActive)
        {
            mFrontSat[0].reset();
            mFrontSat[1].reset();
        }
        mFrontSatWasActive = on;
    }

    // ================= 4) DRIVE SECTION (switchable circuit) =================
    {
        const bool on      = raw (ParamID::driveActive) > 0.5f;
        const int  circuit = (int) raw (ParamID::driveCircuit);
        if (on)
        {
            if (circuit != mDriveLastCircuit)
                mDrive.reset();                       // clean switch between circuits
            if (circuit == DriveCircuits::TCPreamp)
            {
                DriveCircuits::TCParams p;
                p.gain     = raw (ParamID::tcGain);
                p.bassDb   = raw (ParamID::tcBass);
                p.midDb    = raw (ParamID::tcMid);
                p.trebleDb = raw (ParamID::tcTreble);
                p.levelDb  = raw (ParamID::tcLevel);
                for (int ln = 0; ln < nLanes; ++ln)
                    mDrive.processTC (ln, ln == 0 ? lane0 : lane1, numSamples, p);
            }
            else
            {
                DriveCircuits::TSParams p;
                p.drive   = raw (ParamID::tsDrive);
                p.tone    = raw (ParamID::tsTone);
                p.levelDb = raw (ParamID::tsLevel);
                for (int ln = 0; ln < nLanes; ++ln)
                    mDrive.processTS (ln, ln == 0 ? lane0 : lane1, numSamples, p);
            }
        }
        else if (mDriveWasActive)
            mDrive.reset();
        mDriveWasActive   = on;
        mDriveLastCircuit = circuit;
    }

    // ================= 5) LOW CUT ============================================
    if (raw (ParamID::lowCutActive) > 0.5f)
    {
        const float f = raw (ParamID::lowCutFreq);
        if (std::abs (f - mLowCutCachedFreq) > 0.5f)
        {
            mLowCutCachedFreq = f;
            auto co = juce::dsp::IIR::Coefficients<float>::makeHighPass (mSampleRate, f);
            *mLowCut[0].coefficients = *co;
            *mLowCut[1].coefficients = *co;
        }
        for (int ln = 0; ln < nLanes; ++ln)
            processMono (mLowCut[ln], ln == 0 ? lane0 : lane1);
    }

    // ================= 6) DUAL AMP STAGE -> stereo bus =======================
    const float levelA = juce::Decibels::decibelsToGain (raw (ParamID::ampALevel));
    const float levelB = juce::Decibels::decibelsToGain (raw (ParamID::ampBLevel));
    const bool aActive = raw (ParamID::ampAActive) > 0.5f;
    const bool bActive = raw (ParamID::ampBActive) > 0.5f;

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

    if (inMode == InputMode::Stereo)
    {
        // Dual mono: L -> Amp A, R -> Amp B, hard panned.
        if (aActive)
        {
            runAmp (mAmp[0], lane0, aOut);
            juce::FloatVectorOperations::multiply (aOut, levelA, numSamples);
            juce::FloatVectorOperations::copy (busL, aOut, numSamples);
        }
        else
            juce::FloatVectorOperations::copy (busL, lane0, numSamples);

        if (bActive)
        {
            runAmp (mAmp[1], lane1, bOut);
            juce::FloatVectorOperations::multiply (bOut, levelB, numSamples);
            juce::FloatVectorOperations::copy (busR, bOut, numSamples);
        }
        else
            juce::FloatVectorOperations::copy (busR, lane1, numSamples);
    }
    else
    {
        const auto routing = (Routing) (int) raw (ParamID::ampRouting);
        float* mono = lane0;

        if (routing == Routing::Single)
        {
            if (aActive)
            {
                runAmp (mAmp[0], mono, aOut);
                juce::FloatVectorOperations::multiply (aOut, levelA, numSamples);
                juce::FloatVectorOperations::copy (busL, aOut, numSamples);
                juce::FloatVectorOperations::copy (busR, aOut, numSamples);
            }
            else
            {
                juce::FloatVectorOperations::copy (busL, mono, numSamples);
                juce::FloatVectorOperations::copy (busR, mono, numSamples);
            }
        }
        else if (routing == Routing::Series)
        {
            float* sig = mono;
            if (aActive)
            {
                runAmp (mAmp[0], sig, aOut);
                juce::FloatVectorOperations::multiply (aOut, levelA, numSamples);
                sig = aOut;
            }
            if (bActive)
            {
                runAmp (mAmp[1], sig, bOut);
                juce::FloatVectorOperations::multiply (bOut, levelB, numSamples);
                sig = bOut;
            }
            juce::FloatVectorOperations::copy (busL, sig, numSamples);
            juce::FloatVectorOperations::copy (busR, sig, numSamples);
        }
        else // Parallel
        {
            if (aActive && bActive)
            {
                runAmp (mAmp[0], mono, aOut);
                runAmp (mAmp[1], mono, bOut);
                juce::FloatVectorOperations::multiply (aOut, levelA, numSamples);
                juce::FloatVectorOperations::multiply (bOut, levelB, numSamples);

                const float spread = raw (ParamID::ampSpread);
                float gAL, gAR, gBL, gBR;
                equalPowerPan (-spread, gAL, gAR);
                equalPowerPan ( spread, gBL, gBR);
                for (int i = 0; i < numSamples; ++i)
                {
                    busL[i] = aOut[i] * gAL + bOut[i] * gBL;
                    busR[i] = aOut[i] * gAR + bOut[i] * gBR;
                }
            }
            else if (aActive || bActive)
            {
                const int   ai  = aActive ? 0 : 1;
                float*      out = aActive ? aOut : bOut;
                const float lvl = aActive ? levelA : levelB;
                runAmp (mAmp[ai], mono, out);
                juce::FloatVectorOperations::multiply (out, lvl, numSamples);
                juce::FloatVectorOperations::copy (busL, out, numSamples);
                juce::FloatVectorOperations::copy (busR, out, numSamples);
            }
            else
            {
                juce::FloatVectorOperations::copy (busL, mono, numSamples);
                juce::FloatVectorOperations::copy (busR, mono, numSamples);
            }
        }
    }

    // Amp-bus trim + NAM meter.
    const float ampOutGain = juce::Decibels::decibelsToGain (raw (ParamID::namOutput));
    juce::FloatVectorOperations::multiply (busL, ampOutGain, numSamples);
    juce::FloatVectorOperations::multiply (busR, ampOutGain, numSamples);
    accumulatePeak (mNamPeak, juce::jmax (blockPeak (busL, numSamples), blockPeak (busR, numSamples)));

    // Gate applied POST amp (gain computed from the direct signal above).
    if (gateOn && gatePos == 1)
    {
        juce::FloatVectorOperations::multiply (busL, mGateBuf.getReadPointer (0), numSamples);
        juce::FloatVectorOperations::multiply (busR, mGateBuf.getReadPointer (nLanes == 2 ? 1 : 0), numSamples);
    }

    // ================= 7) SAG ================================================
    mSag.process (busL, busR, numSamples, raw (ParamID::sagAmount), inMode == InputMode::Mono);

    // ================= 8) CAB IR =============================================
    {
        const bool aOn = raw (ParamID::cabAActive) > 0.5f && mCab[0].loaded.load();
        const bool bOn = raw (ParamID::cabBActive) > 0.5f && mCab[1].loaded.load();

        auto convolveCab = [&] (int c) -> const float*   // returns scratch chans
        {
            float* sc0 = mCabScratch.getWritePointer (0);
            float* sc1 = mCabScratch.getWritePointer (1);
            juce::FloatVectorOperations::copy (sc0, busL, numSamples);
            juce::FloatVectorOperations::copy (sc1, busR, numSamples);
            float* ch[2] = { sc0, sc1 };
            juce::dsp::AudioBlock<float> block (ch, 2, (size_t) numSamples);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            mCab[c].conv.process (ctx);
            return nullptr;
        };

        if (inMode == InputMode::Stereo)
        {
            // Cab A colours the LEFT channel, Cab B the RIGHT — independent.
            float* sumL = mCabSum.getWritePointer (0);
            float* sumR = mCabSum.getWritePointer (1);
            juce::FloatVectorOperations::copy (sumL, busL, numSamples);
            juce::FloatVectorOperations::copy (sumR, busR, numSamples);

            if (aOn)
            {
                convolveCab (0);
                juce::FloatVectorOperations::copyWithMultiply (
                    sumL, mCabScratch.getReadPointer (0),
                    juce::Decibels::decibelsToGain (raw (ParamID::cabALevel)), numSamples);
            }
            if (bOn)
            {
                convolveCab (1);
                juce::FloatVectorOperations::copyWithMultiply (
                    sumR, mCabScratch.getReadPointer (1),
                    juce::Decibels::decibelsToGain (raw (ParamID::cabBLevel)), numSamples);
            }
            juce::FloatVectorOperations::copy (busL, sumL, numSamples);
            juce::FloatVectorOperations::copy (busR, sumR, numSamples);
        }
        else if (aOn || bOn)
        {
            // Mono mode: both cabs colour the whole bus and are blended.
            mCabSum.clear();
            float* sumL = mCabSum.getWritePointer (0);
            float* sumR = mCabSum.getWritePointer (1);

            if (aOn)
            {
                convolveCab (0);
                const float g = juce::Decibels::decibelsToGain (raw (ParamID::cabALevel));
                juce::FloatVectorOperations::addWithMultiply (sumL, mCabScratch.getReadPointer (0), g, numSamples);
                juce::FloatVectorOperations::addWithMultiply (sumR, mCabScratch.getReadPointer (1), g, numSamples);
            }
            if (bOn)
            {
                convolveCab (1);
                const float g = juce::Decibels::decibelsToGain (raw (ParamID::cabBLevel));
                juce::FloatVectorOperations::addWithMultiply (sumL, mCabScratch.getReadPointer (0), g, numSamples);
                juce::FloatVectorOperations::addWithMultiply (sumR, mCabScratch.getReadPointer (1), g, numSamples);
            }
            juce::FloatVectorOperations::copy (busL, sumL, numSamples);
            juce::FloatVectorOperations::copy (busR, sumR, numSamples);
        }
    }

    float* busCh[2] = { busL, busR };

    // DC blocker (always).
    for (int ch = 0; ch < 2; ++ch)
        processMono (mDCBlocker[ch], busCh[ch]);

    // ================= 9) GRAPHIC EQ =========================================
    if (raw (ParamID::eqActive) > 0.5f)
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

    // ================= 10) LA-2A COMPRESSOR ==================================
    {
        const bool on = raw (ParamID::compActive) > 0.5f;
        if (on)
            mComp.process (busL, busR, numSamples, raw (ParamID::compAmount));
        else if (mCompWasActive)
            mComp.reset();
        mCompWasActive = on;
    }

    // ================= 11) FLESH RENDER POST =================================
    {
        const bool on = raw (ParamID::satActive) > 0.5f;
        if (on)
        {
            auto band = [&] (const char* b)
            {
                Saturation::BandParams p;
                p.sat  = raw (satParamID (false, b, "sat").toRawUTF8());
                p.dist = raw (satParamID (false, b, "dist").toRawUTF8());
                p.fuzz = raw (satParamID (false, b, "fuzz").toRawUTF8());
                return p;
            };
            const auto lo = band ("low"), md = band ("mid"), hi = band ("high");
            const float xl = raw (ParamID::satXLow), xh = raw (ParamID::satXHigh);
            for (int ch = 0; ch < 2; ++ch)
            {
                mSaturation[ch].setCrossovers (xl, xh);
                mSaturation[ch].setParams (lo, md, hi);
                mSaturation[ch].process (busCh[ch], numSamples);
            }
        }
        else if (mPostSatWasActive)
        {
            mSaturation[0].reset();
            mSaturation[1].reset();
        }
        mPostSatWasActive = on;
    }

    // ================= 12) POST FILTERS ======================================
    if (raw (ParamID::hpfActive) > 0.5f)
    {
        const float f = raw (ParamID::hpfFreq);
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
    if (raw (ParamID::lpfActive) > 0.5f)
    {
        const float f = raw (ParamID::lpfFreq);
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

    // ================= 13) FX: DELAY + REVERB (order-switchable) ============
    {
        const int  order    = (int) raw (ParamID::fxOrder);
        const bool delayOn  = raw (ParamID::delayActive)  > 0.5f;
        const bool reverbOn = raw (ParamID::reverbActive) > 0.5f;
        const int  revType  = (int) raw (ParamID::reverbType);

        auto runDelay = [&]
        {
            if (delayOn)
            {
                const float timeMs = raw (ParamID::delayTime);
                const float fb     = raw (ParamID::delayFeedback);
                const float mix    = raw (ParamID::delayMix);
                const float dsamp  = juce::jlimit (1.0f, (float) ((1 << 17) - 2),
                                                   (float) (timeMs * 0.001 * mSampleRate));
                mDelay.setDelay (dsamp);
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* d = busCh[ch];
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
                if (revType != mReverbLastType)
                {
                    mReverb.reset();
                    mSpring.reset();
                }
                const float size = raw (ParamID::reverbSize);
                const float damp = raw (ParamID::reverbDamp);
                const float mix  = raw (ParamID::reverbMix);

                if (revType == 1)      // Spring
                {
                    mSpring.process (busL, busR, numSamples, size, damp, mix);
                }
                else                   // Plate (tuned Freeverb)
                {
                    juce::dsp::Reverb::Parameters rp;
                    rp.roomSize   = 0.25f + size * 0.65f;
                    rp.damping    = damp;
                    rp.wetLevel   = mix;
                    rp.dryLevel   = 1.0f - mix;
                    rp.width      = 1.0f;
                    rp.freezeMode = 0.0f;
                    mReverb.setParameters (rp);

                    juce::dsp::AudioBlock<float> block (busCh, 2, (size_t) numSamples);
                    juce::dsp::ProcessContextReplacing<float> ctx (block);
                    mReverb.process (ctx);
                }
            }
            else if (mReverbWasActive)
            {
                mReverb.reset();
                mSpring.reset();
            }
            mReverbWasActive = reverbOn;
            mReverbLastType  = revType;
        };

        if (order == 0) { runDelay(); runReverb(); }
        else            { runReverb(); runDelay(); }
    }

    // ================= 14) CLEAN DI BLEND ====================================
    {
        const float cb = raw (ParamID::cleanBlend);
        if (cb > 1.0e-4f)
        {
            const float wetG = std::cos (cb * 0.5f * juce::MathConstants<float>::pi);
            const float clnG = std::sin (cb * 0.5f * juce::MathConstants<float>::pi);
            const float* diL = mCleanDI.getReadPointer (0);
            const float* diR = mCleanDI.getReadPointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                busL[i] = busL[i] * wetG + diL[i] * clnG;
                busR[i] = busR[i] * wetG + diR[i] * clnG;
            }
        }
    }

    // ================= 15) MASTER OUTPUT =====================================
    const float outGain = computeOutputGain();
    juce::FloatVectorOperations::multiply (busL, outGain, numSamples);
    juce::FloatVectorOperations::multiply (busR, outGain, numSamples);
    accumulatePeak (mMasterPeak, juce::jmax (blockPeak (busL, numSamples), blockPeak (busR, numSamples)));

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
    mCab[c].loaded.store (false);
    mCab[c].name = {};
    apvts.state.setProperty (kIrPathKey[c], "", nullptr);
}

// ===========================================================================
void NecronamAudioProcessor::parameterChanged (const juce::String&, float)
{
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
