#include "PluginProcessor.h"
#include "PluginEditor.h"

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
            // Referencing create_config keeps each architecture's TU (and its
            // self-registration static initializer) from being dead-stripped.
            // If that static init already ran, registerParser throws "already
            // registered" — which we tolerate, so we end up registered exactly
            // once whether or not the strip happened.
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

    juce::String dbToText (float v, int)  { return juce::String (v, 1) + " dB"; }
    juce::String hzToText (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    }
}

// ===========================================================================
NecronamAudioProcessor::NecronamAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createLayout())
{
    // Watch the A2 quality control so we can apply it off the audio thread.
    apvts.addParameterListener (ParamID::quality, this);
}

NecronamAudioProcessor::~NecronamAudioProcessor()
{
    apvts.removeParameterListener (ParamID::quality, this);
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

    // ---- NAM / levels ----
    params.push_back (fParam (ParamID::inputLevel,  "Input Level",  Range (-20.0f, 20.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::namOutput,   "NAM Output",   Range (-40.0f, 40.0f, 0.1f), 0.0f, dbToText));
    params.push_back (fParam (ParamID::outputLevel, "Output Level", Range (-40.0f, 40.0f, 0.1f), 0.0f, dbToText));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::outputMode, 1 }, "Output Mode",
        juce::StringArray { "Raw", "Normalized", "Calibrated" }, 0));
    params.push_back (fParam (ParamID::inputCal, "Input Calibration", Range (0.0f, 30.0f, 0.1f), 12.0f,
                              [] (float v, int) { return juce::String (v, 1) + " dBu"; }));
    params.push_back (fParam (ParamID::gateThresh, "Gate Threshold", Range (-100.0f, 0.0f, 0.5f), -80.0f, dbToText));
    params.push_back (bParam (ParamID::gateActive, "Gate", false));
    params.push_back (bParam (ParamID::irActive,   "IR",   true));

    // A2 quality / efficiency. 0 = max efficiency (lite), 1 = max quality (full).
    // Defaults to max quality; only affects A2 "slimmable" models.
    params.push_back (fParam (ParamID::quality, "Quality", Range (0.0f, 1.0f, 0.01f), 1.0f,
                              [] (float v, int)
                              {
                                  if (v >= 0.999f) return juce::String ("Max Quality");
                                  if (v <= 0.001f) return juce::String ("Max Efficiency");
                                  return juce::String (juce::roundToInt (v * 100.0f)) + "%";
                              }));

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

    // ---- Saturation (Flesh Render) ----
    params.push_back (bParam (ParamID::satActive, "Saturation", false));
    const char* bands[3] = { "low", "mid", "high" };
    const char* bandsUp[3] = { "Low", "Mid", "High" };
    const char* stages[3] = { "sat", "dist", "fuzz" };
    const char* stagesUp[3] = { "Saturation", "Distortion", "Fuzz" };
    for (int b = 0; b < 3; ++b)
        for (int s = 0; s < 3; ++s)
            params.push_back (fParam (juce::String (bands[b]) + "_" + stages[s],
                                      juce::String (bandsUp[b]) + " " + stagesUp[s],
                                      Range (0.0f, 1.0f, 0.001f), 0.0f,
                                      [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }));

    return { params.begin(), params.end() };
}

// ===========================================================================
void NecronamAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    mSampleRate = sampleRate;
    mMaxBlock   = samplesPerBlock;

    mMonoBuffer.setSize (1, samplesPerBlock);
    mModelOutBuffer.setSize (1, samplesPerBlock);

    mEQ.prepare (sampleRate, samplesPerBlock);
    mSaturation.prepare (sampleRate, samplesPerBlock);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    mHPF.prepare (spec);
    mLPF.prepare (spec);
    mHpfCachedFreq = mLpfCachedFreq = -1.0f;

    mConvolution.prepare (spec);
    *mDCBlocker.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 10.0);
    mDCBlocker.reset();
    mGateEnv = 0.0f;
    mGateGain = 1.0f;

    // Reset the active AND staged models. The staged one matters: when a model
    // is restored from state (setStateInformation) before prepareToPlay, it was
    // staged without a Reset; it must be sized here before it can be swapped in
    // and processed, or WaveNet writes out of bounds.
    {
        const juce::SpinLock::ScopedLockType l (mModelSwapLock);
        if (mModel != nullptr)       mModel->Reset (sampleRate, samplesPerBlock);
        if (mStagedModel != nullptr) mStagedModel->Reset (sampleRate, samplesPerBlock);
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

    if (out != mono && out != stereo)              return false;
    if (in != mono && in != stereo && ! in.isDisabled()) return false;
    return true;
}

// ===========================================================================
float NecronamAudioProcessor::computeOutputGain() const
{
    const int   mode      = (int) apvts.getRawParameterValue (ParamID::outputMode)->load();
    const float outDb     = apvts.getRawParameterValue (ParamID::outputLevel)->load();
    const float inputCal  = apvts.getRawParameterValue (ParamID::inputCal)->load();

    float gain = juce::Decibels::decibelsToGain (outDb);

    if (mode == 1 && mModel != nullptr && mModel->HasLoudness())          // Normalized -> -18 dBFS
        gain *= juce::Decibels::decibelsToGain (-18.0f - (float) mModel->GetLoudness());
    else if (mode == 2 && mModel != nullptr && mModel->HasOutputLevel())  // Calibrated -> real dBu
        gain *= juce::Decibels::decibelsToGain ((float) mModel->GetOutputLevel() - inputCal);

    return gain;
}

void NecronamAudioProcessor::updateLatency()
{
    int latency = 0;
    if (mStagedModel != nullptr)   latency = mStagedModel->GetLatency();
    else if (mModel != nullptr)    latency = mModel->GetLatency();
    setLatencySamples (latency);
}

// ===========================================================================
void NecronamAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();

    // ---- Hot-swap staged model; honour clear request ----
    {
        const juce::SpinLock::ScopedTryLockType l (mModelSwapLock);
        if (l.isLocked() && mStagedModel != nullptr)
            mModel = std::move (mStagedModel);
    }
    if (mClearModel.exchange (false))
        mModel.reset();

    // ---- Sum input to mono ----
    float* mono = mMonoBuffer.getWritePointer (0);
    if (numIn >= 2)
    {
        const float* L = buffer.getReadPointer (0);
        const float* R = buffer.getReadPointer (1);
        for (int i = 0; i < numSamples; ++i)
            mono[i] = 0.5f * (L[i] + R[i]);
    }
    else if (numIn == 1)
    {
        juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0), numSamples);
    }
    else
    {
        juce::FloatVectorOperations::clear (mono, numSamples);
    }

    // ---- Input gain (+ calibrated input alignment) ----
    const int   mode      = (int) apvts.getRawParameterValue (ParamID::outputMode)->load();
    const float inputCal  = apvts.getRawParameterValue (ParamID::inputCal)->load();
    float inputGainDb     = apvts.getRawParameterValue (ParamID::inputLevel)->load();
    if (mode == 2 && mModel != nullptr && mModel->HasInputLevel())
        inputGainDb += inputCal - (float) mModel->GetInputLevel();
    juce::FloatVectorOperations::multiply (mono, juce::Decibels::decibelsToGain (inputGainDb), numSamples);

    // NAM input meter (post input gain — what the model actually sees).
    accumulatePeak (mInPeak, blockPeak (mono, numSamples));

    // ---- Noise gate (simple downward gate on the pre-NAM signal) ----
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

    // ---- NAM model (resampled to/from its native rate) ----
    float* monoPtrs[1]     = { mono };
    float* modelOutPtrs[1] = { mModelOutBuffer.getWritePointer (0) };
    float** stage = monoPtrs;
    if (mModel != nullptr)
    {
        mModel->process (stage, modelOutPtrs, numSamples);
        stage = modelOutPtrs;
    }

    // ---- NAM module output trim ----
    const float namOutGain = juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (ParamID::namOutput)->load());
    juce::FloatVectorOperations::multiply (stage[0], namOutGain, numSamples);

    // NAM output meter (post module output trim).
    accumulatePeak (mNamPeak, blockPeak (stage[0], numSamples));

    // ---- Hand off to the JUCE-domain post chain ----
    float* work = mMonoBuffer.getWritePointer (0);
    juce::FloatVectorOperations::copy (work, stage[0], numSamples);

    // ---- Cab IR ----
    if (mIRLoaded.load() && apvts.getRawParameterValue (ParamID::irActive)->load() > 0.5f)
    {
        float* ch[1] = { work };
        juce::dsp::AudioBlock<float> block (ch, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        mConvolution.process (ctx);
    }

    // ---- DC blocker (~10 Hz, always) ----
    {
        float* ch[1] = { work };
        juce::dsp::AudioBlock<float> block (ch, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        mDCBlocker.process (ctx);
    }

    // EQ
    if (apvts.getRawParameterValue (ParamID::eqActive)->load() > 0.5f)
    {
        for (int i = 0; i < Api560EQ::kNumBands; ++i)
            mEQ.setBandGain (i, apvts.getRawParameterValue (eqParamID (i))->load());
        mEQ.process (work, numSamples);
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
        mSaturation.setParams (band ("low"), band ("mid"), band ("high"));
        mSaturation.process (work, numSamples);
    }

    // Hi-pass
    if (apvts.getRawParameterValue (ParamID::hpfActive)->load() > 0.5f)
    {
        const float f = apvts.getRawParameterValue (ParamID::hpfFreq)->load();
        if (std::abs (f - mHpfCachedFreq) > 0.5f)
        {
            mHpfCachedFreq = f;
            *mHPF.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (mSampleRate, f);
        }
        float* ch[1] = { work };
        juce::dsp::AudioBlock<float> block (ch, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        mHPF.process (ctx);
    }

    // Low-pass
    if (apvts.getRawParameterValue (ParamID::lpfActive)->load() > 0.5f)
    {
        const float f = apvts.getRawParameterValue (ParamID::lpfFreq)->load();
        if (std::abs (f - mLpfCachedFreq) > 0.5f)
        {
            mLpfCachedFreq = f;
            *mLPF.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass (mSampleRate, f);
        }
        float* ch[1] = { work };
        juce::dsp::AudioBlock<float> block (ch, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        mLPF.process (ctx);
    }

    // ---- Output gain / mode ----
    juce::FloatVectorOperations::multiply (work, computeOutputGain(), numSamples);

    // Master output meter (whole-plugin output).
    accumulatePeak (mMasterPeak, blockPeak (work, numSamples));

    // ---- Fan the mono result out to every output channel ----
    for (int ch = 0; ch < numOut; ++ch)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (ch), work, numSamples);
}

// ===========================================================================
void NecronamAudioProcessor::loadNamModel (const juce::File& file)
{
    ensureNamArchitecturesRegistered();
    try
    {
        const auto p = std::filesystem::u8path (file.getFullPathName().toStdString());
        std::unique_ptr<nam::DSP> dsp = nam::get_dsp (p);
        if (dsp == nullptr)
        {
            mModelName = "LOAD FAILED (null)";
            return;
        }

        auto wrapped = std::make_unique<ResamplingNAM> (std::move (dsp));
        if (mPrepared.load())
            wrapped->Reset (mSampleRate, mMaxBlock);

        const bool slimmable = wrapped->IsSlimmable();
        // Apply the current quality before the model goes live (not RT-safe).
        wrapped->SetQuality (apvts.getRawParameterValue (ParamID::quality)->load());

        {
            const juce::SpinLock::ScopedLockType l (mModelSwapLock);
            mStagedModel = std::move (wrapped);
        }

        mModelSlimmable.store (slimmable);
        mModelName = file.getFileNameWithoutExtension();
        apvts.state.setProperty ("namPath", file.getFullPathName(), nullptr);
        updateLatency();
    }
    catch (const std::exception& e)
    {
        mModelName = "LOAD FAILED";
        juce::Logger::writeToLog (juce::String ("NECRONAM: failed to load model: ") + e.what());
    }
}

void NecronamAudioProcessor::clearNamModel()
{
    mClearModel.store (true);
    {
        const juce::SpinLock::ScopedLockType l (mModelSwapLock);
        mStagedModel.reset();
    }
    mModelName = {};
    mModelSlimmable.store (false);
    apvts.state.setProperty ("namPath", "", nullptr);
}

// ===========================================================================
void NecronamAudioProcessor::parameterChanged (const juce::String&, float)
{
    // May fire on the audio thread during automation; defer the (non-RT-safe)
    // SetSlimmableSize call to the message thread.
    triggerAsyncUpdate();
}

void NecronamAudioProcessor::handleAsyncUpdate()
{
    applyQuality();
}

void NecronamAudioProcessor::applyQuality()
{
    const double v = apvts.getRawParameterValue (ParamID::quality)->load();
    // Hold the swap lock so the audio thread can't reassign mModel mid-apply.
    const juce::SpinLock::ScopedLockType l (mModelSwapLock);
    if (mModel != nullptr)        mModel->SetQuality (v);
    if (mStagedModel != nullptr)  mStagedModel->SetQuality (v);
}

void NecronamAudioProcessor::loadImpulseResponse (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    // juce::dsp::Convolution loads on its own background thread and swaps the IR
    // in atomically; it resamples the IR to the current spec automatically.
    mConvolution.loadImpulseResponse (file,
                                      juce::dsp::Convolution::Stereo::no,
                                      juce::dsp::Convolution::Trim::no,
                                      0);
    mIRLoaded.store (true);
    mIRName = file.getFileNameWithoutExtension();
    apvts.state.setProperty ("irPath", file.getFullPathName(), nullptr);
}

void NecronamAudioProcessor::clearImpulseResponse()
{
    mIRLoaded.store (false);   // bypass; the convolver simply isn't run
    mIRName = {};
    apvts.state.setProperty ("irPath", "", nullptr);
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
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            const auto namPath = apvts.state.getProperty ("namPath").toString();
            if (namPath.isNotEmpty() && juce::File (namPath).existsAsFile())
                loadNamModel (juce::File (namPath));

            const auto irPath = apvts.state.getProperty ("irPath").toString();
            if (irPath.isNotEmpty() && juce::File (irPath).existsAsFile())
                loadImpulseResponse (juce::File (irPath));
        }
    }
}

// ===========================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NecronamAudioProcessor();
}
