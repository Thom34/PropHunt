#include "Infrastructure/ThomasEditorModule.h"

#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "RCAllowedRemoteFunctionCall.h"
#include "RemoteControlSettings.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

IMPLEMENT_MODULE(FThomasEditorModule, ThomasEditor)

namespace
{
FString CompactJson(const TSharedRef<FJsonObject>& Object)
{
    FString Output;
    const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Object, Writer);
    return Output;
}

const FSoftClassPath BridgeClassPath(TEXT("/Script/ThomasEditor.ThomasEditorBridge"));
}

void FThomasEditorModule::StartupModule()
{
    CreateSessionToken();
    ConfigureRemoteSecurity();
    RegisterRemoteFunctions();
    if (GLog)
    {
        GLog->AddOutputDevice(this);
    }
}

void FThomasEditorModule::ShutdownModule()
{
    if (GLog)
    {
        GLog->RemoveOutputDevice(this);
    }
    UnregisterRemoteFunctions();
    RestoreRemoteSecurity();
    DeleteSessionToken();
}

void FThomasEditorModule::Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
    if (!Message || Verbosity > ELogVerbosity::Warning)
    {
        return;
    }

    FBufferedMessage Entry;
    Entry.Level = Verbosity <= ELogVerbosity::Error ? TEXT("error") : TEXT("warning");
    Entry.Category = Category.ToString();
    Entry.Text = FString(Message).Left(1200);

    FScopeLock Lock(&MessageMutex);
    Messages.Add(MoveTemp(Entry));
    if (Messages.Num() > 200)
    {
        Messages.RemoveAt(0, Messages.Num() - 200, EAllowShrinking::No);
    }
}

FThomasEditorModule& FThomasEditorModule::Get()
{
    return FModuleManager::LoadModuleChecked<FThomasEditorModule>(TEXT("ThomasEditor"));
}

bool FThomasEditorModule::IsAuthorized(const FString& Token) const
{
    return !SessionToken.IsEmpty() && Token == SessionToken;
}

FString FThomasEditorModule::GetRecentMessagesJson(int32 Limit, const FString& Filter) const
{
    TArray<TSharedPtr<FJsonValue>> Items;
    const int32 SafeLimit = FMath::Clamp(Limit, 1, 50);

    FScopeLock Lock(&MessageMutex);
    for (int32 Index = Messages.Num() - 1; Index >= 0 && Items.Num() < SafeLimit; --Index)
    {
        const FBufferedMessage& Entry = Messages[Index];
        if (!Filter.IsEmpty()
            && !Entry.Category.Contains(Filter, ESearchCase::IgnoreCase)
            && !Entry.Text.Contains(Filter, ESearchCase::IgnoreCase))
        {
            continue;
        }

        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("level"), Entry.Level);
        Item->SetStringField(TEXT("category"), Entry.Category);
        Item->SetStringField(TEXT("message"), Entry.Text);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }
    Algo::Reverse(Items);

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetNumberField(TEXT("count"), Items.Num());
    Result->SetArrayField(TEXT("items"), Items);
    return CompactJson(Result);
}

void FThomasEditorModule::RegisterRemoteFunctions()
{
    URemoteControlSettings* Settings = GetMutableDefault<URemoteControlSettings>();
    const TCHAR* FunctionNames[] = { TEXT("EditorStatus"), TEXT("BlueprintSummary"), TEXT("BlueprintPatch"), TEXT("RecentMessages") };
    for (const TCHAR* FunctionName : FunctionNames)
    {
        FRCAllowedRemoteFunctionCall Entry;
        Entry.ClassPath = BridgeClassPath;
        Entry.FunctionName = FunctionName;
        Entry.bAllowChildClasses = false;
        const bool bAlreadyRegistered = Settings->CustomAllowedRemoteFunctionCalls.ContainsByPredicate(
            [&Entry](const FRCAllowedRemoteFunctionCall& Existing)
            {
                return Existing.ClassPath == Entry.ClassPath
                    && Existing.FunctionName.IsSet()
                    && Entry.FunctionName.IsSet()
                    && Existing.FunctionName.GetValue().Equals(Entry.FunctionName.GetValue(), ESearchCase::IgnoreCase)
                    && !Existing.bAllowChildClasses;
            });
        if (!bAlreadyRegistered)
        {
            Settings->CustomAllowedRemoteFunctionCalls.Add(MoveTemp(Entry));
        }
    }
}

void FThomasEditorModule::UnregisterRemoteFunctions()
{
    if (!UObjectInitialized())
    {
        return;
    }
    GetMutableDefault<URemoteControlSettings>()->CustomAllowedRemoteFunctionCalls.RemoveAll(
        [](const FRCAllowedRemoteFunctionCall& Entry) { return Entry.ClassPath == BridgeClassPath; });
}

void FThomasEditorModule::ConfigureRemoteSecurity()
{
    URemoteControlSettings* Settings = GetMutableDefault<URemoteControlSettings>();
    FRemoteSecuritySnapshot Snapshot;
    Snapshot.bAllowAnyRemoteFunctionCall = Settings->bAllowAnyRemoteFunctionCall;
    Snapshot.bRestrictServerAccess = Settings->bRestrictServerAccess;
    Snapshot.bEnableRemotePythonExecution = Settings->bEnableRemotePythonExecution;
    Snapshot.bAllowConsoleCommandRemoteExecution = Settings->bAllowConsoleCommandRemoteExecution;
    Snapshot.bEnforcePassphraseForRemoteClients = Settings->bEnforcePassphraseForRemoteClients;
    Snapshot.AllowedOrigin = Settings->AllowedOrigin;
    Snapshot.AllowlistedClients = Settings->AllowlistedClients;
    RemoteSecuritySnapshot = MoveTemp(Snapshot);

    Settings->bAllowAnyRemoteFunctionCall = false;
    Settings->bRestrictServerAccess = true;
    Settings->bEnableRemotePythonExecution = false;
    Settings->bAllowConsoleCommandRemoteExecution = false;
    Settings->bEnforcePassphraseForRemoteClients = true;
    Settings->AllowedOrigin = TEXT("http://127.0.0.1");
    Settings->AllowlistedClients.Reset();
    Settings->AllowClient(TEXT("127.0.0.1"));
}

void FThomasEditorModule::RestoreRemoteSecurity()
{
    if (!UObjectInitialized() || !RemoteSecuritySnapshot.IsSet())
    {
        return;
    }

    URemoteControlSettings* Settings = GetMutableDefault<URemoteControlSettings>();
    const FRemoteSecuritySnapshot& Snapshot = RemoteSecuritySnapshot.GetValue();
    Settings->bAllowAnyRemoteFunctionCall = Snapshot.bAllowAnyRemoteFunctionCall;
    Settings->bRestrictServerAccess = Snapshot.bRestrictServerAccess;
    Settings->bEnableRemotePythonExecution = Snapshot.bEnableRemotePythonExecution;
    Settings->bAllowConsoleCommandRemoteExecution = Snapshot.bAllowConsoleCommandRemoteExecution;
    Settings->bEnforcePassphraseForRemoteClients = Snapshot.bEnforcePassphraseForRemoteClients;
    Settings->AllowedOrigin = Snapshot.AllowedOrigin;
    Settings->AllowlistedClients = Snapshot.AllowlistedClients;
    RemoteSecuritySnapshot.Reset();
}

void FThomasEditorModule::CreateSessionToken()
{
    SessionToken = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString SessionDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ThomasEditor"));
    IFileManager::Get().MakeDirectory(*SessionDirectory, true);
    SessionTokenPath = FPaths::Combine(SessionDirectory, TEXT("session.token"));
    if (!FFileHelper::SaveStringToFile(SessionToken, *SessionTokenPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        SessionToken.Reset();
        UE_LOG(LogTemp, Error, TEXT("ThomasEditor could not create its session token."));
    }
}

void FThomasEditorModule::DeleteSessionToken()
{
    if (!SessionTokenPath.IsEmpty())
    {
        IFileManager::Get().Delete(*SessionTokenPath, false, true);
    }
    SessionToken.Reset();
}
