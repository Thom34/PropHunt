#pragma once

#include "ThomasEditorCoreTypes.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ThomasValidationToolset.generated.h"

UCLASS(BlueprintType)
class THOMASEDITORCORE_API UThomasValidationToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Runs UE Data Validation for 1..50 exact project assets. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Validation")
    static FThomasValidationResult ValidateAssets(
        const TArray<FString>& AssetPaths,
        int32 MaxMessages = 100);

    /** Runs native Map Check on the current /Game/PropHunt editor world. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Validation")
    static FThomasValidationResult RunCurrentMapCheck(int32 MaxMessages = 100);
};
