#include "ThomasEditorAssetsProvider.h"
#include "ThomasEditorDomainProvider.h"
#include "ThomasEditorGameplayDataProvider.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EnhancedActionKeyMapping.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr int32 MaxInputOperations = 100;
constexpr double PlanLifetimeSeconds = 300.0;

struct FCachedInputPlan
{
    FThomasEnhancedInputPatchRequest Request;
    double CreatedAtSeconds = 0.0;
};

template <typename TResult>
TResult MakeError(const FString& Code, const FString& Message)
{
    TResult Result;
    Result.Code = Code;
    Result.Message = Message.Left(1200);
    return Result;
}

bool IsAllowedPath(const FString& Path)
{
    return (Path == TEXT("/Game/PropHunt") || Path.StartsWith(TEXT("/Game/PropHunt/")))
        && !Path.Contains(TEXT(".."))
        && !Path.StartsWith(TEXT("/Game/Developers/"));
}

FString NormalizePath(const FString& Input)
{
    FString Path = Input.TrimStartAndEnd();
    if (Path.Contains(TEXT(".")))
    {
        Path = FPackageName::ObjectPathToPackageName(Path);
    }
    return Path;
}

FString ObjectPath(const FString& PackageName)
{
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

UObject* LoadInputAsset(const FString& PackageName)
{
    if (!IsAllowedPath(PackageName))
    {
        return nullptr;
    }
    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath(PackageName)));
    UObject* Object = Asset.IsValid() ? Asset.GetAsset() : nullptr;
    return Object && (Object->IsA<UInputAction>() || Object->IsA<UInputMappingContext>())
        ? Object : nullptr;
}

FString GetAssetKind(const UObject* Asset)
{
    if (Asset && Asset->IsA<UInputAction>()) return TEXT("InputAction");
    if (Asset && Asset->IsA<UInputMappingContext>()) return TEXT("InputMappingContext");
    return FString();
}

FString BuildRevision(const UObject* Asset)
{
    if (!Asset)
    {
        return TEXT("missing");
    }
    const UPackage* Package = Asset->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    const UInputMappingContext* Context = Cast<UInputMappingContext>(Asset);
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d:%d"),
        *Package->GetName(),
        IFileManager::Get().FileSize(*Filename),
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package->IsDirty() ? 1 : 0,
        Context ? Context->GetMappings().Num() : 0);
}

TOptional<EInputActionValueType> ParseValueType(const FString& Input)
{
    if (Input.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase))
        return EInputActionValueType::Boolean;
    if (Input.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase))
        return EInputActionValueType::Axis1D;
    if (Input.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase))
        return EInputActionValueType::Axis2D;
    if (Input.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase))
        return EInputActionValueType::Axis3D;
    return TOptional<EInputActionValueType>();
}

bool HasMapping(
    const UInputMappingContext* Context,
    const UInputAction* Action,
    const FKey Key)
{
    return Context && Context->GetMappings().ContainsByPredicate(
        [Action, Key](const FEnhancedActionKeyMapping& Mapping)
        {
            return Mapping.Action == Action && Mapping.Key == Key;
        });
}

UObject* CreateInputAsset(const FString& PackageName, const FString& Kind)
{
    UClass* Class = Kind.Equals(TEXT("InputAction"), ESearchCase::IgnoreCase)
        ? UInputAction::StaticClass()
        : Kind.Equals(TEXT("InputMappingContext"), ESearchCase::IgnoreCase)
            ? UInputMappingContext::StaticClass()
            : nullptr;
    if (!Class)
    {
        return nullptr;
    }
    UPackage* Package = CreatePackage(*PackageName);
    UObject* Asset = NewObject<UObject>(
        Package,
        Class,
        FName(*FPackageName::GetLongPackageAssetName(PackageName)),
        RF_Public | RF_Standalone | RF_Transactional);
    if (Asset)
    {
        FAssetRegistryModule::AssetCreated(Asset);
        Asset->MarkPackageDirty();
    }
    return Asset;
}

bool SaveAsset(UObject* Asset)
{
    UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
    if (!Package)
    {
        return false;
    }
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    Args.Error = GError;
    return UPackage::SavePackage(Package, Asset, *Filename, Args);
}
}

class FThomasEditorGameplayDataModule final
    : public IThomasEditorGameplayDataProviderModule
{
public:
    virtual void ShutdownModule() override { Plans.Reset(); }

    virtual FThomasDomainInventoryResult GetInventory(
        const FString& PackageRoot,
        const int32 MaxResults) override
    {
        IThomasEditorAssetsProviderModule* Assets =
            FModuleManager::LoadModulePtr<IThomasEditorAssetsProviderModule>(TEXT("ThomasEditorAssets"));
        if (!Assets)
        {
            FThomasDomainInventoryResult Result;
            Result.Code = TEXT("assets_provider_unavailable");
            return Result;
        }
        return Assets->GetDomainInventory(
            TEXT("EnhancedInputGameplayData"),
            PackageRoot,
            {
                TEXT("/Script/EnhancedInput.InputAction"),
                TEXT("/Script/EnhancedInput.InputMappingContext"),
                TEXT("/Script/EnhancedInput.PlayerMappableInputConfig"),
                TEXT("/Script/Engine.PrimaryDataAsset"),
                TEXT("/Script/Engine.DataTable"),
                TEXT("/Script/Engine.CurveTable"),
                TEXT("/Script/Engine.CurveFloat"),
                TEXT("/Script/Engine.CurveVector"),
                TEXT("/Script/DataRegistry.DataRegistry")
            },
            MaxResults);
    }

    virtual FThomasEnhancedInputInspectionResult InspectEnhancedInputAsset(
        const FString& AssetPath) override
    {
        const FString PackageName = NormalizePath(AssetPath);
        if (!IsAllowedPath(PackageName))
        {
            return MakeError<FThomasEnhancedInputInspectionResult>(
                TEXT("path_denied"), TEXT("Enhanced Input asset must remain under /Game/PropHunt."));
        }
        UObject* Asset = LoadInputAsset(PackageName);
        if (!Asset)
        {
            return MakeError<FThomasEnhancedInputInspectionResult>(
                TEXT("input_asset_not_found"), PackageName);
        }
        FThomasEnhancedInputInspectionResult Result;
        Result.bOk = true;
        Result.AssetPath = PackageName;
        Result.AssetKind = GetAssetKind(Asset);
        Result.Revision = BuildRevision(Asset);
        Result.bDirty = Asset->GetOutermost()->IsDirty();
        if (const UInputAction* Action = Cast<UInputAction>(Asset))
        {
            Result.ValueType = StaticEnum<EInputActionValueType>()->GetNameStringByValue(
                static_cast<int64>(Action->ValueType));
        }
        if (const UInputMappingContext* Context = Cast<UInputMappingContext>(Asset))
        {
            Result.MappingCount = Context->GetMappings().Num();
            for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
            {
                FThomasInputMappingRecord Record;
                Record.InputActionPath = Mapping.Action
                    ? Mapping.Action->GetPathName() : FString();
                Record.Key = Mapping.Key.GetFName().ToString();
                Record.bPlayerMappable = Mapping.IsPlayerMappable();
                Record.MappingName = Mapping.GetMappingName().ToString();
                for (const UInputTrigger* Trigger : Mapping.Triggers)
                {
                    if (Trigger) Record.Triggers.Add(Trigger->GetClass()->GetPathName());
                }
                for (const UInputModifier* Modifier : Mapping.Modifiers)
                {
                    if (Modifier) Record.Modifiers.Add(Modifier->GetClass()->GetPathName());
                }
                Result.Mappings.Add(MoveTemp(Record));
            }
        }
        return Result;
    }

    virtual FThomasEnhancedInputPlanResult PlanEnhancedInputPatch(
        const FThomasEnhancedInputPatchRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasEnhancedInputPatchRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        if (!IsAllowedPath(Request.AssetPath))
        {
            return MakeError<FThomasEnhancedInputPlanResult>(
                TEXT("path_denied"), TEXT("Enhanced Input asset must remain under /Game/PropHunt."));
        }
        if (Request.Operations.Num() < 1 || Request.Operations.Num() > MaxInputOperations)
        {
            return MakeError<FThomasEnhancedInputPlanResult>(
                TEXT("invalid_batch_size"), TEXT("Enhanced Input batch must contain 1 to 100 operations."));
        }
        UObject* Existing = LoadInputAsset(Request.AssetPath);
        const FString Revision = BuildRevision(Existing);
        FString Kind = Existing ? GetAssetKind(Existing) : Request.AssetKind;
        if (Existing && Request.bCreateIfMissing)
        {
            return MakeError<FThomasEnhancedInputPlanResult>(TEXT("asset_exists"), Request.AssetPath);
        }
        if (!Existing && (!Request.bCreateIfMissing
            || (!Kind.Equals(TEXT("InputAction"), ESearchCase::IgnoreCase)
                && !Kind.Equals(TEXT("InputMappingContext"), ESearchCase::IgnoreCase))))
        {
            return MakeError<FThomasEnhancedInputPlanResult>(
                TEXT("input_asset_not_found_or_invalid_kind"), Request.AssetPath);
        }
        if (Existing && Existing->GetOutermost()->IsDirty())
        {
            return MakeError<FThomasEnhancedInputPlanResult>(
                TEXT("asset_dirty"), TEXT("Save or revert the input asset before planning mutations."));
        }
        if (Request.ExpectedRevision.IsEmpty() || Request.ExpectedRevision != Revision)
        {
            return MakeError<FThomasEnhancedInputPlanResult>(TEXT("revision_conflict"), Revision);
        }

        bool bDestructive = false;
        for (const FThomasEnhancedInputOperation& Operation : Request.Operations)
        {
            const FString ActionName = Operation.Action.ToLower();
            if (ActionName == TEXT("set_value_type"))
            {
                if (!Kind.Equals(TEXT("InputAction"), ESearchCase::IgnoreCase)
                    || !ParseValueType(Operation.ValueType).IsSet())
                {
                    return MakeError<FThomasEnhancedInputPlanResult>(
                        TEXT("invalid_value_type_operation"), Operation.ValueType);
                }
            }
            else if (ActionName == TEXT("add_mapping") || ActionName == TEXT("remove_mapping"))
            {
                if (!Kind.Equals(TEXT("InputMappingContext"), ESearchCase::IgnoreCase))
                {
                    return MakeError<FThomasEnhancedInputPlanResult>(
                        TEXT("asset_kind_mismatch"), Kind);
                }
                UInputAction* ActionAsset = LoadObject<UInputAction>(
                    nullptr, *Operation.InputActionPath);
                const FKey Key(FName(*Operation.Key));
                if (!ActionAsset || !IsAllowedPath(ActionAsset->GetOutermost()->GetName()) || !Key.IsValid())
                {
                    return MakeError<FThomasEnhancedInputPlanResult>(
                        TEXT("invalid_mapping_endpoint"), Operation.InputActionPath + TEXT("/") + Operation.Key);
                }
                if (const UInputMappingContext* Context = Cast<UInputMappingContext>(Existing))
                {
                    const bool bExists = HasMapping(Context, ActionAsset, Key);
                    if ((ActionName == TEXT("add_mapping") && bExists)
                        || (ActionName == TEXT("remove_mapping") && !bExists))
                    {
                        return MakeError<FThomasEnhancedInputPlanResult>(
                            TEXT("mapping_conflict"), Operation.Key);
                    }
                }
                bDestructive |= ActionName == TEXT("remove_mapping");
            }
            else
            {
                return MakeError<FThomasEnhancedInputPlanResult>(
                    TEXT("unsupported_operation"), Operation.Action);
            }
        }
        if (bDestructive && !Request.bConfirmDestructive)
        {
            return MakeError<FThomasEnhancedInputPlanResult>(
                TEXT("confirmation_required"), TEXT("Mapping removal requires bConfirmDestructive=true."));
        }

        FThomasEnhancedInputPlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.AssetPath = Request.AssetPath;
        Result.ExpectedRevision = Revision;
        Result.AssetKind = Kind;
        Result.Risk = bDestructive ? TEXT("R2") : TEXT("R1");
        Result.OperationCount = Request.Operations.Num();
        Result.Preview.Add(Existing
            ? TEXT("Modify the existing Enhanced Input asset in one transaction.")
            : TEXT("Create a native Enhanced Input asset."));
        FCachedInputPlan& Plan = Plans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasEnhancedInputApplyResult ApplyEnhancedInputPlan(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FCachedInputPlan Plan;
        if (!Plans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasEnhancedInputApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        UObject* Asset = LoadInputAsset(Plan.Request.AssetPath);
        const FString Revision = BuildRevision(Asset);
        if (Revision != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasEnhancedInputApplyResult>(TEXT("revision_conflict"), Revision);
        }
        FThomasEnhancedInputApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.RevisionBefore = Revision;
        if (!Asset)
        {
            Asset = CreateInputAsset(Plan.Request.AssetPath, Plan.Request.AssetKind);
            if (!Asset)
            {
                return MakeError<FThomasEnhancedInputApplyResult>(
                    TEXT("create_failed"), Plan.Request.AssetKind);
            }
            Result.bCreated = true;
        }

        bool bApplied = true;
        {
            const FScopedTransaction Transaction(
                NSLOCTEXT("ThomasEditor", "ApplyEnhancedInput", "ThomasEditor Enhanced Input Patch"));
            Asset->Modify();
            for (const FThomasEnhancedInputOperation& Operation : Plan.Request.Operations)
            {
                const FString ActionName = Operation.Action.ToLower();
                if (ActionName == TEXT("set_value_type"))
                {
                    UInputAction* ActionAsset = Cast<UInputAction>(Asset);
                    const TOptional<EInputActionValueType> Type = ParseValueType(Operation.ValueType);
                    if (!ActionAsset || !Type.IsSet())
                    {
                        bApplied = false;
                    }
                    else
                    {
                        ActionAsset->ValueType = Type.GetValue();
                        ActionAsset->PostEditChange();
                    }
                }
                else
                {
                    UInputMappingContext* Context = Cast<UInputMappingContext>(Asset);
                    UInputAction* ActionAsset = LoadObject<UInputAction>(
                        nullptr, *Operation.InputActionPath);
                    const FKey Key(FName(*Operation.Key));
                    if (!Context || !ActionAsset || !Key.IsValid())
                    {
                        bApplied = false;
                    }
                    else if (ActionName == TEXT("add_mapping"))
                    {
                        if (HasMapping(Context, ActionAsset, Key)) bApplied = false;
                        else Context->MapKey(ActionAsset, Key);
                    }
                    else if (ActionName == TEXT("remove_mapping"))
                    {
                        if (!HasMapping(Context, ActionAsset, Key)) bApplied = false;
                        else Context->UnmapKey(ActionAsset, Key);
                    }
                }
                if (!bApplied) break;
                ++Result.AppliedOperationCount;
            }
            if (bApplied)
            {
                Asset->PostEditChange();
                Asset->MarkPackageDirty();
            }
        }
        if (!bApplied)
        {
            Rollback(Asset, Result);
            Result.Code = TEXT("apply_failed");
            Result.Message = TEXT("Enhanced Input operation failed; rollback was attempted.");
            return Result;
        }
        if (bSave)
        {
            Result.bSaved = SaveAsset(Asset);
            if (!Result.bSaved)
            {
                Rollback(Asset, Result);
                Result.Code = TEXT("save_failed");
                Result.Message = TEXT("Enhanced Input save failed; rollback was attempted.");
                return Result;
            }
        }
        Result.bOk = true;
        Result.RevisionAfter = BuildRevision(Asset);
        return Result;
    }

private:
    void PurgeExpiredPlans()
    {
        const double Now = FPlatformTime::Seconds();
        for (auto It = Plans.CreateIterator(); It; ++It)
        {
            if (Now - It.Value().CreatedAtSeconds > PlanLifetimeSeconds)
                It.RemoveCurrent();
        }
    }

    static void Rollback(UObject* Asset, FThomasEnhancedInputApplyResult& Result)
    {
        if (Result.bCreated)
        {
            Result.bRolledBack = Asset && ObjectTools::DeleteObjectsUnchecked({Asset}) == 1;
            Result.RevisionAfter = TEXT("missing");
        }
        else
        {
            Result.bRolledBack = GEditor && GEditor->UndoTransaction();
            Result.RevisionAfter = BuildRevision(Asset);
        }
    }

    TMap<FString, FCachedInputPlan> Plans;
};

IMPLEMENT_MODULE(FThomasEditorGameplayDataModule, ThomasEditorGameplayData)
