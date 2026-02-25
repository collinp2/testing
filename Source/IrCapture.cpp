#include "IrCapture.h"

IrCapture::IrCapture() = default;
IrCapture::~IrCapture() { threadPool.removeAllJobs (true, 5000); }

//==============================================================================
void IrCapture::prepare (double sr, int block)
{
    sampleRate = sr;
    blockSize  = block;
    phase.store (CapturePhase::Idle);

    silenceSamples = static_cast<int> (kSilencePad * sampleRate);

    generateSweep();
    generateNoise();

    sweepTotalSamples = static_cast<int> (sweepSignal.size()) + silenceSamples;
    noiseTotalSamples = static_cast<int> (noiseSignal.size()) + silenceSamples;

    sweepCapture.assign (sweepTotalSamples, 0.0f);
    noiseCapture.assign (noiseTotalSamples, 0.0f);
}

//==============================================================================
void IrCapture::generateSweep()
{
    const int N    = static_cast<int> (kSweepDuration * sampleRate);
    const double T = kSweepDuration;
    const double L = T / std::log (kF2 / kF1);
    const double w1 = juce::MathConstants<double>::twoPi * kF1;
    const int fadeSamples = static_cast<int> (kFadeDuration * sampleRate);

    sweepSignal.resize (N);
    for (int i = 0; i < N; ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        sweepSignal[i] = 0.9f * static_cast<float> (
            std::sin (w1 * L * (std::exp (t / L) - 1.0)));
    }

    // Fade in / fade out
    for (int i = 0; i < fadeSamples && i < N; ++i)
    {
        float env = static_cast<float> (i) / static_cast<float> (fadeSamples);
        sweepSignal[i] *= env;
        sweepSignal[N - 1 - i] *= env;
    }
}

//==============================================================================
void IrCapture::generateNoise()
{
    const int N = static_cast<int> (kNoiseDuration * sampleRate);
    const int fadeSamples = static_cast<int> (kFadeDuration * sampleRate);

    // Generate white noise then band-limit with a simple FFT filter
    juce::Random rng (42);
    std::vector<float> white (N);
    for (int i = 0; i < N; ++i)
        white[i] = rng.nextFloat() * 2.0f - 1.0f;

    // Pad to next power of two for FFT
    const int fftOrder = static_cast<int> (
        std::ceil (std::log2 (static_cast<double> (N))));
    const int fftSize = 1 << fftOrder;

    juce::dsp::FFT fft (fftOrder);
    std::vector<std::complex<float>> freq (fftSize, { 0.0f, 0.0f });

    // Copy real signal into interleaved complex buffer
    std::vector<float> interleaved (fftSize * 2, 0.0f);
    for (int i = 0; i < N; ++i)
        interleaved[i * 2] = white[i];

    fft.performRealOnlyForwardTransform (interleaved.data());

    // Zero bins outside [f1, f2]
    const double binWidth = sampleRate / static_cast<double> (fftSize);
    for (int bin = 0; bin <= fftSize / 2; ++bin)
    {
        const double freq_hz = bin * binWidth;
        if (freq_hz < kF1 || freq_hz > kF2)
        {
            interleaved[bin * 2]     = 0.0f;
            interleaved[bin * 2 + 1] = 0.0f;
            // Mirror
            if (bin > 0 && bin < fftSize / 2)
            {
                interleaved[(fftSize - bin) * 2]     = 0.0f;
                interleaved[(fftSize - bin) * 2 + 1] = 0.0f;
            }
        }
    }

    fft.performRealOnlyInverseTransform (interleaved.data());

    noiseSignal.resize (N);
    for (int i = 0; i < N; ++i)
        noiseSignal[i] = interleaved[i * 2];

    // Normalize
    float peak = 0.0f;
    for (auto v : noiseSignal) peak = std::max (peak, std::abs (v));
    if (peak > 0.0f)
        for (auto& v : noiseSignal) v /= peak;
    // Scale to 0.9
    for (auto& v : noiseSignal) v *= 0.9f;

    // Fade in / fade out
    for (int i = 0; i < fadeSamples && i < N; ++i)
    {
        float env = static_cast<float> (i) / static_cast<float> (fadeSamples);
        noiseSignal[i] *= env;
        noiseSignal[N - 1 - i] *= env;
    }
}

//==============================================================================
void IrCapture::startCapture()
{
    std::fill (sweepCapture.begin(), sweepCapture.end(), 0.0f);
    std::fill (noiseCapture.begin(), noiseCapture.end(), 0.0f);

    playPos = 0;
    recPos  = 0;
    silenceRemaining = 0;
    resultIR.setSize (0, 0);
    clipDetected.store (false);

    phase.store (CapturePhase::PlayingSweep);
}

//==============================================================================
void IrCapture::processBlock (const float* inputBuffer, float* outputBuffer, int numSamples)
{
    const CapturePhase currentPhase = phase.load();

    // Level metering (always)
    float inPeak  = 0.0f;
    float outPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        inPeak = std::max (inPeak, std::abs (inputBuffer[i]));

    if (currentPhase == CapturePhase::Idle || currentPhase == CapturePhase::Processing
        || currentPhase == CapturePhase::Done || currentPhase == CapturePhase::Clipped)
    {
        std::fill (outputBuffer, outputBuffer + numSamples, 0.0f);
        inputLevel.store (inPeak);
        outputLevel.store (0.0f);
        return;
    }

    const int sweepLen  = static_cast<int> (sweepSignal.size());
    const int noiseLen  = static_cast<int> (noiseSignal.size());
    const float gain    = playbackGain.load();

    for (int i = 0; i < numSamples; ++i)
    {
        float outSample = 0.0f;

        if (currentPhase == CapturePhase::PlayingSweep)
        {
            if (playPos < sweepLen)
            {
                outSample = sweepSignal[playPos++] * gain;
            }
            else
            {
                // Silence pad
                if (silenceRemaining == 0)
                    silenceRemaining = silenceSamples;

                if (silenceRemaining > 0)
                    --silenceRemaining;

                if (silenceRemaining == 0)
                {
                    playPos = 0;
                    recPos  = 0;
                    silenceRemaining = 0;
                    phase.store (CapturePhase::PlayingNoise);
                }
            }

            if (recPos < static_cast<int> (sweepCapture.size()))
                sweepCapture[recPos++] = inputBuffer[i];
        }
        else if (currentPhase == CapturePhase::PlayingNoise)
        {
            if (playPos < noiseLen)
            {
                outSample = noiseSignal[playPos++] * gain;
            }
            else
            {
                if (silenceRemaining == 0)
                    silenceRemaining = silenceSamples;

                if (silenceRemaining > 0)
                    --silenceRemaining;

                if (silenceRemaining == 0)
                {
                    phase.store (CapturePhase::Processing);
                    // Launch background job
                    threadPool.addJob ([this] { runFFTProcessing(); });
                }
            }

            if (recPos < static_cast<int> (noiseCapture.size()))
                noiseCapture[recPos++] = inputBuffer[i];
        }

        outputBuffer[i] = outSample;
        outPeak = std::max (outPeak, std::abs (outSample));
    }

    // Abort if input or output clipped
    if (inPeak >= 1.0f || outPeak >= 1.0f)
    {
        std::fill (outputBuffer, outputBuffer + numSamples, 0.0f);
        clipDetected.store (true);
        phase.store (CapturePhase::Clipped);
        inputLevel.store (inPeak);
        outputLevel.store (outPeak);
        return;
    }

    inputLevel.store (inPeak);
    outputLevel.store (outPeak);
}

//==============================================================================
// FFT helpers
//==============================================================================

static std::vector<float> makeInterleavedComplexFromReal (const float* data, int N, int fftSize)
{
    std::vector<float> buf (fftSize * 2, 0.0f);
    for (int i = 0; i < N; ++i)
        buf[i * 2] = data[i];
    return buf;
}

static void complexMultiplyAccum (std::vector<float>& result,
                                   const std::vector<float>& A,
                                   const std::vector<float>& B,
                                   int fftSize)
{
    for (int i = 0; i < fftSize; ++i)
    {
        const float ar = A[i * 2],     ai = A[i * 2 + 1];
        const float br = B[i * 2],     bi = B[i * 2 + 1];
        result[i * 2]     = ar * br - ai * bi;
        result[i * 2 + 1] = ar * bi + ai * br;
    }
}

//==============================================================================
void IrCapture::runFFTProcessing()
{
    auto irSweep = computeIRfromSweep();
    auto irNoise = computeIRfromNoise();
    resultIR = averageAndFinalize (irSweep, irNoise);
    phase.store (CapturePhase::Done);
}

//==============================================================================
juce::AudioBuffer<float> IrCapture::computeIRfromSweep()
{
    const int recLen   = static_cast<int> (sweepCapture.size());
    const int sweepLen = static_cast<int> (sweepSignal.size());
    const int fftLen   = recLen + sweepLen - 1;

    const int fftOrder = static_cast<int> (std::ceil (std::log2 (static_cast<double> (fftLen))));
    const int fftSize  = 1 << fftOrder;

    juce::dsp::FFT fft (fftOrder);

    // FFT of recorded signal
    auto recBuf = makeInterleavedComplexFromReal (sweepCapture.data(), recLen, fftSize);
    fft.performRealOnlyForwardTransform (recBuf.data());

    // Build inverse sweep (time-reversed, amplitude weighted)
    const double T  = kSweepDuration;
    const double logRatio = std::log (kF2 / kF1);
    std::vector<float> invSweep (sweepLen);
    for (int i = 0; i < sweepLen; ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        // Amplitude weighting: decay proportional to exp(-t * logRatio / T)
        const float weight = static_cast<float> (std::exp (-t * logRatio / T));
        invSweep[sweepLen - 1 - i] = sweepSignal[i] * weight;
    }

    // Normalize inverse sweep
    float peak = 0.0f;
    for (auto v : invSweep) peak = std::max (peak, std::abs (v));
    if (peak > 0.0f)
        for (auto& v : invSweep) v /= peak;

    auto invBuf = makeInterleavedComplexFromReal (invSweep.data(), sweepLen, fftSize);
    fft.performRealOnlyForwardTransform (invBuf.data());

    // Multiply in frequency domain
    std::vector<float> productBuf (fftSize * 2, 0.0f);
    complexMultiplyAccum (productBuf, recBuf, invBuf, fftSize);

    // IFFT
    fft.performRealOnlyInverseTransform (productBuf.data());

    // Extract real part, scale by 1/fftSize
    const float scale = 1.0f / static_cast<float> (fftSize);
    juce::AudioBuffer<float> result (1, fftLen);
    for (int i = 0; i < fftLen; ++i)
        result.setSample (0, i, productBuf[i * 2] * scale);

    return result;
}

//==============================================================================
juce::AudioBuffer<float> IrCapture::computeIRfromNoise()
{
    const int recLen   = static_cast<int> (noiseCapture.size());
    const int noiseLen = static_cast<int> (noiseSignal.size());
    const int fftLen   = recLen + noiseLen - 1;

    const int fftOrder = static_cast<int> (std::ceil (std::log2 (static_cast<double> (fftLen))));
    const int fftSize  = 1 << fftOrder;

    juce::dsp::FFT fft (fftOrder);

    // FFT of recorded noise
    auto recBuf = makeInterleavedComplexFromReal (noiseCapture.data(), recLen, fftSize);
    fft.performRealOnlyForwardTransform (recBuf.data());

    // FFT of reference noise
    auto refBuf = makeInterleavedComplexFromReal (noiseSignal.data(), noiseLen, fftSize);
    fft.performRealOnlyForwardTransform (refBuf.data());

    // Wiener deconvolution: H = conj(X) * Y / (|X|^2 + eps)
    const float eps = 1e-10f;
    std::vector<float> productBuf (fftSize * 2, 0.0f);
    for (int i = 0; i < fftSize; ++i)
    {
        const float xr = refBuf[i * 2],   xi = refBuf[i * 2 + 1];
        const float yr = recBuf[i * 2],   yi = recBuf[i * 2 + 1];
        const float denom = xr * xr + xi * xi + eps;
        // conj(X) * Y
        productBuf[i * 2]     = (xr * yr + xi * yi) / denom;
        productBuf[i * 2 + 1] = (xr * yi - xi * yr) / denom;
    }

    fft.performRealOnlyInverseTransform (productBuf.data());

    const float scale = 1.0f / static_cast<float> (fftSize);
    juce::AudioBuffer<float> result (1, fftLen);
    for (int i = 0; i < fftLen; ++i)
        result.setSample (0, i, productBuf[i * 2] * scale);

    return result;
}

//==============================================================================
juce::AudioBuffer<float> IrCapture::averageAndFinalize (
    const juce::AudioBuffer<float>& a,
    const juce::AudioBuffer<float>& b)
{
    const int lenA = a.getNumSamples();
    const int lenB = b.getNumSamples();
    const int len  = std::min (lenA, lenB);

    juce::AudioBuffer<float> averaged (1, len);
    for (int i = 0; i < len; ++i)
        averaged.setSample (0, i, (a.getSample (0, i) + b.getSample (0, i)) * 0.5f);

    // Trim silence from tail (find last sample > -80 dBFS threshold = 1e-4)
    const float trimThresh = 1e-4f;
    int trimEnd = len - 1;
    while (trimEnd > 0 && std::abs (averaged.getSample (0, trimEnd)) < trimThresh)
        --trimEnd;
    ++trimEnd; // exclusive

    if (trimEnd < len)
    {
        juce::AudioBuffer<float> trimmed (1, trimEnd);
        for (int i = 0; i < trimEnd; ++i)
            trimmed.setSample (0, i, averaged.getSample (0, i));
        averaged = std::move (trimmed);
    }

    return averaged;
}

//==============================================================================
juce::AudioBuffer<float> IrCapture::retrieveIR()
{
    juce::AudioBuffer<float> ir;
    std::swap (ir, resultIR);
    phase.store (CapturePhase::Idle);
    return ir;
}

void IrCapture::resetToIdle()
{
    clipDetected.store (false);
    phase.store (CapturePhase::Idle);
}

//==============================================================================
juce::String IrCapture::saveToFile (const juce::AudioBuffer<float>& ir,
                                     const juce::File& outputFile)
{
    juce::WavAudioFormat wavFormat;
    auto stream = std::unique_ptr<juce::FileOutputStream> (outputFile.createOutputStream());
    if (stream == nullptr)
        return "Failed to open output file for writing.";

    auto writer = std::unique_ptr<juce::AudioFormatWriter> (
        wavFormat.createWriterFor (stream.get(),
                                   sampleRate,
                                   1,       // mono
                                   24,      // bit depth
                                   {},
                                   0));

    if (writer == nullptr)
        return "Failed to create WAV writer.";

    stream.release(); // writer owns the stream now

    if (! writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples()))
        return "Failed to write audio data.";

    return {}; // success
}
