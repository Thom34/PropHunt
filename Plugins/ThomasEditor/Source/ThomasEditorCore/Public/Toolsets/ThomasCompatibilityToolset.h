#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ThomasCompatibilityToolset.generated.h"

/**
 * Native ToolsetRegistry adapter preserving the seven v0.2 service contracts during migration.
 * Returned strings contain the compact v0.2 JSON response and retain its stable error codes.
 */
UCLASS(BlueprintType)
class THOMASEDITORCORE_API UThomasCompatibilityToolset final : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Confirm the canonical project, engine, map and PIE state. Read-only. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Compatibility")
    static FString EditorStatus();

    /** Read one Blueprint. Read-only and capped at 16 explicit property names. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Compatibility")
    static FString BlueprintSummary(
        const FString& BlueprintPath,
        const TArray<FString>& PropertyNames,
        bool bIncludeComponents = false);

    /** Guarded v0.2 parent/class-reference mutation. Inspect first and save only when requested. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Compatibility")
    static FString BlueprintPatch(
        const FString& BlueprintPath,
        const FString& NewParentClass,
        const TMap<FString, FString>& ClassReferences,
        bool bAllowDestructiveReparent = false,
        bool bCompileAndSave = false);

    /** Return a bounded buffer of Editor warnings/errors. Read-only. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Compatibility")
    static FString RecentMessages(
        int32 Limit = 20,
        const FString& Filter = TEXT(""));

    /** Read allowlisted properties from a PropHunt Data Asset. Read-only. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Compatibility")
    static FString DataAssetSummary(
        const FString& AssetPath,
        const TArray<FString>& PropertyNames);

    /**
     * Guarded v0.2 Data Asset mutation. ChangesJson is the frozen compatibility format;
     * new domain providers will replace it with typed batch operations.
     */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Compatibility")
    static FString DataAssetPatch(
        const FString& AssetPath,
        const FString& ChangesJson,
        bool bAllowDirty = false,
        bool bSave = false);

    /** Audit an allowlisted PropHunt level without changing the current Editor map. Read-only. */
    UFUNCTION(meta = (AICallable), Category = "ThomasEditor|Compatibility")
    static FString LevelAudit(
        const FString& MapPath,
        const TArray<FString>& ClassFilters,
        const TArray<FString>& Folders,
        const TArray<FString>& RequiredFlags,
        bool bIncludeActors = false,
        int32 MaxResults = 50);
};
