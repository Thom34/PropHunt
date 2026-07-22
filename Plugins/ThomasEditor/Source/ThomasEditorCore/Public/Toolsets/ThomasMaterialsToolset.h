#pragma once

#include "ThomasEditorCoreTypes.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ThomasMaterialsToolset.generated.h"

UCLASS(BlueprintType)
class THOMASEDITORCORE_API UThomasMaterialsToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Reads a bounded native material expression graph without opening its editor. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Materials")
    static FThomasMaterialInspectionResult InspectMaterial(
        const FString& AssetPath,
        int32 MaxExpressions = 200);

    /** Plans creation/property/connection/deletion operations against one exact material revision. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Materials")
    static FThomasMaterialPlanResult PlanMaterialPatch(
        const FThomasMaterialPatchRequest& Request);

    /** Consumes a material plan; shader compile and package save remain explicit. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Materials")
    static FThomasMaterialApplyResult ApplyMaterialPlan(
        const FString& PlanId,
        bool bCompile = true,
        bool bSave = false);
};
