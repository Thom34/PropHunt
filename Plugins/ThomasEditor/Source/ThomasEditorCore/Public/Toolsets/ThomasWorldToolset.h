#pragma once

#include "ThomasEditorCoreTypes.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ThomasWorldToolset.generated.h"

UCLASS(BlueprintType)
class THOMASEDITORCORE_API UThomasWorldToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Inspects the currently open project level without loading or changing another map. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasWorldInspectionResult InspectCurrentLevel(
        const FString& ClassFilter = TEXT(""),
        bool bSelectedOnly = false,
        int32 MaxActors = 100);

    /** Reads effective renderer/Lumen settings and the lighting actors in the current level. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasLightingInspectionResult InspectLighting();

    /** Inspects World Partition/Data Layers, Landscape, Foliage, HLOD and navigation in the open level. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasEnvironmentInspectionResult InspectEnvironment(
        int32 MaxItems = 200);

    /** Reads one bounded Landscape vertex region and returns exact height/weight hashes plus optional samples. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasLandscapeRegionInspectionResult InspectLandscapeRegion(
        const FString& ActorPath,
        int32 MinX,
        int32 MinY,
        int32 MaxX,
        int32 MaxY,
        const FString& EditLayerGuid = TEXT(""),
        const FString& LayerInfoPath = TEXT(""),
        int32 MaxReturnedSamples = 256);

    /** Plans one native LandscapeLayerInfoObject creation under /Game/PropHunt. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasLandscapeLayerInfoCreatePlanResult PlanLandscapeLayerInfoCreate(
        const FThomasLandscapeLayerInfoCreateRequest& Request);

    /** Consumes one fresh LayerInfo creation plan and saves the new asset atomically. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasLandscapeLayerInfoCreateApplyResult ApplyLandscapeLayerInfoCreate(
        const FString& PlanId);

    /** Reads exact editable Details fields on one component owned by an actor in the open level. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasWorldComponentInspectionResult InspectActorComponent(
        const FString& ActorPath,
        const FString& ComponentPath,
        const TArray<FString>& PropertyNames,
        int32 MaxProperties = 100);

    /** Reads all editable FPostProcessSettings fields and weighted blendables on one exact volume. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PostProcess")
    static FThomasPostProcessInspectionResult InspectPostProcessVolume(
        const FString& ActorPath,
        const FString& SettingFilter = TEXT(""),
        int32 MaxSettings = 500);

    /** Plans typed Post Process/Lumen setting and blendable changes against an exact map revision. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PostProcess")
    static FThomasPostProcessPlanResult PlanPostProcessPatch(
        const FThomasPostProcessPatchRequest& Request);

    /** Consumes one Post Process plan; saving the map remains explicit. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PostProcess")
    static FThomasPostProcessApplyResult ApplyPostProcessPlan(
        const FString& PlanId,
        bool bSave = false);

    /** Plans an exact native map save/blank-or-WP-create/open operation. World switches are R2 and preserve dirty work. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasMapLifecyclePlanResult PlanMapLifecycle(
        const FThomasMapLifecycleRequest& Request);

    /** Consumes one map lifecycle plan after revalidating the exact current map revision. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasMapLifecycleApplyResult ApplyMapLifecyclePlan(
        const FString& PlanId);

    /** Plans a bounded World Partition navigation/HLOD commandlet job against an exact saved map revision. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasEnvironmentBuildPlanResult PlanEnvironmentBuild(
        const FThomasEnvironmentBuildRequest& Request);

    /** Consumes one environment build plan, unloads the map safely, and starts a hidden native UE process. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasEnvironmentBuildJobResult StartEnvironmentBuildJob(
        const FString& PlanId);

    /** Polls a non-blocking environment job and optionally reloads its map after process completion. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasEnvironmentBuildJobResult GetEnvironmentBuildJob(
        const FString& JobId,
        bool bReloadMapWhenComplete = true);

    /** Explicitly terminates a running builder process and restores its map when safe. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasEnvironmentBuildJobResult CancelEnvironmentBuildJob(
        const FString& JobId,
        bool bConfirmCancel = false);

    /** Plans a bounded actor edit against the exact currently open map revision. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasWorldPatchPlanResult PlanWorldPatch(
        const FThomasWorldPatchRequest& Request);

    /** Consumes one world plan. Save is explicit; failures undo the scoped transaction. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|World")
    static FThomasWorldPatchApplyResult ApplyWorldPlan(
        const FString& PlanId,
        bool bSave = false);
};
