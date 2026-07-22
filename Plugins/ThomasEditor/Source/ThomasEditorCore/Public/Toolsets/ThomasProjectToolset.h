#pragma once

#include "ThomasEditorCoreTypes.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ThomasProjectToolset.generated.h"

UCLASS(BlueprintType)
class THOMASEDITORCORE_API UThomasProjectToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Reads the effective descriptor, project override, modules and dependencies for one plugin. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Project")
    static FThomasPluginDetailsResult GetPluginDetails(const FString& PluginName);

    /**
     * Creates an R2 plan to change one project-local plugin reference.
     * Confirmation acknowledges that PropHunt.uproject will be rewritten and an Editor restart is required.
     */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Project")
    static FThomasPluginChangePlanResult PlanPluginEnabled(
        const FString& PluginName,
        bool bEnabled,
        bool bConfirmProjectFileMutation = false);

    /** Applies a fresh project-plugin plan and saves PropHunt.uproject. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Project")
    static FThomasPluginChangeApplyResult ApplyPluginPlan(const FString& PlanId);

#if WITH_DEV_AUTOMATION_TESTS
    static bool ExpirePluginPlanForTests(const FString& PlanId);
#endif
};
