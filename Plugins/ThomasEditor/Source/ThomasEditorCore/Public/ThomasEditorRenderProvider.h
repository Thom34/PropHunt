#pragma once

#include "Modules/ModuleInterface.h"
#include "ThomasEditorCoreTypes.h"

class THOMASEDITORCORE_API IThomasEditorRenderProviderModule : public IModuleInterface
{
public:
    virtual FThomasStaticMeshInspectionResult InspectStaticMesh(
        const FString& AssetPath) = 0;
    virtual FThomasNanitePlanResult PlanNanite(
        const FString& AssetPath,
        const FString& ExpectedRevision,
        bool bEnabled) = 0;
    virtual FThomasNaniteApplyResult ApplyNanitePlan(
        const FString& PlanId,
        bool bSave) = 0;
};
