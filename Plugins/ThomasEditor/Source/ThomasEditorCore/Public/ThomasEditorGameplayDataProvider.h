#pragma once

#include "ThomasEditorCoreTypes.h"
#include "ThomasEditorDomainProvider.h"

class THOMASEDITORCORE_API IThomasEditorGameplayDataProviderModule
    : public IThomasEditorDomainProviderModule
{
public:
    virtual FThomasEnhancedInputInspectionResult InspectEnhancedInputAsset(
        const FString& AssetPath) = 0;

    virtual FThomasEnhancedInputPlanResult PlanEnhancedInputPatch(
        const FThomasEnhancedInputPatchRequest& Request) = 0;

    virtual FThomasEnhancedInputApplyResult ApplyEnhancedInputPlan(
        const FString& PlanId,
        bool bSave) = 0;
};
