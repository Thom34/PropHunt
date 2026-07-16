#include "Bridge/ThomasEditorBridge.h"

#include "Blueprint/ThomasEditorBlueprintService.h"
#include "Infrastructure/ThomasEditorModule.h"

namespace
{
FString Unauthorized()
{
    return FThomasEditorBlueprintService::Error(TEXT("unauthorized"), TEXT("Invalid ThomasEditor session token."));
}
}

FString UThomasEditorBridge::EditorStatus(const FString& SecurityToken)
{
    return FThomasEditorModule::Get().IsAuthorized(SecurityToken)
        ? FThomasEditorBlueprintService::BuildStatusJson()
        : Unauthorized();
}

FString UThomasEditorBridge::BlueprintSummary(
    const FString& BlueprintPath,
    const FString& PropertyNamesJson,
    bool bIncludeComponents,
    const FString& SecurityToken)
{
    return FThomasEditorModule::Get().IsAuthorized(SecurityToken)
        ? FThomasEditorBlueprintService::BuildSummaryJson(BlueprintPath, PropertyNamesJson, bIncludeComponents)
        : Unauthorized();
}

FString UThomasEditorBridge::BlueprintPatch(
    const FString& BlueprintPath,
    const FString& NewParentClass,
    const FString& ClassReferencesJson,
    bool bAllowDestructiveReparent,
    bool bCompileAndSave,
    const FString& SecurityToken)
{
    return FThomasEditorModule::Get().IsAuthorized(SecurityToken)
        ? FThomasEditorBlueprintService::ApplyPatchJson(
            BlueprintPath,
            NewParentClass,
            ClassReferencesJson,
            bAllowDestructiveReparent,
            bCompileAndSave)
        : Unauthorized();
}

FString UThomasEditorBridge::RecentMessages(int32 Limit, const FString& Filter, const FString& SecurityToken)
{
    return FThomasEditorModule::Get().IsAuthorized(SecurityToken)
        ? FThomasEditorModule::Get().GetRecentMessagesJson(Limit, Filter)
        : Unauthorized();
}

FString UThomasEditorBridge::CreatePrototypeSkeleton(
    const FString& SkeletalMeshPath,
    const FString& SkeletonPath,
    const FString& SecurityToken)
{
    return FThomasEditorModule::Get().IsAuthorized(SecurityToken)
        ? FThomasEditorBlueprintService::CreatePrototypeSkeletonJson(
            SkeletalMeshPath, SkeletonPath)
        : Unauthorized();
}
