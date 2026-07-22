#pragma once

#include "Modules/ModuleInterface.h"
#include "ThomasEditorCoreTypes.h"
#include "ThomasEditorDomainProvider.h"

class THOMASEDITORCORE_API IThomasEditorDataAIProviderModule
    : public IThomasEditorDomainProviderModule
{
public:
    virtual FThomasDataAssetInspectionResult InspectDataAsset(
        const FString& AssetPath,
        int32 MaxRows) = 0;

    virtual FThomasAIAssetInspectionResult InspectAIAsset(
        const FString& AssetPath,
        int32 MaxItems) = 0;

    virtual FThomasDataAIPlanResult PlanDataAIPatch(
        const FThomasDataAIPatchRequest& Request) = 0;

    virtual FThomasDataAIApplyResult ApplyDataAIPlan(
        const FString& PlanId,
        bool bSave) = 0;
};
