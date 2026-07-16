#include "Blueprint/ThomasEditorBlueprintService.h"

#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
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
    Result->SetStringField(TEXT("version"), TEXT("0.1.0"));
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

    auto Rollback = [&]()
    {
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
        }
        if (PreviousParentClass && Blueprint->ParentClass != PreviousParentClass)
        {
            UBlueprintEditorLibrary::ReparentBlueprint(Blueprint, PreviousParentClass);
        }
        Compile(Blueprint);
        if (bCompileAndSave)
        {
            Save(Blueprint);
        }
        else
        {
            Blueprint->GetOutermost()->SetDirtyFlag(false);
        }
        Transaction.Cancel();
    };

    if (bRequestsParentChange)
    {
        UBlueprintEditorLibrary::ReparentBlueprint(Blueprint, Parent);
        if (Blueprint->Status == BS_Error)
        {
            Rollback();
            return Error(TEXT("compile_failed_after_reparent"), TEXT("Blueprint compilation failed."));
        }
        Changes.Add(MakeShared<FJsonValueString>(TEXT("parent")));
    }

    if (ResolvedReferences.Num() > 0)
    {
        UObject* Defaults = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
        if (!Defaults)
        {
            Rollback();
            return Error(TEXT("missing_generated_class"), BlueprintPath);
        }
        Defaults->Modify();

        for (const auto& Pair : ResolvedReferences)
        {
            FClassProperty* Property = FindFProperty<FClassProperty>(Blueprint->GeneratedClass, *Pair.Key);
            if (!Property || (Property->MetaClass && !Pair.Value->IsChildOf(Property->MetaClass)))
            {
                Rollback();
                return Error(TEXT("invalid_class_property"), Pair.Key);
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
        Rollback();
        return Error(TEXT("compile_failed"), TEXT("Changes were rolled back; inspect RecentMessages for details."));
    }

    bool bSaved = false;
    if (bCompileAndSave && Changes.Num() > 0)
    {
        bSaved = Save(Blueprint);
        if (!bSaved)
        {
            Rollback();
            return Error(TEXT("save_failed"), TEXT("Changes were rolled back; inspect RecentMessages for details."));
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
    const FString SkeletonPackagePath = SkeletonPath.TrimStartAndEnd();
    const FString ExpectedPrefix = TEXT("/Game/PropHunt/Characters/");
    const bool bAllowedPrototypeFolder =
        MeshObjectPath.Contains(TEXT("/Prototype/Mixamo/"))
        || MeshObjectPath.Contains(TEXT("/Prototype/PropnightReference/"));
    const bool bAllowedSkeletonFolder =
        SkeletonPackagePath.Contains(TEXT("/Prototype/Mixamo/"))
        || SkeletonPackagePath.Contains(TEXT("/Prototype/PropnightReference/"));
    if (!MeshObjectPath.StartsWith(ExpectedPrefix)
        || !bAllowedPrototypeFolder
        || !SkeletonPackagePath.StartsWith(ExpectedPrefix)
        || !bAllowedSkeletonFolder
        || !FPackageName::IsValidLongPackageName(SkeletonPackagePath))
    {
        return Error(
            TEXT("prototype_path_required"),
            TEXT("Mesh and Skeleton must use an approved PropHunt Characters prototype path."));
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

        ExistingSkeleton->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
        ExistingSkeleton->MarkPackageDirty();
        Mesh->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(ExistingSkeleton);
        if (!SaveAsset(ExistingSkeleton) || !SaveAsset(Mesh))
        {
            return Error(TEXT("skeleton_save_failed"), SkeletonPackagePath);
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
        Mesh->Modify();
        Mesh->SetSkeleton(ExistingAsset);
        Mesh->MarkPackageDirty();
        if (!SaveAsset(Mesh))
        {
            Mesh->SetSkeleton(nullptr);
            return Error(TEXT("mesh_skeleton_assignment_save_failed"), SkeletonPackagePath);
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

    Mesh->Modify();
    Mesh->SetSkeleton(Skeleton);
    Mesh->MarkPackageDirty();
    Skeleton->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(Skeleton);

    if (!SaveAsset(Skeleton) || !SaveAsset(Mesh))
    {
        Mesh->SetSkeleton(nullptr);
        return Error(TEXT("skeleton_save_failed"), SkeletonPackagePath);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("mesh"), Mesh->GetPathName());
    Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
    return CompactJson(Result);
}
