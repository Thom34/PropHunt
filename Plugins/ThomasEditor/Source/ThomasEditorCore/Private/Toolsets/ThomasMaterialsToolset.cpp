#include "Toolsets/ThomasMaterialsToolset.h"

#include "Modules/ModuleManager.h"
#include "ThomasEditorMaterialsProvider.h"

namespace
{
template <typename TResult>
TResult ProviderUnavailable()
{
    TResult Result;
    Result.bOk = false;
    Result.Code = TEXT("provider_unavailable");
    Result.Message = TEXT("ThomasEditorMaterials could not be loaded.");
    return Result;
}

IThomasEditorMaterialsProviderModule* LoadProvider()
{
    return FModuleManager::LoadModulePtr<IThomasEditorMaterialsProviderModule>(
        TEXT("ThomasEditorMaterials"));
}
}

FThomasMaterialInspectionResult UThomasMaterialsToolset::InspectMaterial(
    const FString& AssetPath,
    const int32 MaxExpressions)
{
    if (IThomasEditorMaterialsProviderModule* Provider = LoadProvider())
    {
        return Provider->InspectMaterial(AssetPath, MaxExpressions);
    }
    return ProviderUnavailable<FThomasMaterialInspectionResult>();
}

FThomasMaterialPlanResult UThomasMaterialsToolset::PlanMaterialPatch(
    const FThomasMaterialPatchRequest& Request)
{
    if (IThomasEditorMaterialsProviderModule* Provider = LoadProvider())
    {
        return Provider->PlanMaterialPatch(Request);
    }
    return ProviderUnavailable<FThomasMaterialPlanResult>();
}

FThomasMaterialApplyResult UThomasMaterialsToolset::ApplyMaterialPlan(
    const FString& PlanId,
    const bool bCompile,
    const bool bSave)
{
    if (IThomasEditorMaterialsProviderModule* Provider = LoadProvider())
    {
        return Provider->ApplyMaterialPlan(PlanId, bCompile, bSave);
    }
    return ProviderUnavailable<FThomasMaterialApplyResult>();
}
