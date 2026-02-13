/// AAX plugin entry point.
///
/// The AAX SDK requires a GetEffectDescriptions() function that the
/// Pro Tools host calls to discover plugins in the .aaxplugin bundle.
///
/// Build this file only when the AAX SDK is available (HAS_AAX_SDK=1).

#include "GEQ12_AAX.h"

#ifdef HAS_AAX_SDK

#include "AAX_ICollection.h"
#include "AAX_IEffectDescriptor.h"

// AAX SDK entry point — called by Pro Tools on plugin load.
AAX_Result GetEffectDescriptions(AAX_ICollection* outCollection) {
    AAX_IEffectDescriptor* descriptor = nullptr;
    outCollection->NewDescriptor(&descriptor);

    AAX_Result result = DescribeEffect(descriptor);
    if (result != AAX_SUCCESS)
        return result;

    outCollection->AddEffect(GEQ12::kEffectName, descriptor);
    outCollection->SetManufacturerName(GEQ12::kVendorName);
    outCollection->AddPackageName(GEQ12::kEffectName);
    outCollection->SetPackageVersion(GEQ12::kPluginVersion);

    return AAX_SUCCESS;
}

#endif // HAS_AAX_SDK
