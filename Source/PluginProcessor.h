#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "ResamplingNAM.h"
#include "Api560EQ.h"
#include "Saturation.h"

// ============================================================================
//  NECRONAM MAX  —  AudioProcessor (feature-loaded, true-stereo)
//
//  Signal chain:
//    sum-to-mono -> input gain -> noise gate
//      -> DUAL AMP STAGE (two NAM models) producing a STEREO bus:
//           Single   : Amp A only                  (centred)
//           Series   : Amp A -> Amp B              (centred)
//           Parallel : Amp A + Amp B, spread L/R   (true stereo)
//      -> overall amp-bus trim (OUT meter)
//      -> DUAL CAB IR mixer (stereo): IR A + IR B convolved & blended
//      -> [per-channel L/R] DC blocker -> API-560 EQ -> saturation -> HPF -> LPF
//      -> master output gain / mode
//
//  Utility DSP (gate, IR, DC, EQ, filters, saturation) is implemented in JUCE;
//  only the amp models come from NeuralAmpModelerCore. A separate plugin from
//  NECRONAM (distinct identity) so both can run side by side.
// ============================================================================
class NecronamAudioProcessor : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::AsyncUpdater
{
public:
    NecronamAudioProcessor();
    ~NecronamAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NECRONAM MAX"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int sizeInBytes) override;

    // ----- Amp / Cab management (called from the message thread) -------------
    // ampIndex / cabIndex: 0 = A, 1 = B.
    void loadNamModel (const juce::File&, int ampIndex);
    void clearNamModel (int ampIndex);
    void loadImpulseResponse (const juce::File&, int cabIndex);
    void clearImpulseResponse (int cabIndex);

    juce::String getLoadedModelName (int ampIndex) const { return mAmp[idx (ampIndex)].name; }
    juce::String getLoadedIRName    (int cabIndex) const { return mCab[idx (cabIndex)].name; }

    // True when the given amp's loaded model is an A2 "slimmable" model that
    // responds to the Quality control (older A1 models always return false).
    bool isModelSlimmable (int ampIndex) const { return mAmp[idx (ampIndex)].slimmable.load(); }
    bool isAnyModelSlimmable() const { return mAmp[0].slimmable.load() || mAmp[1].slimmable.load(); }

    // ----- Whole-plugin state (used by the preset browser) ------------------
    juce::ValueTree getStateTree() { return apvts.copyState(); }
    void setStateTree (const juce::ValueTree&);

    // Meter taps (peak since last read, linear). Read+reset from the editor.
    float fetchInputPeak()  { return mInPeak.exchange (0.0f); }
    float fetchNamPeak()    { return mNamPeak.exchange (0.0f); }
    float fetchMasterPeak() { return mMasterPeak.exchange (0.0f); }

    juce::AudioProcessorValueTreeState apvts;

    // Parameter IDs (single source of truth, shared with the editor).
    struct ParamID
    {
        static constexpr auto inputLevel   = "input_level";
        static constexpr auto namOutput    = "nam_output";     // overall amp-bus trim
        static constexpr auto outputLevel  = "output_level";   // master output
        static constexpr auto outputMode   = "output_mode";
        static constexpr auto inputCal     = "input_cal";
        static constexpr auto gateThresh   = "gate_threshold";
        static constexpr auto gateActive   = "gate_active";
        static constexpr auto quality      = "quality";        // A2 slimmable size (shared)

        // Dual amp.
        static constexpr auto ampRouting   = "amp_routing";    // Single / Series / Parallel
        static constexpr auto ampALevel    = "amp_a_level";
        static constexpr auto ampBLevel    = "amp_b_level";
        static constexpr auto ampSpread    = "amp_spread";     // parallel stereo spread

        // Dual cab IR mixer.
        static constexpr auto cabAActive   = "cab_a_active";
        static constexpr auto cabBActive   = "cab_b_active";
        static constexpr auto cabALevel    = "cab_a_level";
        static constexpr auto cabBLevel    = "cab_b_level";

        static constexpr auto hpfFreq      = "hpf_freq";
        static constexpr auto hpfActive    = "hpf_active";
        static constexpr auto lpfFreq      = "lpf_freq";
        static constexpr auto lpfActive    = "lpf_active";

        static constexpr auto eqActive     = "eq_active";
        // eq_0 .. eq_9 generated for the ten bands.

        static constexpr auto satActive    = "sat_active";        // post/output saturator
        // {low,mid,high}_{sat,dist,fuzz} generated for the post saturator.

        // Front saturation (pre-amp Flesh Render, immediately before the amps).
        static constexpr auto frontSatActive = "fsat_active";
        // fs_{low,mid,high}_{sat,dist,fuzz} generated for the front saturator.

        // Strobe tuner (engaging it mutes the plugin output).
        static constexpr auto tunerActive  = "tuner_active";

        // Delay (stereo, end of chain).
        static constexpr auto delayActive   = "dly_active";
        static constexpr auto delayTime     = "dly_time";       // ms
        static constexpr auto delayFeedback = "dly_fb";
        static constexpr auto delayMix      = "dly_mix";

        // Reverb (stereo, end of chain).
        static constexpr auto reverbActive  = "rev_active";
        static constexpr auto reverbSize    = "rev_size";
        static constexpr auto reverbDamp    = "rev_damp";
        static constexpr auto reverbMix     = "rev_mix";

        // FX order: 0 = Delay -> Reverb, 1 = Reverb -> Delay.
        static constexpr auto fxOrder       = "fx_order";

        // Front filter section (mono, between front saturation and the amps).
        static constexpr auto frontHpfFreq   = "front_hpf_freq";
        static constexpr auto frontHpfActive = "front_hpf_active";
        static constexpr auto frontLpfFreq   = "front_lpf_freq";
        static constexpr auto frontLpfActive = "front_lpf_active";

        // Per-amp bypass.
        static constexpr auto ampAActive = "amp_a_active";
        static constexpr auto ampBActive = "amp_b_active";

        // Clean DI blend at the output.
        static constexpr auto cleanBlend = "clean_blend";
    };

    enum class Routing { Single = 0, Series = 1, Parallel = 2 };

    static juce::String eqParamID (int band) { return "eq_" + juce::String (band); }

    // Saturation parameter id. front=true -> "fs_low_sat"; front=false -> "low_sat".
    static juce::String satParamID (bool front, const char* band, const char* stage)
    {
        return juce::String (front ? "fs_" : "") + band + "_" + stage;
    }

    // Copy the most recent `maxN` dry-input samples (for the tuner) into dest.
    // Lock-free read of the audio-thread ring; benign tearing is acceptable.
    int readTunerWindow (float* dest, int maxN) const
    {
        const int n = juce::jmin (maxN, kTunerRing);
        const int w = mTunerWrite.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            int idx = ((w - n + i) % kTunerRing + kTunerRing) % kTunerRing;
            dest[i] = mTunerRing[(size_t) idx];
        }
        return n;
    }

    // State-tree property keys for the loaded files (persisted with the preset).
    static constexpr const char* kNamPathKey[2] = { "namPathA", "namPathB" };
    static constexpr const char* kIrPathKey[2]  = { "irPathA",  "irPathB"  };

private:
    static int idx (int i) noexcept { return juce::jlimit (0, 1, i); }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    float computeOutputGain() const;
    void  updateLatency();
    void  reloadReferencedFiles();   // re-load models/IRs named in apvts.state

    // A2 quality + routing are applied off the audio thread (SetSlimmableSize and
    // setLatencySamples are not real-time safe). Parameter changes trigger an
    // async update on the message thread.
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void applyQuality();

    // ----- NAM amp slots (staged-swap, lock-free on the audio thread) --------
    struct AmpSlot
    {
        std::unique_ptr<ResamplingNAM> model;
        std::unique_ptr<ResamplingNAM> staged;
        juce::SpinLock                 swapLock;
        juce::String                   name;
        std::atomic<bool>              clear { false };
        std::atomic<bool>              slimmable { false };
    };
    AmpSlot mAmp[2];

    // ----- Cab IR slots (juce::dsp::Convolution loads/swaps on its own thread) -
    struct CabSlot
    {
        juce::dsp::Convolution conv;
        juce::String           name;
        std::atomic<bool>      loaded { false };
    };
    CabSlot mCab[2];

    // ----- Fixed DSP blocks, duplicated per channel for the stereo path ------
    Api560EQ                      mEQ[2];
    Saturation                    mSaturation[2];
    juce::dsp::IIR::Filter<float> mDCBlocker[2];       // ~10 Hz, always on
    juce::dsp::IIR::Filter<float> mHPF[2];             // user hi-pass
    juce::dsp::IIR::Filter<float> mLPF[2];             // user low-pass
    float mHpfCachedFreq = -1.0f;
    float mLpfCachedFreq = -1.0f;

    // ----- Front filter section (mono, between front saturation and amps) ----
    juce::dsp::IIR::Filter<float> mFrontHPF, mFrontLPF;
    float mFrontHpfCachedFreq = -1.0f;
    float mFrontLpfCachedFreq = -1.0f;

    // ----- Front saturation (mono, immediately before the dual amps) ---------
    Saturation mFrontSat;

    // ----- End-of-chain time FX (stereo), order-switchable, true-bypassed ----
    juce::dsp::Reverb mReverb;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> mDelay { 1 << 17 };

    // Edge-detection so a module is reset the instant it is switched off,
    // guaranteeing zero effect / no leftover tail while bypassed.
    bool mFrontSatWasActive = false;
    bool mDelayWasActive     = false;
    bool mReverbWasActive    = false;

    // ----- Tuner: lock-free ring capture of the dry input --------------------
    static constexpr int kTunerRing = 1 << 13;   // 8192 samples
    std::array<float, (size_t) kTunerRing> mTunerRing {};
    std::atomic<int> mTunerWrite { 0 };

    // ----- Noise gate (simple downward gate on the pre-amp mono signal) ------
    float mGateEnv  = 0.0f;
    float mGateGain = 1.0f;

    // ----- Scratch buffers ---------------------------------------------------
    juce::AudioBuffer<float> mMonoIn;     // summed mono amp input
    juce::AudioBuffer<float> mAmpAOut;    // Amp A output (mono)
    juce::AudioBuffer<float> mAmpBOut;    // Amp B output (mono)
    juce::AudioBuffer<float> mBus;        // stereo bus (cab + post chain)
    juce::AudioBuffer<float> mCabScratch; // per-cab convolution scratch (stereo)
    juce::AudioBuffer<float> mCabSum;     // cab mix accumulator (stereo)
    juce::AudioBuffer<float> mCleanDI;    // clean DI (mono) for the output blend

    // Meter accumulators (peak, linear), reset when the editor reads them.
    std::atomic<float> mInPeak     { 0.0f };
    std::atomic<float> mNamPeak    { 0.0f };
    std::atomic<float> mMasterPeak { 0.0f };

    double mSampleRate = 44100.0;
    int    mMaxBlock   = 512;

    std::atomic<bool> mPrepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NecronamAudioProcessor)
};
