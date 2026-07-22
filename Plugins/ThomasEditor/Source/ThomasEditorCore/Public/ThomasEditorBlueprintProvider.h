#pragma once

#include "Modules/ModuleInterface.h"
#include "ThomasEditorCoreTypes.h"

class THOMASEDITORCORE_API IThomasEditorBlueprintProviderModule : public IModuleInterface
{
public:
    virtual FThomasBlueprintInspectionResult InspectBlueprint(
        const FString& AssetPath,
        bool bIncludeNodes,
        int32 MaxNodes) = 0;

    virtual FThomasBlueprintPlanResult PlanBlueprintBatch(
        const FThomasBlueprintBatchRequest& Request) = 0;

    virtual FThomasBlueprintApplyResult ApplyBlueprintPlan(
        const FString& PlanId,
        bool bCompile,
        bool bSave) = 0;

    virtual FThomasBlueprintValidationResult ValidateBlueprint(
        const FString& AssetPath,
        const TArray<FString>& RequiredWidgetNames) = 0;
};
