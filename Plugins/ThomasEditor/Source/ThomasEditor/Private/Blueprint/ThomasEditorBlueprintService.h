#pragma once

#include "CoreMinimal.h"

class FThomasEditorBlueprintService
{
public:
    static FString BuildStatusJson();
    static FString BuildSummaryJson(const FString& BlueprintPath, const FString& PropertyNamesJson, bool bIncludeComponents);
    static FString ApplyPatchJson(
        const FString& BlueprintPath,
        const FString& NewParentClass,
        const FString& ClassReferencesJson,
        bool bAllowDestructiveReparent,
        bool bCompileAndSave);
    static FString CreatePrototypeSkeletonJson(
        const FString& SkeletalMeshPath,
        const FString& SkeletonPath);

    static FString Error(const FString& Code, const FString& Message);
};
