#pragma once

#include "Modules/ModuleInterface.h"
#include "ThomasEditorCoreTypes.h"

class THOMASEDITORCORE_API IThomasEditorAssetsProviderModule : public IModuleInterface
{
public:
    virtual FThomasAssetInventoryResult GetAssetInventory(
        const FString& PackageRoot,
        const FString& ClassPath,
        int32 MaxResults) = 0;

    virtual FThomasObjectDetailsResult GetObjectDetails(
        const FString& ObjectPath,
        const TArray<FString>& PropertyNames,
        int32 MaxProperties) = 0;

    virtual FThomasAssetReferencesResult GetAssetReferences(
        const FString& AssetPath,
        int32 MaxResults) = 0;

    virtual FThomasObjectPatchPlanResult PlanObjectPatch(
        const FThomasObjectPatchRequest& Request) = 0;

    virtual FThomasObjectPatchApplyResult ApplyObjectPatch(
        const FString& PlanId,
        bool bSave) = 0;

    virtual FThomasAssetSaveResult SaveAssets(
        const FThomasAssetSaveRequest& Request) = 0;

    /** Internal helper used by lazy specialized providers without loading domain assets. */
    virtual FThomasDomainInventoryResult GetDomainInventory(
        const FString& Domain,
        const FString& PackageRoot,
        const TArray<FString>& ClassPaths,
        int32 MaxResults) = 0;
};
