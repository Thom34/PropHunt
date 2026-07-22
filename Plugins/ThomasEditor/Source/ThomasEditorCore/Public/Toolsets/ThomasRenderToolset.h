#pragma once

#include "ThomasEditorCoreTypes.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ThomasRenderToolset.generated.h"

UCLASS(BlueprintType)
class THOMASEDITORCORE_API UThomasRenderToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Reads mesh, material, collision, LOD and Nanite build state. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Render")
    static FThomasStaticMeshInspectionResult InspectStaticMesh(const FString& AssetPath);

    /** Preflights a Nanite toggle against the exact mesh revision. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Render")
    static FThomasNanitePlanResult PlanNanite(
        const FString& AssetPath,
        const FString& ExpectedRevision,
        bool bEnabled);

    /** Rebuilds after a planned Nanite change; saving remains explicit. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Render")
    static FThomasNaniteApplyResult ApplyNanitePlan(
        const FString& PlanId,
        bool bSave = false);
};
