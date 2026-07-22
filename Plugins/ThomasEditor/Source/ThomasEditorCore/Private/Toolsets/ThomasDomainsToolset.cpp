#include "Toolsets/ThomasDomainsToolset.h"

#include "Modules/ModuleManager.h"
#include "ThomasEditorAudioProvider.h"
#include "ThomasEditorAnimationProvider.h"
#include "ThomasEditorCinematicsProvider.h"
#include "ThomasEditorDataAIProvider.h"
#include "ThomasEditorDomainProvider.h"
#include "ThomasEditorGameplayDataProvider.h"
#include "ThomasEditorFXProvider.h"
#include "ThomasEditorPaper2DProvider.h"
#include "ThomasEditorPCGProvider.h"

namespace
{
FThomasDomainInventoryResult Invoke(
    const TCHAR* ModuleName,
    const FString& PackageRoot,
    const int32 MaxResults)
{
    if (IThomasEditorDomainProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorDomainProviderModule>(ModuleName))
    {
        return Provider->GetInventory(PackageRoot, MaxResults);
    }
    FThomasDomainInventoryResult Result;
    Result.bOk = false;
    Result.Code = TEXT("provider_unavailable");
    Result.Message = FString::Printf(TEXT("%s could not be loaded."), ModuleName);
    return Result;
}
}

FThomasDomainInventoryResult UThomasDomainsToolset::InspectAnimationAssets(
    const FString& PackageRoot,
    const int32 MaxResults)
{
    return Invoke(TEXT("ThomasEditorAnimation"), PackageRoot, MaxResults);
}

FThomasSpecializedAssetInspectionResult UThomasDomainsToolset::InspectAnimationAsset(
    const FString& AssetPath,
    const int32 MaxItems)
{
    if (IThomasEditorAnimationProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorAnimationProviderModule>(
                TEXT("ThomasEditorAnimation")))
    {
        return Provider->InspectAnimationAsset(AssetPath, MaxItems);
    }
    FThomasSpecializedAssetInspectionResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDomainAssetCreatePlanResult UThomasDomainsToolset::PlanAnimationAssetCreate(
    const FThomasDomainAssetCreateRequest& Request)
{
    if (IThomasEditorAnimationProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorAnimationProviderModule>(TEXT("ThomasEditorAnimation")))
    {
        return Provider->PlanAnimationAssetCreate(Request);
    }
    FThomasDomainAssetCreatePlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDomainAssetCreateApplyResult UThomasDomainsToolset::ApplyAnimationAssetCreate(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorAnimationProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorAnimationProviderModule>(TEXT("ThomasEditorAnimation")))
    {
        return Provider->ApplyAnimationAssetCreate(PlanId, bSave);
    }
    FThomasDomainAssetCreateApplyResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasAnimationPlanResult UThomasDomainsToolset::PlanAnimationPatch(
    const FThomasAnimationPatchRequest& Request)
{
    if (IThomasEditorAnimationProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorAnimationProviderModule>(TEXT("ThomasEditorAnimation")))
    {
        return Provider->PlanAnimationPatch(Request);
    }
    FThomasAnimationPlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasAnimationApplyResult UThomasDomainsToolset::ApplyAnimationPlan(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorAnimationProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorAnimationProviderModule>(TEXT("ThomasEditorAnimation")))
    {
        return Provider->ApplyAnimationPlan(PlanId, bSave);
    }
    FThomasAnimationApplyResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDomainInventoryResult UThomasDomainsToolset::InspectCinematicAssets(
    const FString& PackageRoot,
    const int32 MaxResults)
{
    return Invoke(TEXT("ThomasEditorCinematics"), PackageRoot, MaxResults);
}

FThomasCinematicInspectionResult UThomasDomainsToolset::InspectLevelSequence(
    const FString& AssetPath,
    const int32 MaxItems)
{
    if (IThomasEditorCinematicsProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorCinematicsProviderModule>(
                TEXT("ThomasEditorCinematics")))
    {
        return Provider->InspectLevelSequence(AssetPath, MaxItems);
    }
    FThomasCinematicInspectionResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasCinematicPlanResult UThomasDomainsToolset::PlanCinematicPatch(
    const FThomasCinematicPatchRequest& Request)
{
    if (IThomasEditorCinematicsProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorCinematicsProviderModule>(
                TEXT("ThomasEditorCinematics")))
    {
        return Provider->PlanCinematicPatch(Request);
    }
    FThomasCinematicPlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasCinematicApplyResult UThomasDomainsToolset::ApplyCinematicPlan(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorCinematicsProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorCinematicsProviderModule>(
                TEXT("ThomasEditorCinematics")))
    {
        return Provider->ApplyCinematicPlan(PlanId, bSave);
    }
    FThomasCinematicApplyResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDomainInventoryResult UThomasDomainsToolset::InspectPaper2DAssets(
    const FString& PackageRoot,
    const int32 MaxResults)
{
    return Invoke(TEXT("ThomasEditorPaper2D"), PackageRoot, MaxResults);
}

FThomasPaper2DInspectionResult UThomasDomainsToolset::InspectPaper2DAsset(
    const FString& AssetPath,
    const int32 MaxFrames)
{
    if (IThomasEditorPaper2DProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPaper2DProviderModule>(
                TEXT("ThomasEditorPaper2D")))
    {
        return Provider->InspectPaper2DAsset(AssetPath, MaxFrames);
    }
    FThomasPaper2DInspectionResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasPaper2DPlanResult UThomasDomainsToolset::PlanPaper2DPatch(
    const FThomasPaper2DPatchRequest& Request)
{
    if (IThomasEditorPaper2DProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPaper2DProviderModule>(
                TEXT("ThomasEditorPaper2D")))
    {
        return Provider->PlanPaper2DPatch(Request);
    }
    FThomasPaper2DPlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasPaper2DApplyResult UThomasDomainsToolset::ApplyPaper2DPlan(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorPaper2DProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPaper2DProviderModule>(
                TEXT("ThomasEditorPaper2D")))
    {
        return Provider->ApplyPaper2DPlan(PlanId, bSave);
    }
    FThomasPaper2DApplyResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDomainInventoryResult UThomasDomainsToolset::InspectGameplayDataAssets(
    const FString& PackageRoot,
    const int32 MaxResults)
{
    return Invoke(TEXT("ThomasEditorGameplayData"), PackageRoot, MaxResults);
}

FThomasDomainInventoryResult UThomasDomainsToolset::InspectAIAssets(
    const FString& PackageRoot,
    const int32 MaxResults)
{
    return Invoke(TEXT("ThomasEditorDataAI"), PackageRoot, MaxResults);
}

FThomasDataAssetInspectionResult UThomasDomainsToolset::InspectDataAsset(
    const FString& AssetPath,
    const int32 MaxRows)
{
    if (IThomasEditorDataAIProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorDataAIProviderModule>(
                TEXT("ThomasEditorDataAI")))
    {
        return Provider->InspectDataAsset(AssetPath, MaxRows);
    }
    FThomasDataAssetInspectionResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasAIAssetInspectionResult UThomasDomainsToolset::InspectAIAsset(
    const FString& AssetPath,
    const int32 MaxItems)
{
    if (IThomasEditorDataAIProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorDataAIProviderModule>(
                TEXT("ThomasEditorDataAI")))
    {
        return Provider->InspectAIAsset(AssetPath, MaxItems);
    }
    FThomasAIAssetInspectionResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDataAIPlanResult UThomasDomainsToolset::PlanDataAIPatch(
    const FThomasDataAIPatchRequest& Request)
{
    if (IThomasEditorDataAIProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorDataAIProviderModule>(
                TEXT("ThomasEditorDataAI")))
    {
        return Provider->PlanDataAIPatch(Request);
    }
    FThomasDataAIPlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDataAIApplyResult UThomasDomainsToolset::ApplyDataAIPlan(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorDataAIProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorDataAIProviderModule>(
                TEXT("ThomasEditorDataAI")))
    {
        return Provider->ApplyDataAIPlan(PlanId, bSave);
    }
    FThomasDataAIApplyResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDomainInventoryResult UThomasDomainsToolset::InspectFxAssets(
    const FString& PackageRoot,
    const int32 MaxResults)
{
    return Invoke(TEXT("ThomasEditorFX"), PackageRoot, MaxResults);
}

FThomasSpecializedAssetInspectionResult UThomasDomainsToolset::InspectFxAsset(
    const FString& AssetPath,
    const int32 MaxItems)
{
    if (IThomasEditorFXProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorFXProviderModule>(
                TEXT("ThomasEditorFX")))
    {
        return Provider->InspectFxAsset(AssetPath, MaxItems);
    }
    FThomasSpecializedAssetInspectionResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDomainAssetCreatePlanResult UThomasDomainsToolset::PlanFxAssetCreate(
    const FThomasDomainAssetCreateRequest& Request)
{
    if (IThomasEditorFXProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorFXProviderModule>(TEXT("ThomasEditorFX")))
    {
        return Provider->PlanFxAssetCreate(Request);
    }
    FThomasDomainAssetCreatePlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDomainAssetCreateApplyResult UThomasDomainsToolset::ApplyFxAssetCreate(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorFXProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorFXProviderModule>(TEXT("ThomasEditorFX")))
    {
        return Provider->ApplyFxAssetCreate(PlanId, bSave);
    }
    FThomasDomainAssetCreateApplyResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasSpecializedAssetInspectionResult UThomasDomainsToolset::SearchNiagaraModules(
    const FString& Query,
    const FString& TargetUsage,
    const int32 MaxResults)
{
    if (IThomasEditorFXProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorFXProviderModule>(TEXT("ThomasEditorFX")))
    {
        return Provider->SearchNiagaraModules(Query, TargetUsage, MaxResults);
    }
    FThomasSpecializedAssetInspectionResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasNiagaraPlanResult UThomasDomainsToolset::PlanNiagaraPatch(
    const FThomasNiagaraPatchRequest& Request)
{
    if (IThomasEditorFXProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorFXProviderModule>(TEXT("ThomasEditorFX")))
    {
        return Provider->PlanNiagaraPatch(Request);
    }
    FThomasNiagaraPlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasNiagaraApplyResult UThomasDomainsToolset::ApplyNiagaraPlan(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorFXProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorFXProviderModule>(TEXT("ThomasEditorFX")))
    {
        return Provider->ApplyNiagaraPlan(PlanId, bSave);
    }
    FThomasNiagaraApplyResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDomainInventoryResult UThomasDomainsToolset::InspectAudioAssets(
    const FString& PackageRoot,
    const int32 MaxResults)
{
    return Invoke(TEXT("ThomasEditorAudio"), PackageRoot, MaxResults);
}

FThomasSpecializedAssetInspectionResult UThomasDomainsToolset::InspectAudioAsset(
    const FString& AssetPath,
    const int32 MaxItems)
{
    if (IThomasEditorAudioProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorAudioProviderModule>(
                TEXT("ThomasEditorAudio")))
    {
        return Provider->InspectAudioAsset(AssetPath, MaxItems);
    }
    FThomasSpecializedAssetInspectionResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasAudioCreatePlanResult UThomasDomainsToolset::PlanAudioAssetCreate(
    const FThomasAudioCreateRequest& Request)
{
    if (IThomasEditorAudioProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorAudioProviderModule>(
                TEXT("ThomasEditorAudio")))
    {
        return Provider->PlanAudioAssetCreate(Request);
    }
    FThomasAudioCreatePlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasAudioCreateApplyResult UThomasDomainsToolset::ApplyAudioAssetCreate(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorAudioProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorAudioProviderModule>(
                TEXT("ThomasEditorAudio")))
    {
        return Provider->ApplyAudioAssetCreate(PlanId, bSave);
    }
    FThomasAudioCreateApplyResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasSpecializedAssetInspectionResult UThomasDomainsToolset::SearchMetaSoundNodeClasses(
    const FString& Query,
    const int32 MaxResults)
{
    if (IThomasEditorAudioProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorAudioProviderModule>(TEXT("ThomasEditorAudio")))
    {
        return Provider->SearchMetaSoundNodeClasses(Query, MaxResults);
    }
    FThomasSpecializedAssetInspectionResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasMetaSoundPlanResult UThomasDomainsToolset::PlanMetaSoundPatch(
    const FThomasMetaSoundPatchRequest& Request)
{
    if (IThomasEditorAudioProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorAudioProviderModule>(TEXT("ThomasEditorAudio")))
    {
        return Provider->PlanMetaSoundPatch(Request);
    }
    FThomasMetaSoundPlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasMetaSoundApplyResult UThomasDomainsToolset::ApplyMetaSoundPlan(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorAudioProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorAudioProviderModule>(TEXT("ThomasEditorAudio")))
    {
        return Provider->ApplyMetaSoundPlan(PlanId, bSave);
    }
    FThomasMetaSoundApplyResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasEnhancedInputInspectionResult UThomasDomainsToolset::InspectEnhancedInputAsset(
    const FString& AssetPath)
{
    if (IThomasEditorGameplayDataProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorGameplayDataProviderModule>(
                TEXT("ThomasEditorGameplayData")))
    {
        return Provider->InspectEnhancedInputAsset(AssetPath);
    }
    FThomasEnhancedInputInspectionResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasEnhancedInputPlanResult UThomasDomainsToolset::PlanEnhancedInputPatch(
    const FThomasEnhancedInputPatchRequest& Request)
{
    if (IThomasEditorGameplayDataProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorGameplayDataProviderModule>(
                TEXT("ThomasEditorGameplayData")))
    {
        return Provider->PlanEnhancedInputPatch(Request);
    }
    FThomasEnhancedInputPlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasEnhancedInputApplyResult UThomasDomainsToolset::ApplyEnhancedInputPlan(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorGameplayDataProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorGameplayDataProviderModule>(
                TEXT("ThomasEditorGameplayData")))
    {
        return Provider->ApplyEnhancedInputPlan(PlanId, bSave);
    }
    FThomasEnhancedInputApplyResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDomainInventoryResult UThomasDomainsToolset::InspectPCGAssets(
    const FString& PackageRoot,
    const int32 MaxResults)
{
    return Invoke(TEXT("ThomasEditorPCG"), PackageRoot, MaxResults);
}

FThomasSpecializedAssetInspectionResult UThomasDomainsToolset::InspectPCGGraph(
    const FString& AssetPath,
    const int32 MaxItems)
{
    if (IThomasEditorPCGProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPCGProviderModule>(TEXT("ThomasEditorPCG")))
    {
        return Provider->InspectPCGGraph(AssetPath, MaxItems);
    }
    FThomasSpecializedAssetInspectionResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDomainAssetCreatePlanResult UThomasDomainsToolset::PlanPCGAssetCreate(
    const FThomasDomainAssetCreateRequest& Request)
{
    if (IThomasEditorPCGProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPCGProviderModule>(TEXT("ThomasEditorPCG")))
    {
        return Provider->PlanPCGAssetCreate(Request);
    }
    FThomasDomainAssetCreatePlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasDomainAssetCreateApplyResult UThomasDomainsToolset::ApplyPCGAssetCreate(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorPCGProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPCGProviderModule>(TEXT("ThomasEditorPCG")))
    {
        return Provider->ApplyPCGAssetCreate(PlanId, bSave);
    }
    FThomasDomainAssetCreateApplyResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasSpecializedAssetInspectionResult UThomasDomainsToolset::SearchPCGSettingsClasses(
    const FString& Query,
    const int32 MaxResults)
{
    if (IThomasEditorPCGProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPCGProviderModule>(TEXT("ThomasEditorPCG")))
    {
        return Provider->SearchPCGSettingsClasses(Query, MaxResults);
    }
    FThomasSpecializedAssetInspectionResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasPCGPlanResult UThomasDomainsToolset::PlanPCGPatch(
    const FThomasPCGPatchRequest& Request)
{
    if (IThomasEditorPCGProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPCGProviderModule>(TEXT("ThomasEditorPCG")))
    {
        return Provider->PlanPCGPatch(Request);
    }
    FThomasPCGPlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasPCGApplyResult UThomasDomainsToolset::ApplyPCGPlan(
    const FString& PlanId,
    const bool bSave)
{
    if (IThomasEditorPCGProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPCGProviderModule>(TEXT("ThomasEditorPCG")))
    {
        return Provider->ApplyPCGPlan(PlanId, bSave);
    }
    FThomasPCGApplyResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasPCGExecutionPlanResult UThomasDomainsToolset::PlanPCGExecution(
    const FThomasPCGExecutionPlanRequest& Request)
{
    if (IThomasEditorPCGProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPCGProviderModule>(TEXT("ThomasEditorPCG")))
    {
        return Provider->PlanPCGExecution(Request);
    }
    FThomasPCGExecutionPlanResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasPCGExecutionStartResult UThomasDomainsToolset::StartPCGExecution(
    const FString& PlanId)
{
    if (IThomasEditorPCGProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPCGProviderModule>(TEXT("ThomasEditorPCG")))
    {
        return Provider->StartPCGExecution(PlanId);
    }
    FThomasPCGExecutionStartResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasPCGExecutionStatusResult UThomasDomainsToolset::GetPCGExecutionStatus(
    const FString& JobId)
{
    if (IThomasEditorPCGProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPCGProviderModule>(TEXT("ThomasEditorPCG")))
    {
        return Provider->GetPCGExecutionStatus(JobId);
    }
    FThomasPCGExecutionStatusResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}

FThomasPCGExecutionCancelResult UThomasDomainsToolset::CancelPCGExecution(
    const FString& JobId)
{
    if (IThomasEditorPCGProviderModule* Provider =
            FModuleManager::LoadModulePtr<IThomasEditorPCGProviderModule>(TEXT("ThomasEditorPCG")))
    {
        return Provider->CancelPCGExecution(JobId);
    }
    FThomasPCGExecutionCancelResult Result;
    Result.Code = TEXT("provider_unavailable");
    return Result;
}
