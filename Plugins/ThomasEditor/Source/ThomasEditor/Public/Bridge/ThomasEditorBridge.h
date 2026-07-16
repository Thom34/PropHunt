#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ThomasEditorBridge.generated.h"

UCLASS()
class THOMASEDITOR_API UThomasEditorBridge final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "ThomasEditor")
    static FString EditorStatus(const FString& SecurityToken);

    UFUNCTION(BlueprintCallable, Category = "ThomasEditor")
    static FString BlueprintSummary(
        const FString& BlueprintPath,
        const FString& PropertyNamesJson,
        bool bIncludeComponents,
        const FString& SecurityToken);

    UFUNCTION(BlueprintCallable, Category = "ThomasEditor")
    static FString BlueprintPatch(
        const FString& BlueprintPath,
        const FString& NewParentClass,
        const FString& ClassReferencesJson,
        bool bAllowDestructiveReparent,
        bool bCompileAndSave,
        const FString& SecurityToken);

    UFUNCTION(BlueprintCallable, Category = "ThomasEditor")
    static FString RecentMessages(int32 Limit, const FString& Filter, const FString& SecurityToken);

    UFUNCTION(BlueprintCallable, Category = "ThomasEditor")
    static FString CreatePrototypeSkeleton(
        const FString& SkeletalMeshPath,
        const FString& SkeletonPath,
        const FString& SecurityToken);
};
