#pragma once

#include "ThomasEditorCoreTypes.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ThomasAssetsToolset.generated.h"

UCLASS(BlueprintType)
class THOMASEDITORCORE_API UThomasAssetsToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /**
     * Lists assets below an allowlisted project package root without loading them.
     * The Assets provider is loaded only when this function is called.
     */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Assets")
    static FThomasAssetInventoryResult GetAssetInventory(
        const FString& PackageRoot = TEXT("/Game/PropHunt"),
        const FString& ClassPath = TEXT(""),
        int32 MaxResults = 50);

    /**
     * Reads selected reflected properties and Details metadata from one project object.
     * Empty PropertyNames means the first editable/Blueprint-visible properties up to MaxProperties.
     */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Details")
    static FThomasObjectDetailsResult GetObjectDetails(
        const FString& ObjectPath,
        const TArray<FString>& PropertyNames,
        int32 MaxProperties = 50);

    /**
     * Returns on-disk package dependencies and referencers for one project asset.
     */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Assets")
    static FThomasAssetReferencesResult GetAssetReferences(
        const FString& AssetPath,
        int32 MaxResults = 100);

    /**
     * Prevalidates a bounded scalar Details change without mutating the asset.
     * Every operation is checked for editability, type compatibility, clamps and optional current value.
     */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Details")
    static FThomasObjectPatchPlanResult PlanObjectPatch(
        const FThomasObjectPatchRequest& Request);

    /** Applies one fresh plan transactionally. Saving is always explicit. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Details")
    static FThomasObjectPatchApplyResult ApplyObjectPatch(
        const FString& PlanId,
        bool bSave = false);

    /**
     * Saves explicit dirty project assets inside the current Editor process.
     * The call is retryable after an external file lock and never launches a second Unreal process.
     */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Assets")
    static FThomasAssetSaveResult SaveAssets(
        const FThomasAssetSaveRequest& Request);
};
