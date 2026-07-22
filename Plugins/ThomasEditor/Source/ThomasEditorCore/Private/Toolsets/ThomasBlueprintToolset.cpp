#include "Toolsets/ThomasBlueprintToolset.h"

#include "Modules/ModuleManager.h"
#include "ThomasEditorBlueprintProvider.h"

namespace
{
IThomasEditorBlueprintProviderModule* LoadProvider()
{
    return FModuleManager::LoadModulePtr<IThomasEditorBlueprintProviderModule>(
        TEXT("ThomasEditorBlueprint"));
}

template <typename TResult>
TResult ProviderUnavailable()
{
    TResult Result;
    Result.bOk = false;
    Result.Code = TEXT("provider_unavailable");
    Result.Message = TEXT("ThomasEditorBlueprint provider could not be loaded.");
    return Result;
}
}

FThomasBlueprintInspectionResult UThomasBlueprintToolset::InspectBlueprint(
    const FString& AssetPath,
    const bool bIncludeNodes,
    const int32 MaxNodes)
{
    if (IThomasEditorBlueprintProviderModule* Provider = LoadProvider())
    {
        return Provider->InspectBlueprint(AssetPath, bIncludeNodes, MaxNodes);
    }
    return ProviderUnavailable<FThomasBlueprintInspectionResult>();
}

FThomasBlueprintPlanResult UThomasBlueprintToolset::PlanBlueprintBatch(
    const FThomasBlueprintBatchRequest& Request)
{
    if (IThomasEditorBlueprintProviderModule* Provider = LoadProvider())
    {
        return Provider->PlanBlueprintBatch(Request);
    }
    return ProviderUnavailable<FThomasBlueprintPlanResult>();
}

FThomasBlueprintApplyResult UThomasBlueprintToolset::ApplyBlueprintPlan(
    const FString& PlanId,
    const bool bCompile,
    const bool bSave)
{
    if (IThomasEditorBlueprintProviderModule* Provider = LoadProvider())
    {
        return Provider->ApplyBlueprintPlan(PlanId, bCompile, bSave);
    }
    return ProviderUnavailable<FThomasBlueprintApplyResult>();
}

FThomasBlueprintValidationResult UThomasBlueprintToolset::ValidateBlueprint(
    const FString& AssetPath,
    const TArray<FString>& RequiredWidgetNames)
{
    if (IThomasEditorBlueprintProviderModule* Provider = LoadProvider())
    {
        return Provider->ValidateBlueprint(AssetPath, RequiredWidgetNames);
    }
    return ProviderUnavailable<FThomasBlueprintValidationResult>();
}
