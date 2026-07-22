#include "Toolsets/ThomasRenderToolset.h"

#include "Modules/ModuleManager.h"
#include "ThomasEditorRenderProvider.h"

namespace
{
template <typename TResult>
TResult ProviderUnavailable()
{
    TResult Result;
    Result.bOk = false;
    Result.Code = TEXT("provider_unavailable");
    Result.Message = TEXT("ThomasEditorRender could not be loaded.");
    return Result;
}

IThomasEditorRenderProviderModule* LoadProvider()
{
    return FModuleManager::LoadModulePtr<IThomasEditorRenderProviderModule>(
        TEXT("ThomasEditorRender"));
}
}

FThomasStaticMeshInspectionResult UThomasRenderToolset::InspectStaticMesh(
    const FString& AssetPath)
{
    if (IThomasEditorRenderProviderModule* Provider = LoadProvider())
    {
        return Provider->InspectStaticMesh(AssetPath);
    }
    return ProviderUnavailable<FThomasStaticMeshInspectionResult>();
}

FThomasNanitePlanResult UThomasRenderToolset::PlanNanite(
    const FString& AssetPath,
    const FString& ExpectedRevision,
    const bool bEnabled)
{
    if (IThomasEditorRenderProviderModule* Provider = LoadProvider())
    {
        return Provider->PlanNanite(AssetPath, ExpectedRevision, bEnabled);
    }
    return ProviderUnavailable<FThomasNanitePlanResult>();
}

FThomasNaniteApplyResult UThomasRenderToolset::ApplyNanitePlan(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorRenderProviderModule* Provider = LoadProvider())
    {
        return Provider->ApplyNanitePlan(PlanId, bSave);
    }
    return ProviderUnavailable<FThomasNaniteApplyResult>();
}
