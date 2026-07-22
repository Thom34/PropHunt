#pragma once

#include "CoreMinimal.h"

class FThomasEditorAssetService
{
public:
    static FString BuildSummaryJson(const FString& AssetPath, const FString& PropertyNamesJson);
    static FString ApplyPatchJson(
        const FString& AssetPath,
        const FString& ChangesJson,
        bool bAllowDirty,
        bool bSave);

#if WITH_DEV_AUTOMATION_TESTS
    static void SetForceSaveFailureForTests(bool bForceFailure);
#endif
};
