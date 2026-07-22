#pragma once

#include "ThomasEditorCoreTypes.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ThomasDomainsToolset.generated.h"

UCLASS(BlueprintType)
class THOMASEDITORCORE_API UThomasDomainsToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Animation Blueprints, sequences, montages, blend spaces, skeletons, skeletal meshes and rigs. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Animation")
    static FThomasDomainInventoryResult InspectAnimationAssets(
        const FString& PackageRoot = TEXT("/Game/PropHunt"),
        int32 MaxResults = 100);

    /** Reads sequence/montage/skeleton data plus Anim Blueprint state machines, states and transitions. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Animation")
    static FThomasSpecializedAssetInspectionResult InspectAnimationAsset(
        const FString& AssetPath,
        int32 MaxItems = 200);

    /** Plans Anim Blueprint, Sequence, Montage or Blend Space creation against an explicit Skeleton context. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Animation")
    static FThomasDomainAssetCreatePlanResult PlanAnimationAssetCreate(
        const FThomasDomainAssetCreateRequest& Request);

    /** Consumes one native Animation creation plan; saving remains explicit. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Animation")
    static FThomasDomainAssetCreateApplyResult ApplyAnimationAssetCreate(
        const FString& PlanId,
        bool bSave = false);

    /** Preflights typed Sequence/Montage/Skeleton/Anim Blueprint edits against an exact clean revision. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Animation")
    static FThomasAnimationPlanResult PlanAnimationPatch(
        const FThomasAnimationPatchRequest& Request);

    /** Consumes one Animation patch plan; destructive edits require R2 confirmation. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Animation")
    static FThomasAnimationApplyResult ApplyAnimationPlan(
        const FString& PlanId,
        bool bSave = false);

    /** Level Sequence inventory without loading the Cinematics provider at startup. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Cinematics")
    static FThomasDomainInventoryResult InspectCinematicAssets(
        const FString& PackageRoot = TEXT("/Game/PropHunt"),
        int32 MaxResults = 100);

    /** Reads Level Sequence rates, playback range, bindings, tracks and sections. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Cinematics")
    static FThomasCinematicInspectionResult InspectLevelSequence(
        const FString& AssetPath,
        int32 MaxItems = 200);

    /** Plans native Level Sequence creation and guarded structural timeline edits. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Cinematics")
    static FThomasCinematicPlanResult PlanCinematicPatch(
        const FThomasCinematicPatchRequest& Request);

    /** Consumes one Cinematics plan; saving remains explicit. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Cinematics")
    static FThomasCinematicApplyResult ApplyCinematicPlan(
        const FString& PlanId,
        bool bSave = false);

    /** Sprites, Flipbooks, Tile Sets and Tile Maps. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Paper2D")
    static FThomasDomainInventoryResult InspectPaper2DAssets(
        const FString& PackageRoot = TEXT("/Game/PropHunt"),
        int32 MaxResults = 100);

    /** Reads typed Sprite source data or Flipbook frames. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Paper2D")
    static FThomasPaper2DInspectionResult InspectPaper2DAsset(
        const FString& AssetPath,
        int32 MaxFrames = 200);

    /** Plans native Paper2D creation and guarded Flipbook edits. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Paper2D")
    static FThomasPaper2DPlanResult PlanPaper2DPatch(
        const FThomasPaper2DPatchRequest& Request);

    /** Consumes one Paper2D plan; saving remains explicit. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Paper2D")
    static FThomasPaper2DApplyResult ApplyPaper2DPlan(
        const FString& PlanId,
        bool bSave = false);

    /** Enhanced Input, Data Assets/Tables, curves, registries and gameplay-tag related assets. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|GameplayData")
    static FThomasDomainInventoryResult InspectGameplayDataAssets(
        const FString& PackageRoot = TEXT("/Game/PropHunt"),
        int32 MaxResults = 100);

    /** Behavior Trees, Blackboards, EQS and StateTree assets. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|AI")
    static FThomasDomainInventoryResult InspectAIAssets(
        const FString& PackageRoot = TEXT("/Game/PropHunt"),
        int32 MaxResults = 100);

    /** Reads a Data Asset or Data Table, including bounded row values. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|GameplayData")
    static FThomasDataAssetInspectionResult InspectDataAsset(
        const FString& AssetPath,
        int32 MaxRows = 100);

    /** Reads typed Blackboard/Behavior Tree/EQS/StateTree structure. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|AI")
    static FThomasAIAssetInspectionResult InspectAIAsset(
        const FString& AssetPath,
        int32 MaxItems = 200);

    /** Plans guarded Data Asset/Data Table/AI asset creation and structural changes. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|DataAI")
    static FThomasDataAIPlanResult PlanDataAIPatch(
        const FThomasDataAIPatchRequest& Request);

    /** Consumes one Data/AI plan; saving is explicit and failures roll back. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|DataAI")
    static FThomasDataAIApplyResult ApplyDataAIPlan(
        const FString& PlanId,
        bool bSave = false);

    /** Niagara systems, emitters and parameter collections. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|FX")
    static FThomasDomainInventoryResult InspectFxAssets(
        const FString& PackageRoot = TEXT("/Game/PropHunt"),
        int32 MaxResults = 100);

    /** Reads Niagara system emitters, exposed parameters and script state. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|FX")
    static FThomasSpecializedAssetInspectionResult InspectFxAsset(
        const FString& AssetPath,
        int32 MaxItems = 200);

    /** Plans native Niagara System, Emitter or Parameter Collection creation. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|FX")
    static FThomasDomainAssetCreatePlanResult PlanFxAssetCreate(
        const FThomasDomainAssetCreateRequest& Request);

    /** Consumes one Niagara creation plan; saving remains explicit. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|FX")
    static FThomasDomainAssetCreateApplyResult ApplyFxAssetCreate(
        const FString& PlanId,
        bool bSave = false);

    /** Searches native/project Niagara module scripts compatible with a target stack usage. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|FX")
    static FThomasSpecializedAssetInspectionResult SearchNiagaraModules(
        const FString& Query = TEXT(""),
        const FString& TargetUsage = TEXT("particle_update"),
        int32 MaxResults = 100);

    /** Plans emitters, user parameters, stack modules, renderers and compilation changes on a Niagara System. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|FX")
    static FThomasNiagaraPlanResult PlanNiagaraPatch(
        const FThomasNiagaraPatchRequest& Request);

    /** Consumes a one-shot Niagara structural plan; compile and save remain explicit operations/flags. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|FX")
    static FThomasNiagaraApplyResult ApplyNiagaraPlan(
        const FString& PlanId,
        bool bSave = false);

    /** Sound waves/cues/classes/mixes plus MetaSound sources, patches and presets. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Audio")
    static FThomasDomainInventoryResult InspectAudioAssets(
        const FString& PackageRoot = TEXT("/Game/PropHunt"),
        int32 MaxResults = 100);

    /** Reads MetaSound graph nodes/pins or typed attenuation/concurrency state. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Audio")
    static FThomasSpecializedAssetInspectionResult InspectAudioAsset(
        const FString& AssetPath,
        int32 MaxItems = 200);

    /** Plans native common Audio or MetaSound asset creation through Epic editor factories. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Audio")
    static FThomasAudioCreatePlanResult PlanAudioAssetCreate(
        const FThomasAudioCreateRequest& Request);

    /** Consumes a one-shot audio creation plan; saving remains explicit. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Audio")
    static FThomasAudioCreateApplyResult ApplyAudioAssetCreate(
        const FString& PlanId,
        bool bSave = false);

    /** Searches registered MetaSound node classes and versions without exposing the registry directly. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Audio")
    static FThomasSpecializedAssetInspectionResult SearchMetaSoundNodeClasses(
        const FString& Query = TEXT(""),
        int32 MaxResults = 100);

    /** Plans a typed MetaSound graph/interface/default batch against an existing Source or Patch. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Audio")
    static FThomasMetaSoundPlanResult PlanMetaSoundPatch(
        const FThomasMetaSoundPatchRequest& Request);

    /** Consumes one MetaSound graph plan, re-registers the graph and optionally saves. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Audio")
    static FThomasMetaSoundApplyResult ApplyMetaSoundPlan(
        const FString& PlanId,
        bool bSave = false);

    /** Reads an Input Action or Input Mapping Context, including mappings/triggers/modifiers. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|EnhancedInput")
    static FThomasEnhancedInputInspectionResult InspectEnhancedInputAsset(
        const FString& AssetPath);

    /** Plans native Input Action creation/value type or Mapping Context add/remove operations. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|EnhancedInput")
    static FThomasEnhancedInputPlanResult PlanEnhancedInputPatch(
        const FThomasEnhancedInputPatchRequest& Request);

    /** Consumes a one-shot Enhanced Input plan; saving remains explicit. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|EnhancedInput")
    static FThomasEnhancedInputApplyResult ApplyEnhancedInputPlan(
        const FString& PlanId,
        bool bSave = false);

    /** PCG Graph inventory without loading the optional PCG provider at Core startup. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PCG")
    static FThomasDomainInventoryResult InspectPCGAssets(
        const FString& PackageRoot = TEXT("/Game/PropHunt"),
        int32 MaxResults = 100);

    /** Reads PCG graph nodes, settings, pins, edges, positions and graph metrics. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PCG")
    static FThomasSpecializedAssetInspectionResult InspectPCGGraph(
        const FString& AssetPath,
        int32 MaxItems = 300);

    /** Plans creation of a native PCG Graph asset. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PCG")
    static FThomasDomainAssetCreatePlanResult PlanPCGAssetCreate(
        const FThomasDomainAssetCreateRequest& Request);

    /** Consumes one PCG Graph creation plan; saving remains explicit. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PCG")
    static FThomasDomainAssetCreateApplyResult ApplyPCGAssetCreate(
        const FString& PlanId,
        bool bSave = false);

    /** Discovers loaded native UPCGSettings node classes by name/path. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PCG")
    static FThomasSpecializedAssetInspectionResult SearchPCGSettingsClasses(
        const FString& Query = TEXT(""),
        int32 MaxResults = 100);

    /** Plans guarded PCG node/settings/edge edits against an exact graph revision. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PCG")
    static FThomasPCGPlanResult PlanPCGPatch(
        const FThomasPCGPatchRequest& Request);

    /** Consumes one PCG graph plan; destructive graph edits require R2 confirmation. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PCG")
    static FThomasPCGApplyResult ApplyPCGPlan(
        const FString& PlanId,
        bool bSave = false);

    /** Plans a confirmed standalone PCG Asset graph execution against an exact clean revision. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PCG")
    static FThomasPCGExecutionPlanResult PlanPCGExecution(
        const FThomasPCGExecutionPlanRequest& Request);

    /** Consumes one PCG execution plan and starts a bounded engine-subsystem job. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PCG")
    static FThomasPCGExecutionStartResult StartPCGExecution(
        const FString& PlanId);

    /** Reads bounded PCG execution state and output summaries. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PCG")
    static FThomasPCGExecutionStatusResult GetPCGExecutionStatus(
        const FString& JobId);

    /** Requests cancellation of a running PCG execution job. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|PCG")
    static FThomasPCGExecutionCancelResult CancelPCGExecution(
        const FString& JobId);
};
