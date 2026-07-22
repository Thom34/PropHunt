#pragma once

#include "ThomasEditorDomainProvider.h"

class THOMASEDITORCORE_API IThomasEditorCinematicsProviderModule
    : public IThomasEditorDomainProviderModule
{
public:
    virtual FThomasCinematicInspectionResult InspectLevelSequence(
        const FString& AssetPath,
        int32 MaxItems) = 0;
    virtual FThomasCinematicPlanResult PlanCinematicPatch(
        const FThomasCinematicPatchRequest& Request) = 0;
    virtual FThomasCinematicApplyResult ApplyCinematicPlan(
        const FString& PlanId,
        bool bSave) = 0;
};
