#include "Toolsets/ThomasAssetsToolset.h"

#include "Modules/ModuleManager.h"
#include "ThomasEditorAssetsProvider.h"

namespace
{
IThomasEditorAssetsProviderModule* LoadProvider()
{
    return FModuleManager::LoadModulePtr<IThomasEditorAssetsProviderModule>(
        TEXT("ThomasEditorAssets"));
}

template <typename TResult>
TResult ProviderUnavailable()
{
    TResult Result;
    Result.bOk = false;
    Result.Code = TEXT("provider_unavailable");
    Result.Message = TEXT("ThomasEditorAssets could not be loaded.");
    return Result;
}
}

FThomasAssetInventoryResult UThomasAssetsToolset::GetAssetInventory(
    const FString& PackageRoot,
    const FString& ClassPath,
    const int32 MaxResults)
{
    if (IThomasEditorAssetsProviderModule* Provider = LoadProvider())
    {
        return Provider->GetAssetInventory(PackageRoot, ClassPath, MaxResults);
    }
    return ProviderUnavailable<FThomasAssetInventoryResult>();
}

FThomasObjectDetailsResult UThomasAssetsToolset::GetObjectDetails(
    const FString& ObjectPath,
    const TArray<FString>& PropertyNames,
    const int32 MaxProperties)
{
    if (IThomasEditorAssetsProviderModule* Provider = LoadProvider())
    {
        return Provider->GetObjectDetails(ObjectPath, PropertyNames, MaxProperties);
    }
    return ProviderUnavailable<FThomasObjectDetailsResult>();
}

FThomasAssetReferencesResult UThomasAssetsToolset::GetAssetReferences(
    const FString& AssetPath,
    const int32 MaxResults)
{
    if (IThomasEditorAssetsProviderModule* Provider = LoadProvider())
    {
        return Provider->GetAssetReferences(AssetPath, MaxResults);
    }
    return ProviderUnavailable<FThomasAssetReferencesResult>();
}

FThomasObjectPatchPlanResult UThomasAssetsToolset::PlanObjectPatch(
    const FThomasObjectPatchRequest& Request)
{
    if (IThomasEditorAssetsProviderModule* Provider = LoadProvider())
    {
        return Provider->PlanObjectPatch(Request);
    }
    return ProviderUnavailable<FThomasObjectPatchPlanResult>();
}

FThomasObjectPatchApplyResult UThomasAssetsToolset::ApplyObjectPatch(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorAssetsProviderModule* Provider = LoadProvider())
    {
        return Provider->ApplyObjectPatch(PlanId, bSave);
    }
    return ProviderUnavailable<FThomasObjectPatchApplyResult>();
}

FThomasAssetSaveResult UThomasAssetsToolset::SaveAssets(
    const FThomasAssetSaveRequest& Request)
{
    if (IThomasEditorAssetsProviderModule* Provider = LoadProvider())
    {
        return Provider->SaveAssets(Request);
    }
    return ProviderUnavailable<FThomasAssetSaveResult>();
}
