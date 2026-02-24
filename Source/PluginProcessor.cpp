#include "PluginProcessor.h"
#include "PluginEditor.h"

static const char* kBandLabels[GraphicEQ::kNumBands] = {
    "25 Hz",  "40 Hz",  "63 Hz",  "100 Hz",
    "160 Hz", "250 Hz", "400 Hz", "630 Hz",
    "1 kHz",  "2.5 kHz","6.3 kHz","16 kHz"
};

juce::String GEQ12AudioProcessor::getBandParamID(int band)
{
    return "band" + juce::String(band);
}

juce::String GEQ12AudioProcessor::getBandParamName(int band)
{
    return kBandLabels[band];
}

juce::AudioProcessorValueTreeState::ParameterLayout
GEQ12AudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int b = 0; b < kNumBands; ++b) {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { getBandParamID(b), 1 },
            getBandParamName(b),
            juce::NormalisableRange<float>(
                static_cast<float>(GraphicEQ::kMinGainDB),
                static_cast<float>(GraphicEQ::kMaxGainDB),
                0.1f),  // step size
            0.0f,       // default
            juce::String(),
            juce::AudioProcessorParameter::genericParameter,
            [](float val, int) { return juce::String(val, 1) + " dB"; },
            [](const juce::String& text) { return text.getFloatValue(); }
        ));
    }

    return { params.begin(), params.end() };
}

GEQ12AudioProcessor::GEQ12AudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input",   juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      mAPVTS(*this, nullptr, "GEQ12_STATE", createParameterLayout())
{
}

void GEQ12AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    mEQ.prepare(sampleRate, samplesPerBlock);
}

void GEQ12AudioProcessor::releaseResources()
{
    mEQ.reset();
}

void GEQ12AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Read current parameter values and update the EQ bands
    for (int b = 0; b < kNumBands; ++b) {
        float gain = *mAPVTS.getRawParameterValue(getBandParamID(b));
        mEQ.setBandGain(b, static_cast<double>(gain));
    }

    // Build channel pointer array for the DSP core
    int numChannels = buffer.getNumChannels();
    int numSamples  = buffer.getNumSamples();

    float* channelPtrs[GraphicEQ::kMaxChannels] = {};
    for (int ch = 0; ch < std::min(numChannels, (int)GraphicEQ::kMaxChannels); ++ch)
        channelPtrs[ch] = buffer.getWritePointer(ch);

    mEQ.processBlock(channelPtrs, numChannels, numSamples);
}

juce::AudioProcessorEditor* GEQ12AudioProcessor::createEditor()
{
    return new GEQ12AudioProcessorEditor(*this);
}

void GEQ12AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = mAPVTS.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void GEQ12AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(mAPVTS.state.getType()))
        mAPVTS.replaceState(juce::ValueTree::fromXml(*xml));
}

// VST3 entry point — JUCE macro creates the plugin factory
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GEQ12AudioProcessor();
}
