#pragma once

#include "ThomasEditorDomainProvider.h"

class THOMASEDITORCORE_API IThomasEditorAudioProviderModule
    : public IThomasEditorDomainProviderModule
{
public:
    virtual FThomasSpecializedAssetInspectionResult InspectAudioAsset(
        const FString& AssetPath,
        int32 MaxItems) = 0;
    virtual FThomasAudioCreatePlanResult PlanAudioAssetCreate(
        const FThomasAudioCreateRequest& Request) = 0;
    virtual FThomasAudioCreateApplyResult ApplyAudioAssetCreate(
        const FString& PlanId,
        bool bSave) = 0;
    virtual FThomasSpecializedAssetInspectionResult SearchMetaSoundNodeClasses(
        const FString& Query,
        int32 MaxResults) = 0;
    virtual FThomasMetaSoundPlanResult PlanMetaSoundPatch(
        const FThomasMetaSoundPatchRequest& Request) = 0;
    virtual FThomasMetaSoundApplyResult ApplyMetaSoundPlan(
        const FString& PlanId,
        bool bSave) = 0;
};
