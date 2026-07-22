#pragma once

#include "ThomasEditorDomainProvider.h"

class THOMASEDITORCORE_API IThomasEditorPCGProviderModule
    : public IThomasEditorDomainProviderModule
{
public:
    virtual FThomasSpecializedAssetInspectionResult InspectPCGGraph(
        const FString& AssetPath,
        int32 MaxItems) = 0;
    virtual FThomasDomainAssetCreatePlanResult PlanPCGAssetCreate(
        const FThomasDomainAssetCreateRequest& Request) = 0;
    virtual FThomasDomainAssetCreateApplyResult ApplyPCGAssetCreate(
        const FString& PlanId,
        bool bSave) = 0;
    virtual FThomasSpecializedAssetInspectionResult SearchPCGSettingsClasses(
        const FString& Query,
        int32 MaxResults) = 0;
    virtual FThomasPCGPlanResult PlanPCGPatch(
        const FThomasPCGPatchRequest& Request) = 0;
    virtual FThomasPCGApplyResult ApplyPCGPlan(
        const FString& PlanId,
        bool bSave) = 0;
    virtual FThomasPCGExecutionPlanResult PlanPCGExecution(
        const FThomasPCGExecutionPlanRequest& Request) = 0;
    virtual FThomasPCGExecutionStartResult StartPCGExecution(
        const FString& PlanId) = 0;
    virtual FThomasPCGExecutionStatusResult GetPCGExecutionStatus(
        const FString& JobId) = 0;
    virtual FThomasPCGExecutionCancelResult CancelPCGExecution(
        const FString& JobId) = 0;
};
