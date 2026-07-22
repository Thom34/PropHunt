#include "Blueprint/ThomasEditorBlueprintService.h"

#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "BlueprintEditorLibrary.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"

namespace
{
FString CompactJson(const TSharedRef<FJsonObject>& Object)
{
    FString Output;
    const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Object, Writer);
    return Output;
}

FString ObjectPath(const FString& Input, bool bGeneratedClass)
{
    FString Path = Input.TrimStartAndEnd();
    if (Path.IsEmpty() || Path.StartsWith(TEXT("/Script/")))
    {
        return Path;
    }
    FString PackagePath = Path;
    FString Name;
    if (!Path.Split(TEXT("."), &PackagePath, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
    {
        Name = FPackageName::GetLongPackageAssetName(PackagePath);
    }
    if (bGeneratedClass && !Name.EndsWith(TEXT("_C")))
    {
        Name += TEXT("_C");
    }
    return PackagePath + TEXT(".") + Name;
}

UBlueprint* LoadBlueprint(const FString& Path)
{
    return LoadObject<UBlueprint>(nullptr, *ObjectPath(Path, false));
}

UClass* ResolveClass(const FString& Input)
{
    FString Path = Input.TrimStartAndEnd();
    if (Path.IsEmpty())
    {
        return nullptr;
    }
    if (Path.StartsWith(TEXT("/Game/")))
    {
        Path = ObjectPath(Path, true);
    }
    if (UClass* Loaded = LoadObject<UClass>(nullptr, *Path))
    {
        return Loaded;
    }
    return nullptr;
}

bool ParseStringArray(const FString& Json, int32 MaxValues, TArray<FString>& OutValues)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json.IsEmpty() ? TEXT("[]") : Json);
    if (!FJsonSerializer::Deserialize(Reader, Values) || Values.Num() > MaxValues)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : Values)
    {
        FString Text;
        if (!Value.IsValid() || !Value->TryGetString(Text))
        {
            return false;
        }
        OutValues.Add(Text);
    }
    return true;
}

bool Compile(UBlueprint* Blueprint)
{
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
    return Blueprint->Status != BS_Error;
}

bool Save(UBlueprint* Blueprint)
{
    UPackage* Package = Blueprint->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    Args.Error = GError;
    return UPackage::SavePackage(Package, Blueprint, *Filename, Args);
}

bool SaveAsset(UObject* Asset)
{
    if (!Asset)
    {
        return false;
    }
    UPackage* Package = Asset->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    Args.Error = GError;
    return UPackage::SavePackage(Package, Asset, *Filename, Args);
}

FString RollbackError(
    const FString& Code,
    const FString& Message,
    const bool bRollbackCompiled,
    const bool bRollbackSaved)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), false);
    Result->SetStringField(TEXT("code"), Code);
    Result->SetStringField(TEXT("message"), Message);
    Result->SetBoolField(TEXT("rollback_compiled"), bRollbackCompiled);
    Result->SetBoolField(TEXT("rollback_saved"), bRollbackSaved);
    return CompactJson(Result);
}
}

FString FThomasEditorBlueprintService::Error(const FString& Code, const FString& Message)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), false);
    Result->SetStringField(TEXT("code"), Code);
    Result->SetStringField(TEXT("message"), Message);
    return CompactJson(Result);
}

FString FThomasEditorBlueprintService::BuildStatusJson()
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("plugin"), TEXT("ThomasEditor"));
    Result->SetStringField(TEXT("version"), TEXT("0.4.0"));
    Result->SetStringField(TEXT("transport"), TEXT("ue58_native_mcp"));
    Result->SetStringField(TEXT("project"), FApp::GetProjectName());
    Result->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    Result->SetBoolField(TEXT("pie"), GEditor && GEditor->PlayWorld != nullptr);
    if (GEditor && GEditor->GetEditorWorldContext().World())
    {
        Result->SetStringField(TEXT("map"), GEditor->GetEditorWorldContext().World()->GetMapName());
    }
    return CompactJson(Result);
}

FString FThomasEditorBlueprintService::BuildSummaryJson(
    const FString& BlueprintPath,
    const FString& PropertyNamesJson,
    bool bIncludeComponents)
{
    UBlueprint* Blueprint = LoadBlueprint(BlueprintPath);
    if (!Blueprint)
    {
        return Error(TEXT("blueprint_not_found"), BlueprintPath);
    }

    TArray<FString> PropertyNames;
    if (!ParseStringArray(PropertyNamesJson, 16, PropertyNames))
    {
        return Error(TEXT("invalid_properties"), TEXT("Expected at most 16 property names."));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("asset"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("parent"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT("None"));
    Result->SetStringField(TEXT("generated_class"), Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetPathName() : TEXT("None"));
    Result->SetBoolField(TEXT("dirty"), Blueprint->GetOutermost()->IsDirty());
    Result->SetBoolField(TEXT("compile_error"), Blueprint->Status == BS_Error);

    if (Blueprint->GeneratedClass && PropertyNames.Num() > 0)
    {
        UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject();
        TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
        for (const FString& Name : PropertyNames)
        {
            FProperty* Property = FindFProperty<FProperty>(Blueprint->GeneratedClass, *Name);
            FString Value = TEXT("<missing>");
            if (Property)
            {
                Value.Reset();
                Property->ExportText_InContainer(0, Value, Defaults, Defaults, Defaults, PPF_None);
            }
            Properties->SetStringField(Name, Value.Left(1000));
        }
        Result->SetObjectField(TEXT("properties"), Properties);
    }

    if (bIncludeComponents && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
    {
        TArray<TSharedPtr<FJsonValue>> Components;
        TInlineComponentArray<UActorComponent*> ActorComponents;
        CastChecked<AActor>(Blueprint->GeneratedClass->GetDefaultObject())->GetComponents(ActorComponents);
        for (int32 Index = 0; Index < ActorComponents.Num() && Index < 32; ++Index)
        {
            TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("name"), ActorComponents[Index]->GetName());
            Item->SetStringField(TEXT("class"), ActorComponents[Index]->GetClass()->GetPathName());
            Components.Add(MakeShared<FJsonValueObject>(Item));
        }
        Result->SetArrayField(TEXT("components"), Components);
        Result->SetBoolField(TEXT("components_truncated"), ActorComponents.Num() > 32);
    }
    return CompactJson(Result);
}

FString FThomasEditorBlueprintService::ApplyPatchJson(
    const FString& BlueprintPath,
    const FString& NewParentClass,
    const FString& ClassReferencesJson,
    bool bAllowDestructiveReparent,
    bool bCompileAndSave)
{
    UBlueprint* Blueprint = LoadBlueprint(BlueprintPath);
    if (!Blueprint)
    {
        return Error(TEXT("blueprint_not_found"), BlueprintPath);
    }

    UClass* Parent = NewParentClass.IsEmpty() ? nullptr : ResolveClass(NewParentClass);
    if (!NewParentClass.IsEmpty() && !Parent)
    {
        return Error(TEXT("parent_not_found"), NewParentClass);
    }
    if (Parent && !FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent))
    {
        return Error(TEXT("invalid_parent"), Parent->GetPathName());
    }
    if (Parent && Blueprint->GeneratedClass && Parent->IsChildOf(Blueprint->GeneratedClass))
    {
        return Error(TEXT("inheritance_cycle"), TEXT("The requested parent is the Blueprint or its descendant."));
    }
    if (Parent && Blueprint->ParentClass != Parent && Blueprint->ParentClass
        && !Parent->IsChildOf(Blueprint->ParentClass) && !bAllowDestructiveReparent)
    {
        return Error(TEXT("destructive_reparent_requires_confirmation"), TEXT("Use bAllowDestructiveReparent only after reviewing lost defaults."));
    }

    TSharedPtr<FJsonObject> References;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ClassReferencesJson.IsEmpty() ? TEXT("{}") : ClassReferencesJson);
    if (!FJsonSerializer::Deserialize(Reader, References) || !References.IsValid() || References->Values.Num() > 16)
    {
        return Error(TEXT("invalid_class_references"), TEXT("Expected an object with at most 16 class references."));
    }
    TMap<FString, UClass*> ResolvedReferences;
    for (const auto& Pair : References->Values)
    {
        const FString PropertyName(Pair.Key.Len(), *Pair.Key);
        FString ClassPath;
        UClass* Resolved = Pair.Value.IsValid() && Pair.Value->TryGetString(ClassPath) ? ResolveClass(ClassPath) : nullptr;
        if (!Resolved)
        {
            return Error(TEXT("class_reference_not_found"), PropertyName);
        }
        ResolvedReferences.Add(PropertyName, Resolved);
    }

    const bool bRequestsParentChange = Parent && Blueprint->ParentClass != Parent;
    const bool bRequestsMutation = bRequestsParentChange || ResolvedReferences.Num() > 0;
    if (bRequestsMutation && Blueprint->GetOutermost()->IsDirty())
    {
        return Error(TEXT("blueprint_dirty"), TEXT("Save or revert the Blueprint before applying a remote patch."));
    }

    UClass* PreviousParentClass = Blueprint->ParentClass;
    const FString PreviousParent = Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT("None");
    if (!bRequestsMutation)
    {
        TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("ok"), true);
        Result->SetStringField(TEXT("asset"), Blueprint->GetPathName());
        Result->SetStringField(TEXT("previous_parent"), PreviousParent);
        Result->SetStringField(TEXT("parent"), PreviousParent);
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetArrayField(TEXT("changes"), TArray<TSharedPtr<FJsonValue>>());
        return CompactJson(Result);
    }

    FScopedTransaction Transaction(NSLOCTEXT("ThomasEditor", "BlueprintPatch", "ThomasEditor Blueprint Patch"));
    Blueprint->Modify();
    TArray<TSharedPtr<FJsonValue>> Changes;
    TMap<FString, UClass*> PreviousReferenceValues;

    auto Rollback = [&]() -> TPair<bool, bool>
    {
        if (PreviousParentClass && Blueprint->ParentClass != PreviousParentClass)
        {
            UBlueprintEditorLibrary::ReparentBlueprint(Blueprint, PreviousParentClass);
        }
        bool bRollbackCompiled = Compile(Blueprint);
        if (Blueprint->GeneratedClass)
        {
            UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject();
            for (const auto& Pair : PreviousReferenceValues)
            {
                if (FClassProperty* Property = FindFProperty<FClassProperty>(Blueprint->GeneratedClass, *Pair.Key))
                {
                    Property->SetPropertyValue_InContainer(Defaults, Pair.Value);
                }
            }
            bRollbackCompiled = Compile(Blueprint) && bRollbackCompiled;
        }
        bool bRollbackSaved = true;
        if (bCompileAndSave)
        {
            bRollbackSaved = bRollbackCompiled && Save(Blueprint);
        }
        else
        {
            Blueprint->GetOutermost()->SetDirtyFlag(false);
        }
        Transaction.Cancel();
        return TPair<bool, bool>(bRollbackCompiled, bRollbackSaved);
    };

    if (bRequestsParentChange)
    {
        UBlueprintEditorLibrary::ReparentBlueprint(Blueprint, Parent);
        if (Blueprint->Status == BS_Error)
        {
            const TPair<bool, bool> RollbackResult = Rollback();
            return RollbackError(TEXT("compile_failed_after_reparent"), TEXT("Blueprint compilation failed; rollback status is explicit."), RollbackResult.Key, RollbackResult.Value);
        }
        Changes.Add(MakeShared<FJsonValueString>(TEXT("parent")));
    }

    if (ResolvedReferences.Num() > 0)
    {
        UObject* Defaults = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
        if (!Defaults)
        {
            const TPair<bool, bool> RollbackResult = Rollback();
            return RollbackError(TEXT("missing_generated_class"), BlueprintPath, RollbackResult.Key, RollbackResult.Value);
        }
        Defaults->Modify();

        for (const auto& Pair : ResolvedReferences)
        {
            FClassProperty* Property = FindFProperty<FClassProperty>(Blueprint->GeneratedClass, *Pair.Key);
            if (!Property || (Property->MetaClass && !Pair.Value->IsChildOf(Property->MetaClass)))
            {
                const TPair<bool, bool> RollbackResult = Rollback();
                return RollbackError(TEXT("invalid_class_property"), Pair.Key, RollbackResult.Key, RollbackResult.Value);
            }
            PreviousReferenceValues.Add(Pair.Key, Cast<UClass>(Property->GetObjectPropertyValue_InContainer(Defaults)));
        }

        for (const auto& Pair : ResolvedReferences)
        {
            FClassProperty* Property = FindFProperty<FClassProperty>(Blueprint->GeneratedClass, *Pair.Key);
            Property->SetPropertyValue_InContainer(Defaults, Pair.Value);
            Changes.Add(MakeShared<FJsonValueString>(Pair.Key));
        }
        Blueprint->MarkPackageDirty();
    }

    if (ResolvedReferences.Num() > 0 && !Compile(Blueprint))
    {
        const TPair<bool, bool> RollbackResult = Rollback();
        return RollbackError(TEXT("compile_failed"), TEXT("Compilation failed; inspect RecentMessages and rollback status."), RollbackResult.Key, RollbackResult.Value);
    }

    bool bSaved = false;
    if (bCompileAndSave && Changes.Num() > 0)
    {
        bSaved = Save(Blueprint);
        if (!bSaved)
        {
            const TPair<bool, bool> RollbackResult = Rollback();
            return RollbackError(TEXT("save_failed"), TEXT("Save failed; inspect RecentMessages and rollback status."), RollbackResult.Key, RollbackResult.Value);
        }
    }
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("asset"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("previous_parent"), PreviousParent);
    Result->SetStringField(TEXT("parent"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT("None"));
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetArrayField(TEXT("changes"), Changes);
    return CompactJson(Result);
}

FString FThomasEditorBlueprintService::CreatePrototypeSkeletonJson(
    const FString& SkeletalMeshPath,
    const FString& SkeletonPath)
{
    const FString MeshObjectPath = ObjectPath(SkeletalMeshPath, false);
    FString SkeletonPackagePath = SkeletonPath.TrimStartAndEnd();
    FString UnusedObjectName;
    SkeletonPackagePath.Split(TEXT("."), &SkeletonPackagePath, &UnusedObjectName);
    const TCHAR* AllowedRoots[] = {
        TEXT("/Game/PropHunt/Characters/Hunter/Prototype/Mixamo/"),
        TEXT("/Game/PropHunt/Characters/Survivor/Prototype/Mixamo/"),
        TEXT("/Game/PropHunt/Characters/Hunter/Prototype/PropnightReference/"),
        TEXT("/Game/PropHunt/Characters/Survivor/Prototype/PropnightReference/") };
    int32 MeshRoot = INDEX_NONE;
    int32 SkeletonRoot = INDEX_NONE;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(AllowedRoots); ++Index)
    {
        MeshRoot = MeshObjectPath.StartsWith(AllowedRoots[Index]) ? Index : MeshRoot;
        SkeletonRoot = SkeletonPackagePath.StartsWith(AllowedRoots[Index]) ? Index : SkeletonRoot;
    }
    if (MeshRoot == INDEX_NONE || SkeletonRoot == INDEX_NONE || MeshRoot != SkeletonRoot
        || !FPackageName::IsValidLongPackageName(SkeletonPackagePath))
    {
        return Error(
            TEXT("prototype_path_required"),
            TEXT("Mesh and Skeleton must use the same approved Hunter or Survivor prototype root."));
    }

    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshObjectPath);
    if (!Mesh)
    {
        return Error(TEXT("skeletal_mesh_not_found"), SkeletalMeshPath);
    }
    if (USkeleton* ExistingSkeleton = Mesh->GetSkeleton())
    {
        if (ExistingSkeleton->GetOutermost()->GetName() != SkeletonPackagePath)
        {
            return Error(
                TEXT("mesh_already_has_other_skeleton"),
                ExistingSkeleton->GetPathName());
        }

        FScopedTransaction Transaction(NSLOCTEXT("ThomasEditor", "PersistPrototypeSkeleton", "Persist Prototype Skeleton"));
        ExistingSkeleton->Modify();
        Mesh->Modify();
        const EObjectFlags PreviousFlags = ExistingSkeleton->GetFlags();
        const bool bSkeletonWasDirty = ExistingSkeleton->GetOutermost()->IsDirty();
        const bool bMeshWasDirty = Mesh->GetOutermost()->IsDirty();
        ExistingSkeleton->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
        ExistingSkeleton->MarkPackageDirty();
        Mesh->MarkPackageDirty();
        const bool bSkeletonSaved = SaveAsset(ExistingSkeleton);
        const bool bMeshSaved = bSkeletonSaved && SaveAsset(Mesh);
        if (!bSkeletonSaved || !bMeshSaved)
        {
            ExistingSkeleton->ClearFlags(RF_Public | RF_Standalone | RF_Transactional);
            ExistingSkeleton->SetFlags(PreviousFlags & (RF_Public | RF_Standalone | RF_Transactional));
            ExistingSkeleton->GetOutermost()->SetDirtyFlag(bSkeletonWasDirty);
            Mesh->GetOutermost()->SetDirtyFlag(bMeshWasDirty);
            const bool bRollbackSaved = !bSkeletonSaved || SaveAsset(ExistingSkeleton);
            Transaction.Cancel();
            return RollbackError(TEXT("skeleton_save_failed"), SkeletonPackagePath, true, bRollbackSaved);
        }

        TSharedRef<FJsonObject> ExistingResult = MakeShared<FJsonObject>();
        ExistingResult->SetBoolField(TEXT("ok"), true);
        ExistingResult->SetStringField(TEXT("mesh"), Mesh->GetPathName());
        ExistingResult->SetStringField(TEXT("skeleton"), ExistingSkeleton->GetPathName());
        ExistingResult->SetBoolField(TEXT("persisted_import_skeleton"), true);
        return CompactJson(ExistingResult);
    }
    if (USkeleton* ExistingAsset = LoadObject<USkeleton>(
        nullptr, *ObjectPath(SkeletonPackagePath, false)))
    {
        FScopedTransaction Transaction(NSLOCTEXT("ThomasEditor", "AssignPrototypeSkeleton", "Assign Prototype Skeleton"));
        const bool bMeshWasDirty = Mesh->GetOutermost()->IsDirty();
        Mesh->Modify();
        Mesh->SetSkeleton(ExistingAsset);
        Mesh->MarkPackageDirty();
        if (!SaveAsset(Mesh))
        {
            Mesh->SetSkeleton(nullptr);
            Mesh->MarkPackageDirty();
            const bool bRollbackSaved = SaveAsset(Mesh);
            Mesh->GetOutermost()->SetDirtyFlag(bMeshWasDirty);
            Transaction.Cancel();
            return RollbackError(TEXT("mesh_skeleton_assignment_save_failed"), SkeletonPackagePath, true, bRollbackSaved);
        }

        TSharedRef<FJsonObject> AssignedResult = MakeShared<FJsonObject>();
        AssignedResult->SetBoolField(TEXT("ok"), true);
        AssignedResult->SetStringField(TEXT("mesh"), Mesh->GetPathName());
        AssignedResult->SetStringField(TEXT("skeleton"), ExistingAsset->GetPathName());
        AssignedResult->SetBoolField(TEXT("assigned_existing_skeleton"), true);
        return CompactJson(AssignedResult);
    }
    if (FPackageName::DoesPackageExist(SkeletonPackagePath))
    {
        return Error(TEXT("skeleton_package_invalid"), SkeletonPackagePath);
    }

    const FString SkeletonName = FPackageName::GetLongPackageAssetName(SkeletonPackagePath);
    UPackage* SkeletonPackage = CreatePackage(*SkeletonPackagePath);
    if (!SkeletonPackage)
    {
        return Error(TEXT("package_creation_failed"), SkeletonPackagePath);
    }

    USkeleton* Skeleton = NewObject<USkeleton>(
        SkeletonPackage,
        *SkeletonName,
        RF_Public | RF_Standalone | RF_Transactional);
    if (!Skeleton || !Skeleton->MergeAllBonesToBoneTree(Mesh, false))
    {
        return Error(TEXT("skeleton_bone_merge_failed"), SkeletalMeshPath);
    }

    FScopedTransaction Transaction(NSLOCTEXT("ThomasEditor", "CreatePrototypeSkeleton", "Create Prototype Skeleton"));
    const bool bMeshWasDirty = Mesh->GetOutermost()->IsDirty();
    Mesh->Modify();
    Mesh->SetSkeleton(Skeleton);
    Mesh->MarkPackageDirty();
    Skeleton->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(Skeleton);

    const bool bSkeletonSaved = SaveAsset(Skeleton);
    const bool bMeshSaved = bSkeletonSaved && SaveAsset(Mesh);
    if (!bSkeletonSaved || !bMeshSaved)
    {
        Mesh->SetSkeleton(nullptr);
        Mesh->MarkPackageDirty();
        const bool bMeshRollbackSaved = SaveAsset(Mesh);
        Mesh->GetOutermost()->SetDirtyFlag(bMeshWasDirty);
        const FString SkeletonFilename = FPackageName::LongPackageNameToFilename(
            SkeletonPackagePath, FPackageName::GetAssetPackageExtension());
        FAssetRegistryModule::AssetDeleted(Skeleton);
        Skeleton->ClearFlags(RF_Public | RF_Standalone);
        Skeleton->MarkAsGarbage();
        SkeletonPackage->SetDirtyFlag(false);
        const bool bSkeletonRemoved = !IFileManager::Get().FileExists(*SkeletonFilename)
            || IFileManager::Get().Delete(*SkeletonFilename, false, true, true);
        Transaction.Cancel();
        return RollbackError(
            TEXT("skeleton_save_failed"), SkeletonPackagePath, true, bMeshRollbackSaved && bSkeletonRemoved);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("mesh"), Mesh->GetPathName());
    Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
    return CompactJson(Result);
}
