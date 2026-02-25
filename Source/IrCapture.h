#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

enum class CapturePhase
{
    Idle,
    PlayingSweep,
    PlayingNoise,
    Processing,
    Done,
    Clipped
};

class IrCapture
{
public:
    IrCapture();
    ~IrCapture();

    /** Call from the message thread before starting a capture. */
    void prepare (double sampleRate, int blockSize);

    /** Start a new capture. Call from message thread. */
    void startCapture();

    /** Call from audio thread each block. Fills outputBuffer[0] with the
        stimulus signal and reads inputBuffer[0] as the recorded signal.
        Both buffers must be at least numSamples long. */
    void processBlock (const float* inputBuffer, float* outputBuffer, int numSamples);

    /** Returns true when FFT processing is complete and IR is ready. */
    bool isComplete() const { return phase.load() == CapturePhase::Done; }

    CapturePhase getPhase() const { return phase.load(); }

    /** Call from message thread to return to Idle after a clip abort. */
    void resetToIdle();

    /** True if the last capture was aborted due to input clipping. */
    bool wasClipped() const { return clipDetected.load(); }

    /** Set the playback gain for the test signal (linear, audio-thread safe). */
    void setPlaybackGain (float gain) { playbackGain.store (gain); }

    /** Call from message thread after isComplete(). Returns a mono IR buffer.
        Resets state back to Idle. */
    juce::AudioBuffer<float> retrieveIR();

    /** Save the IR to the given file path. Returns empty string on success or error message. */
    juce::String saveToFile (const juce::AudioBuffer<float>& ir,
                             const juce::File& outputFile);

    /** Input level for metering (0..1 linear), updated each processBlock. */
    float getInputLevel() const  { return inputLevel.load(); }
    float getOutputLevel() const { return outputLevel.load(); }

private:
    void generateSweep();
    void generateNoise();
    void runFFTProcessing();

    juce::AudioBuffer<float> computeIRfromSweep();
    juce::AudioBuffer<float> computeIRfromNoise();
    juce::AudioBuffer<float> averageAndFinalize (const juce::AudioBuffer<float>& a,
                                                  const juce::AudioBuffer<float>& b);

    double sampleRate { 48000.0 };
    int    blockSize  { 512 };

    static constexpr double kSweepDuration  = 3.0;
    static constexpr double kNoiseDuration  = 3.0;
    static constexpr double kSilencePad     = 0.5;
    static constexpr double kF1             = 20.0;
    static constexpr double kF2             = 20000.0;
    static constexpr double kFadeDuration   = 0.010; // 10 ms

    std::atomic<CapturePhase> phase { CapturePhase::Idle };

    // Pre-generated stimulus
    std::vector<float> sweepSignal;
    std::vector<float> noiseSignal;

    // Capture buffers (allocated on prepare / startCapture)
    std::vector<float> sweepCapture;
    std::vector<float> noiseCapture;

    // Playback / record positions (audio thread only)
    int  playPos  { 0 };
    int  recPos   { 0 };

    // Silence pad counter (audio thread)
    int  silenceRemaining { 0 };
    int  silenceSamples   { 0 };

    // Total samples in each phase
    int sweepTotalSamples  { 0 };
    int noiseTotalSamples  { 0 };

    // Background processing
    juce::ThreadPool threadPool { 1 };
    juce::AudioBuffer<float> resultIR;

    std::atomic<float> inputLevel    { 0.0f };
    std::atomic<float> outputLevel  { 0.0f };
    std::atomic<bool>  clipDetected { false };
    std::atomic<float> playbackGain { 1.0f };
};
