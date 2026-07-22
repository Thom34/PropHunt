#pragma once

#include "Modules/ModuleInterface.h"
#include "ThomasEditorCoreTypes.h"

class THOMASEDITORCORE_API IThomasEditorMaterialsProviderModule : public IModuleInterface
{
public:
    virtual FThomasMaterialInspectionResult InspectMaterial(
        const FString& AssetPath,
        int32 MaxExpressions) = 0;

    virtual FThomasMaterialPlanResult PlanMaterialPatch(
        const FThomasMaterialPatchRequest& Request) = 0;

    virtual FThomasMaterialApplyResult ApplyMaterialPlan(
        const FString& PlanId,
        bool bCompile,
        bool bSave) = 0;
};
