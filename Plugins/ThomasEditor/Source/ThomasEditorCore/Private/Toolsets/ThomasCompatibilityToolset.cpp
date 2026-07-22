#include "Toolsets/ThomasCompatibilityToolset.h"

#include "Dom/JsonObject.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ThomasEditorCoreModule.h"
#include "ThomasEditorLegacyProvider.h"

namespace
{
IThomasEditorLegacyProviderModule* LoadProvider()
{
    return FModuleManager::LoadModulePtr<IThomasEditorLegacyProviderModule>(
        TEXT("ThomasEditor"));
}

FString ProviderUnavailable()
{
    return TEXT("{\"ok\":false,\"code\":\"provider_unavailable\","
                "\"message\":\"ThomasEditor project provider could not be loaded.\"}");
}

FString StringArrayJson(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> JsonValues;
    for (const FString& Value : Values)
    {
        JsonValues.Add(MakeShared<FJsonValueString>(Value));
    }
    FString Output;
    const auto Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(JsonValues, Writer);
    return Output;
}

FString StringMapJson(const TMap<FString, FString>& Values)
{
    TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
    for (const TPair<FString, FString>& Pair : Values)
    {
        Object->SetStringField(Pair.Key, Pair.Value);
    }
    FString Output;
    const auto Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Object, Writer);
    return Output;
}
}

FString UThomasCompatibilityToolset::EditorStatus()
{
    if (IThomasEditorLegacyProviderModule* Provider = LoadProvider())
    {
        return Provider->GetEditorStatusJson();
    }
    return ProviderUnavailable();
}

FString UThomasCompatibilityToolset::BlueprintSummary(
    const FString& BlueprintPath,
    const TArray<FString>& PropertyNames,
    const bool bIncludeComponents)
{
    if (IThomasEditorLegacyProviderModule* Provider = LoadProvider())
    {
        return Provider->GetBlueprintSummaryJson(
            BlueprintPath, StringArrayJson(PropertyNames), bIncludeComponents);
    }
    return ProviderUnavailable();
}

FString UThomasCompatibilityToolset::BlueprintPatch(
    const FString& BlueprintPath,
    const FString& NewParentClass,
    const TMap<FString, FString>& ClassReferences,
    const bool bAllowDestructiveReparent,
    const bool bCompileAndSave)
{
    if (IThomasEditorLegacyProviderModule* Provider = LoadProvider())
    {
        return Provider->ApplyBlueprintPatchJson(
            BlueprintPath,
            NewParentClass,
            StringMapJson(ClassReferences),
            bAllowDestructiveReparent,
            bCompileAndSave);
    }
    return ProviderUnavailable();
}

FString UThomasCompatibilityToolset::RecentMessages(
    const int32 Limit,
    const FString& Filter)
{
    return IThomasEditorCoreModule::Get().GetRecentMessagesJson(Limit, Filter);
}

FString UThomasCompatibilityToolset::DataAssetSummary(
    const FString& AssetPath,
    const TArray<FString>& PropertyNames)
{
    if (IThomasEditorLegacyProviderModule* Provider = LoadProvider())
    {
        return Provider->GetDataAssetSummaryJson(
            AssetPath, StringArrayJson(PropertyNames));
    }
    return ProviderUnavailable();
}

FString UThomasCompatibilityToolset::DataAssetPatch(
    const FString& AssetPath,
    const FString& ChangesJson,
    const bool bAllowDirty,
    const bool bSave)
{
    if (IThomasEditorLegacyProviderModule* Provider = LoadProvider())
    {
        return Provider->ApplyDataAssetPatchJson(
            AssetPath, ChangesJson, bAllowDirty, bSave);
    }
    return ProviderUnavailable();
}

FString UThomasCompatibilityToolset::LevelAudit(
    const FString& MapPath,
    const TArray<FString>& ClassFilters,
    const TArray<FString>& Folders,
    const TArray<FString>& RequiredFlags,
    const bool bIncludeActors,
    const int32 MaxResults)
{
    if (IThomasEditorLegacyProviderModule* Provider = LoadProvider())
    {
        return Provider->GetLevelAuditJson(
            MapPath,
            StringArrayJson(ClassFilters),
            StringArrayJson(Folders),
            StringArrayJson(RequiredFlags),
            bIncludeActors,
            MaxResults);
    }
    return ProviderUnavailable();
}
