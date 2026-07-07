#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Parameter layout
//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
FleshRenderProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addParam = [&] (const juce::String& id, const juce::String& name)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f, 1.0f), 0.0f));
    };
    auto addDb = [&] (const juce::String& id, const juce::String& name,
                      float lo, float hi, float def, float skew = 1.0f)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (lo, hi, 0.1f, skew), def,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
    };
    auto addBool = [&] (const juce::String& id, const juce::String& name, bool def)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    };
    auto addChoice = [&] (const juce::String& id, const juce::String& name,
                          const juce::StringArray& items, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
    };
    auto hzText = [] (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    };
    auto addHz = [&] (const juce::String& id, const juce::String& name,
                      float lo, float hi, float def)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (lo, hi, 1.0f, 0.3f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));
    };

    // ---- Neve 1073/74-style pre EQ ----
    addBool   ("neve_active", "Neve EQ", false);
    addChoice ("neve_hpf", "Neve HPF", { "Off", "50 Hz", "80 Hz", "160 Hz", "300 Hz" }, 0);
    addChoice ("neve_low_freq", "Neve Low Freq", { "35 Hz", "60 Hz", "110 Hz", "220 Hz" }, 1);
    addDb     ("neve_low_gain", "Neve Low Gain", -16.0f, 16.0f, 0.0f);
    addChoice ("neve_mid_freq", "Neve Mid Freq",
               { "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" }, 1);
    addDb     ("neve_mid_gain", "Neve Mid Gain", -18.0f, 18.0f, 0.0f);
    addDb     ("neve_high_gain", "Neve High Gain (12k)", -16.0f, 16.0f, 0.0f);

    // ---- Multiband stages (ids unchanged from v1 for session compatibility;
    //      the middle stage is displayed as DRIVE) ----
    addParam ("low_sat",  "Low Saturation");
    addParam ("low_dist", "Low Drive");
    addParam ("low_fuzz", "Low Fuzz");
    addParam ("mid_sat",  "Mid Saturation");
    addParam ("mid_dist", "Mid Drive");
    addParam ("mid_fuzz", "Mid Fuzz");
    addParam ("high_sat",  "High Saturation");
    addParam ("high_dist", "High Drive");
    addParam ("high_fuzz", "High Fuzz");

    // ---- Sweepable crossovers + wet/dry mix ----
    addHz ("xover_low",  "Crossover Low",  60.0f, 800.0f, 250.0f);
    addHz ("xover_high", "Crossover High", 800.0f, 8000.0f, 2000.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sat_mix", 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
                { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })));

    // ---- API-560 style post EQ ----
    addBool ("geq_active", "Graphic EQ", false);
    for (int i = 0; i < Api560EQ::kNumBands; ++i)
    {
        const float f = Api560EQ::kFrequencies[(size_t) i];
        const juce::String name = (f >= 1000.0f ? juce::String (f / 1000.0f, (f == 16000.0f ? 0 : 1)) + "k"
                                                : juce::String (juce::roundToInt (f))) + " Hz";
        addDb ("geq_" + juce::String (i), "GEQ " + name, -12.0f, 12.0f, 0.0f);
    }

    // ---- Post filters ----
    addHz   ("post_hpf_freq", "Post Hi-Pass", 20.0f, 2000.0f, 20.0f);
    addBool ("post_hpf_active", "Post Hi-Pass On", false);
    addHz   ("post_lpf_freq", "Post Low-Pass", 1000.0f, 20000.0f, 20000.0f);
    addBool ("post_lpf_active", "Post Low-Pass On", false);

    // ---- Master output level (unchanged id) ----
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output_level", 1 },
        "Output Level",
        juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 1.0f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })
            .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));

    return layout;
}

//==============================================================================
FleshRenderProcessor::FleshRenderProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    outputLevelParam = apvts.getRawParameterValue ("output_level");
}

FleshRenderProcessor::~FleshRenderProcessor() {}

//==============================================================================
void FleshRenderProcessor::updateCrossovers (float lowMidHz, float midHighHz)
{
    midHighHz = juce::jmax (midHighHz, lowMidHz * 2.0f);   // keep an octave apart
    if (std::abs (lowMidHz - xoverLowCached) < 0.5f
        && std::abs (midHighHz - xoverHighCached) < 0.5f)
        return;

    xoverLowCached  = lowMidHz;
    xoverHighCached = midHighHz;

    const double fLow  = juce::jlimit (40.0,  1000.0, (double) lowMidHz);
    const double fHigh = juce::jlimit (500.0, 9000.0, (double) midHighHz);

    {
        auto c = Coeffs::makeLowPass (currentSampleRate, fLow);
        *lowLP1.state = *c;  *lowLP2.state = *c;
    }
    {
        auto c = Coeffs::makeHighPass (currentSampleRate, fLow);
        *midHP1.state = *c;  *midHP2.state = *c;
    }
    {
        auto c = Coeffs::makeLowPass (currentSampleRate, fHigh);
        *midLP1.state = *c;  *midLP2.state = *c;
    }
    {
        auto c = Coeffs::makeHighPass (currentSampleRate, fHigh);
        *highHP1.state = *c; *highHP2.state = *c;
    }
}

//==============================================================================
void FleshRenderProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    const int numCh = juce::jmax (getTotalNumInputChannels(),
                                   getTotalNumOutputChannels());

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = (juce::uint32) numCh;

    neveEQ.prepare (spec);

    lowLP1.prepare (spec);  lowLP2.prepare (spec);
    midHP1.prepare (spec);  midHP2.prepare (spec);
    midLP1.prepare (spec);  midLP2.prepare (spec);
    highHP1.prepare (spec); highHP2.prepare (spec);

    xoverLowCached = xoverHighCached = -1.0f;
    updateCrossovers (*apvts.getRawParameterValue ("xover_low"),
                      *apvts.getRawParameterValue ("xover_high"));
    bandsWereActive = false;

    for (int ch = 0; ch < 2; ++ch)
        geq[ch].prepare (sampleRate, samplesPerBlock);

    postHPF.prepare (spec);
    postLPF.prepare (spec);
    postHpfCached = postLpfCached = -1.0f;

    lowBuf.setSize  (numCh, samplesPerBlock);
    midBuf.setSize  (numCh, samplesPerBlock);
    highBuf.setSize (numCh, samplesPerBlock);
    dryBuf.setSize  (numCh, samplesPerBlock);

    outputGainSmoothed.reset (sampleRate, 0.05);
    outputGainSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputLevelParam->load(), -60.0f));

    outputPeak.store (0.0f);
}

void FleshRenderProcessor::releaseResources()
{
    neveEQ.reset();
    lowLP1.reset();  lowLP2.reset();
    midHP1.reset();  midHP2.reset();
    midLP1.reset();  midLP2.reset();
    highHP1.reset(); highHP2.reset();
    geq[0].reset();  geq[1].reset();
    postHPF.reset(); postLPF.reset();
}

//==============================================================================
void FleshRenderProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), lowBuf.getNumChannels());

    if (numSamples == 0 || numChannels == 0)
        return;

    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    // ======================= 1) NEVE 1073/74 PRE EQ ==========================
    if (raw ("neve_active") > 0.5f)
    {
        neveEQ.setParams ((int) raw ("neve_hpf") - 1,          // 0 = Off -> -1
                          (int) raw ("neve_low_freq"),
                          raw ("neve_low_gain"),
                          (int) raw ("neve_mid_freq"),
                          raw ("neve_mid_gain"),
                          raw ("neve_high_gain"));
        if (neveEQ.wouldProcess())
            neveEQ.process (buffer, numChannels, numSamples);
    }

    // ======================= 2) MULTIBAND SATURATION =========================
    const float lowSat  = raw ("low_sat"),  lowDist  = raw ("low_dist"),  lowFuzz  = raw ("low_fuzz");
    const float midSat  = raw ("mid_sat"),  midDist  = raw ("mid_dist"),  midFuzz  = raw ("mid_fuzz");
    const float highSat = raw ("high_sat"), highDist = raw ("high_dist"), highFuzz = raw ("high_fuzz");

    const bool bandsActive = (lowSat + lowDist + lowFuzz + midSat + midDist + midFuzz
                              + highSat + highDist + highFuzz) > 1e-4f;

    if (bandsActive)
    {
        // Re-engaging after a silent stretch: clear stale crossover state.
        if (! bandsWereActive)
        {
            lowLP1.reset();  lowLP2.reset();
            midHP1.reset();  midHP2.reset();
            midLP1.reset();  midLP2.reset();
            highHP1.reset(); highHP2.reset();
        }

        updateCrossovers (raw ("xover_low"), raw ("xover_high"));

        const float mix = raw ("sat_mix");
        if (mix < 0.999f)
            for (int ch = 0; ch < numChannels; ++ch)
                dryBuf.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            lowBuf .copyFrom (ch, 0, buffer, ch, 0, numSamples);
            midBuf .copyFrom (ch, 0, buffer, ch, 0, numSamples);
            highBuf.copyFrom (ch, 0, buffer, ch, 0, numSamples);
        }

        {
            juce::dsp::AudioBlock<float> blk (lowBuf.getArrayOfWritePointers(),
                                              (size_t) numChannels, (size_t) numSamples);
            juce::dsp::ProcessContextReplacing<float> ctx (blk);
            lowLP1.process (ctx);
            lowLP2.process (ctx);
        }
        {
            juce::dsp::AudioBlock<float> blk (midBuf.getArrayOfWritePointers(),
                                              (size_t) numChannels, (size_t) numSamples);
            juce::dsp::ProcessContextReplacing<float> ctx (blk);
            midHP1.process (ctx);
            midHP2.process (ctx);
            midLP1.process (ctx);
            midLP2.process (ctx);
        }
        {
            juce::dsp::AudioBlock<float> blk (highBuf.getArrayOfWritePointers(),
                                              (size_t) numChannels, (size_t) numSamples);
            juce::dsp::ProcessContextReplacing<float> ctx (blk);
            highHP1.process (ctx);
            highHP2.process (ctx);
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* low  = lowBuf .getWritePointer (ch);
            float* mid  = midBuf .getWritePointer (ch);
            float* high = highBuf.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                low [i] = processChain (low [i], lowSat,  lowDist,  lowFuzz);
                mid [i] = processChain (mid [i], midSat,  midDist,  midFuzz);
                high[i] = processChain (high[i], highSat, highDist, highFuzz);
            }
        }

        buffer.clear();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            buffer.addFrom (ch, 0, lowBuf,  ch, 0, numSamples);
            buffer.addFrom (ch, 0, midBuf,  ch, 0, numSamples);
            buffer.addFrom (ch, 0, highBuf, ch, 0, numSamples);
        }

        // Wet/dry MIX (linear blend — parallel saturation).
        if (mix < 0.999f)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float*       wet = buffer.getWritePointer (ch);
                const float* dry = dryBuf.getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    wet[i] = wet[i] * mix + dry[i] * (1.0f - mix);
            }
    }
    bandsWereActive = bandsActive;
    // All stages at zero: the whole split/shape/sum is skipped — pure passthrough.

    // ======================= 3) API-560 GRAPHIC EQ ===========================
    if (raw ("geq_active") > 0.5f)
    {
        for (int i = 0; i < Api560EQ::kNumBands; ++i)
        {
            const float g = raw (("geq_" + juce::String (i)).toRawUTF8());
            geq[0].setBandGain (i, g);
            geq[1].setBandGain (i, g);
        }
        for (int ch = 0; ch < juce::jmin (numChannels, 2); ++ch)
            geq[ch].process (buffer.getWritePointer (ch), numSamples);
    }

    // ======================= 4) POST FILTERS =================================
    if (raw ("post_hpf_active") > 0.5f)
    {
        const float f = raw ("post_hpf_freq");
        if (std::abs (f - postHpfCached) > 0.5f)
        {
            postHpfCached = f;
            *postHPF.state = *Coeffs::makeHighPass (currentSampleRate, f);
        }
        juce::dsp::AudioBlock<float> blk (buffer.getArrayOfWritePointers(),
                                          (size_t) numChannels, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        postHPF.process (ctx);
    }
    if (raw ("post_lpf_active") > 0.5f)
    {
        const float f = raw ("post_lpf_freq");
        if (std::abs (f - postLpfCached) > 0.5f)
        {
            postLpfCached = f;
            *postLPF.state = *Coeffs::makeLowPass (currentSampleRate, f);
        }
        juce::dsp::AudioBlock<float> blk (buffer.getArrayOfWritePointers(),
                                          (size_t) numChannels, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        postLPF.process (ctx);
    }

    // ======================= 5) MASTER OUTPUT + METER ========================
    const float targetGain = juce::Decibels::decibelsToGain (outputLevelParam->load(), -60.0f);
    outputGainSmoothed.setTargetValue (targetGain);

    if (outputGainSmoothed.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = outputGainSmoothed.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
    }
    else
    {
        buffer.applyGain (outputGainSmoothed.getCurrentValue());
    }

    // Peak accumulate for the editor meter (lock-free).
    float pk = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* d = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            pk = juce::jmax (pk, std::abs (d[i]));
    }
    float cur = outputPeak.load (std::memory_order_relaxed);
    while (pk > cur && ! outputPeak.compare_exchange_weak (cur, pk, std::memory_order_relaxed)) {}
}

//==============================================================================
juce::AudioProcessorEditor* FleshRenderProcessor::createEditor()
{
    return new FleshRenderEditor (*this);
}

//==============================================================================
void FleshRenderProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void FleshRenderProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FleshRenderProcessor();
}
