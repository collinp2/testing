#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "ResamplingNAM.h"
#include "Api560EQ.h"
#include "Saturation.h"
#include "DriveCircuits.h"
#include "SagProcessor.h"
#include "OptoCompressor.h"
#include "SpringReverb.h"

// ============================================================================
//  NECRONAM MAX v2  —  AudioProcessor
//
//  Signal chain (strict order — the UI tabs follow it):
//    master input level  (master strip, all pages)
//      -> noise gate     (detector always keyed from this direct signal;
//                         gain applied PRE or POST amp via a position switch)
//      -> Flesh Render PRE   (multiband saturation, sweepable crossovers)
//      -> DRIVE section      (switchable circuit: TC-style integrated preamp
//                             or generic Tube Screamer)
//      -> LOW CUT
//      -> DUAL AMP STAGE (two NAM models)
//           input mode MONO  : Single / Series / Parallel (+ spread)
//           input mode STEREO: dual mono — L -> Amp A, R -> Amp B, hard panned
//      -> [gate applied here when position = POST]
//      -> SAG            (tube power-amp sag, 0..10)
//      -> CAB IR         (mono: dual-IR mixer on the bus; stereo: Cab A -> L,
//                         Cab B -> R, independent)
//      -> DC blocker
//      -> API-560 graphic EQ
//      -> LA-2A style compressor (one knob, auto make-up, GR meter)
//      -> Flesh Render POST  (stereo, sweepable crossovers)
//      -> post hi-pass / low-pass
//      -> DELAY + REVERB (order-switchable; reverb = plate or spring;
//                         all true-bypassed)
//      -> clean DI blend
//      -> master output (Raw / Normalized / Calibrated)
//
//  The tuner taps the direct input (post input gain) and mutes the output
//  while engaged.
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

    bool isModelSlimmable (int ampIndex) const { return mAmp[idx (ampIndex)].slimmable.load(); }
    bool isAnyModelSlimmable() const { return mAmp[0].slimmable.load() || mAmp[1].slimmable.load(); }

    // ----- Whole-plugin state (used by the preset browser) ------------------
    juce::ValueTree getStateTree() { return apvts.copyState(); }
    void setStateTree (const juce::ValueTree&);

    // Meter taps (peak since last read, linear). Read+reset from the editor.
    float fetchInputPeak()  { return mInPeak.exchange (0.0f); }
    float fetchNamPeak()    { return mNamPeak.exchange (0.0f); }
    float fetchMasterPeak() { return mMasterPeak.exchange (0.0f); }
    // Compressor gain reduction (dB, peak since last read).
    float fetchGainReductionDb() { return mComp.fetchGainReductionDb(); }

    juce::AudioProcessorValueTreeState apvts;

    // Parameter IDs (single source of truth, shared with the editor).
    struct ParamID
    {
        // Master / IO
        static constexpr auto inputLevel   = "input_level";
        static constexpr auto namOutput    = "nam_output";     // amp-bus trim
        static constexpr auto outputLevel  = "output_level";
        static constexpr auto outputMode   = "output_mode";
        static constexpr auto inputCal     = "input_cal";
        static constexpr auto cleanBlend   = "clean_blend";
        static constexpr auto inputMode    = "input_mode";     // Mono / Stereo (dual mono)

        // Gate (detector always keyed from the direct pre-amp signal)
        static constexpr auto gateThresh   = "gate_threshold";
        static constexpr auto gateActive   = "gate_active";
        static constexpr auto gatePosition = "gate_position";  // Pre Amp / Post Amp

        // Flesh Render PRE (front)
        static constexpr auto frontSatActive = "fsat_active";
        static constexpr auto frontSatXLow   = "fsat_xlow";
        static constexpr auto frontSatXHigh  = "fsat_xhigh";
        // fs_{low,mid,high}_{sat,dist,fuzz} generated.

        // Drive section (switchable circuit)
        static constexpr auto driveActive  = "drive_active";
        static constexpr auto driveCircuit = "drive_circuit";  // TC Preamp / Tube Screamer
        static constexpr auto tcGain   = "tc_gain";
        static constexpr auto tcBass   = "tc_bass";
        static constexpr auto tcMid    = "tc_mid";
        static constexpr auto tcTreble = "tc_treble";
        static constexpr auto tcLevel  = "tc_level";
        static constexpr auto tsDrive  = "ts_drive";
        static constexpr auto tsTone   = "ts_tone";
        static constexpr auto tsLevel  = "ts_level";

        // Low cut (pre-amp)
        static constexpr auto lowCutFreq   = "front_hpf_freq";   // id kept from v1
        static constexpr auto lowCutActive = "front_hpf_active";

        // Dual amp
        static constexpr auto ampRouting   = "amp_routing";
        static constexpr auto ampALevel    = "amp_a_level";
        static constexpr auto ampBLevel    = "amp_b_level";
        static constexpr auto ampSpread    = "amp_spread";
        static constexpr auto ampAActive   = "amp_a_active";
        static constexpr auto ampBActive   = "amp_b_active";
        static constexpr auto quality      = "quality";

        // Sag
        static constexpr auto sagAmount    = "sag_amount";

        // Dual cab
        static constexpr auto cabAActive   = "cab_a_active";
        static constexpr auto cabBActive   = "cab_b_active";
        static constexpr auto cabALevel    = "cab_a_level";
        static constexpr auto cabBLevel    = "cab_b_level";

        // EQ
        static constexpr auto eqActive     = "eq_active";
        // eq_0 .. eq_9 generated.

        // LA-2A style compressor
        static constexpr auto compActive   = "comp_active";
        static constexpr auto compAmount   = "comp_amount";    // Peak Reduction 0..100

        // Flesh Render POST
        static constexpr auto satActive    = "sat_active";
        static constexpr auto satXLow      = "sat_xlow";
        static constexpr auto satXHigh     = "sat_xhigh";
        // {low,mid,high}_{sat,dist,fuzz} generated.

        // Post filters
        static constexpr auto hpfFreq      = "hpf_freq";
        static constexpr auto hpfActive    = "hpf_active";
        static constexpr auto lpfFreq      = "lpf_freq";
        static constexpr auto lpfActive    = "lpf_active";

        // FX
        static constexpr auto delayActive   = "dly_active";
        static constexpr auto delayTime     = "dly_time";
        static constexpr auto delayFeedback = "dly_fb";
        static constexpr auto delayMix      = "dly_mix";
        static constexpr auto reverbActive  = "rev_active";
        static constexpr auto reverbType    = "rev_type";      // Plate / Spring
        static constexpr auto reverbSize    = "rev_size";
        static constexpr auto reverbDamp    = "rev_damp";
        static constexpr auto reverbMix     = "rev_mix";
        static constexpr auto fxOrder       = "fx_order";

        // Tuner
        static constexpr auto tunerActive  = "tuner_active";
    };

    enum class Routing   { Single = 0, Series = 1, Parallel = 2 };
    enum class InputMode { Mono = 0, Stereo = 1 };

    static juce::String eqParamID (int band) { return "eq_" + juce::String (band); }

    // Saturation stage param id. front=true -> "fs_low_sat"; front=false -> "low_sat".
    static juce::String satParamID (bool front, const char* band, const char* stage)
    {
        return juce::String (front ? "fs_" : "") + band + "_" + stage;
    }

    // Copy the most recent `maxN` dry-input samples (for the tuner) into dest.
    int readTunerWindow (float* dest, int maxN) const
    {
        const int n = juce::jmin (maxN, kTunerRing);
        const int w = mTunerWrite.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            int idx2 = ((w - n + i) % kTunerRing + kTunerRing) % kTunerRing;
            dest[i] = mTunerRing[(size_t) idx2];
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
    void  reloadReferencedFiles();

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

    // ----- Cab IR slots -------------------------------------------------------
    struct CabSlot
    {
        juce::dsp::Convolution conv;
        juce::String           name;
        std::atomic<bool>      loaded { false };
    };
    CabSlot mCab[2];

    // ----- Pre-chain DSP (per lane: 0 = mono / left, 1 = right) --------------
    Saturation    mFrontSat[2];
    DriveCircuits mDrive;
    juce::dsp::IIR::Filter<float> mLowCut[2];
    float mLowCutCachedFreq = -1.0f;

    // Gate: envelopes per lane; per-sample gains buffered so the gain can be
    // applied pre OR post amp (always keyed from the direct signal).
    float mGateEnv[2]  { 0.0f, 0.0f };
    float mGateGain[2] { 1.0f, 1.0f };
    juce::AudioBuffer<float> mGateBuf;

    // ----- Post-chain DSP (stereo) --------------------------------------------
    SagProcessor   mSag;
    Api560EQ       mEQ[2];
    OptoCompressor mComp;
    Saturation     mSaturation[2];                    // Flesh Render POST
    juce::dsp::IIR::Filter<float> mDCBlocker[2];
    juce::dsp::IIR::Filter<float> mHPF[2], mLPF[2];   // post filters
    float mHpfCachedFreq = -1.0f;
    float mLpfCachedFreq = -1.0f;

    // FX
    juce::dsp::Reverb mReverb;                        // plate
    SpringReverb      mSpring;                        // spring
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> mDelay { 1 << 17 };

    // True-bypass edge flags.
    bool mFrontSatWasActive = false;
    bool mDriveWasActive    = false;
    int  mDriveLastCircuit  = -1;
    bool mCompWasActive     = false;
    bool mDelayWasActive    = false;
    bool mReverbWasActive   = false;
    int  mReverbLastType    = -1;
    bool mGateWasActive     = false;
    bool mPostSatWasActive  = false;

    // ----- Tuner capture ring --------------------------------------------------
    static constexpr int kTunerRing = 1 << 13;
    std::array<float, (size_t) kTunerRing> mTunerRing {};
    std::atomic<int> mTunerWrite { 0 };

    // ----- Scratch buffers ------------------------------------------------------
    juce::AudioBuffer<float> mLanes;      // pre-chain lanes (2ch; mono mode uses ch0)
    juce::AudioBuffer<float> mAmpAOut;    // Amp A output (mono)
    juce::AudioBuffer<float> mAmpBOut;    // Amp B output (mono)
    juce::AudioBuffer<float> mBus;        // stereo bus
    juce::AudioBuffer<float> mCabScratch;
    juce::AudioBuffer<float> mCabSum;
    juce::AudioBuffer<float> mCleanDI;    // clean DI (stereo lanes)

    // Meter accumulators.
    std::atomic<float> mInPeak     { 0.0f };
    std::atomic<float> mNamPeak    { 0.0f };
    std::atomic<float> mMasterPeak { 0.0f };

    double mSampleRate = 44100.0;
    int    mMaxBlock   = 512;

    std::atomic<bool> mPrepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NecronamAudioProcessor)
};
