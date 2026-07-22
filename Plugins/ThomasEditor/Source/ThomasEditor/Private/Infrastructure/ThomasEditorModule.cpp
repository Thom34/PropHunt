#include "Infrastructure/ThomasEditorModule.h"

#include "Asset/ThomasEditorAssetService.h"
#include "Blueprint/ThomasEditorBlueprintService.h"
#include "Level/ThomasEditorLevelService.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/ThomasEditorV02ParityTests.h"
#endif

IMPLEMENT_MODULE(FThomasEditorModule, ThomasEditor)

void FThomasEditorModule::StartupModule()
{
}

void FThomasEditorModule::ShutdownModule()
{
}

FString FThomasEditorModule::GetEditorStatusJson() const
{
    return FThomasEditorBlueprintService::BuildStatusJson();
}

FString FThomasEditorModule::GetBlueprintSummaryJson(
    const FString& BlueprintPath,
    const FString& PropertyNamesJson,
    const bool bIncludeComponents) const
{
    return FThomasEditorBlueprintService::BuildSummaryJson(
        BlueprintPath, PropertyNamesJson, bIncludeComponents);
}

FString FThomasEditorModule::ApplyBlueprintPatchJson(
    const FString& BlueprintPath,
    const FString& NewParentClass,
    const FString& ClassReferencesJson,
    const bool bAllowDestructiveReparent,
    const bool bCompileAndSave) const
{
    return FThomasEditorBlueprintService::ApplyPatchJson(
        BlueprintPath,
        NewParentClass,
        ClassReferencesJson,
        bAllowDestructiveReparent,
        bCompileAndSave);
}

FString FThomasEditorModule::GetDataAssetSummaryJson(
    const FString& AssetPath,
    const FString& PropertyNamesJson) const
{
    return FThomasEditorAssetService::BuildSummaryJson(
        AssetPath, PropertyNamesJson);
}

FString FThomasEditorModule::ApplyDataAssetPatchJson(
    const FString& AssetPath,
    const FString& ChangesJson,
    const bool bAllowDirty,
    const bool bSave) const
{
    return FThomasEditorAssetService::ApplyPatchJson(
        AssetPath, ChangesJson, bAllowDirty, bSave);
}

FString FThomasEditorModule::GetLevelAuditJson(
    const FString& MapPath,
    const FString& ClassFiltersJson,
    const FString& FoldersJson,
    const FString& RequiredFlagsJson,
    const bool bIncludeActors,
    const int32 MaxResults) const
{
    return FThomasEditorLevelService::BuildAuditJson(
        MapPath,
        ClassFiltersJson,
        FoldersJson,
        RequiredFlagsJson,
        bIncludeActors,
        MaxResults);
}

#if WITH_DEV_AUTOMATION_TESTS
bool FThomasEditorModule::RunLegacyMutationGateAutomation() const
{
    return RunThomasEditorV02MutationGates();
}
#endif
