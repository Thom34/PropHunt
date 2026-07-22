#pragma once

#include "ThomasEditorCoreTypes.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ThomasBlueprintToolset.generated.h"

/** Transactional Blueprint and Widget Blueprint authoring facade. */
UCLASS(BlueprintType)
class THOMASEDITORCORE_API UThomasBlueprintToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Inspect parent, interfaces, variables, components, graph nodes and Widget Tree. Read-only. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Blueprint")
    static FThomasBlueprintInspectionResult InspectBlueprint(
        const FString& AssetPath,
        bool bIncludeNodes = true,
        int32 MaxNodes = 200);

    /** Validate and freeze a bounded batch without mutating the asset. Plans expire after five minutes. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Blueprint")
    static FThomasBlueprintPlanResult PlanBlueprintBatch(
        const FThomasBlueprintBatchRequest& Request);

    /** Apply a previously validated plan atomically; compile and save are explicit. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Blueprint")
    static FThomasBlueprintApplyResult ApplyBlueprintPlan(
        const FString& PlanId,
        bool bCompile = true,
        bool bSave = false);

    /** Validate compile state and optionally require named widgets. Read-only. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Blueprint")
    static FThomasBlueprintValidationResult ValidateBlueprint(
        const FString& AssetPath,
        const TArray<FString>& RequiredWidgetNames);
};
