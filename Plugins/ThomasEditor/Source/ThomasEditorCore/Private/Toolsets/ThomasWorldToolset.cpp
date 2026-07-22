#include "Toolsets/ThomasWorldToolset.h"

#include "Modules/ModuleManager.h"
#include "ThomasEditorWorldProvider.h"

namespace
{
template <typename TResult>
TResult ProviderUnavailable()
{
    TResult Result;
    Result.bOk = false;
    Result.Code = TEXT("provider_unavailable");
    Result.Message = TEXT("ThomasEditorWorld could not be loaded.");
    return Result;
}

IThomasEditorWorldProviderModule* LoadProvider()
{
    return FModuleManager::LoadModulePtr<IThomasEditorWorldProviderModule>(
        TEXT("ThomasEditorWorld"));
}
}

FThomasWorldInspectionResult UThomasWorldToolset::InspectCurrentLevel(
    const FString& ClassFilter,
    const bool bSelectedOnly,
    const int32 MaxActors)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->InspectCurrentLevel(ClassFilter, bSelectedOnly, MaxActors);
    }
    return ProviderUnavailable<FThomasWorldInspectionResult>();
}

FThomasLightingInspectionResult UThomasWorldToolset::InspectLighting()
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->InspectLighting();
    }
    return ProviderUnavailable<FThomasLightingInspectionResult>();
}

FThomasEnvironmentInspectionResult UThomasWorldToolset::InspectEnvironment(
    const int32 MaxItems)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->InspectEnvironment(MaxItems);
    }
    return ProviderUnavailable<FThomasEnvironmentInspectionResult>();
}

FThomasLandscapeRegionInspectionResult UThomasWorldToolset::InspectLandscapeRegion(
    const FString& ActorPath,
    const int32 MinX,
    const int32 MinY,
    const int32 MaxX,
    const int32 MaxY,
    const FString& EditLayerGuid,
    const FString& LayerInfoPath,
    const int32 MaxReturnedSamples)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->InspectLandscapeRegion(
            ActorPath,
            MinX,
            MinY,
            MaxX,
            MaxY,
            EditLayerGuid,
            LayerInfoPath,
            MaxReturnedSamples);
    }
    return ProviderUnavailable<FThomasLandscapeRegionInspectionResult>();
}

FThomasLandscapeLayerInfoCreatePlanResult UThomasWorldToolset::PlanLandscapeLayerInfoCreate(
    const FThomasLandscapeLayerInfoCreateRequest& Request)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->PlanLandscapeLayerInfoCreate(Request);
    }
    return ProviderUnavailable<FThomasLandscapeLayerInfoCreatePlanResult>();
}

FThomasLandscapeLayerInfoCreateApplyResult UThomasWorldToolset::ApplyLandscapeLayerInfoCreate(
    const FString& PlanId)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->ApplyLandscapeLayerInfoCreate(PlanId);
    }
    return ProviderUnavailable<FThomasLandscapeLayerInfoCreateApplyResult>();
}

FThomasWorldComponentInspectionResult UThomasWorldToolset::InspectActorComponent(
    const FString& ActorPath,
    const FString& ComponentPath,
    const TArray<FString>& PropertyNames,
    const int32 MaxProperties)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->InspectActorComponent(
            ActorPath, ComponentPath, PropertyNames, MaxProperties);
    }
    return ProviderUnavailable<FThomasWorldComponentInspectionResult>();
}

FThomasPostProcessInspectionResult UThomasWorldToolset::InspectPostProcessVolume(
    const FString& ActorPath,
    const FString& SettingFilter,
    const int32 MaxSettings)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->InspectPostProcessVolume(ActorPath, SettingFilter, MaxSettings);
    }
    return ProviderUnavailable<FThomasPostProcessInspectionResult>();
}

FThomasPostProcessPlanResult UThomasWorldToolset::PlanPostProcessPatch(
    const FThomasPostProcessPatchRequest& Request)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->PlanPostProcessPatch(Request);
    }
    return ProviderUnavailable<FThomasPostProcessPlanResult>();
}

FThomasPostProcessApplyResult UThomasWorldToolset::ApplyPostProcessPlan(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->ApplyPostProcessPlan(PlanId, bSave);
    }
    return ProviderUnavailable<FThomasPostProcessApplyResult>();
}

FThomasMapLifecyclePlanResult UThomasWorldToolset::PlanMapLifecycle(
    const FThomasMapLifecycleRequest& Request)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->PlanMapLifecycle(Request);
    }
    return ProviderUnavailable<FThomasMapLifecyclePlanResult>();
}

FThomasMapLifecycleApplyResult UThomasWorldToolset::ApplyMapLifecyclePlan(
    const FString& PlanId)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->ApplyMapLifecyclePlan(PlanId);
    }
    return ProviderUnavailable<FThomasMapLifecycleApplyResult>();
}

FThomasEnvironmentBuildPlanResult UThomasWorldToolset::PlanEnvironmentBuild(
    const FThomasEnvironmentBuildRequest& Request)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->PlanEnvironmentBuild(Request);
    }
    return ProviderUnavailable<FThomasEnvironmentBuildPlanResult>();
}

FThomasEnvironmentBuildJobResult UThomasWorldToolset::StartEnvironmentBuildJob(
    const FString& PlanId)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->StartEnvironmentBuildJob(PlanId);
    }
    return ProviderUnavailable<FThomasEnvironmentBuildJobResult>();
}

FThomasEnvironmentBuildJobResult UThomasWorldToolset::GetEnvironmentBuildJob(
    const FString& JobId,
    const bool bReloadMapWhenComplete)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->GetEnvironmentBuildJob(JobId, bReloadMapWhenComplete);
    }
    return ProviderUnavailable<FThomasEnvironmentBuildJobResult>();
}

FThomasEnvironmentBuildJobResult UThomasWorldToolset::CancelEnvironmentBuildJob(
    const FString& JobId,
    const bool bConfirmCancel)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->CancelEnvironmentBuildJob(JobId, bConfirmCancel);
    }
    return ProviderUnavailable<FThomasEnvironmentBuildJobResult>();
}

FThomasWorldPatchPlanResult UThomasWorldToolset::PlanWorldPatch(
    const FThomasWorldPatchRequest& Request)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->PlanWorldPatch(Request);
    }
    return ProviderUnavailable<FThomasWorldPatchPlanResult>();
}

FThomasWorldPatchApplyResult UThomasWorldToolset::ApplyWorldPlan(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorWorldProviderModule* Provider = LoadProvider())
    {
        return Provider->ApplyWorldPlan(PlanId, bSave);
    }
    return ProviderUnavailable<FThomasWorldPatchApplyResult>();
}
