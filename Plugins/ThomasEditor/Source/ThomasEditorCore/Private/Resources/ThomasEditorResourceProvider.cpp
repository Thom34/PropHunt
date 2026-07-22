#include "Resources/ThomasEditorResourceProvider.h"

#include "Dom/JsonObject.h"
#include "ModelContextProtocolResources.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ThomasEditorCoreModule.h"

namespace
{
const FString StatusUri = TEXT("thomas://editor/status");
const FString PolicyUri = TEXT("thomas://project/policy");
const FString MessagesUri = TEXT("thomas://editor/recent-messages");

TOptional<FString> OptionalText(const TCHAR* Value)
{
    return TOptional<FString>(FString(Value));
}

FString CompactJson(const TSharedRef<FJsonObject>& Object)
{
    FString Output;
    const auto Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Object, Writer);
    return Output;
}

FString BuildStatusJson()
{
    const FThomasCoreStatus Status = IThomasEditorCoreModule::Get().GetStatus();
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("ok"), Status.bOk);
    Root->SetStringField(TEXT("project"), Status.ProjectName);
    Root->SetStringField(TEXT("engine"), Status.EngineVersion);
    Root->SetStringField(TEXT("architecture"), Status.ArchitectureVersion);
    Root->SetBoolField(TEXT("tool_search"), Status.bToolSearchRequired);
    Root->SetNumberField(TEXT("loaded_provider_count"), Status.LoadedProviderCount);

    TArray<TSharedPtr<FJsonValue>> Providers;
    for (const FThomasProviderStatus& Provider : Status.Providers)
    {
        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Provider.Name);
        Item->SetStringField(TEXT("module"), Provider.ModuleName);
        Item->SetBoolField(TEXT("available"), Provider.bAvailable);
        Item->SetBoolField(TEXT("loaded"), Provider.bLoaded);
        Providers.Add(MakeShared<FJsonValueObject>(Item));
    }
    Root->SetArrayField(TEXT("providers"), Providers);
    return CompactJson(Root);
}
}

void FThomasEditorResourceProvider::ListResources(
    FModelContextProtocolResourceDescriptorList& OutResourceDescriptors) const
{
    const TSharedRef<const IModelContextProtocolResourceProvider> ThisProvider =
        ConstCastSharedRef<const IModelContextProtocolResourceProvider>(AsShared());
    OutResourceDescriptors.Add(
        FModelContextProtocolResourceDescriptor(
            StatusUri,
            OptionalText(TEXT("ThomasEditor status")),
            OptionalText(TEXT("ThomasEditor provider status")),
            OptionalText(TEXT("Compact native Core status without loading a domain provider.")),
            OptionalText(TEXT("application/json"))),
        ThisProvider);
    OutResourceDescriptors.Add(
        FModelContextProtocolResourceDescriptor(
            PolicyUri,
            OptionalText(TEXT("PropHunt editor policy")),
            OptionalText(TEXT("ThomasEditor mutation policy")),
            OptionalText(TEXT("Stable project-local safety and authority rules.")),
            OptionalText(TEXT("text/markdown"))),
        ThisProvider);
    OutResourceDescriptors.Add(
        FModelContextProtocolResourceDescriptor(
            MessagesUri,
            OptionalText(TEXT("Recent editor messages")),
            OptionalText(TEXT("Recent Unreal warnings and errors")),
            OptionalText(TEXT("Bounded Core log buffer; read-only.")),
            OptionalText(TEXT("application/json"))),
        ThisProvider);
}

TValueOrError<FModelContextProtocolResource, FString>
FThomasEditorResourceProvider::ReadResource(const FString& Uri) const
{
    if (Uri == StatusUri)
    {
        return MakeValue(FModelContextProtocolResource(
            Uri,
            BuildStatusJson(),
            OptionalText(TEXT("ThomasEditor status")),
            OptionalText(TEXT("ThomasEditor provider status")),
            OptionalText(TEXT("application/json"))));
    }
    if (Uri == PolicyUri)
    {
        FString Policy =
            TEXT("# ThomasEditor policy\n\n")
            TEXT("- Canonical project: PropHunt.\n")
            TEXT("- Canonical engine: K:/UE58 (UE 5.8 source checkout).\n")
            TEXT("- Editor-only: no runtime module, gameplay authority, replication, or production asset.\n")
            TEXT("- Assets are restricted to /Game/PropHunt; /Game/Developers is denied.\n")
            TEXT("- Inspect and preflight before mutation; save is always explicit.\n")
            TEXT("- Structural mutations require expected revision, transaction, validation, and rollback.\n")
            TEXT("- C++ owns authority and validation; Blueprint owns presentation and configuration.\n");
        return MakeValue(FModelContextProtocolResource(
            Uri,
            MoveTemp(Policy),
            OptionalText(TEXT("PropHunt editor policy")),
            OptionalText(TEXT("ThomasEditor mutation policy")),
            OptionalText(TEXT("text/markdown"))));
    }
    if (Uri == MessagesUri)
    {
        return MakeValue(FModelContextProtocolResource(
            Uri,
            IThomasEditorCoreModule::Get().GetRecentMessagesJson(20, TEXT("")),
            OptionalText(TEXT("Recent editor messages")),
            OptionalText(TEXT("Recent Unreal warnings and errors")),
            OptionalText(TEXT("application/json"))));
    }
    return MakeError(FString::Printf(TEXT("ThomasEditor resource not found: %s"), *Uri));
}
