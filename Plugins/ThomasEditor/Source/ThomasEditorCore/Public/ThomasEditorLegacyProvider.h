#pragma once

#include "Modules/ModuleInterface.h"

class THOMASEDITORCORE_API IThomasEditorLegacyProviderModule : public IModuleInterface
{
public:
    virtual FString GetEditorStatusJson() const = 0;

    virtual FString GetBlueprintSummaryJson(
        const FString& BlueprintPath,
        const FString& PropertyNamesJson,
        bool bIncludeComponents) const = 0;

    virtual FString ApplyBlueprintPatchJson(
        const FString& BlueprintPath,
        const FString& NewParentClass,
        const FString& ClassReferencesJson,
        bool bAllowDestructiveReparent,
        bool bCompileAndSave) const = 0;

    virtual FString GetDataAssetSummaryJson(
        const FString& AssetPath,
        const FString& PropertyNamesJson) const = 0;

    virtual FString ApplyDataAssetPatchJson(
        const FString& AssetPath,
        const FString& ChangesJson,
        bool bAllowDirty,
        bool bSave) const = 0;

    virtual FString GetLevelAuditJson(
        const FString& MapPath,
        const FString& ClassFiltersJson,
        const FString& FoldersJson,
        const FString& RequiredFlagsJson,
        bool bIncludeActors,
        int32 MaxResults) const = 0;

#if WITH_DEV_AUTOMATION_TESTS
    /** Execute the provider's destructive rollback/save gates from the eager Core test module. */
    virtual bool RunLegacyMutationGateAutomation() const = 0;
#endif
};
