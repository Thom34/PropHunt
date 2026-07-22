#include "ThomasEditorAssetsProvider.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "Misc/Guid.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr int32 MaxInventoryResults = 500;
constexpr int32 MaxDetailProperties = 100;
constexpr int32 MaxReferenceResults = 500;
constexpr int32 MaxPatchOperations = 50;
constexpr int32 MaxSaveItems = 32;
constexpr double PatchPlanLifetimeSeconds = 300.0;

struct FObjectPatchPlan
{
    FThomasObjectPatchRequest Request;
    double CreatedAtSeconds = 0.0;
};

bool IsAllowedPackagePath(const FString& Path)
{
    return (Path == TEXT("/Game/PropHunt") || Path.StartsWith(TEXT("/Game/PropHunt/")))
        && !Path.Contains(TEXT(".."))
        && !Path.StartsWith(TEXT("/Game/Developers/"));
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

FString PackageNameFromAssetPath(const FString& Input)
{
    const FString ObjectPath = NormalizeObjectPath(Input);
    return FPackageName::ObjectPathToPackageName(ObjectPath);
}

template <typename TResult>
TResult MakeError(const FString& Code, const FString& Message)
{
    TResult Result;
    Result.bOk = false;
    Result.Code = Code;
    Result.Message = Message.Left(1200);
    return Result;
}

FString BuildRevision(const UObject* Object)
{
    const UPackage* Package = Object ? Object->GetOutermost() : nullptr;
    if (!Package)
    {
        return FString();
    }

    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    const int64 Size = IFileManager::Get().FileSize(*Filename);
    const int64 Timestamp = IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp();
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d"),
        *Package->GetName(),
        Size,
        Timestamp,
        Package->IsDirty() ? 1 : 0);
}

UObject* LoadProjectAsset(const FString& Input)
{
    const FString ObjectPath = NormalizeObjectPath(Input);
    if (!IsAllowedPackagePath(FPackageName::ObjectPathToPackageName(ObjectPath)))
    {
        return nullptr;
    }

    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
    return Asset.IsValid() ? Asset.GetAsset() : nullptr;
}

bool IsSupportedPatchProperty(const FProperty* Property)
{
    if (!Property
        || !Property->HasAnyPropertyFlags(CPF_Edit)
        || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated))
    {
        return false;
    }

    return CastField<const FBoolProperty>(Property)
        || CastField<const FNumericProperty>(Property)
        || CastField<const FEnumProperty>(Property)
        || CastField<const FNameProperty>(Property)
        || CastField<const FStrProperty>(Property)
        || CastField<const FTextProperty>(Property)
        || CastField<const FObjectPropertyBase>(Property)
        || CastField<const FSoftObjectProperty>(Property)
        || CastField<const FSoftClassProperty>(Property);
}

bool ResolvePropertyPath(
    UObject* Object,
    const FString& PropertyPath,
    FProperty*& OutProperty,
    void*& OutContainer,
    FString& OutError)
{
    OutProperty = nullptr;
    OutContainer = nullptr;
    TArray<FString> Segments;
    PropertyPath.ParseIntoArray(Segments, TEXT("."), true);
    if (!Object || Segments.IsEmpty() || Segments.Num() > 8)
    {
        OutError = TEXT("invalid_property_path");
        return false;
    }

    UStruct* CurrentStruct = Object->GetClass();
    void* CurrentContainer = Object;
    for (int32 Index = 0; Index < Segments.Num(); ++Index)
    {
        if (Segments[Index].IsEmpty()
            || Segments[Index].Contains(TEXT("["))
            || Segments[Index].Contains(TEXT("]")))
        {
            OutError = TEXT("invalid_property_path");
            return false;
        }
        FProperty* Property = FindFProperty<FProperty>(
            CurrentStruct, FName(*Segments[Index]));
        if (!Property)
        {
            OutError = TEXT("property_not_found");
            return false;
        }
        if (Index == Segments.Num() - 1)
        {
            OutProperty = Property;
            OutContainer = CurrentContainer;
            return true;
        }
        FStructProperty* StructProperty = CastField<FStructProperty>(Property);
        if (!StructProperty || !StructProperty->Struct)
        {
            OutError = TEXT("property_path_not_struct");
            return false;
        }
        CurrentContainer = StructProperty->ContainerPtrToValuePtr<void>(CurrentContainer);
        CurrentStruct = StructProperty->Struct;
    }
    OutError = TEXT("invalid_property_path");
    return false;
}

bool ExportPropertyValue(
    const UObject* Object,
    const FProperty* Property,
    const void* Container,
    FString& OutValue)
{
    if (!Object || !Property || !Container)
    {
        return false;
    }
    Property->ExportText_InContainer(
        0,
        OutValue,
        Container,
        Container,
        const_cast<UObject*>(Object),
        PPF_None);
    return true;
}

bool ValidateClampMetadata(
    const FProperty* Property,
    const void* ImportedValue,
    FString& OutError)
{
    const FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
    if (!Numeric)
    {
        return true;
    }

    const double Value = Numeric->IsFloatingPoint()
        ? Numeric->GetFloatingPointPropertyValue(ImportedValue)
        : static_cast<double>(Numeric->GetSignedIntPropertyValue(ImportedValue));

    const FString MinText = Property->GetMetaData(TEXT("ClampMin"));
    const FString MaxText = Property->GetMetaData(TEXT("ClampMax"));
    double Bound = 0.0;
    if (!MinText.IsEmpty() && LexTryParseString(Bound, *MinText) && Value < Bound)
    {
        OutError = FString::Printf(TEXT("Value is below ClampMin %s."), *MinText);
        return false;
    }
    if (!MaxText.IsEmpty() && LexTryParseString(Bound, *MaxText) && Value > Bound)
    {
        OutError = FString::Printf(TEXT("Value is above ClampMax %s."), *MaxText);
        return false;
    }
    return true;
}

bool PreflightOperation(
    UObject* Object,
    const FThomasObjectPatchOperation& Operation,
    FProperty*& OutProperty,
    FString& OutCurrentValue,
    FString& OutError)
{
    void* Container = nullptr;
    if (!ResolvePropertyPath(
            Object, Operation.Name, OutProperty, Container, OutError))
    {
        return false;
    }
    if (!IsSupportedPatchProperty(OutProperty))
    {
        OutError = TEXT("property_not_supported_or_editable");
        return false;
    }

    ExportPropertyValue(Object, OutProperty, Container, OutCurrentValue);
    if (!Operation.ExpectedValue.IsEmpty()
        && Operation.ExpectedValue != OutCurrentValue)
    {
        OutError = TEXT("property_value_conflict");
        return false;
    }

    TArray<uint8> Temporary;
    Temporary.SetNumZeroed(OutProperty->GetSize());
    OutProperty->InitializeValue(Temporary.GetData());
    const TCHAR* ImportEnd = OutProperty->ImportText_Direct(
        *Operation.Value, Temporary.GetData(), Object, PPF_None);
    const bool bImported = ImportEnd != nullptr && *ImportEnd == TEXT('\0');
    const bool bClampValid = bImported
        && ValidateClampMetadata(OutProperty, Temporary.GetData(), OutError);
    OutProperty->DestroyValue(Temporary.GetData());

    if (!bImported)
    {
        OutError = TEXT("invalid_property_value");
        return false;
    }
    return bClampValid;
}

bool PropertyMatchesText(
    UObject* Object,
    const FString& PropertyPath,
    const FString& Text)
{
    FProperty* Property = nullptr;
    void* Container = nullptr;
    FString Error;
    if (!ResolvePropertyPath(
            Object, PropertyPath, Property, Container, Error))
    {
        return false;
    }
    if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
    {
        const FString Normalized = Text.TrimStartAndEnd().ToLower();
        const bool bExpected = Normalized == TEXT("true") || Normalized == TEXT("1");
        if (!bExpected && Normalized != TEXT("false") && Normalized != TEXT("0"))
        {
            return false;
        }
        return BoolProperty->GetPropertyValue_InContainer(Container) == bExpected;
    }

    TArray<uint8> Temporary;
    Temporary.SetNumZeroed(Property->GetSize());
    Property->InitializeValue(Temporary.GetData());
    const TCHAR* ImportEnd = Property->ImportText_Direct(
        *Text, Temporary.GetData(), const_cast<UObject*>(Object), PPF_None);
    FString CurrentValue;
    FString ImportedValue;
    if (ImportEnd && *ImportEnd == TEXT('\0'))
    {
        ExportPropertyValue(Object, Property, Container, CurrentValue);
        Property->ExportText_Direct(
            ImportedValue,
            Temporary.GetData(),
            nullptr,
            Object,
            PPF_None);
    }
    const bool bMatches = ImportEnd
        && *ImportEnd == TEXT('\0')
        && CurrentValue == ImportedValue;
    Property->DestroyValue(Temporary.GetData());
    return bMatches;
}

void SortAndLimit(TArray<FName>& Values, const int32 Limit, bool& bOutTruncated)
{
    Values.Sort(FNameLexicalLess());
    bOutTruncated = Values.Num() > Limit;
    if (bOutTruncated)
    {
        Values.SetNum(Limit, EAllowShrinking::No);
    }
}
}

class FThomasEditorAssetsModule final : public IThomasEditorAssetsProviderModule
{
public:
    virtual FThomasAssetInventoryResult GetAssetInventory(
        const FString& PackageRoot,
        const FString& ClassPath,
        const int32 MaxResults) override
    {
        const FString Root = PackageRoot.IsEmpty()
            ? TEXT("/Game/PropHunt")
            : PackageRoot.TrimStartAndEnd();
        if (!IsAllowedPackagePath(Root))
        {
            return MakeError<FThomasAssetInventoryResult>(
                TEXT("path_denied"), TEXT("Package root must remain under /Game/PropHunt."));
        }
        if (MaxResults < 1 || MaxResults > MaxInventoryResults)
        {
            return MakeError<FThomasAssetInventoryResult>(
                TEXT("invalid_limit"), TEXT("MaxResults must be between 1 and 500."));
        }

        FARFilter Filter;
        Filter.PackagePaths.Add(FName(*Root));
        Filter.bRecursivePaths = true;

        TArray<FAssetData> Assets;
        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        Registry.GetAssets(Filter, Assets);
        Assets.Sort([](const FAssetData& Left, const FAssetData& Right)
        {
            return Left.GetSoftObjectPath().ToString() < Right.GetSoftObjectPath().ToString();
        });

        FThomasAssetInventoryResult Result;
        Result.bOk = true;
        Result.PackageRoot = Root;
        for (const FAssetData& Asset : Assets)
        {
            if (!ClassPath.IsEmpty() && Asset.AssetClassPath.ToString() != ClassPath)
            {
                continue;
            }

            ++Result.MatchedCount;
            if (Result.Items.Num() >= MaxResults)
            {
                continue;
            }

            FThomasAssetRecord Item;
            Item.AssetPath = Asset.GetSoftObjectPath().ToString();
            Item.PackageName = Asset.PackageName.ToString();
            Item.ClassPath = Asset.AssetClassPath.ToString();
            if (UObject* Loaded = FindObject<UObject>(nullptr, *Item.AssetPath))
            {
                Item.bLoaded = true;
                Item.bDirty = Loaded->GetOutermost()->IsDirty();
            }
            Result.Items.Add(MoveTemp(Item));
        }
        Result.bTruncated = Result.MatchedCount > Result.Items.Num();
        return Result;
    }

    virtual FThomasObjectDetailsResult GetObjectDetails(
        const FString& ObjectPath,
        const TArray<FString>& PropertyNames,
        const int32 MaxProperties) override
    {
        const FString Normalized = NormalizeObjectPath(ObjectPath);
        const FString PackageName = FPackageName::ObjectPathToPackageName(Normalized);
        if (!IsAllowedPackagePath(PackageName))
        {
            return MakeError<FThomasObjectDetailsResult>(
                TEXT("path_denied"), TEXT("Object must remain under /Game/PropHunt."));
        }
        if (PropertyNames.Num() > MaxDetailProperties
            || MaxProperties < 1
            || MaxProperties > MaxDetailProperties)
        {
            return MakeError<FThomasObjectDetailsResult>(
                TEXT("invalid_limit"), TEXT("Properties are capped at 100."));
        }

        UObject* Object = LoadProjectAsset(Normalized);
        if (!Object)
        {
            return MakeError<FThomasObjectDetailsResult>(
                TEXT("object_not_found"), Normalized);
        }

        TArray<FString> SelectedPaths;
        TArray<FProperty*> SelectedProperties;
        TArray<void*> SelectedContainers;
        if (PropertyNames.IsEmpty())
        {
            for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
            {
                FProperty* Property = *It;
                if (Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
                {
                    SelectedPaths.Add(Property->GetName());
                    SelectedProperties.Add(Property);
                    SelectedContainers.Add(Object);
                }
            }
            TArray<int32> Order;
            for (int32 Index = 0; Index < SelectedProperties.Num(); ++Index)
            {
                Order.Add(Index);
            }
            Order.Sort([&SelectedPaths](const int32 Left, const int32 Right)
            {
                return SelectedPaths[Left] < SelectedPaths[Right];
            });
            TArray<FString> SortedPaths;
            TArray<FProperty*> SortedProperties;
            TArray<void*> SortedContainers;
            SortedPaths.Reserve(Order.Num());
            SortedProperties.Reserve(Order.Num());
            SortedContainers.Reserve(Order.Num());
            for (const int32 Index : Order)
            {
                SortedPaths.Add(MoveTemp(SelectedPaths[Index]));
                SortedProperties.Add(SelectedProperties[Index]);
                SortedContainers.Add(SelectedContainers[Index]);
            }
            SelectedPaths = MoveTemp(SortedPaths);
            SelectedProperties = MoveTemp(SortedProperties);
            SelectedContainers = MoveTemp(SortedContainers);
        }
        else
        {
            TSet<FString> UniqueNames;
            for (const FString& Name : PropertyNames)
            {
                if (Name.IsEmpty() || UniqueNames.Contains(Name))
                {
                    return MakeError<FThomasObjectDetailsResult>(
                        TEXT("invalid_property_list"), Name);
                }
                UniqueNames.Add(Name);
                FProperty* Property = nullptr;
                void* Container = nullptr;
                FString Error;
                if (!ResolvePropertyPath(
                        Object, Name, Property, Container, Error))
                {
                    return MakeError<FThomasObjectDetailsResult>(
                        Error, Name);
                }
                SelectedPaths.Add(Name);
                SelectedProperties.Add(Property);
                SelectedContainers.Add(Container);
            }
        }

        FThomasObjectDetailsResult Result;
        Result.bOk = true;
        Result.ObjectPath = Object->GetPathName();
        Result.ClassPath = Object->GetClass()->GetPathName();
        Result.Revision = BuildRevision(Object);
        Result.bDirty = Object->GetOutermost()->IsDirty();
        Result.bTruncated = SelectedProperties.Num() > MaxProperties;

        const int32 Count = FMath::Min(MaxProperties, SelectedProperties.Num());
        for (int32 Index = 0; Index < Count; ++Index)
        {
            FProperty* Property = SelectedProperties[Index];
            FThomasPropertyRecord Item;
            Item.Name = SelectedPaths[Index];
            Item.Type = Property->GetCPPType();
            Property->ExportText_InContainer(
                0,
                Item.Value,
                SelectedContainers[Index],
                SelectedContainers[Index],
                Object,
                PPF_None);
            Item.Value = Item.Value.Left(1200);
            Item.Category = Property->GetMetaData(TEXT("Category"));
            Item.Tooltip = Property->GetMetaData(TEXT("ToolTip")).Left(1200);
            Item.ClampMin = Property->GetMetaData(TEXT("ClampMin"));
            Item.ClampMax = Property->GetMetaData(TEXT("ClampMax"));
            Item.bEditable = Property->HasAnyPropertyFlags(CPF_Edit);
            Item.bBlueprintVisible =
                Property->HasAnyPropertyFlags(CPF_BlueprintVisible);
            Result.Properties.Add(MoveTemp(Item));
        }
        return Result;
    }

    virtual FThomasAssetReferencesResult GetAssetReferences(
        const FString& AssetPath,
        const int32 MaxResults) override
    {
        const FString PackageName = PackageNameFromAssetPath(AssetPath);
        if (!IsAllowedPackagePath(PackageName))
        {
            return MakeError<FThomasAssetReferencesResult>(
                TEXT("path_denied"), TEXT("Asset must remain under /Game/PropHunt."));
        }
        if (MaxResults < 1 || MaxResults > MaxReferenceResults)
        {
            return MakeError<FThomasAssetReferencesResult>(
                TEXT("invalid_limit"), TEXT("MaxResults must be between 1 and 500."));
        }

        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<FAssetData> PackageAssets;
        if (!Registry.GetAssetsByPackageName(FName(*PackageName), PackageAssets)
            || PackageAssets.IsEmpty())
        {
            return MakeError<FThomasAssetReferencesResult>(
                TEXT("asset_not_found"), PackageName);
        }

        TArray<FName> Dependencies;
        TArray<FName> Referencers;
        Registry.GetDependencies(FName(*PackageName), Dependencies);
        Registry.GetReferencers(FName(*PackageName), Referencers);

        FThomasAssetReferencesResult Result;
        Result.bOk = true;
        Result.PackageName = PackageName;
        SortAndLimit(Dependencies, MaxResults, Result.bDependenciesTruncated);
        SortAndLimit(Referencers, MaxResults, Result.bReferencersTruncated);
        for (const FName Name : Dependencies)
        {
            Result.Dependencies.Add(Name.ToString());
        }
        for (const FName Name : Referencers)
        {
            Result.Referencers.Add(Name.ToString());
        }
        return Result;
    }

    virtual FThomasObjectPatchPlanResult PlanObjectPatch(
        const FThomasObjectPatchRequest& Request) override
    {
        const FString Normalized = NormalizeObjectPath(Request.ObjectPath);
        const FString PackageName = FPackageName::ObjectPathToPackageName(Normalized);
        if (!IsAllowedPackagePath(PackageName))
        {
            return MakeError<FThomasObjectPatchPlanResult>(
                TEXT("path_denied"), TEXT("Object must remain under /Game/PropHunt."));
        }
        if (Request.Operations.IsEmpty()
            || Request.Operations.Num() > MaxPatchOperations)
        {
            return MakeError<FThomasObjectPatchPlanResult>(
                TEXT("invalid_batch_size"), TEXT("Patch must contain 1 to 50 operations."));
        }

        UObject* Object = LoadProjectAsset(Normalized);
        if (!Object)
        {
            return MakeError<FThomasObjectPatchPlanResult>(
                TEXT("object_not_found"), Normalized);
        }
        UPackage* Package = Object->GetOutermost();
        if (!Package || Package->IsDirty())
        {
            return MakeError<FThomasObjectPatchPlanResult>(
                TEXT("dirty_asset"), TEXT("Save or revert existing human changes first."));
        }

        const FString CurrentRevision = BuildRevision(Object);
        if (Request.ExpectedRevision.IsEmpty()
            || Request.ExpectedRevision != CurrentRevision)
        {
            return MakeError<FThomasObjectPatchPlanResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }

        FThomasObjectPatchPlanResult Result;
        Result.bOk = true;
        Result.ObjectPath = Object->GetPathName();
        Result.ExpectedRevision = CurrentRevision;
        Result.OperationCount = Request.Operations.Num();

        TSet<FString> SeenProperties;
        for (const FThomasObjectPatchOperation& Operation : Request.Operations)
        {
            if (Operation.Name.IsEmpty() || SeenProperties.Contains(Operation.Name))
            {
                return MakeError<FThomasObjectPatchPlanResult>(
                    TEXT("duplicate_or_empty_property"), Operation.Name);
            }
            SeenProperties.Add(Operation.Name);

            FProperty* Property = nullptr;
            FString CurrentValue;
            FString Error;
            if (!PreflightOperation(Object, Operation, Property, CurrentValue, Error))
            {
                return MakeError<FThomasObjectPatchPlanResult>(
                    Error, Operation.Name);
            }
            Result.Preview.Add(FString::Printf(
                TEXT("%s: %s -> %s"),
                *Operation.Name,
                *CurrentValue.Left(300),
                *Operation.Value.Left(300)));
        }

        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        FObjectPatchPlan Plan;
        Plan.Request = Request;
        Plan.Request.ObjectPath = Normalized;
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        Plans.Add(Result.PlanId, MoveTemp(Plan));
        return Result;
    }

    virtual FThomasObjectPatchApplyResult ApplyObjectPatch(
        const FString& PlanId,
        const bool bSave) override
    {
        FObjectPatchPlan Plan;
        if (FObjectPatchPlan* Found = Plans.Find(PlanId))
        {
            Plan = MoveTemp(*Found);
            Plans.Remove(PlanId);
        }
        else
        {
            return MakeError<FThomasObjectPatchApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        if (FPlatformTime::Seconds() - Plan.CreatedAtSeconds > PatchPlanLifetimeSeconds)
        {
            return MakeError<FThomasObjectPatchApplyResult>(
                TEXT("plan_expired"), TEXT("Create a fresh plan."));
        }

        UObject* Object = LoadProjectAsset(Plan.Request.ObjectPath);
        if (!Object)
        {
            return MakeError<FThomasObjectPatchApplyResult>(
                TEXT("object_not_found"), Plan.Request.ObjectPath);
        }
        UPackage* Package = Object->GetOutermost();
        const FString RevisionBefore = BuildRevision(Object);
        if (!Package
            || Package->IsDirty()
            || RevisionBefore != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasObjectPatchApplyResult>(
                TEXT("revision_conflict"), RevisionBefore);
        }

        TArray<FString> PropertyPaths;
        TArray<FString> PreviousValues;
        for (const FThomasObjectPatchOperation& Operation : Plan.Request.Operations)
        {
            FProperty* Property = nullptr;
            FString CurrentValue;
            FString Error;
            if (!PreflightOperation(Object, Operation, Property, CurrentValue, Error))
            {
                return MakeError<FThomasObjectPatchApplyResult>(Error, Operation.Name);
            }
            PropertyPaths.Add(Operation.Name);
            PreviousValues.Add(MoveTemp(CurrentValue));
        }

        FThomasObjectPatchApplyResult Result;
        Result.ObjectPath = Object->GetPathName();
        Result.RevisionBefore = RevisionBefore;

        FScopedTransaction Transaction(
            NSLOCTEXT("ThomasEditor", "ObjectPatch", "ThomasEditor Details Patch"));
        Object->Modify();
        Package->Modify();

        auto Restore = [&]()
        {
            for (int32 Index = 0; Index < PropertyPaths.Num(); ++Index)
            {
                FProperty* Property = nullptr;
                void* Container = nullptr;
                FString Error;
                if (ResolvePropertyPath(
                        Object,
                        PropertyPaths[Index],
                        Property,
                        Container,
                        Error))
                {
                    Property->ImportText_InContainer(
                        *PreviousValues[Index],
                        Container,
                        Object,
                        PPF_None);
                }
            }
            Object->PostEditChange();
            Package->SetDirtyFlag(false);
            Result.bRolledBack = true;
        };

        for (int32 Index = 0; Index < PropertyPaths.Num(); ++Index)
        {
            FProperty* Property = nullptr;
            void* Container = nullptr;
            FString ResolveError;
            const FThomasObjectPatchOperation& Operation = Plan.Request.Operations[Index];
            if (!ResolvePropertyPath(
                    Object,
                    PropertyPaths[Index],
                    Property,
                    Container,
                    ResolveError))
            {
                Restore();
                Transaction.Cancel();
                Result.Code = TEXT("apply_failed_rolled_back");
                Result.Message = ResolveError;
                return Result;
            }
            const TCHAR* ImportEnd = Property->ImportText_InContainer(
                *Operation.Value, Container, Object, PPF_None);
            if (!ImportEnd || *ImportEnd != TEXT('\0'))
            {
                Restore();
                Transaction.Cancel();
                Result.Code = TEXT("apply_failed_rolled_back");
                Result.Message = Operation.Name;
                return Result;
            }
            FPropertyChangedEvent ChangedEvent(Property, EPropertyChangeType::ValueSet);
            Object->PostEditChangeProperty(ChangedEvent);
            ++Result.AppliedOperationCount;
        }
        Package->MarkPackageDirty();

        for (int32 Index = 0; Index < PropertyPaths.Num(); ++Index)
        {
            if (!PropertyMatchesText(
                    Object,
                    PropertyPaths[Index],
                    Plan.Request.Operations[Index].Value))
            {
                Result.Diagnostics.Add(FString::Printf(
                    TEXT("%s did not match the planned value."),
                    *PropertyPaths[Index]));
            }
        }
        if (!Result.Diagnostics.IsEmpty())
        {
            Restore();
            Transaction.Cancel();
            Result.Code = TEXT("post_validation_failed");
            Result.Message = TEXT("The asset was restored after post-apply validation failed.");
            Result.RevisionAfter = BuildRevision(Object);
            return Result;
        }

        if (bSave)
        {
            const FString Filename = FPackageName::LongPackageNameToFilename(
                Package->GetName(), FPackageName::GetAssetPackageExtension());
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            SaveArgs.SaveFlags = SAVE_NoError;
            SaveArgs.Error = GError;
            if (!UPackage::SavePackage(Package, Object, *Filename, SaveArgs))
            {
                Restore();
                Transaction.Cancel();
                Result.Code = TEXT("save_failed_rolled_back");
                Result.Message = TEXT("The in-memory asset was restored; no successful save was reported.");
                Result.RevisionAfter = BuildRevision(Object);
                return Result;
            }
            Result.bSaved = true;
        }

        Result.bOk = true;
        Result.RevisionAfter = BuildRevision(Object);
        return Result;
    }

    virtual FThomasAssetSaveResult SaveAssets(
        const FThomasAssetSaveRequest& Request) override
    {
        if (Request.Items.IsEmpty() || Request.Items.Num() > MaxSaveItems)
        {
            return MakeError<FThomasAssetSaveResult>(
                TEXT("invalid_batch_size"), TEXT("Save must contain 1 to 32 explicit project assets."));
        }

        FThomasAssetSaveResult Result;
        TArray<UPackage*> Packages;
        TSet<UPackage*> SeenPackages;
        for (const FThomasAssetSaveItem& Item : Request.Items)
        {
            const FString ObjectPath = NormalizeObjectPath(Item.AssetPath);
            const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
            if (!IsAllowedPackagePath(PackageName))
            {
                return MakeError<FThomasAssetSaveResult>(
                    TEXT("path_denied"), TEXT("Every saved asset must remain under /Game/PropHunt."));
            }
            UObject* Asset = LoadProjectAsset(ObjectPath);
            if (!Asset)
            {
                return MakeError<FThomasAssetSaveResult>(TEXT("asset_not_found"), ObjectPath);
            }
            UPackage* Package = Asset->GetOutermost();
            if (!Package || SeenPackages.Contains(Package))
            {
                return MakeError<FThomasAssetSaveResult>(
                    TEXT("duplicate_or_invalid_package"), PackageName);
            }

            const FString CurrentRevision = BuildRevision(Asset);
            if (Item.ExpectedRevision.IsEmpty() || Item.ExpectedRevision != CurrentRevision)
            {
                return MakeError<FThomasAssetSaveResult>(TEXT("revision_conflict"), CurrentRevision);
            }
            const FString Filename = FPackageName::LongPackageNameToFilename(
                Package->GetName(),
                Package->ContainsMap()
                    ? FPackageName::GetMapPackageExtension()
                    : FPackageName::GetAssetPackageExtension());
            if (IFileManager::Get().FileExists(*Filename)
                && IFileManager::Get().IsReadOnly(*Filename))
            {
                return MakeError<FThomasAssetSaveResult>(TEXT("read_only_denied"), Filename);
            }

            FThomasAssetSaveRecord& Record = Result.Items.AddDefaulted_GetRef();
            Record.AssetPath = Asset->GetPathName();
            Record.Filename = Filename;
            Record.RevisionBefore = CurrentRevision;
            Record.bWasDirty = Package->IsDirty();
            if (Record.bWasDirty)
            {
                Packages.Add(Package);
            }
            else
            {
                ++Result.AlreadyCleanCount;
            }
            SeenPackages.Add(Package);
        }

        if (!Packages.IsEmpty())
        {
            // Use the Editor-native path in the already-open canonical process. Fully loading first
            // releases partial-load write handles before SavePackages performs its atomic replace.
            UEditorLoadingAndSavingUtils::FullyLoadPackages(Packages);
            const bool bSaveCallSucceeded =
                UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
            if (!bSaveCallSucceeded)
            {
                Result.Code = TEXT("save_failed_external_lock_or_io");
                Result.Message = TEXT(
                    "Save failed in the current Editor. Close any second Unreal/commandlet process "
                    "using this project, then retry SaveAssets with the returned current revision.");
            }
        }

        bool bAllClean = true;
        for (FThomasAssetSaveRecord& Record : Result.Items)
        {
            UObject* Asset = LoadProjectAsset(Record.AssetPath);
            UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
            Record.RevisionAfter = Asset ? BuildRevision(Asset) : TEXT("missing");
            Record.bSaved = Record.bWasDirty && Package && !Package->IsDirty();
            if (Record.bSaved)
            {
                ++Result.SavedCount;
            }
            if (!Package || Package->IsDirty())
            {
                bAllClean = false;
                Result.Diagnostics.Add(FString::Printf(
                    TEXT("Package still dirty after save: %s (file: %s)."),
                    *Record.AssetPath,
                    *Record.Filename));
            }
        }

        if (!bAllClean)
        {
            if (Result.Code.IsEmpty())
            {
                Result.Code = TEXT("save_failed_external_lock_or_io");
                Result.Message = TEXT(
                    "At least one package remains dirty. Retry after removing an external file lock.");
            }
            return Result;
        }

        Result.bOk = true;
        Result.Code = TEXT("ok");
        Result.Message = Packages.IsEmpty()
            ? TEXT("All requested packages were already clean.")
            : TEXT("All requested dirty packages were saved by the current Editor process.");
        return Result;
    }

    virtual FThomasDomainInventoryResult GetDomainInventory(
        const FString& Domain,
        const FString& PackageRoot,
        const TArray<FString>& ClassPaths,
        const int32 MaxResults) override
    {
        const FString Root = PackageRoot.IsEmpty()
            ? TEXT("/Game/PropHunt")
            : PackageRoot.TrimStartAndEnd();
        if (!IsAllowedPackagePath(Root))
        {
            return MakeError<FThomasDomainInventoryResult>(
                TEXT("path_denied"), TEXT("Package root must remain under /Game/PropHunt."));
        }
        if (MaxResults < 1 || MaxResults > MaxInventoryResults
            || ClassPaths.IsEmpty() || ClassPaths.Num() > 32)
        {
            return MakeError<FThomasDomainInventoryResult>(
                TEXT("invalid_limit"),
                TEXT("Use 1..500 results and 1..32 specialized asset classes."));
        }

        TSet<FTopLevelAssetPath> AllowedClassPaths;
        TArray<FTopLevelAssetPath> BaseClassPaths;
        for (const FString& ClassPath : ClassPaths)
        {
            const FTopLevelAssetPath Parsed(ClassPath);
            AllowedClassPaths.Add(Parsed);
            BaseClassPaths.Add(Parsed);
        }

        FARFilter Filter;
        Filter.PackagePaths.Add(FName(*Root));
        Filter.bRecursivePaths = true;
        TArray<FAssetData> Assets;
        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TSet<FTopLevelAssetPath> DerivedClassPaths;
        Registry.GetDerivedClassNames(BaseClassPaths, {}, DerivedClassPaths);
        AllowedClassPaths.Append(DerivedClassPaths);
        Registry.GetAssets(Filter, Assets);
        Assets.Sort([](const FAssetData& Left, const FAssetData& Right)
        {
            return Left.GetSoftObjectPath().ToString() < Right.GetSoftObjectPath().ToString();
        });

        FThomasDomainInventoryResult Result;
        Result.bOk = true;
        Result.Domain = Domain;
        Result.PackageRoot = Root;
        TMap<FString, int32> Counts;
        for (const FAssetData& Asset : Assets)
        {
            const FString AssetClass = Asset.AssetClassPath.ToString();
            if (!AllowedClassPaths.Contains(Asset.AssetClassPath))
            {
                continue;
            }
            ++Result.MatchedCount;
            ++Counts.FindOrAdd(AssetClass);
            if (Result.Items.Num() >= MaxResults)
            {
                continue;
            }

            FThomasAssetRecord Item;
            Item.AssetPath = Asset.GetSoftObjectPath().ToString();
            Item.PackageName = Asset.PackageName.ToString();
            Item.ClassPath = AssetClass;
            if (UObject* Loaded = FindObject<UObject>(nullptr, *Item.AssetPath))
            {
                Item.bLoaded = true;
                Item.bDirty = Loaded->GetOutermost()->IsDirty();
            }
            Result.Items.Add(MoveTemp(Item));
        }
        TArray<FString> CountClasses;
        Counts.GetKeys(CountClasses);
        CountClasses.Sort();
        for (const FString& ClassPath : CountClasses)
        {
            Result.ClassCounts.Add(FString::Printf(
                TEXT("%s=%d"), *ClassPath, Counts[ClassPath]));
        }
        Result.bTruncated = Result.MatchedCount > Result.Items.Num();
        return Result;
    }

private:
    TMap<FString, FObjectPatchPlan> Plans;
};

IMPLEMENT_MODULE(FThomasEditorAssetsModule, ThomasEditorAssets)
