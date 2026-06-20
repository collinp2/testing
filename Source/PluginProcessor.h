#pragma once

#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "ResamplingNAM.h"
#include "Api560EQ.h"
#include "Saturation.h"

// ============================================================================
//  NECRONAM  —  AudioProcessor
//  Signal chain:
//    input gain -> noise gate -> NAM model -> NAM output -> cab IR
//    -> DC blocker -> API-560 EQ -> saturation -> HPF -> LPF -> output
//  Utility DSP (gate, IR, DC) is implemented in JUCE; only the model comes
//  from NeuralAmpModelerCore.
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

    const juce::String getName() const override { return "NECRONAM"; }
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

    // ----- Model / IR management (called from the message thread) -----------
    void loadNamModel (const juce::File&);
    void clearNamModel();
    void loadImpulseResponse (const juce::File&);
    void clearImpulseResponse();

    juce::String getLoadedModelName() const { return mModelName; }
    juce::String getLoadedIRName()    const { return mIRName;    }

    // True when the loaded model is an A2 "slimmable" model that responds to
    // the Quality control (older A1 models always return false).
    bool isModelSlimmable() const { return mModelSlimmable.load(); }

    // Meter taps (peak since last read, linear). Read+reset from the editor.
    float fetchInputPeak()  { return mInPeak.exchange (0.0f); }
    float fetchNamPeak()    { return mNamPeak.exchange (0.0f); }
    float fetchMasterPeak() { return mMasterPeak.exchange (0.0f); }

    juce::AudioProcessorValueTreeState apvts;

    // Parameter IDs (single source of truth, shared with the editor).
    struct ParamID
    {
        static constexpr auto inputLevel   = "input_level";
        static constexpr auto namOutput    = "nam_output";     // NAM module output trim
        static constexpr auto outputLevel  = "output_level";   // master output
        static constexpr auto outputMode   = "output_mode";
        static constexpr auto inputCal     = "input_cal";
        static constexpr auto gateThresh   = "gate_threshold";
        static constexpr auto gateActive   = "gate_active";
        static constexpr auto irActive     = "ir_active";
        static constexpr auto quality      = "quality";        // A2 slimmable size

        static constexpr auto hpfFreq      = "hpf_freq";
        static constexpr auto hpfActive    = "hpf_active";
        static constexpr auto lpfFreq      = "lpf_freq";
        static constexpr auto lpfActive    = "lpf_active";

        static constexpr auto eqActive     = "eq_active";
        // eq_0 .. eq_9 generated for the ten bands.

        static constexpr auto satActive    = "sat_active";
        // {low,mid,high}_{sat,dist,fuzz} generated for the saturator.
    };

    static juce::String eqParamID (int band) { return "eq_" + juce::String (band); }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    float computeOutputGain() const;
    void  updateLatency();

    // A2 quality control: applied off the audio thread (SetSlimmableSize is not
    // real-time safe). Parameter changes trigger an async update on the message
    // thread, which calls applyQuality().
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void applyQuality();

    // ----- NAM model (staged-swap, lock-free on the audio thread) -----------
    std::unique_ptr<ResamplingNAM> mModel;
    std::unique_ptr<ResamplingNAM> mStagedModel;
    juce::SpinLock                 mModelSwapLock;
    juce::String                   mModelName;
    std::atomic<bool>              mClearModel { false };
    std::atomic<bool>              mModelSlimmable { false };

    // ----- Cab IR (juce::dsp::Convolution loads/swaps on its own thread) ------
    juce::dsp::Convolution mConvolution;
    juce::String           mIRName;
    std::atomic<bool>      mIRLoaded { false };

    // ----- Fixed DSP blocks (JUCE) ------------------------------------------
    Api560EQ                      mEQ;
    Saturation                    mSaturation;
    juce::dsp::IIR::Filter<float> mDCBlocker;          // ~10 Hz, always on
    juce::dsp::IIR::Filter<float> mHPF;                // user hi-pass
    juce::dsp::IIR::Filter<float> mLPF;                // user low-pass
    float mHpfCachedFreq = -1.0f;
    float mLpfCachedFreq = -1.0f;

    // ----- Noise gate (simple downward gate on the pre-NAM signal) ----------
    float mGateEnv  = 0.0f;
    float mGateGain = 1.0f;

    // ----- Scratch buffers ---------------------------------------------------
    juce::AudioBuffer<float> mMonoBuffer;     // summed mono working buffer
    juce::AudioBuffer<float> mModelOutBuffer; // NAM model output

    // Meter accumulators (peak, linear), reset when the editor reads them.
    std::atomic<float> mInPeak     { 0.0f };
    std::atomic<float> mNamPeak    { 0.0f };
    std::atomic<float> mMasterPeak { 0.0f };

    double mSampleRate = 44100.0;
    int    mMaxBlock   = 512;

    std::atomic<bool> mPrepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NecronamAudioProcessor)
};
