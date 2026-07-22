#pragma once

#include "ThomasEditorDomainProvider.h"

class THOMASEDITORCORE_API IThomasEditorPaper2DProviderModule
    : public IThomasEditorDomainProviderModule
{
public:
    virtual FThomasPaper2DInspectionResult InspectPaper2DAsset(
        const FString& AssetPath,
        int32 MaxFrames) = 0;
    virtual FThomasPaper2DPlanResult PlanPaper2DPatch(
        const FThomasPaper2DPatchRequest& Request) = 0;
    virtual FThomasPaper2DApplyResult ApplyPaper2DPlan(
        const FString& PlanId,
        bool bSave) = 0;
};
