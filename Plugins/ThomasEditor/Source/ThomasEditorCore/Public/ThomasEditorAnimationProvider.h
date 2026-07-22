#pragma once

#include "ThomasEditorDomainProvider.h"

class THOMASEDITORCORE_API IThomasEditorAnimationProviderModule
    : public IThomasEditorDomainProviderModule
{
public:
    virtual FThomasSpecializedAssetInspectionResult InspectAnimationAsset(
        const FString& AssetPath,
        int32 MaxItems) = 0;
    virtual FThomasDomainAssetCreatePlanResult PlanAnimationAssetCreate(
        const FThomasDomainAssetCreateRequest& Request) = 0;
    virtual FThomasDomainAssetCreateApplyResult ApplyAnimationAssetCreate(
        const FString& PlanId,
        bool bSave) = 0;
    virtual FThomasAnimationPlanResult PlanAnimationPatch(
        const FThomasAnimationPatchRequest& Request) = 0;
    virtual FThomasAnimationApplyResult ApplyAnimationPlan(
        const FString& PlanId,
        bool bSave) = 0;
};
