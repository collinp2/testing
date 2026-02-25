#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Parameter layout
//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
FleshRenderProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Helper: create a 0…1 float parameter
    auto addParam = [&] (const juce::String& id, const juce::String& name)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 },
            name,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f, 1.0f),
            0.0f));
    };

    // Low band
    addParam ("low_sat",  "Low Saturation");
    addParam ("low_dist", "Low Distortion");
    addParam ("low_fuzz", "Low Fuzz");

    // Mid band
    addParam ("mid_sat",  "Mid Saturation");
    addParam ("mid_dist", "Mid Distortion");
    addParam ("mid_fuzz", "Mid Fuzz");

    // High band
    addParam ("high_sat",  "High Saturation");
    addParam ("high_dist", "High Distortion");
    addParam ("high_fuzz", "High Fuzz");

    return layout;
}

//==============================================================================
FleshRenderProcessor::FleshRenderProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

FleshRenderProcessor::~FleshRenderProcessor() {}

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

    // Prepare all filter stages
    lowLP1.prepare (spec);  lowLP2.prepare (spec);
    midHP1.prepare (spec);  midHP2.prepare (spec);
    midLP1.prepare (spec);  midLP2.prepare (spec);
    highHP1.prepare (spec); highHP2.prepare (spec);

    // Set crossover coefficients
    // 250 Hz low-pass (LR4: two cascaded 2nd-order Butterworth LPs)
    {
        auto c = Coeffs::makeLowPass (sampleRate, 250.0);
        *lowLP1.state = *c;
        *lowLP2.state = *c;
    }
    // 250 Hz high-pass (for mid band)
    {
        auto c = Coeffs::makeHighPass (sampleRate, 250.0);
        *midHP1.state = *c;
        *midHP2.state = *c;
    }
    // 2 kHz low-pass (for mid band)
    {
        auto c = Coeffs::makeLowPass (sampleRate, 2000.0);
        *midLP1.state = *c;
        *midLP2.state = *c;
    }
    // 2 kHz high-pass (for high band)
    {
        auto c = Coeffs::makeHighPass (sampleRate, 2000.0);
        *highHP1.state = *c;
        *highHP2.state = *c;
    }

    // Allocate band buffers
    lowBuf.setSize  (numCh, samplesPerBlock);
    midBuf.setSize  (numCh, samplesPerBlock);
    highBuf.setSize (numCh, samplesPerBlock);
}

void FleshRenderProcessor::releaseResources()
{
    lowLP1.reset();  lowLP2.reset();
    midHP1.reset();  midHP2.reset();
    midLP1.reset();  midLP2.reset();
    highHP1.reset(); highHP2.reset();
}

//==============================================================================
void FleshRenderProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(),
                                         juce::jmin (lowBuf.getNumChannels(), numSamples > 0 ? buffer.getNumChannels() : 0));

    if (numSamples == 0 || numChannels == 0)
        return;

    // Fetch parameters (thread-safe atomic read)
    const float lowSat  = *apvts.getRawParameterValue ("low_sat");
    const float lowDist = *apvts.getRawParameterValue ("low_dist");
    const float lowFuzz = *apvts.getRawParameterValue ("low_fuzz");

    const float midSat  = *apvts.getRawParameterValue ("mid_sat");
    const float midDist = *apvts.getRawParameterValue ("mid_dist");
    const float midFuzz = *apvts.getRawParameterValue ("mid_fuzz");

    const float highSat  = *apvts.getRawParameterValue ("high_sat");
    const float highDist = *apvts.getRawParameterValue ("high_dist");
    const float highFuzz = *apvts.getRawParameterValue ("high_fuzz");

    // ---- Copy input to each band buffer ------------------------------------
    for (int ch = 0; ch < numChannels; ++ch)
    {
        lowBuf .copyFrom (ch, 0, buffer, ch, 0, numSamples);
        midBuf .copyFrom (ch, 0, buffer, ch, 0, numSamples);
        highBuf.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    }

    // ---- Apply crossover filters -------------------------------------------
    // Low band: LP at 250 Hz (two cascaded stages → LR4)
    {
        juce::dsp::AudioBlock<float> blk (lowBuf.getArrayOfWritePointers(),
                                           (size_t) numChannels, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        lowLP1.process (ctx);
        lowLP2.process (ctx);
    }
    // Mid band: HP at 250 Hz, then LP at 2 kHz
    {
        juce::dsp::AudioBlock<float> blk (midBuf.getArrayOfWritePointers(),
                                           (size_t) numChannels, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        midHP1.process (ctx);
        midHP2.process (ctx);
        midLP1.process (ctx);
        midLP2.process (ctx);
    }
    // High band: HP at 2 kHz
    {
        juce::dsp::AudioBlock<float> blk (highBuf.getArrayOfWritePointers(),
                                           (size_t) numChannels, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        highHP1.process (ctx);
        highHP2.process (ctx);
    }

    // ---- Apply waveshaping to each band ------------------------------------
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

    // ---- Recombine bands into output ---------------------------------------
    // The LR4 crossover is complementary: LP + HP = flat (all-pass in magnitude).
    // Summing all three bands reconstructs the original spectrum (with processing).
    buffer.clear();
    for (int ch = 0; ch < numChannels; ++ch)
    {
        buffer.addFrom (ch, 0, lowBuf,  ch, 0, numSamples);
        buffer.addFrom (ch, 0, midBuf,  ch, 0, numSamples);
        buffer.addFrom (ch, 0, highBuf, ch, 0, numSamples);
    }
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
// This creates the plugin instance (required by the JUCE plugin framework)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FleshRenderProcessor();
}
