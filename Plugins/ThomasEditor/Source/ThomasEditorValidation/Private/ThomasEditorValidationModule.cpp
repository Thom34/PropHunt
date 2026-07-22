#include "ThomasEditorValidationProvider.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "EditorValidatorSubsystem.h"
#include "Engine/World.h"
#include "IMessageLogListing.h"
#include "Logging/TokenizedMessage.h"
#include "MessageLogModule.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"

namespace
{
constexpr int32 MaxAssetPaths = 50;
constexpr int32 HardMaxMessages = 500;

template <typename TResult>
TResult MakeError(const FString& Code, const FString& Message)
{
    TResult Result;
    Result.bOk = false;
    Result.Code = Code;
    Result.Message = Message.Left(1200);
    return Result;
}

FString NormalizeObjectPath(const FString& Input)
{
    FString Path = Input.TrimStartAndEnd();
    if (!Path.Contains(TEXT(".")))
    {
        Path += TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
    }
    return Path;
}

bool IsAllowedObjectPath(const FString& ObjectPath)
{
    const FString Package = FPackageName::ObjectPathToPackageName(ObjectPath);
    return Package.StartsWith(TEXT("/Game/PropHunt/"))
        && !Package.StartsWith(TEXT("/Game/Developers/"))
        && !Package.Contains(TEXT(".."));
}

FString ResultText(const EDataValidationResult Result)
{
    switch (Result)
    {
    case EDataValidationResult::Valid: return TEXT("valid");
    case EDataValidationResult::Invalid: return TEXT("invalid");
    case EDataValidationResult::NotValidated: return TEXT("not_validated");
    default: return TEXT("unknown");
    }
}

class FMapCheckCapture final : public FOutputDevice
{
public:
    explicit FMapCheckCapture(const int32 InLimit)
        : Limit(InLimit)
    {
    }

    virtual void Serialize(
        const TCHAR* Message,
        const ELogVerbosity::Type Verbosity,
        const FName& Category) override
    {
        if (!Message || Verbosity > ELogVerbosity::Warning)
        {
            return;
        }
        ++Total;
        Errors += Verbosity <= ELogVerbosity::Error ? 1 : 0;
        Warnings += Verbosity == ELogVerbosity::Warning ? 1 : 0;
        if (Messages.Num() >= Limit)
        {
            return;
        }
        FThomasValidationMessage Entry;
        Entry.Severity = Verbosity <= ELogVerbosity::Error ? TEXT("error") : TEXT("warning");
        Entry.ObjectPath = Category.ToString();
        Entry.Message = FString(Message).Left(1200);
        Messages.Add(MoveTemp(Entry));
    }

    int32 Limit = 0;
    int32 Total = 0;
    int32 Errors = 0;
    int32 Warnings = 0;
    TArray<FThomasValidationMessage> Messages;
};
}

class FThomasEditorValidationModule final : public IThomasEditorValidationProviderModule
{
public:
    virtual FThomasValidationResult ValidateAssets(
        const TArray<FString>& AssetPaths,
        const int32 MaxMessages) override
    {
        if (AssetPaths.IsEmpty() || AssetPaths.Num() > MaxAssetPaths)
        {
            return MakeError<FThomasValidationResult>(
                TEXT("invalid_asset_count"), TEXT("Validate 1 to 50 exact assets per call."));
        }
        if (MaxMessages < 1 || MaxMessages > HardMaxMessages)
        {
            return MakeError<FThomasValidationResult>(
                TEXT("invalid_limit"), TEXT("MaxMessages must be between 1 and 500."));
        }

        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<FAssetData> Assets;
        TSet<FString> UniquePaths;
        for (const FString& Input : AssetPaths)
        {
            const FString ObjectPath = NormalizeObjectPath(Input);
            if (!IsAllowedObjectPath(ObjectPath) || UniquePaths.Contains(ObjectPath))
            {
                return MakeError<FThomasValidationResult>(
                    TEXT("invalid_or_duplicate_path"), ObjectPath);
            }
            UniquePaths.Add(ObjectPath);
            const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
            if (!Asset.IsValid())
            {
                return MakeError<FThomasValidationResult>(
                    TEXT("asset_not_found"), ObjectPath);
            }
            Assets.Add(Asset);
        }

        UEditorValidatorSubsystem* Subsystem =
            GEditor ? GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>() : nullptr;
        if (!Subsystem)
        {
            return MakeError<FThomasValidationResult>(
                TEXT("validation_subsystem_unavailable"), TEXT("UEditorValidatorSubsystem is unavailable."));
        }

        FValidateAssetsSettings Settings;
        Settings.bSilent = true;
        Settings.bCollectPerAssetDetails = true;
        Settings.bLoadAssetsForValidation = true;
        Settings.bUnloadAssetsLoadedForValidation = false;
        Settings.bLoadExternalObjectsForValidation = true;
        Settings.MaxAssetsToValidate = AssetPaths.Num();
        Settings.ShowMessageLogSeverity.Reset();

        FValidateAssetsResults NativeResults;
        Subsystem->ValidateAssetsWithSettings(Assets, Settings, NativeResults);

        FThomasValidationResult Result;
        Result.Scope = TEXT("assets");
        Result.RequestedCount = NativeResults.NumRequested;
        Result.CheckedCount = NativeResults.NumChecked;
        Result.ValidCount = NativeResults.NumValid;
        Result.InvalidCount = NativeResults.NumInvalid;
        Result.WarningCount = NativeResults.NumWarnings;
        Result.SkippedCount = NativeResults.NumSkipped + NativeResults.NumUnableToValidate;

        for (const TPair<FString, FValidateAssetsDetails>& Pair : NativeResults.AssetsDetails)
        {
            const FValidateAssetsDetails& Details = Pair.Value;
            for (const FText& Error : Details.ValidationErrors)
            {
                if (Result.Messages.Num() < MaxMessages)
                {
                    FThomasValidationMessage Entry;
                    Entry.Severity = TEXT("error");
                    Entry.ObjectPath = Pair.Key;
                    Entry.Message = Error.ToString().Left(1200);
                    Result.Messages.Add(MoveTemp(Entry));
                }
                else
                {
                    Result.bTruncated = true;
                }
            }
            for (const FText& Warning : Details.ValidationWarnings)
            {
                if (Result.Messages.Num() < MaxMessages)
                {
                    FThomasValidationMessage Entry;
                    Entry.Severity = TEXT("warning");
                    Entry.ObjectPath = Pair.Key;
                    Entry.Message = Warning.ToString().Left(1200);
                    Result.Messages.Add(MoveTemp(Entry));
                }
                else
                {
                    Result.bTruncated = true;
                }
            }
            if (Details.ValidationErrors.IsEmpty()
                && Details.ValidationWarnings.IsEmpty()
                && Result.Messages.Num() < MaxMessages)
            {
                FThomasValidationMessage Entry;
                Entry.Severity = ResultText(Details.Result);
                Entry.ObjectPath = Pair.Key;
                Entry.Message = ResultText(Details.Result);
                Result.Messages.Add(MoveTemp(Entry));
            }
        }
        Result.bOk = Result.InvalidCount == 0;
        Result.Code = Result.bOk ? FString() : TEXT("asset_validation_failed");
        return Result;
    }

    virtual FThomasValidationResult RunCurrentMapCheck(
        const int32 MaxMessages) override
    {
        if (MaxMessages < 1 || MaxMessages > HardMaxMessages)
        {
            return MakeError<FThomasValidationResult>(
                TEXT("invalid_limit"), TEXT("MaxMessages must be between 1 and 500."));
        }
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!World
            || World->WorldType != EWorldType::Editor
            || !World->GetOutermost()->GetName().StartsWith(TEXT("/Game/PropHunt/")))
        {
            return MakeError<FThomasValidationResult>(
                TEXT("project_editor_world_unavailable"),
                TEXT("Open a /Game/PropHunt level in the canonical Editor."));
        }

        IMessageLogListingRef Listing =
            FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"))
                .GetLogListing(TEXT("MapCheck"));
        Listing->ClearMessages();
        FMapCheckCapture Capture(MaxMessages);
        // This is a fixed, dedicated UE command because Map_Check itself is private in UE 5.8.
        // No caller-controlled console text is accepted or forwarded by ThomasEditor.
        const bool bNativeOk = GEditor->Exec(
            World, TEXT("MAP CHECK DONTDISPLAYDIALOG"), Capture);

        int32 NativeMessageCount = 0;
        for (const TSharedRef<FTokenizedMessage>& NativeMessage : Listing->GetFilteredMessages())
        {
            const EMessageSeverity::Type Severity = NativeMessage->GetSeverity();
            const bool bError = static_cast<int32>(Severity)
                <= static_cast<int32>(EMessageSeverity::Error);
            const bool bWarning = Severity == EMessageSeverity::Warning
                || Severity == EMessageSeverity::PerformanceWarning;
            if (!bError && !bWarning)
            {
                continue;
            }
            ++NativeMessageCount;
            Capture.Errors += bError ? 1 : 0;
            Capture.Warnings += bWarning ? 1 : 0;
            if (Capture.Messages.Num() < MaxMessages)
            {
                FThomasValidationMessage Entry;
                Entry.Severity = bError ? TEXT("error") : TEXT("warning");
                Entry.ObjectPath = World->GetOutermost()->GetName();
                Entry.Message = NativeMessage->ToText().ToString().Left(1200);
                Capture.Messages.Add(MoveTemp(Entry));
            }
        }

        FThomasValidationResult Result;
        Result.Scope = World->GetOutermost()->GetName();
        Result.RequestedCount = 1;
        Result.CheckedCount = 1;
        Result.InvalidCount = Capture.Errors;
        Result.WarningCount = Capture.Warnings;
        Result.ValidCount = Capture.Errors == 0 ? 1 : 0;
        Result.Messages = MoveTemp(Capture.Messages);
        Result.bTruncated = NativeMessageCount > Result.Messages.Num();
        Result.bOk = bNativeOk && Capture.Errors == 0;
        Result.Code = Result.bOk ? FString() : TEXT("map_check_failed");
        return Result;
    }
};

IMPLEMENT_MODULE(FThomasEditorValidationModule, ThomasEditorValidation)
