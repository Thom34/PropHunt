#pragma once

#include "ThomasEditorCoreTypes.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ThomasDiscoveryToolset.generated.h"

UCLASS(BlueprintType)
class THOMASEDITORCORE_API UThomasDiscoveryToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /**
     * Returns compact project, engine, provider availability and lazy-load state.
     * This call never loads a provider or scans the Asset Registry.
     */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Discovery")
    static FThomasCoreStatus GetStatus();
};
