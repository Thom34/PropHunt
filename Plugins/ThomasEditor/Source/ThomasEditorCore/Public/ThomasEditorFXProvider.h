#pragma once

#include "ThomasEditorDomainProvider.h"

class THOMASEDITORCORE_API IThomasEditorFXProviderModule
    : public IThomasEditorDomainProviderModule
{
public:
    virtual FThomasSpecializedAssetInspectionResult InspectFxAsset(
        const FString& AssetPath,
        int32 MaxItems) = 0;
    virtual FThomasDomainAssetCreatePlanResult PlanFxAssetCreate(
        const FThomasDomainAssetCreateRequest& Request) = 0;
    virtual FThomasDomainAssetCreateApplyResult ApplyFxAssetCreate(
        const FString& PlanId,
        bool bSave) = 0;
    virtual FThomasSpecializedAssetInspectionResult SearchNiagaraModules(
        const FString& Query,
        const FString& TargetUsage,
        int32 MaxResults) = 0;
    virtual FThomasNiagaraPlanResult PlanNiagaraPatch(
        const FThomasNiagaraPatchRequest& Request) = 0;
    virtual FThomasNiagaraApplyResult ApplyNiagaraPlan(
        const FString& PlanId,
        bool bSave) = 0;
};
