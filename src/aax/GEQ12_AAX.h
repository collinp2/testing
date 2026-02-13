#pragma once

/// AAX Effect wrapper for the 12-band Graphic EQ.
///
/// This file provides the AAX SDK interface. To compile, you need
/// the Avid AAX SDK (available under license from Avid).
///
/// The wrapper maps AAX callbacks to the format-agnostic PluginProcessor.

// -----------------------------------------------------------------------
// When building against the real AAX SDK, define HAS_AAX_SDK=1.
// Otherwise, this header provides the interface declarations only.
// -----------------------------------------------------------------------

#include "../plugin/PluginProcessor.h"
#include "../plugin/PluginParameters.h"

#ifdef HAS_AAX_SDK
#include "AAX_CEffectParameters.h"
#include "AAX_IEffectDescriptor.h"
#include "AAX_IComponentDescriptor.h"
#include "AAX_IPropertyMap.h"
#include "AAX_CEffectGUI.h"
#endif

/// Plugin identity constants.
namespace GEQ12 {
    static constexpr const char* kEffectName    = "GEQ-12";
    static constexpr const char* kVendorName    = "AudioTools";
    static constexpr const char* kPluginCategory = "EQ";

    // AAX plugin and type IDs (must be registered with Avid for distribution).
    // These are placeholder IDs for development — replace before signing.
    static constexpr const char* kManufacturerID = "ATLS";  // 4-char code
    static constexpr const char* kProductID      = "GQ12";  // 4-char code

    static constexpr int kPluginVersion = 0x00010000; // 1.0.0
}

#ifdef HAS_AAX_SDK

// ---------------------------------------------------------------------------
// AAX Algorithm processing callback
// ---------------------------------------------------------------------------

/// Context structure passed into the AAX algorithm render callback.
struct GEQ12_AlgorithmContext {
    float**     mInputs;
    float**     mOutputs;
    int32_t*    mBufferSize;
    int32_t*    mNumChannels;
    float*      mBandGains;    // kNumBands floats
    int32_t*    mBypass;
};

/// Real-time audio render callback for AAX.
void GEQ12_AlgorithmProcessFunction(
    GEQ12_AlgorithmContext* const inInstancesBegin[],
    const void*                   inInstancesEnd)
{
    for (auto* ctx = *inInstancesBegin; ctx != inInstancesEnd; /* AAX iterates */) {
        int numSamples  = *ctx->mBufferSize;
        int numChannels = *ctx->mNumChannels;
        bool bypass     = (*ctx->mBypass != 0);

        // Copy input to output, then process in-place
        for (int ch = 0; ch < numChannels; ++ch) {
            if (ctx->mInputs[ch] != ctx->mOutputs[ch]) {
                std::memcpy(ctx->mOutputs[ch], ctx->mInputs[ch],
                            numSamples * sizeof(float));
            }
        }

        if (!bypass) {
            // Create a temporary processor for stateless processing
            // (In production, state would be in the instance context)
            static thread_local PluginProcessor proc;
            static thread_local double lastSR = 0;
            double sr = 44100.0; // Would come from host in real implementation
            if (sr != lastSR) {
                proc.prepare(sr, numSamples);
                lastSR = sr;
            }
            for (int b = 0; b < GraphicEQ::kNumBands; ++b) {
                proc.setParameter(PluginParams::kBand1 + b, ctx->mBandGains[b]);
            }
            proc.processBlock(ctx->mOutputs, numChannels, numSamples);
        }
        break; // Single instance per call in this simplified example
    }
}

// ---------------------------------------------------------------------------
// AAX Effect Parameters (handles parameter state and host communication)
// ---------------------------------------------------------------------------

class GEQ12_Parameters : public AAX_CEffectParameters {
public:
    static AAX_CEffectParameters* Create() { return new GEQ12_Parameters(); }

    GEQ12_Parameters() = default;

    AAX_Result EffectInit() override {
        // Register bypass parameter
        AAX_CString bypassName("Bypass");
        AAX_Result result = AddSynchronizedParameter(
            bypassName, PluginParams::kBypass, 0.0);
        if (result != AAX_SUCCESS) return result;

        // Register band gain parameters
        for (int b = 0; b < GraphicEQ::kNumBands; ++b) {
            int paramID = PluginParams::kBand1 + b;
            AAX_CString name(PluginParams::getParamName(paramID));
            result = AddSynchronizedParameter(
                name, paramID, 0.0,
                PluginParams::kMinGainDB, PluginParams::kMaxGainDB);
            if (result != AAX_SUCCESS) return result;
        }
        return AAX_SUCCESS;
    }
};

// ---------------------------------------------------------------------------
// AAX Plugin Description (called by the host to discover the plugin)
// ---------------------------------------------------------------------------

AAX_Result DescribeEffect(AAX_IEffectDescriptor* outDescriptor) {
    outDescriptor->AddName(GEQ12::kEffectName);
    outDescriptor->AddCategory(AAX_ePlugInCategory_EQ);

    // Describe the processing component
    AAX_IComponentDescriptor* compDesc = nullptr;
    outDescriptor->NewComponentDescriptor(&compDesc);

    // Register the algorithm processing callback
    compDesc->AddProcessProc_Native(
        reinterpret_cast<AAX_CProcessProc>(&GEQ12_AlgorithmProcessFunction),
        nullptr);

    // Add audio ports (stereo in/out)
    compDesc->AddAudioIn(0);
    compDesc->AddAudioIn(1);
    compDesc->AddAudioOut(0);
    compDesc->AddAudioOut(1);

    // Add parameters
    for (int i = 0; i < PluginParams::kNumParams; ++i) {
        AAX_IPropertyMap* paramProps = nullptr;
        compDesc->NewParameterPropertyMap(&paramProps);
        compDesc->AddParameter(i, paramProps);
    }

    // Set properties
    AAX_IPropertyMap* props = nullptr;
    outDescriptor->NewPropertyMap(&props);
    props->AddProperty(AAX_eProperty_ManufacturerID,
                       *reinterpret_cast<const int32_t*>(GEQ12::kManufacturerID));
    props->AddProperty(AAX_eProperty_ProductID,
                       *reinterpret_cast<const int32_t*>(GEQ12::kProductID));
    props->AddProperty(AAX_eProperty_PlugInID_Native, GEQ12::kPluginVersion);

    outDescriptor->AddComponent(compDesc);
    return AAX_SUCCESS;
}

#endif // HAS_AAX_SDK
