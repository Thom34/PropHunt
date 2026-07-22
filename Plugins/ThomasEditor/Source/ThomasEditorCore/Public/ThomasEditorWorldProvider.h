#pragma once

#include "Modules/ModuleInterface.h"
#include "ThomasEditorCoreTypes.h"

class THOMASEDITORCORE_API IThomasEditorWorldProviderModule : public IModuleInterface
{
public:
    virtual FThomasWorldInspectionResult InspectCurrentLevel(
        const FString& ClassFilter,
        bool bSelectedOnly,
        int32 MaxActors) = 0;

    virtual FThomasLightingInspectionResult InspectLighting() = 0;

    virtual FThomasEnvironmentInspectionResult InspectEnvironment(
        int32 MaxItems) = 0;

    virtual FThomasLandscapeRegionInspectionResult InspectLandscapeRegion(
        const FString& ActorPath,
        int32 MinX,
        int32 MinY,
        int32 MaxX,
        int32 MaxY,
        const FString& EditLayerGuid,
        const FString& LayerInfoPath,
        int32 MaxReturnedSamples) = 0;

    virtual FThomasLandscapeLayerInfoCreatePlanResult PlanLandscapeLayerInfoCreate(
        const FThomasLandscapeLayerInfoCreateRequest& Request) = 0;

    virtual FThomasLandscapeLayerInfoCreateApplyResult ApplyLandscapeLayerInfoCreate(
        const FString& PlanId) = 0;

    virtual FThomasWorldComponentInspectionResult InspectActorComponent(
        const FString& ActorPath,
        const FString& ComponentPath,
        const TArray<FString>& PropertyNames,
        int32 MaxProperties) = 0;

    virtual FThomasPostProcessInspectionResult InspectPostProcessVolume(
        const FString& ActorPath,
        const FString& SettingFilter,
        int32 MaxSettings) = 0;

    virtual FThomasPostProcessPlanResult PlanPostProcessPatch(
        const FThomasPostProcessPatchRequest& Request) = 0;

    virtual FThomasPostProcessApplyResult ApplyPostProcessPlan(
        const FString& PlanId,
        bool bSave) = 0;

    virtual FThomasMapLifecyclePlanResult PlanMapLifecycle(
        const FThomasMapLifecycleRequest& Request) = 0;

    virtual FThomasMapLifecycleApplyResult ApplyMapLifecyclePlan(
        const FString& PlanId) = 0;

    virtual FThomasEnvironmentBuildPlanResult PlanEnvironmentBuild(
        const FThomasEnvironmentBuildRequest& Request) = 0;

    virtual FThomasEnvironmentBuildJobResult StartEnvironmentBuildJob(
        const FString& PlanId) = 0;

    virtual FThomasEnvironmentBuildJobResult GetEnvironmentBuildJob(
        const FString& JobId,
        bool bReloadMapWhenComplete) = 0;

    virtual FThomasEnvironmentBuildJobResult CancelEnvironmentBuildJob(
        const FString& JobId,
        bool bConfirmCancel) = 0;

    virtual FThomasWorldPatchPlanResult PlanWorldPatch(
        const FThomasWorldPatchRequest& Request) = 0;

    virtual FThomasWorldPatchApplyResult ApplyWorldPlan(
        const FString& PlanId,
        bool bSave) = 0;
};
