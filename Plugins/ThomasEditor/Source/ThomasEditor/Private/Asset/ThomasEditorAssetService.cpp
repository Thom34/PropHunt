#include "Asset/ThomasEditorAssetService.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/DataAsset.h"
#include "Game/PHMatchRulesDataAsset.h"
#include "Gameplay/Physics/PHPhysicsPropDataAsset.h"
#include "Gameplay/Transformation/PHPropFormDataAsset.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr int32 MaxPropertiesPerCall = 16;

#if WITH_DEV_AUTOMATION_TESTS
bool GForceSaveFailureForTests = false;
#endif

FString CompactJson(const TSharedRef<FJsonObject>& Object)
{
    FString Output;
    const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Object, Writer);
    return Output;
}

FString MakeErrorResponse(const FString& Code, const FString& Message)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), false);
    Result->SetStringField(TEXT("code"), Code);
    Result->SetStringField(TEXT("message"), Message.Left(1200));
    return CompactJson(Result);
}

FString ObjectPath(const FString& Input)
{
    FString Path = Input.TrimStartAndEnd();
    if (!Path.Contains(TEXT(".")))
    {
        Path += TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
    }
    return Path;
}

bool IsProductionObjectPath(const FString& Path)
{
    return Path.StartsWith(TEXT("/Game/PropHunt/")) && !Path.StartsWith(TEXT("/Game/Developers/"));
}

bool ParseStringArray(const FString& Json, TArray<FString>& OutValues)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json.IsEmpty() ? TEXT("[]") : Json);
    if (!FJsonSerializer::Deserialize(Reader, Values) || Values.Num() > MaxPropertiesPerCall)
    {
        return false;
    }
    TSet<FString> Unique;
    for (const TSharedPtr<FJsonValue>& Value : Values)
    {
        FString Text;
        if (!Value.IsValid() || !Value->TryGetString(Text) || Text.IsEmpty() || Unique.Contains(Text))
        {
            return false;
        }
        Unique.Add(Text);
        OutValues.Add(Text);
    }
    return true;
}

const TSet<FName>& PhysicsProperties()
{
    static const TSet<FName> Names = {
        TEXT("PropId"), TEXT("StaticMesh"), TEXT("ImpactSound"), TEXT("FreePhysicalMaterial"),
        TEXT("StraightenPhysicalMaterial"), TEXT("MassOverrideKilograms"), TEXT("ImpactImpulsePerMassThreshold"),
        TEXT("ImpactSoundCooldownSeconds"), TEXT("ImpactSoundInnerRadius"), TEXT("ImpactSoundFalloffDistance"),
        TEXT("bImpactSoundOcclusion"), TEXT("ImpactSoundOcclusionVolumeAttenuation"),
        TEXT("ImpactSoundOcclusionLowPassFilterFrequency"), TEXT("NetworkUpdateFrequency"),
        TEXT("NetworkVisualLocationSmoothingSpeed"), TEXT("NetworkVisualRotationSmoothingSpeed"),
        TEXT("NetworkVisualMaximumExtrapolationSeconds"), TEXT("NetworkVisualTeleportDistance"),
        TEXT("FreeLinearDamping"), TEXT("FreeAngularDamping"), TEXT("bUseExperimentalAngularAssists"),
        TEXT("MovementTorqueDegrees"), TEXT("MovementAngularAccelerationDegrees"),
        TEXT("MinimumMovementRotationSpeedDegrees"), TEXT("EndTipAngularAccelerationDegrees"),
        TEXT("EndBalanceTranslationScale"), TEXT("LyingTumbleAngularAccelerationDegrees"),
        TEXT("MinimumLyingTumbleSpeedDegrees"), TEXT("MaximumHorizontalSpeed"),
        TEXT("MovementVelocityInterpSpeed"), TEXT("MovementStopInterpSpeed"), TEXT("AxialVaultLiftAcceleration"),
        TEXT("AxialEscapeAlignmentThreshold"), TEXT("AxialEscapeStallSpeed"), TEXT("AxialVaultLeverArmScale"),
        TEXT("MaximumAngularVelocityDegrees"), TEXT("MovementHopImpulse"), TEXT("JumpVelocity"),
        TEXT("JumpHorizontalBoostVelocity"), TEXT("MaximumJumpHorizontalSpeed"), TEXT("MaximumJumpCount"),
        TEXT("JumpCooldownSeconds"), TEXT("GroundProbeDistance"), TEXT("StraightenAngularDamping"),
        TEXT("StraightenTargetInterpSpeed"), TEXT("StraightenInterpSpeedAcceleration"),
        TEXT("BlockedRotationMultiplier"), TEXT("bUseContinuousCollisionDetection")
    };
    return Names;
}

const TSet<FName>& PropFormProperties()
{
    static const TSet<FName> Names = {
        TEXT("FormId"), TEXT("StaticMesh"), TEXT("MeshRelativeLocation"), TEXT("MeshRelativeRotation"),
        TEXT("MeshRelativeScale"), TEXT("CapsuleRadius"), TEXT("CapsuleHalfHeight"), TEXT("HitboxShape"),
        TEXT("HitboxRelativeLocation"), TEXT("HitboxRelativeRotation"), TEXT("HitboxBoxHalfExtents"),
        TEXT("HitboxCapsuleRadius"), TEXT("HitboxCapsuleHalfHeight")
    };
    return Names;
}

const TSet<FName>& MatchRulesProperties()
{
    static const TSet<FName> Names = {
        TEXT("MinimumPropPlayers"), TEXT("MaximumPropPlayers"), TEXT("LobbyWaitDuration"),
        TEXT("LobbyReadyCountdownDuration"), TEXT("RosterTravelTimeoutDuration"), TEXT("PreparationDuration"),
        TEXT("HuntDuration"), TEXT("EscapeDuration"), TEXT("AllPropsRetainedEliminationDelay"),
        TEXT("ActiveObjectiveCount"), TEXT("RequiredObjectiveCount"), TEXT("ObjectiveScore"), TEXT("RescueScore"),
        TEXT("HunterDownScore"), TEXT("HunterRetentionScore"), TEXT("HunterEliminationScore")
    };
    return Names;
}

const TSet<FName>* AllowedProperties(const UDataAsset* Asset)
{
    if (Asset && Asset->GetClass() == UPHPhysicsPropDataAsset::StaticClass())
    {
        return &PhysicsProperties();
    }
    if (Asset && Asset->GetClass() == UPHPropFormDataAsset::StaticClass())
    {
        return &PropFormProperties();
    }
    if (Asset && Asset->GetClass() == UPHMatchRulesDataAsset::StaticClass())
    {
        return &MatchRulesProperties();
    }
    return nullptr;
}

bool IsMutableProperty(const UDataAsset* Asset, const FName Name)
{
    const TSet<FName>* Allowed = AllowedProperties(Asset);
    if (!Allowed || !Allowed->Contains(Name))
    {
        return false;
    }
    return Name != TEXT("PropId") && Name != TEXT("FormId");
}

TSharedPtr<FJsonValue> PropertyToJson(const FProperty* Property, const void* Container)
{
    const void* Value = Property->ContainerPtrToValuePtr<void>(Container);
    if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        const int64 EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
        return MakeShared<FJsonValueString>(EnumProperty->GetEnum()->GetNameStringByValue(EnumValue));
    }
    if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property); ByteProperty && ByteProperty->Enum)
    {
        return MakeShared<FJsonValueString>(ByteProperty->Enum->GetNameStringByValue(ByteProperty->GetPropertyValue(Value)));
    }
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
    {
        return MakeShared<FJsonValueNumber>(Numeric->IsFloatingPoint()
            ? Numeric->GetFloatingPointPropertyValue(Value)
            : static_cast<double>(Numeric->GetSignedIntPropertyValue(Value)));
    }
    if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
    {
        return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(Value));
    }
    if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
    {
        return MakeShared<FJsonValueString>(NameProperty->GetPropertyValue(Value).ToString());
    }
    if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
    {
        return MakeShared<FJsonValueString>(StringProperty->GetPropertyValue(Value));
    }
    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        const UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
        return MakeShared<FJsonValueString>(Object ? Object->GetPathName() : TEXT(""));
    }
    if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        if (StructProperty->Struct == TBaseStructure<FVector>::Get())
        {
            const FVector& Vector = *static_cast<const FVector*>(Value);
            Object->SetNumberField(TEXT("x"), Vector.X);
            Object->SetNumberField(TEXT("y"), Vector.Y);
            Object->SetNumberField(TEXT("z"), Vector.Z);
            return MakeShared<FJsonValueObject>(Object);
        }
        if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
        {
            const FRotator& Rotator = *static_cast<const FRotator*>(Value);
            Object->SetNumberField(TEXT("pitch"), Rotator.Pitch);
            Object->SetNumberField(TEXT("yaw"), Rotator.Yaw);
            Object->SetNumberField(TEXT("roll"), Rotator.Roll);
            return MakeShared<FJsonValueObject>(Object);
        }
    }
    return MakeShared<FJsonValueString>(TEXT("<unsupported>"));
}

FString PropertyTypeName(const FProperty* Property)
{
    if (CastField<FEnumProperty>(Property) || (CastField<FByteProperty>(Property) && CastField<FByteProperty>(Property)->Enum))
    {
        return TEXT("enum");
    }
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
    {
        return Numeric->IsInteger() ? TEXT("integer") : TEXT("number");
    }
    if (CastField<FBoolProperty>(Property))
    {
        return TEXT("boolean");
    }
    if (CastField<FNameProperty>(Property))
    {
        return TEXT("name");
    }
    if (CastField<FStrProperty>(Property))
    {
        return TEXT("string");
    }
    if (CastField<FObjectPropertyBase>(Property))
    {
        return TEXT("object_path");
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        if (Struct->Struct == TBaseStructure<FVector>::Get())
        {
            return TEXT("vector");
        }
        if (Struct->Struct == TBaseStructure<FRotator>::Get())
        {
            return TEXT("rotator");
        }
    }
    return TEXT("unsupported");
}

bool ValidateNumericMetadata(const FProperty* Property, const double Number, FString& OutCode, FString& OutMessage)
{
    if (!FMath::IsFinite(Number))
    {
        OutCode = TEXT("nan_or_inf");
        OutMessage = Property->GetName();
        return false;
    }
    if (Property->HasMetaData(TEXT("ClampMin")) && Number < FCString::Atod(*Property->GetMetaData(TEXT("ClampMin"))))
    {
        OutCode = TEXT("out_of_range");
        OutMessage = Property->GetName();
        return false;
    }
    if (Property->HasMetaData(TEXT("ClampMax")) && Number > FCString::Atod(*Property->GetMetaData(TEXT("ClampMax"))))
    {
        OutCode = TEXT("out_of_range");
        OutMessage = Property->GetName();
        return false;
    }
    return true;
}

bool ReadExactVector(const TSharedPtr<FJsonObject>& Object, FVector& OutVector)
{
    return Object.IsValid() && Object->Values.Num() == 3
        && Object->TryGetNumberField(TEXT("x"), OutVector.X)
        && Object->TryGetNumberField(TEXT("y"), OutVector.Y)
        && Object->TryGetNumberField(TEXT("z"), OutVector.Z)
        && !OutVector.ContainsNaN();
}

bool ReadExactRotator(const TSharedPtr<FJsonObject>& Object, FRotator& OutRotator)
{
    return Object.IsValid() && Object->Values.Num() == 3
        && Object->TryGetNumberField(TEXT("pitch"), OutRotator.Pitch)
        && Object->TryGetNumberField(TEXT("yaw"), OutRotator.Yaw)
        && Object->TryGetNumberField(TEXT("roll"), OutRotator.Roll)
        && !OutRotator.ContainsNaN();
}

bool JsonToProperty(
    const TSharedPtr<FJsonValue>& Json,
    FProperty* Property,
    void* Destination,
    FString& OutCode,
    FString& OutMessage)
{
    if (!Json.IsValid())
    {
        OutCode = TEXT("type_mismatch");
        OutMessage = Property->GetName();
        return false;
    }
    if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        FString Name;
        const int64 Value = Json->TryGetString(Name) ? EnumProperty->GetEnum()->GetValueByNameString(Name) : INDEX_NONE;
        if (Value == INDEX_NONE)
        {
            OutCode = TEXT("type_mismatch");
            OutMessage = Property->GetName();
            return false;
        }
        EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(Destination, Value);
        return true;
    }
    if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property); ByteProperty && ByteProperty->Enum)
    {
        FString Name;
        const int64 Value = Json->TryGetString(Name) ? ByteProperty->Enum->GetValueByNameString(Name) : INDEX_NONE;
        if (Value == INDEX_NONE)
        {
            OutCode = TEXT("type_mismatch");
            OutMessage = Property->GetName();
            return false;
        }
        ByteProperty->SetPropertyValue(Destination, static_cast<uint8>(Value));
        return true;
    }
    if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
    {
        double Number = 0.0;
        if (!Json->TryGetNumber(Number) || !ValidateNumericMetadata(Property, Number, OutCode, OutMessage))
        {
            if (OutCode.IsEmpty())
            {
                OutCode = TEXT("type_mismatch");
                OutMessage = Property->GetName();
            }
            return false;
        }
        if (Numeric->IsInteger())
        {
            if (Number != FMath::TruncToDouble(Number))
            {
                OutCode = TEXT("type_mismatch");
                OutMessage = Property->GetName();
                return false;
            }
            Numeric->SetIntPropertyValue(Destination, static_cast<int64>(Number));
        }
        else
        {
            Numeric->SetFloatingPointPropertyValue(Destination, Number);
        }
        return true;
    }
    if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
    {
        if (Json->Type != EJson::Boolean)
        {
            OutCode = TEXT("type_mismatch");
            OutMessage = Property->GetName();
            return false;
        }
        BoolProperty->SetPropertyValue(Destination, Json->AsBool());
        return true;
    }
    if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
    {
        FString Text;
        if (!Json->TryGetString(Text))
        {
            OutCode = TEXT("type_mismatch");
            OutMessage = Property->GetName();
            return false;
        }
        NameProperty->SetPropertyValue(Destination, FName(*Text));
        return true;
    }
    if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
    {
        FString Text;
        if (!Json->TryGetString(Text))
        {
            OutCode = TEXT("type_mismatch");
            OutMessage = Property->GetName();
            return false;
        }
        StringProperty->SetPropertyValue(Destination, Text.Left(1200));
        return true;
    }
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        FString Path;
        if (!Json->TryGetString(Path) || (!Path.IsEmpty() && !IsProductionObjectPath(Path)))
        {
            OutCode = Path.IsEmpty() ? TEXT("type_mismatch") : TEXT("object_path_denied");
            OutMessage = Property->GetName();
            return false;
        }
        UObject* Object = Path.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *ObjectPath(Path));
        if ((!Path.IsEmpty() && !Object) || (Object && !Object->IsA(ObjectProperty->PropertyClass)))
        {
            OutCode = TEXT("type_mismatch");
            OutMessage = Property->GetName();
            return false;
        }
        ObjectProperty->SetObjectPropertyValue(Destination, Object);
        return true;
    }
    if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        if (Json->Type != EJson::Object)
        {
            OutCode = TEXT("type_mismatch");
            OutMessage = Property->GetName();
            return false;
        }
        if (StructProperty->Struct == TBaseStructure<FVector>::Get())
        {
            FVector Value;
            if (!ReadExactVector(Json->AsObject(), Value))
            {
                OutCode = TEXT("type_mismatch");
                OutMessage = Property->GetName();
                return false;
            }
            *static_cast<FVector*>(Destination) = Value;
            return true;
        }
        if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
        {
            FRotator Value;
            if (!ReadExactRotator(Json->AsObject(), Value))
            {
                OutCode = TEXT("type_mismatch");
                OutMessage = Property->GetName();
                return false;
            }
            *static_cast<FRotator*>(Destination) = Value;
            return true;
        }
    }
    OutCode = TEXT("type_mismatch");
    OutMessage = Property->GetName();
    return false;
}

bool ValidateBusinessRules(UDataAsset* Asset, FText& OutError)
{
    if (UPHPhysicsPropDataAsset* Physics = Cast<UPHPhysicsPropDataAsset>(Asset))
    {
        return Physics->HasValidDefinition(&OutError);
    }
    if (UPHPropFormDataAsset* Form = Cast<UPHPropFormDataAsset>(Asset))
    {
        return Form->HasValidDefinition(&OutError);
    }
    if (UPHMatchRulesDataAsset* Rules = Cast<UPHMatchRulesDataAsset>(Asset))
    {
        return Rules->HasValidRules(&OutError);
    }
    return false;
}

bool SaveAsset(UObject* Asset)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (GForceSaveFailureForTests)
    {
        return false;
    }
#endif

    UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
    if (!Package)
    {
        return false;
    }
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    Args.Error = GError;
    return UPackage::SavePackage(Package, Asset, *Filename, Args);
}

struct FPreparedChange
{
    FProperty* Property = nullptr;
    TArray<uint8> NewValue;
    TArray<uint8> PreviousValue;

    ~FPreparedChange()
    {
        if (Property)
        {
            Property->DestroyValue(NewValue.GetData());
            Property->DestroyValue(PreviousValue.GetData());
        }
    }
};
}

#if WITH_DEV_AUTOMATION_TESTS
void FThomasEditorAssetService::SetForceSaveFailureForTests(const bool bForceFailure)
{
    GForceSaveFailureForTests = bForceFailure;
}
#endif

FString FThomasEditorAssetService::BuildSummaryJson(const FString& AssetPath, const FString& PropertyNamesJson)
{
    if (!IsProductionObjectPath(AssetPath))
    {
        return MakeErrorResponse(TEXT("path_denied"), AssetPath);
    }
    TArray<FString> PropertyNames;
    if (!ParseStringArray(PropertyNamesJson, PropertyNames))
    {
        return MakeErrorResponse(TEXT("too_many_properties"), TEXT("Expected at most 16 unique property names."));
    }
    UObject* LoadedObject = LoadObject<UObject>(nullptr, *ObjectPath(AssetPath));
    if (!LoadedObject)
    {
        return MakeErrorResponse(TEXT("asset_not_found"), AssetPath);
    }
    UDataAsset* Asset = Cast<UDataAsset>(LoadedObject);
    if (!Asset)
    {
        return MakeErrorResponse(TEXT("not_data_asset"), LoadedObject->GetClass()->GetPathName());
    }
    const TSet<FName>* Allowed = AllowedProperties(Asset);
    if (!Allowed)
    {
        return MakeErrorResponse(TEXT("data_asset_class_denied"), Asset->GetClass()->GetPathName());
    }

    TArray<TSharedPtr<FJsonValue>> Properties;
    for (const FString& Name : PropertyNames)
    {
        if (!Allowed->Contains(FName(*Name)))
        {
            return MakeErrorResponse(TEXT("unknown_property"), Name);
        }
        FProperty* Property = FindFProperty<FProperty>(Asset->GetClass(), *Name);
        if (!Property)
        {
            return MakeErrorResponse(TEXT("unknown_property"), Name);
        }
        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Name);
        Item->SetStringField(TEXT("type"), PropertyTypeName(Property));
        Item->SetField(TEXT("value"), PropertyToJson(Property, Asset));
        Properties.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
    Result->SetStringField(TEXT("class_path"), Asset->GetClass()->GetPathName());
    Result->SetBoolField(TEXT("dirty"), Asset->GetOutermost()->IsDirty());
    Result->SetArrayField(TEXT("properties"), Properties);
    return CompactJson(Result);
}

FString FThomasEditorAssetService::ApplyPatchJson(
    const FString& AssetPath,
    const FString& ChangesJson,
    const bool bAllowDirty,
    const bool bSave)
{
    if (GEditor && GEditor->PlayWorld)
    {
        return MakeErrorResponse(TEXT("pie_active"), TEXT("Data Asset mutation is disabled while PIE is active."));
    }
    if (!IsProductionObjectPath(AssetPath))
    {
        return MakeErrorResponse(TEXT("path_denied"), AssetPath);
    }

    TArray<TSharedPtr<FJsonValue>> Changes;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ChangesJson.IsEmpty() ? TEXT("[]") : ChangesJson);
    if (!FJsonSerializer::Deserialize(Reader, Changes) || Changes.Num() == 0 || Changes.Num() > MaxPropertiesPerCall)
    {
        return MakeErrorResponse(TEXT("too_many_properties"), TEXT("Expected between 1 and 16 changes."));
    }

    UObject* LoadedObject = LoadObject<UObject>(nullptr, *ObjectPath(AssetPath));
    if (!LoadedObject)
    {
        return MakeErrorResponse(TEXT("asset_not_found"), AssetPath);
    }
    UDataAsset* Asset = Cast<UDataAsset>(LoadedObject);
    if (!Asset)
    {
        return MakeErrorResponse(TEXT("not_data_asset"), LoadedObject->GetClass()->GetPathName());
    }
    const TSet<FName>* Allowed = AllowedProperties(Asset);
    if (!Allowed)
    {
        return MakeErrorResponse(TEXT("data_asset_class_denied"), Asset->GetClass()->GetPathName());
    }
    const bool bWasDirty = Asset->GetOutermost()->IsDirty();
    if (bWasDirty && !bAllowDirty)
    {
        return MakeErrorResponse(TEXT("dirty_asset"), TEXT("Set allow_dirty only after reviewing the existing editor changes."));
    }
    if (bWasDirty && bSave)
    {
        return MakeErrorResponse(TEXT("dirty_save_denied"), TEXT("ThomasEditor never saves pre-existing user changes."));
    }

    TSet<FName> SeenNames;
    TArray<TUniquePtr<FPreparedChange>> Prepared;
    for (const TSharedPtr<FJsonValue>& ChangeValue : Changes)
    {
        const TSharedPtr<FJsonObject> Change = ChangeValue.IsValid() && ChangeValue->Type == EJson::Object
            ? ChangeValue->AsObject() : nullptr;
        FString Name;
        if (!Change.IsValid() || Change->Values.Num() < 2 || Change->Values.Num() > 3
            || !Change->TryGetStringField(TEXT("name"), Name) || !Change->HasField(TEXT("value")))
        {
            return MakeErrorResponse(TEXT("type_mismatch"), TEXT("Each change requires name/value and optional expected."));
        }
        const FName PropertyName(*Name);
        if (SeenNames.Contains(PropertyName))
        {
            return MakeErrorResponse(TEXT("duplicate_property"), Name);
        }
        SeenNames.Add(PropertyName);
        if (!Allowed->Contains(PropertyName))
        {
            return MakeErrorResponse(TEXT("unknown_property"), Name);
        }
        if (!IsMutableProperty(Asset, PropertyName))
        {
            return MakeErrorResponse(TEXT("property_not_mutable"), Name);
        }
        FProperty* Property = FindFProperty<FProperty>(Asset->GetClass(), PropertyName);
        if (!Property)
        {
            return MakeErrorResponse(TEXT("unknown_property"), Name);
        }

        TUniquePtr<FPreparedChange> Item = MakeUnique<FPreparedChange>();
        Item->Property = Property;
        Item->NewValue.SetNumUninitialized(Property->GetSize());
        Item->PreviousValue.SetNumUninitialized(Property->GetSize());
        Property->InitializeValue(Item->NewValue.GetData());
        Property->InitializeValue(Item->PreviousValue.GetData());
        void* CurrentValue = Property->ContainerPtrToValuePtr<void>(Asset);
        Property->CopyCompleteValue(Item->PreviousValue.GetData(), CurrentValue);

        FString Code;
        FString Message;
        if (!JsonToProperty(Change->TryGetField(TEXT("value")), Property, Item->NewValue.GetData(), Code, Message))
        {
            return MakeErrorResponse(Code, Message);
        }
        if (const TSharedPtr<FJsonValue> Expected = Change->TryGetField(TEXT("expected")))
        {
            TArray<uint8> ExpectedValue;
            ExpectedValue.SetNumUninitialized(Property->GetSize());
            Property->InitializeValue(ExpectedValue.GetData());
            const bool bParsed = JsonToProperty(Expected, Property, ExpectedValue.GetData(), Code, Message);
            const bool bIdentical = bParsed && Property->Identical(CurrentValue, ExpectedValue.GetData());
            Property->DestroyValue(ExpectedValue.GetData());
            if (!bParsed)
            {
                return MakeErrorResponse(Code, Message);
            }
            if (!bIdentical)
            {
                return MakeErrorResponse(TEXT("value_mismatch"), Name);
            }
        }
        Prepared.Add(MoveTemp(Item));
    }

    FScopedTransaction Transaction(NSLOCTEXT("ThomasEditor", "DataAssetPatch", "ThomasEditor Data Asset Patch"));
    Asset->Modify();
    for (const TUniquePtr<FPreparedChange>& Item : Prepared)
    {
        Item->Property->CopyCompleteValue(Item->Property->ContainerPtrToValuePtr<void>(Asset), Item->NewValue.GetData());
    }
    Asset->MarkPackageDirty();

    auto Restore = [&]()
    {
        for (const TUniquePtr<FPreparedChange>& Item : Prepared)
        {
            Item->Property->CopyCompleteValue(Item->Property->ContainerPtrToValuePtr<void>(Asset), Item->PreviousValue.GetData());
        }
        Asset->GetOutermost()->SetDirtyFlag(bWasDirty);
        Transaction.Cancel();
    };

    FText ValidationError;
    if (!ValidateBusinessRules(Asset, ValidationError))
    {
        Restore();
        return MakeErrorResponse(TEXT("post_validation_failed"), ValidationError.ToString());
    }

    bool bSaved = false;
    if (bSave)
    {
        bSaved = SaveAsset(Asset);
        if (!bSaved)
        {
            Restore();
            FText RollbackError;
            const bool bRollbackValidated = ValidateBusinessRules(Asset, RollbackError);
            const bool bRollbackSaved = SaveAsset(Asset);
            TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("ok"), false);
            Result->SetStringField(TEXT("code"), bRollbackValidated && bRollbackSaved
                ? TEXT("compile_or_save_failed") : TEXT("rollback_failed"));
            Result->SetStringField(TEXT("message"), TEXT("Save failed; rollback status is reported explicitly."));
            Result->SetBoolField(TEXT("rollback_validated"), bRollbackValidated);
            Result->SetBoolField(TEXT("rollback_saved"), bRollbackSaved);
            return CompactJson(Result);
        }
    }

    TArray<TSharedPtr<FJsonValue>> ChangedNames;
    for (const TUniquePtr<FPreparedChange>& Item : Prepared)
    {
        ChangedNames.Add(MakeShared<FJsonValueString>(Item->Property->GetName()));
    }
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
    Result->SetBoolField(TEXT("dirty"), Asset->GetOutermost()->IsDirty());
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetBoolField(TEXT("validated"), true);
    Result->SetArrayField(TEXT("changed"), ChangedNames);
    return CompactJson(Result);
}
