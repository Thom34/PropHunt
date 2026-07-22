#include "ThomasEditorCinematicsProvider.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Channels/MovieSceneEventChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/Factory.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LevelSequence.h"
#include "LevelSequenceDirector.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSection.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneTrack.h"
#include "Channels/MovieSceneStringChannel.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "MovieSceneEventUtils.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneEventSectionBase.h"
#include "Sections/MovieSceneEventTriggerSection.h"
#include "ScopedTransaction.h"
#include "ThomasEditorAssetsProvider.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr int32 MaxOperations = 100;
constexpr int32 MaxInspectionItems = 500;
constexpr double PlanLifetimeSeconds = 300.0;
const TCHAR* LevelSequenceFactoryClassPath =
    TEXT("/Script/LevelSequenceEditor.LevelSequenceFactoryNew");

struct FCachedPlan
{
    FThomasCinematicPatchRequest Request;
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

FString NormalizePath(const FString& Input)
{
    FString Path = Input.TrimStartAndEnd();
    if (Path.Contains(TEXT(".")))
    {
        Path = FPackageName::ObjectPathToPackageName(Path);
    }
    return Path;
}

bool IsAllowedPath(const FString& Path)
{
    return Path.StartsWith(TEXT("/Game/PropHunt/"))
        && !Path.StartsWith(TEXT("/Game/Developers/"))
        && !Path.Contains(TEXT(".."));
}

FString ObjectPath(const FString& PackageName)
{
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

ULevelSequence* LoadSequence(const FString& AssetPath)
{
    const FString PackageName = NormalizePath(AssetPath);
    if (!IsAllowedPath(PackageName))
    {
        return nullptr;
    }
    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData AssetData = Registry.GetAssetByObjectPath(
        FSoftObjectPath(ObjectPath(PackageName)));
    return AssetData.IsValid() ? Cast<ULevelSequence>(AssetData.GetAsset()) : nullptr;
}

FString BuildRevision(const ULevelSequence* Sequence)
{
    if (!Sequence)
    {
        return TEXT("missing");
    }
    const UPackage* Package = Sequence->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    int32 StructuralCount = 0;
    if (const UMovieScene* MovieScene = Sequence->GetMovieScene())
    {
        StructuralCount += MovieScene->GetTracks().Num() + MovieScene->GetBindings().Num();
        for (const UMovieSceneTrack* Track : MovieScene->GetTracks())
        {
            StructuralCount += Track ? Track->GetAllSections().Num() : 0;
        }
        if (const UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack())
        {
            StructuralCount += 1 + CameraCutTrack->GetAllSections().Num();
        }
    }
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d:%d"),
        *Package->GetName(),
        IFileManager::Get().FileSize(*Filename),
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package->IsDirty() ? 1 : 0,
        StructuralCount);
}

UFactory* CreateLevelSequenceFactory(FString& OutError)
{
    if (!FModuleManager::Get().ModuleExists(TEXT("LevelSequenceEditor"))
        || !FModuleManager::Get().LoadModule(TEXT("LevelSequenceEditor")))
    {
        OutError = TEXT("LevelSequenceEditor module is unavailable.");
        return nullptr;
    }
    UClass* FactoryClass = LoadObject<UClass>(nullptr, LevelSequenceFactoryClassPath);
    UFactory* Factory = FactoryClass
        ? NewObject<UFactory>(GetTransientPackage(), FactoryClass)
        : nullptr;
    if (!Factory || !Factory->SupportedClass)
    {
        OutError = TEXT("Native Level Sequence factory is unavailable.");
        return nullptr;
    }
    return Factory;
}

bool SaveSequence(ULevelSequence* Sequence)
{
    UPackage* Package = Sequence ? Sequence->GetOutermost() : nullptr;
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
    return UPackage::SavePackage(Package, Sequence, *Filename, Args);
}

FString BlueprintStatusName(const UBlueprint* Blueprint)
{
    if (!Blueprint)
    {
        return TEXT("missing");
    }
    switch (Blueprint->Status)
    {
    case BS_UpToDate:
        return TEXT("up_to_date");
    case BS_UpToDateWithWarnings:
        return TEXT("up_to_date_with_warnings");
    case BS_Error:
        return TEXT("error");
    case BS_BeingCreated:
        return TEXT("being_created");
    case BS_Dirty:
        return TEXT("dirty");
    default:
        return TEXT("unknown");
    }
}

UBlueprint* GetOrCreateDirectorBlueprint(ULevelSequence* Sequence)
{
    if (!Sequence)
    {
        return nullptr;
    }
    if (UBlueprint* Existing = Sequence->GetDirectorBlueprint())
    {
        return Existing;
    }
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ULevelSequenceDirector::StaticClass(),
        Sequence,
        FName(*Sequence->GetDirectorBlueprintName()),
        BPTYPE_Normal,
        UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass());
    if (Blueprint)
    {
        Blueprint->ClearFlags(RF_Standalone);
        Sequence->SetDirectorBlueprint(Blueprint);
    }
    return Blueprint;
}

const UClass* GetBindingClass(UMovieScene* MovieScene, const FGuid& Guid)
{
    if (!MovieScene)
    {
        return nullptr;
    }
    if (const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Guid))
    {
        return Possessable->GetPossessedObjectClass();
    }
    if (const FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(Guid))
    {
        const UObject* Template = Spawnable->GetObjectTemplate();
        return Template ? Template->GetClass() : nullptr;
    }
    return nullptr;
}

bool IsValidEventName(const FString& Name)
{
    if (Name.IsEmpty() || Name.Len() > 64
        || !(FChar::IsAlpha(Name[0]) || Name[0] == TEXT('_')))
    {
        return false;
    }
    for (const TCHAR Character : Name)
    {
        if (!(FChar::IsAlnum(Character) || Character == TEXT('_')))
        {
            return false;
        }
    }
    return true;
}

void FillActorBindingRecord(
    const AActor* Actor,
    FThomasCinematicBindingRecord& Record)
{
    if (!Actor)
    {
        return;
    }
    const FVector Location = Actor->GetActorLocation();
    const FRotator Rotation = Actor->GetActorRotation();
    Record.LocationX = Location.X;
    Record.LocationY = Location.Y;
    Record.LocationZ = Location.Z;
    Record.RotationPitch = Rotation.Pitch;
    Record.RotationYaw = Rotation.Yaw;
    Record.RotationRoll = Rotation.Roll;
    if (const ACineCameraActor* CineCamera = Cast<ACineCameraActor>(Actor))
    {
        if (const UCineCameraComponent* Camera = CineCamera->GetCineCameraComponent())
        {
            Record.bCineCamera = true;
            Record.CurrentFocalLength = Camera->CurrentFocalLength;
            Record.CurrentAperture = Camera->CurrentAperture;
            Record.ManualFocusDistance = Camera->FocusSettings.ManualFocusDistance;
        }
    }
}

void AddTrackRecord(
    const UMovieSceneTrack* Track,
    const FString& BindingGuid,
    const int32 Index,
    FThomasCinematicInspectionResult& Result,
    const int32 MaxItems)
{
    if (!Track || Result.Tracks.Num() >= MaxItems)
    {
        return;
    }
    FThomasCinematicTrackRecord Record;
    Record.Index = Index;
    Record.Id = Track->GetName();
    Record.ClassPath = Track->GetClass()->GetPathName();
    Record.DisplayName = Track->GetDisplayName().ToString();
    Record.BindingGuid = BindingGuid;
    const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
    for (int32 SectionIndex = 0;
         SectionIndex < Sections.Num() && Record.Sections.Num() < MaxItems;
         ++SectionIndex)
    {
        const UMovieSceneSection* Section = Sections[SectionIndex];
        if (!Section)
        {
            continue;
        }
        FThomasCinematicSectionRecord SectionRecord;
        SectionRecord.Index = SectionIndex;
        SectionRecord.Id = Section->GetName();
        SectionRecord.ClassPath = Section->GetClass()->GetPathName();
        const TRange<FFrameNumber> Range = Section->GetRange();
        SectionRecord.bHasStartFrame = Range.HasLowerBound();
        SectionRecord.bHasEndFrame = Range.HasUpperBound();
        SectionRecord.StartFrame = Range.HasLowerBound()
            ? Range.GetLowerBoundValue().Value : 0;
        SectionRecord.EndFrame = Range.HasUpperBound()
            ? Range.GetUpperBoundValue().Value : 0;
        SectionRecord.RowIndex = Section->GetRowIndex();
        SectionRecord.PreRollFrames = Section->GetPreRollFrames();
        SectionRecord.PostRollFrames = Section->GetPostRollFrames();
        SectionRecord.bActive = Section->IsActive();
        SectionRecord.bLocked = Section->IsLocked();
        if (const UMovieSceneCameraCutSection* CameraCutSection =
                Cast<UMovieSceneCameraCutSection>(Section))
        {
            SectionRecord.CameraBindingGuid = CameraCutSection->GetCameraBindingID()
                .GetGuid().ToString(EGuidFormats::DigitsWithHyphens);
            SectionRecord.bLockPreviousCamera = CameraCutSection->bLockPreviousCamera;
        }
        const FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
        for (const FMovieSceneChannelEntry& Entry : Proxy.GetAllEntries())
        {
            const FString ChannelType = Entry.GetChannelTypeName().ToString();
            const TArrayView<FMovieSceneChannel* const> Channels = Entry.GetChannels();
#if WITH_EDITOR
            const TArrayView<const FMovieSceneChannelMetaData> MetaData =
                Entry.GetMetaData();
#endif
            for (int32 ChannelIndex = 0;
                 ChannelIndex < Channels.Num()
                    && SectionRecord.Channels.Num() < MaxItems;
                 ++ChannelIndex)
            {
                FThomasCinematicChannelRecord ChannelRecord;
                ChannelRecord.Type = ChannelType;
                ChannelRecord.Index = ChannelIndex;
#if WITH_EDITOR
                ChannelRecord.Name = MetaData.IsValidIndex(ChannelIndex)
                    ? MetaData[ChannelIndex].Name.ToString() : FString();
#endif
                FMovieSceneChannel* Channel = Channels[ChannelIndex];
                auto AppendKeys = [&ChannelRecord, MaxItems](const auto& Data, auto Format)
                {
                    const auto Times = Data.GetTimes();
                    const auto Values = Data.GetValues();
                    for (int32 KeyIndex = 0;
                         KeyIndex < Times.Num()
                            && ChannelRecord.Keys.Num() < MaxItems;
                         ++KeyIndex)
                    {
                        FThomasCinematicKeyRecord Key;
                        Key.Frame = Times[KeyIndex].Value;
                        Key.Value = Format(Values[KeyIndex]);
                        ChannelRecord.Keys.Add(MoveTemp(Key));
                    }
                };
                if (const FMovieSceneFloatChannel* FloatChannel =
                        static_cast<FMovieSceneFloatChannel*>(ChannelType
                            == FMovieSceneFloatChannel::StaticStruct()->GetName()
                            ? Channel : nullptr))
                {
                    AppendKeys(FloatChannel->GetData(), [](const FMovieSceneFloatValue& Item)
                    {
                        return FString::SanitizeFloat(Item.Value);
                    });
                }
                else if (const FMovieSceneDoubleChannel* DoubleChannel =
                        static_cast<FMovieSceneDoubleChannel*>(ChannelType
                            == FMovieSceneDoubleChannel::StaticStruct()->GetName()
                            ? Channel : nullptr))
                {
                    AppendKeys(DoubleChannel->GetData(), [](const FMovieSceneDoubleValue& Item)
                    {
                        return FString::SanitizeFloat(Item.Value);
                    });
                }
                else if (const FMovieSceneIntegerChannel* IntegerChannel =
                        static_cast<FMovieSceneIntegerChannel*>(ChannelType
                            == FMovieSceneIntegerChannel::StaticStruct()->GetName()
                            ? Channel : nullptr))
                {
                    AppendKeys(IntegerChannel->GetData(), [](const int32 Item)
                    {
                        return LexToString(Item);
                    });
                }
                else if (const FMovieSceneBoolChannel* BoolChannel =
                        static_cast<FMovieSceneBoolChannel*>(ChannelType
                            == FMovieSceneBoolChannel::StaticStruct()->GetName()
                            ? Channel : nullptr))
                {
                    AppendKeys(BoolChannel->GetData(), [](const bool Item)
                    {
                        return Item ? FString(TEXT("true")) : FString(TEXT("false"));
                    });
                }
                else if (const FMovieSceneByteChannel* ByteChannel =
                        static_cast<FMovieSceneByteChannel*>(ChannelType
                            == FMovieSceneByteChannel::StaticStruct()->GetName()
                            ? Channel : nullptr))
                {
                    AppendKeys(ByteChannel->GetData(), [](const uint8 Item)
                    {
                        return LexToString(Item);
                    });
                }
                else if (const FMovieSceneStringChannel* StringChannel =
                        static_cast<FMovieSceneStringChannel*>(ChannelType
                            == FMovieSceneStringChannel::StaticStruct()->GetName()
                            ? Channel : nullptr))
                {
                    AppendKeys(StringChannel->GetData(), [](const FString& Item)
                    {
                        return Item;
                    });
                }
                else if (const FMovieSceneEventChannel* EventChannel =
                        static_cast<FMovieSceneEventChannel*>(ChannelType
                            == FMovieSceneEventChannel::StaticStruct()->GetName()
                            ? Channel : nullptr))
                {
                    AppendKeys(EventChannel->GetData(), [](const FMovieSceneEvent& Item)
                    {
#if WITH_EDITORONLY_DATA
                        if (const UK2Node_CustomEvent* Endpoint =
                                Cast<UK2Node_CustomEvent>(Item.WeakEndpoint.Get()))
                        {
                            return Endpoint->CustomFunctionName.ToString();
                        }
                        if (!Item.CompiledFunctionName.IsNone())
                        {
                            return Item.CompiledFunctionName.ToString();
                        }
#endif
                        return FString(TEXT("unbound"));
                    });
                }
                SectionRecord.Channels.Add(MoveTemp(ChannelRecord));
            }
        }
        Record.Sections.Add(MoveTemp(SectionRecord));
    }
    Result.Tracks.Add(MoveTemp(Record));
}

UMovieSceneTrack* FindRootTrack(
    UMovieScene* MovieScene,
    const UClass* TrackClass,
    const int32 ClassIndex)
{
    int32 MatchIndex = 0;
    if (!MovieScene || !TrackClass || ClassIndex < 0)
    {
        return nullptr;
    }
    for (UMovieSceneTrack* Track : MovieScene->GetTracks())
    {
        if (Track && Track->GetClass() == TrackClass)
        {
            if (MatchIndex == ClassIndex)
            {
                return Track;
            }
            ++MatchIndex;
        }
    }
    return nullptr;
}

UClass* LoadTrackClass(const FString& ClassPath)
{
    UClass* Class = LoadObject<UClass>(nullptr, *ClassPath);
    return Class && Class->IsChildOf(UMovieSceneTrack::StaticClass())
        && !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)
        ? Class : nullptr;
}

bool ResolveBindingGuid(
    const FString& IdOrAlias,
    const TMap<FString, FGuid>& Aliases,
    FGuid& OutGuid)
{
    if (const FGuid* Alias = Aliases.Find(IdOrAlias))
    {
        OutGuid = *Alias;
        return true;
    }
    return FGuid::Parse(IdOrAlias, OutGuid) && OutGuid.IsValid();
}

UMovieSceneTrack* FindTrack(
    UMovieScene* MovieScene,
    const UClass* TrackClass,
    const int32 ClassIndex,
    const FString& BindingIdOrAlias,
    const TMap<FString, FGuid>& Aliases)
{
    if (BindingIdOrAlias.IsEmpty())
    {
        return FindRootTrack(MovieScene, TrackClass, ClassIndex);
    }
    FGuid BindingGuid;
    FMovieSceneBinding* Binding = ResolveBindingGuid(
        BindingIdOrAlias, Aliases, BindingGuid)
        ? MovieScene->FindBinding(BindingGuid) : nullptr;
    int32 MatchIndex = 0;
    if (!Binding || !TrackClass || ClassIndex < 0)
    {
        return nullptr;
    }
    for (UMovieSceneTrack* Track : Binding->GetTracks())
    {
        if (Track && Track->GetClass() == TrackClass)
        {
            if (MatchIndex++ == ClassIndex)
            {
                return Track;
            }
        }
    }
    return nullptr;
}

UMovieSceneSection* FindSection(
    UMovieScene* MovieScene,
    const FThomasCinematicOperation& Operation,
    const TMap<FString, FGuid>& Aliases)
{
    UMovieSceneTrack* Track = FindTrack(
        MovieScene,
        LoadTrackClass(Operation.TrackClassPath),
        Operation.TrackIndex,
        Operation.BindingGuid,
        Aliases);
    const TArray<UMovieSceneSection*> Sections =
        Track ? Track->GetAllSections() : TArray<UMovieSceneSection*>();
    return Sections.IsValidIndex(Operation.SectionIndex)
        ? Sections[Operation.SectionIndex] : nullptr;
}

FMovieSceneChannel* FindSectionChannel(
    UMovieSceneSection* Section,
    const FString& ChannelType,
    const int32 ChannelIndex)
{
    if (!Section || ChannelType.IsEmpty() || ChannelIndex < 0)
    {
        return nullptr;
    }
    const FString NormalizedType = ChannelType.StartsWith(TEXT("F"))
        ? ChannelType.RightChop(1) : ChannelType;
    for (const FMovieSceneChannelEntry& Entry
        : Section->GetChannelProxy().GetAllEntries())
    {
        if (Entry.GetChannelTypeName().ToString() == NormalizedType)
        {
            const TArrayView<FMovieSceneChannel* const> Channels =
                Entry.GetChannels();
            return Channels.IsValidIndex(ChannelIndex)
                ? Channels[ChannelIndex] : nullptr;
        }
    }
    return nullptr;
}

bool IsSupportedChannelType(const FString& ChannelType)
{
    const FString Type = ChannelType.StartsWith(TEXT("F"))
        ? ChannelType.RightChop(1) : ChannelType;
    return Type == FMovieSceneFloatChannel::StaticStruct()->GetName()
        || Type == FMovieSceneDoubleChannel::StaticStruct()->GetName()
        || Type == FMovieSceneIntegerChannel::StaticStruct()->GetName()
        || Type == FMovieSceneBoolChannel::StaticStruct()->GetName()
        || Type == FMovieSceneByteChannel::StaticStruct()->GetName()
        || Type == FMovieSceneStringChannel::StaticStruct()->GetName();
}

bool SetChannelKey(
    FMovieSceneChannel* Channel,
    const FString& ChannelType,
    const int32 Frame,
    const FString& Value,
    const FString& Interpolation,
    FString& OutError)
{
    const FString Type = ChannelType.StartsWith(TEXT("F"))
        ? ChannelType.RightChop(1) : ChannelType;
    const FFrameNumber Time(Frame);
    if (Type == FMovieSceneFloatChannel::StaticStruct()->GetName())
    {
        double Parsed = 0.0;
        if (!LexTryParseString(Parsed, *Value))
        {
            OutError = TEXT("Float channel value is invalid.");
            return false;
        }
        FMovieSceneFloatValue Key(static_cast<float>(Parsed));
        const FString Mode = Interpolation.ToLower();
        Key.InterpMode = Mode == TEXT("constant") ? RCIM_Constant
            : Mode == TEXT("linear") ? RCIM_Linear : RCIM_Cubic;
        static_cast<FMovieSceneFloatChannel*>(Channel)->GetData().UpdateOrAddKey(Time, Key);
        return true;
    }
    if (Type == FMovieSceneDoubleChannel::StaticStruct()->GetName())
    {
        double Parsed = 0.0;
        if (!LexTryParseString(Parsed, *Value))
        {
            OutError = TEXT("Double channel value is invalid.");
            return false;
        }
        FMovieSceneDoubleValue Key(Parsed);
        const FString Mode = Interpolation.ToLower();
        Key.InterpMode = Mode == TEXT("constant") ? RCIM_Constant
            : Mode == TEXT("linear") ? RCIM_Linear : RCIM_Cubic;
        static_cast<FMovieSceneDoubleChannel*>(Channel)->GetData().UpdateOrAddKey(Time, Key);
        return true;
    }
    if (Type == FMovieSceneIntegerChannel::StaticStruct()->GetName())
    {
        int32 Parsed = 0;
        if (!LexTryParseString(Parsed, *Value))
        {
            OutError = TEXT("Integer channel value is invalid.");
            return false;
        }
        static_cast<FMovieSceneIntegerChannel*>(Channel)->GetData().UpdateOrAddKey(Time, Parsed);
        return true;
    }
    if (Type == FMovieSceneBoolChannel::StaticStruct()->GetName())
    {
        const FString Lower = Value.ToLower();
        if (Lower != TEXT("true") && Lower != TEXT("false")
            && Lower != TEXT("1") && Lower != TEXT("0"))
        {
            OutError = TEXT("Bool channel value is invalid.");
            return false;
        }
        static_cast<FMovieSceneBoolChannel*>(Channel)->GetData().UpdateOrAddKey(
            Time, Lower == TEXT("true") || Lower == TEXT("1"));
        return true;
    }
    if (Type == FMovieSceneByteChannel::StaticStruct()->GetName())
    {
        int32 Parsed = 0;
        if (!LexTryParseString(Parsed, *Value) || Parsed < 0 || Parsed > 255)
        {
            OutError = TEXT("Byte channel value is invalid.");
            return false;
        }
        static_cast<FMovieSceneByteChannel*>(Channel)->GetData().UpdateOrAddKey(
            Time, static_cast<uint8>(Parsed));
        return true;
    }
    if (Type == FMovieSceneStringChannel::StaticStruct()->GetName())
    {
        static_cast<FMovieSceneStringChannel*>(Channel)->GetData().UpdateOrAddKey(Time, Value);
        return true;
    }
    OutError = TEXT("Unsupported Sequencer channel type.");
    return false;
}

bool RemoveChannelKey(FMovieSceneChannel* Channel, const int32 Frame)
{
    if (!Channel)
    {
        return false;
    }
    TArray<FKeyHandle> Handles;
    Channel->GetKeys(
        TRange<FFrameNumber>::Inclusive(FFrameNumber(Frame), FFrameNumber(Frame)),
        nullptr,
        &Handles);
    if (Handles.IsEmpty())
    {
        return false;
    }
    Channel->DeleteKeys(Handles);
    return true;
}

FString TrackCountKey(const FString& BindingToken, const FString& TrackClassPath)
{
    return BindingToken + TEXT("|") + TrackClassPath;
}

FString SectionCountKey(
    const FString& BindingToken,
    const FString& TrackClassPath,
    const int32 TrackIndex)
{
    return FString::Printf(
        TEXT("%s|%s|%d"), *BindingToken, *TrackClassPath, TrackIndex);
}
}

class FThomasEditorCinematicsModule final
    : public IThomasEditorCinematicsProviderModule
{
public:
    virtual void ShutdownModule() override
    {
        Plans.Reset();
    }

    virtual FThomasDomainInventoryResult GetInventory(
        const FString& PackageRoot,
        const int32 MaxResults) override
    {
        IThomasEditorAssetsProviderModule* Assets =
            FModuleManager::LoadModulePtr<IThomasEditorAssetsProviderModule>(
                TEXT("ThomasEditorAssets"));
        if (!Assets)
        {
            return MakeError<FThomasDomainInventoryResult>(
                TEXT("assets_provider_unavailable"), FString());
        }
        return Assets->GetDomainInventory(
            TEXT("Cinematics"),
            PackageRoot,
            {TEXT("/Script/LevelSequence.LevelSequence")},
            MaxResults);
    }

    virtual FThomasCinematicInspectionResult InspectLevelSequence(
        const FString& AssetPath,
        const int32 MaxItems) override
    {
        const FString PackageName = NormalizePath(AssetPath);
        if (!IsAllowedPath(PackageName))
        {
            return MakeError<FThomasCinematicInspectionResult>(
                TEXT("path_denied"), TEXT("Sequence must remain under /Game/PropHunt."));
        }
        if (MaxItems < 1 || MaxItems > MaxInspectionItems)
        {
            return MakeError<FThomasCinematicInspectionResult>(
                TEXT("invalid_limit"), TEXT("MaxItems must be between 1 and 500."));
        }
        ULevelSequence* Sequence = LoadSequence(PackageName);
        UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
        if (!Sequence || !MovieScene)
        {
            return MakeError<FThomasCinematicInspectionResult>(
                TEXT("level_sequence_not_found"), PackageName);
        }
        FThomasCinematicInspectionResult Result;
        Result.bOk = true;
        Result.AssetPath = PackageName;
        Result.Revision = BuildRevision(Sequence);
        Result.bDirty = Sequence->GetOutermost()->IsDirty();
        const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
        const FFrameRate TickResolution = MovieScene->GetTickResolution();
        Result.DisplayRateNumerator = DisplayRate.Numerator;
        Result.DisplayRateDenominator = DisplayRate.Denominator;
        Result.TickResolutionNumerator = TickResolution.Numerator;
        Result.TickResolutionDenominator = TickResolution.Denominator;
        const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
        Result.PlaybackStartFrame = PlaybackRange.HasLowerBound()
            ? PlaybackRange.GetLowerBoundValue().Value : 0;
        Result.PlaybackEndFrame = PlaybackRange.HasUpperBound()
            ? PlaybackRange.GetUpperBoundValue().Value : 0;
        if (const UBlueprint* DirectorBlueprint = Sequence->GetDirectorBlueprint())
        {
            Result.DirectorBlueprintPath = DirectorBlueprint->GetPathName();
            Result.DirectorBlueprintStatus = BlueprintStatusName(DirectorBlueprint);
        }
        else
        {
            Result.DirectorBlueprintStatus = TEXT("missing");
        }

        if (const UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack())
        {
            AddTrackRecord(CameraCutTrack, FString(), 0, Result, MaxItems);
        }
        const TArray<UMovieSceneTrack*>& RootTracks = MovieScene->GetTracks();
        for (int32 Index = 0; Index < RootTracks.Num(); ++Index)
        {
            AddTrackRecord(RootTracks[Index], FString(), Index, Result, MaxItems);
        }
        const UMovieScene* ConstMovieScene = MovieScene;
        for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
        {
            if (Result.Bindings.Num() >= MaxItems)
            {
                break;
            }
            FThomasCinematicBindingRecord BindingRecord;
            const FGuid Guid = Binding.GetObjectGuid();
            BindingRecord.Guid = Guid.ToString(EGuidFormats::DigitsWithHyphens);
            if (const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Guid))
            {
                BindingRecord.Name = Possessable->GetName();
                BindingRecord.Kind = TEXT("possessable");
                if (const UClass* BindingClass = Possessable->GetPossessedObjectClass())
                {
                    BindingRecord.ClassPath = BindingClass->GetPathName();
                }
            }
            else if (const FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(Guid))
            {
                BindingRecord.Name = Spawnable->GetName();
                BindingRecord.Kind = TEXT("spawnable");
                if (const UObject* Template = Spawnable->GetObjectTemplate())
                {
                    BindingRecord.ClassPath = Template->GetClass()->GetPathName();
                    BindingRecord.TemplateObjectPath = Template->GetPathName();
                    FillActorBindingRecord(Cast<AActor>(Template), BindingRecord);
                }
            }
            else
            {
                BindingRecord.Kind = TEXT("binding");
            }
            BindingRecord.TrackCount = Binding.GetTracks().Num();
            Result.Bindings.Add(MoveTemp(BindingRecord));
            for (int32 Index = 0; Index < Binding.GetTracks().Num(); ++Index)
            {
                AddTrackRecord(
                    Binding.GetTracks()[Index],
                    Guid.ToString(EGuidFormats::DigitsWithHyphens),
                    Index,
                    Result,
                    MaxItems);
            }
        }
        return Result;
    }

    virtual FThomasCinematicPlanResult PlanCinematicPatch(
        const FThomasCinematicPatchRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasCinematicPatchRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        if (!IsAllowedPath(Request.AssetPath))
        {
            return MakeError<FThomasCinematicPlanResult>(
                TEXT("path_denied"), TEXT("Sequence must remain under /Game/PropHunt."));
        }
        if (Request.Operations.IsEmpty() || Request.Operations.Num() > MaxOperations)
        {
            return MakeError<FThomasCinematicPlanResult>(
                TEXT("invalid_batch_size"), TEXT("Use 1 to 100 cinematic operations."));
        }
        ULevelSequence* Sequence = LoadSequence(Request.AssetPath);
        UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
        const FString Revision = BuildRevision(Sequence);
        if (Sequence)
        {
            if (Request.bCreateIfMissing || Request.ExpectedRevision != Revision)
            {
                return MakeError<FThomasCinematicPlanResult>(
                    Request.bCreateIfMissing ? TEXT("asset_exists") : TEXT("revision_conflict"),
                    Revision);
            }
            if (Sequence->GetOutermost()->IsDirty())
            {
                return MakeError<FThomasCinematicPlanResult>(
                    TEXT("asset_dirty"), TEXT("Save or revert the sequence before planning."));
            }
        }
        else
        {
            if (!Request.bCreateIfMissing
                || (Request.ExpectedRevision != TEXT("missing")
                    && !Request.ExpectedRevision.IsEmpty())
                || Request.Operations[0].Action.ToLower() != TEXT("create_level_sequence"))
            {
                return MakeError<FThomasCinematicPlanResult>(
                    TEXT("create_operation_required"), TEXT("create_level_sequence must be first."));
            }
            FString FactoryError;
            if (!CreateLevelSequenceFactory(FactoryError))
            {
                return MakeError<FThomasCinematicPlanResult>(
                    TEXT("cinematic_factory_unavailable"), FactoryError);
            }
            Request.ExpectedRevision = TEXT("missing");
        }

        TSet<FString> BindingTokens;
        TSet<FString> BindingAliases;
        TMap<FString, const UClass*> BindingClasses;
        TMap<FString, int32> TrackCounts;
        TMap<FString, int32> SectionCounts;
        TSet<int32> CameraCutFrames;
        TSet<FString> PlannedEventNames;
        int32 PlannedPlaybackStart = 0;
        int32 PlannedPlaybackEnd = 0;
        bool bHasPlannedPlaybackRange = false;
        if (MovieScene)
        {
            const TRange<FFrameNumber> ExistingPlaybackRange =
                MovieScene->GetPlaybackRange();
            if (ExistingPlaybackRange.HasLowerBound()
                && ExistingPlaybackRange.HasUpperBound())
            {
                PlannedPlaybackStart =
                    ExistingPlaybackRange.GetLowerBoundValue().Value;
                PlannedPlaybackEnd =
                    ExistingPlaybackRange.GetUpperBoundValue().Value;
                bHasPlannedPlaybackRange =
                    PlannedPlaybackEnd > PlannedPlaybackStart;
            }
            if (const UMovieSceneTrack* CameraTrack = MovieScene->GetCameraCutTrack())
            {
                for (const UMovieSceneSection* Section : CameraTrack->GetAllSections())
                {
                    if (Section && Section->HasStartFrame())
                    {
                        CameraCutFrames.Add(Section->GetInclusiveStartFrame().Value);
                    }
                }
            }
            for (const UMovieSceneTrack* Track : MovieScene->GetTracks())
            {
                if (Track)
                {
                    const FString ClassPath = Track->GetClass()->GetPathName();
                    const FString CountKey = TrackCountKey(FString(), ClassPath);
                    const int32 TrackIndex = TrackCounts.FindOrAdd(CountKey)++;
                    SectionCounts.Add(
                        SectionCountKey(FString(), ClassPath, TrackIndex),
                        Track->GetAllSections().Num());
                }
            }
            const UMovieScene* ConstMovieScene = MovieScene;
            for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
            {
                const FString BindingGuid = Binding.GetObjectGuid().ToString(
                    EGuidFormats::DigitsWithHyphens);
                BindingTokens.Add(BindingGuid);
                BindingClasses.Add(
                    BindingGuid,
                    GetBindingClass(MovieScene, Binding.GetObjectGuid()));
                for (const UMovieSceneTrack* Track : Binding.GetTracks())
                {
                    if (!Track)
                    {
                        continue;
                    }
                    const FString ClassPath = Track->GetClass()->GetPathName();
                    const FString CountKey = TrackCountKey(BindingGuid, ClassPath);
                    const int32 TrackIndex = TrackCounts.FindOrAdd(CountKey)++;
                    SectionCounts.Add(
                        SectionCountKey(BindingGuid, ClassPath, TrackIndex),
                        Track->GetAllSections().Num());
                }
            }
        }
        auto IsKnownBinding = [&BindingTokens, &BindingAliases](const FString& Token)
        {
            if (BindingAliases.Contains(Token))
            {
                return true;
            }
            FGuid Guid;
            return FGuid::Parse(Token, Guid)
                && BindingTokens.Contains(Guid.ToString(EGuidFormats::DigitsWithHyphens));
        };
        auto FindBindingClass = [&BindingClasses](const FString& Token) -> const UClass*
        {
            if (const UClass* const* Direct = BindingClasses.Find(Token))
            {
                return *Direct;
            }
            FGuid Guid;
            if (FGuid::Parse(Token, Guid))
            {
                if (const UClass* const* Existing = BindingClasses.Find(
                        Guid.ToString(EGuidFormats::DigitsWithHyphens)))
                {
                    return *Existing;
                }
            }
            return nullptr;
        };
        FThomasCinematicPlanResult Result;
        Result.AssetPath = Request.AssetPath;
        Result.ExpectedRevision = Request.ExpectedRevision;
        for (int32 OperationIndex = 0;
             OperationIndex < Request.Operations.Num();
             ++OperationIndex)
        {
            const FThomasCinematicOperation& Operation = Request.Operations[OperationIndex];
            const FString Action = Operation.Action.ToLower();
            if (Action == TEXT("create_level_sequence"))
            {
                if (Sequence || OperationIndex != 0)
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        TEXT("invalid_create_operation"), Operation.Action);
                }
            }
            else if (Action == TEXT("set_display_rate")
                || Action == TEXT("set_tick_resolution"))
            {
                if (Operation.Numerator <= 0 || Operation.Denominator <= 0)
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        TEXT("invalid_frame_rate"), Operation.Action);
                }
            }
            else if (Action == TEXT("set_playback_range"))
            {
                if (Operation.EndFrame <= Operation.StartFrame)
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        TEXT("invalid_playback_range"), Operation.Action);
                }
                PlannedPlaybackStart = Operation.StartFrame;
                PlannedPlaybackEnd = Operation.EndFrame;
                bHasPlannedPlaybackRange = true;
            }
            else if (Action == TEXT("add_possessable"))
            {
                UClass* BindingClass = LoadObject<UClass>(
                    nullptr, *Operation.BindingClassPath);
                if (Operation.BindingName.IsEmpty()
                    || Operation.ResultId.IsEmpty()
                    || BindingAliases.Contains(Operation.ResultId)
                    || !BindingClass
                    || BindingClass->HasAnyClassFlags(CLASS_Deprecated))
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        TEXT("invalid_possessable"), Operation.BindingName);
                }
                BindingAliases.Add(Operation.ResultId);
                BindingClasses.Add(Operation.ResultId, BindingClass);
            }
            else if (Action == TEXT("add_spawnable"))
            {
                UClass* BindingClass = LoadObject<UClass>(
                    nullptr, *Operation.BindingClassPath);
                const bool bFiniteTransform =
                    FMath::IsFinite(Operation.LocationX)
                    && FMath::IsFinite(Operation.LocationY)
                    && FMath::IsFinite(Operation.LocationZ)
                    && FMath::IsFinite(Operation.RotationPitch)
                    && FMath::IsFinite(Operation.RotationYaw)
                    && FMath::IsFinite(Operation.RotationRoll);
                const bool bValidCineSettings = !BindingClass
                    || !BindingClass->IsChildOf(ACineCameraActor::StaticClass())
                    || (Operation.FocalLength > 0.0f
                        && Operation.Aperture > 0.0f
                        && Operation.FocusDistance >= 0.0f);
                if (Operation.BindingName.IsEmpty()
                    || Operation.ResultId.IsEmpty()
                    || BindingAliases.Contains(Operation.ResultId)
                    || !BindingClass
                    || !BindingClass->IsChildOf(AActor::StaticClass())
                    || BindingClass->HasAnyClassFlags(
                        CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
                    || !bFiniteTransform
                    || !bValidCineSettings)
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        TEXT("invalid_spawnable"), Operation.BindingName);
                }
                BindingAliases.Add(Operation.ResultId);
                BindingClasses.Add(Operation.ResultId, BindingClass);
            }
            else if (Action == TEXT("add_camera_cut"))
            {
                const UClass* BindingClass = FindBindingClass(Operation.BindingGuid);
                const bool bOverwritesExisting =
                    CameraCutFrames.Contains(Operation.Frame);
                if (!IsKnownBinding(Operation.BindingGuid)
                    || !BindingClass
                    || !BindingClass->IsChildOf(ACameraActor::StaticClass())
                    || (bOverwritesExisting && !Request.bConfirmDestructive))
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        bOverwritesExisting && !Request.bConfirmDestructive
                            ? TEXT("confirmation_required")
                            : TEXT("invalid_camera_cut"),
                        Operation.BindingGuid);
                }
                CameraCutFrames.Add(Operation.Frame);
                if (bOverwritesExisting)
                {
                    Result.Risk = TEXT("R2");
                }
            }
            else if (Action == TEXT("add_event"))
            {
                const FName DesiredName(*Operation.EventName);
                const UBlueprint* DirectorBlueprint = Sequence
                    ? Sequence->GetDirectorBlueprint() : nullptr;
                const bool bUniqueInExistingBlueprint = !DirectorBlueprint
                    || FBlueprintEditorUtils::FindUniqueKismetName(
                        DirectorBlueprint, Operation.EventName) == DesiredName;
                if (!IsValidEventName(Operation.EventName)
                    || PlannedEventNames.Contains(Operation.EventName)
                    || !bUniqueInExistingBlueprint
                    || !bHasPlannedPlaybackRange
                    || Operation.Frame < PlannedPlaybackStart
                    || Operation.Frame >= PlannedPlaybackEnd)
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        TEXT("invalid_event"), Operation.EventName);
                }
                PlannedEventNames.Add(Operation.EventName);
            }
            else if (Action == TEXT("add_root_track"))
            {
                UClass* TrackClass = LoadTrackClass(Operation.TrackClassPath);
                if (!TrackClass
                    || TrackClass->IsChildOf(UMovieSceneCameraCutTrack::StaticClass())
                    || TrackClass->IsChildOf(UMovieSceneEventTrack::StaticClass()))
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        TEXT("invalid_track_class"), Operation.TrackClassPath);
                }
                const FString CountKey = TrackCountKey(
                    FString(), Operation.TrackClassPath);
                const int32 TrackIndex = TrackCounts.FindOrAdd(CountKey)++;
                SectionCounts.Add(
                    SectionCountKey(FString(), Operation.TrackClassPath, TrackIndex), 0);
            }
            else if (Action == TEXT("add_binding_track")
                || Action == TEXT("add_property_track"))
            {
                UClass* TrackClass = LoadTrackClass(Operation.TrackClassPath);
                if (!IsKnownBinding(Operation.BindingGuid)
                    || !TrackClass
                    || (Action == TEXT("add_property_track")
                        && (!TrackClass->IsChildOf(UMovieScenePropertyTrack::StaticClass())
                            || Operation.PropertyName.IsEmpty()
                            || Operation.PropertyPath.IsEmpty())))
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        TEXT("invalid_binding_track"), Operation.TrackClassPath);
                }
                const FString CountKey = TrackCountKey(
                    Operation.BindingGuid, Operation.TrackClassPath);
                const int32 TrackIndex = TrackCounts.FindOrAdd(CountKey)++;
                SectionCounts.Add(SectionCountKey(
                    Operation.BindingGuid,
                    Operation.TrackClassPath,
                    TrackIndex), 0);
            }
            else if (Action == TEXT("add_section"))
            {
                UClass* TrackClass = LoadTrackClass(Operation.TrackClassPath);
                const FString CountKey = TrackCountKey(
                    Operation.BindingGuid, Operation.TrackClassPath);
                const FString SectionKey = SectionCountKey(
                    Operation.BindingGuid,
                    Operation.TrackClassPath,
                    Operation.TrackIndex);
                if (!TrackClass
                    || TrackClass->IsChildOf(UMovieSceneCameraCutTrack::StaticClass())
                    || TrackClass->IsChildOf(UMovieSceneEventTrack::StaticClass())
                    || (!Operation.BindingGuid.IsEmpty()
                        && !IsKnownBinding(Operation.BindingGuid))
                    || Operation.TrackIndex < 0
                    || TrackCounts.FindRef(CountKey) <= Operation.TrackIndex
                    || Operation.EndFrame <= Operation.StartFrame)
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        TEXT("invalid_section_target"), Operation.TrackClassPath);
                }
                ++SectionCounts.FindOrAdd(SectionKey);
            }
            else if (Action == TEXT("set_section_range")
                || Action == TEXT("set_section_state")
                || Action == TEXT("set_channel_key")
                || Action == TEXT("remove_channel_key")
                || Action == TEXT("remove_section"))
            {
                const FString SectionKey = SectionCountKey(
                    Operation.BindingGuid,
                    Operation.TrackClassPath,
                    Operation.TrackIndex);
                int32& SectionCount = SectionCounts.FindOrAdd(SectionKey);
                const bool bDestructive = Action == TEXT("remove_channel_key")
                    || Action == TEXT("remove_section");
                if (!LoadTrackClass(Operation.TrackClassPath)
                    || (!Operation.BindingGuid.IsEmpty()
                        && !IsKnownBinding(Operation.BindingGuid))
                    || Operation.SectionIndex < 0
                    || SectionCount <= Operation.SectionIndex
                    || (Action == TEXT("set_section_range")
                        && Operation.EndFrame <= Operation.StartFrame)
                    || ((Action == TEXT("set_channel_key")
                            || Action == TEXT("remove_channel_key"))
                        && (!IsSupportedChannelType(Operation.ChannelType)
                            || Operation.ChannelIndex < 0
                            || (Action == TEXT("set_channel_key")
                                && Operation.Value.IsEmpty())))
                    || (bDestructive && !Request.bConfirmDestructive))
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        bDestructive && !Request.bConfirmDestructive
                            ? TEXT("confirmation_required")
                            : TEXT("invalid_section_operation"),
                        Operation.TrackClassPath);
                }
                if (Action == TEXT("remove_section"))
                {
                    --SectionCount;
                }
                if (bDestructive)
                {
                    Result.Risk = TEXT("R2");
                }
            }
            else if (Action == TEXT("remove_root_track")
                || Action == TEXT("remove_binding_track"))
            {
                const FString BindingToken = Action == TEXT("remove_root_track")
                    ? FString() : Operation.BindingGuid;
                int32& Count = TrackCounts.FindOrAdd(
                    TrackCountKey(BindingToken, Operation.TrackClassPath));
                if (!Request.bConfirmDestructive
                    || (Action == TEXT("remove_binding_track")
                        && !IsKnownBinding(BindingToken))
                    || Operation.TrackIndex < 0 || Count <= Operation.TrackIndex)
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("track_not_found") : TEXT("confirmation_required"),
                        Operation.TrackClassPath);
                }
                --Count;
                Result.Risk = TEXT("R2");
            }
            else if (Action == TEXT("remove_camera_cut"))
            {
                if (!Request.bConfirmDestructive
                    || !CameraCutFrames.Contains(Operation.Frame))
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("camera_cut_not_found")
                            : TEXT("confirmation_required"),
                        LexToString(Operation.Frame));
                }
                CameraCutFrames.Remove(Operation.Frame);
                Result.Risk = TEXT("R2");
            }
            else if (Action == TEXT("remove_binding"))
            {
                if (!Request.bConfirmDestructive
                    || !IsKnownBinding(Operation.BindingGuid))
                {
                    return MakeError<FThomasCinematicPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("binding_not_found") : TEXT("confirmation_required"),
                        Operation.BindingGuid);
                }
                Result.Risk = TEXT("R2");
            }
            else
            {
                return MakeError<FThomasCinematicPlanResult>(
                    TEXT("unsupported_operation"), Operation.Action);
            }
            Result.Preview.Add(Operation.Action + TEXT(":") + Operation.TrackClassPath);
        }
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.OperationCount = Request.Operations.Num();
        FCachedPlan& Plan = Plans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasCinematicApplyResult ApplyCinematicPlan(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FCachedPlan Plan;
        if (!Plans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasCinematicApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        ULevelSequence* Sequence = LoadSequence(Plan.Request.AssetPath);
        const FString RevisionBefore = BuildRevision(Sequence);
        if (RevisionBefore != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasCinematicApplyResult>(
                TEXT("revision_conflict"), RevisionBefore);
        }

        FThomasCinematicApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.RevisionBefore = RevisionBefore;
        bool bCreated = false;
        if (!Sequence)
        {
            FString FactoryError;
            UFactory* Factory = CreateLevelSequenceFactory(FactoryError);
            UPackage* Package = CreatePackage(*Plan.Request.AssetPath);
            Sequence = Factory ? Cast<ULevelSequence>(Factory->FactoryCreateNew(
                Factory->SupportedClass,
                Package,
                FName(*FPackageName::GetLongPackageAssetName(Plan.Request.AssetPath)),
                RF_Public | RF_Standalone | RF_Transactional,
                nullptr,
                GWarn)) : nullptr;
            if (!Sequence)
            {
                return MakeError<FThomasCinematicApplyResult>(
                    TEXT("create_failed"), FactoryError);
            }
            FAssetRegistryModule::AssetCreated(Sequence);
            Sequence->GetOutermost()->MarkPackageDirty();
            bCreated = true;
            Result.bCreated = true;
        }
        UMovieScene* MovieScene = Sequence->GetMovieScene();
        UPackage* Package = Sequence->GetOutermost();
        if (!MovieScene || !Package)
        {
            return MakeError<FThomasCinematicApplyResult>(
                TEXT("invalid_sequence"), Plan.Request.AssetPath);
        }

        const FScopedTransaction Transaction(
            NSLOCTEXT("ThomasEditor", "CinematicPatch", "ThomasEditor Cinematic Patch"));
        Sequence->Modify();
        MovieScene->Modify();
        Package->Modify();
        auto Rollback = [&]()
        {
            if (bCreated)
            {
                Result.bRolledBack = ObjectTools::DeleteObjectsUnchecked({Sequence}) == 1;
            }
            else
            {
                Result.bRolledBack = UPackageTools::ReloadPackages({Package});
            }
        };

        TMap<FString, FGuid> BindingAliases;
        TSet<UBlueprint*> DirectorBlueprintsToCompile;
        for (const FThomasCinematicOperation& Operation : Plan.Request.Operations)
        {
            const FString Action = Operation.Action.ToLower();
            bool bApplied = true;
            if (Action == TEXT("create_level_sequence"))
            {
                // Creation was performed before the transactional operation loop.
            }
            else if (Action == TEXT("set_display_rate"))
            {
                MovieScene->SetDisplayRate(FFrameRate(Operation.Numerator, Operation.Denominator));
            }
            else if (Action == TEXT("set_tick_resolution"))
            {
                MovieScene->SetTickResolutionDirectly(
                    FFrameRate(Operation.Numerator, Operation.Denominator));
            }
            else if (Action == TEXT("set_playback_range"))
            {
                MovieScene->SetPlaybackRange(
                    FFrameNumber(Operation.StartFrame),
                    Operation.EndFrame - Operation.StartFrame);
            }
            else if (Action == TEXT("add_possessable"))
            {
                UClass* BindingClass = LoadObject<UClass>(
                    nullptr, *Operation.BindingClassPath);
                const FGuid BindingGuid = BindingClass
                    ? MovieScene->AddPossessable(Operation.BindingName, BindingClass)
                    : FGuid();
                bApplied = BindingGuid.IsValid();
                if (bApplied && !Operation.ResultId.IsEmpty())
                {
                    BindingAliases.Add(Operation.ResultId, BindingGuid);
                }
            }
            else if (Action == TEXT("add_spawnable"))
            {
                UClass* BindingClass = LoadObject<UClass>(
                    nullptr, *Operation.BindingClassPath);
                AActor* SourceActor = BindingClass
                    ? Cast<AActor>(BindingClass->GetDefaultObject()) : nullptr;
                UObject* Template = SourceActor
                    ? Sequence->MakeSpawnableTemplateFromInstance(
                        *SourceActor,
                        MakeUniqueObjectName(
                            MovieScene,
                            BindingClass,
                            FName(*Operation.BindingName)))
                    : nullptr;
                AActor* TemplateActor = Cast<AActor>(Template);
                if (TemplateActor)
                {
                    TemplateActor->Modify();
                    TemplateActor->SetActorLocationAndRotation(
                        FVector(
                            Operation.LocationX,
                            Operation.LocationY,
                            Operation.LocationZ),
                        FRotator(
                            Operation.RotationPitch,
                            Operation.RotationYaw,
                            Operation.RotationRoll),
                        false,
                        nullptr,
                        ETeleportType::TeleportPhysics);
                    if (ACineCameraActor* CineCamera =
                            Cast<ACineCameraActor>(TemplateActor))
                    {
                        if (UCineCameraComponent* Camera =
                                CineCamera->GetCineCameraComponent())
                        {
                            Camera->Modify();
                            Camera->SetCurrentFocalLength(Operation.FocalLength);
                            Camera->SetCurrentAperture(Operation.Aperture);
                            FCameraFocusSettings FocusSettings = Camera->FocusSettings;
                            FocusSettings.FocusMethod = ECameraFocusMethod::Manual;
                            FocusSettings.ManualFocusDistance = Operation.FocusDistance;
                            Camera->SetFocusSettings(FocusSettings);
                        }
                    }
                }
                const FGuid BindingGuid = TemplateActor
                    ? MovieScene->AddSpawnable(Operation.BindingName, *TemplateActor)
                    : FGuid();
                bApplied = BindingGuid.IsValid();
                if (bApplied && !Operation.ResultId.IsEmpty())
                {
                    BindingAliases.Add(Operation.ResultId, BindingGuid);
                }
            }
            else if (Action == TEXT("add_camera_cut"))
            {
                FGuid BindingGuid;
                UMovieSceneCameraCutTrack* CameraTrack = ResolveBindingGuid(
                    Operation.BindingGuid, BindingAliases, BindingGuid)
                    ? Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack())
                    : nullptr;
                if (!CameraTrack && BindingGuid.IsValid())
                {
                    CameraTrack = Cast<UMovieSceneCameraCutTrack>(
                        MovieScene->AddCameraCutTrack(
                            UMovieSceneCameraCutTrack::StaticClass()));
                }
                UMovieSceneCameraCutSection* CameraSection = CameraTrack
                    ? CameraTrack->AddNewCameraCut(
                        FMovieSceneObjectBindingID(
                            UE::MovieScene::FRelativeObjectBindingID(BindingGuid)),
                        FFrameNumber(Operation.Frame))
                    : nullptr;
                if (CameraSection)
                {
                    CameraSection->Modify();
                    CameraSection->bLockPreviousCamera =
                        Operation.bLockPreviousCamera;
                }
                bApplied = CameraSection != nullptr;
            }
            else if (Action == TEXT("add_event"))
            {
                UMovieSceneEventTrack* EventTrack = Cast<UMovieSceneEventTrack>(
                    FindRootTrack(MovieScene, UMovieSceneEventTrack::StaticClass(), 0));
                if (!EventTrack)
                {
                    EventTrack = Cast<UMovieSceneEventTrack>(
                        MovieScene->AddTrack(UMovieSceneEventTrack::StaticClass()));
                }
                UMovieSceneEventTriggerSection* EventSection = nullptr;
                if (EventTrack)
                {
                    for (UMovieSceneSection* ExistingSection
                        : EventTrack->GetAllSections())
                    {
                        UMovieSceneEventTriggerSection* TriggerSection =
                            Cast<UMovieSceneEventTriggerSection>(ExistingSection);
                        if (TriggerSection
                            && TriggerSection->GetRange().Contains(
                                FFrameNumber(Operation.Frame)))
                        {
                            EventSection = TriggerSection;
                            break;
                        }
                    }
                }
                if (!EventSection && EventTrack)
                {
                    EventSection = Cast<UMovieSceneEventTriggerSection>(
                        EventTrack->CreateNewSection());
                    if (EventSection)
                    {
                        EventSection->SetRange(MovieScene->GetPlaybackRange());
                        EventTrack->AddSection(*EventSection);
                    }
                }
                UBlueprint* DirectorBlueprint =
                    GetOrCreateDirectorBlueprint(Sequence);
                UK2Node_CustomEvent* Endpoint = nullptr;
                if (EventSection && DirectorBlueprint)
                {
                    EventSection->Modify();
                    TMovieSceneChannelData<FMovieSceneEvent> EventData =
                        EventSection->EventChannel.GetData();
                    const int32 EventIndex = EventData.AddKey(
                        FFrameNumber(Operation.Frame), FMovieSceneEvent());
                    if (EventData.GetValues().IsValidIndex(EventIndex))
                    {
                        FMovieSceneEvent* Event =
                            &EventData.GetValues()[EventIndex];
                        Endpoint = FMovieSceneEventUtils::BindNewUserFacingEvent(
                            Event, EventSection, DirectorBlueprint);
                        if (Endpoint)
                        {
                            Endpoint->OnRenameNode(Operation.EventName);
                            DirectorBlueprintsToCompile.Add(DirectorBlueprint);
                        }
                    }
                }
                bApplied = Endpoint != nullptr;
            }
            else if (Action == TEXT("add_root_track"))
            {
                bApplied = MovieScene->AddTrack(LoadTrackClass(Operation.TrackClassPath)) != nullptr;
            }
            else if (Action == TEXT("add_binding_track")
                || Action == TEXT("add_property_track"))
            {
                FGuid BindingGuid;
                UMovieSceneTrack* Track = ResolveBindingGuid(
                    Operation.BindingGuid, BindingAliases, BindingGuid)
                    ? MovieScene->AddTrack(
                        LoadTrackClass(Operation.TrackClassPath), BindingGuid)
                    : nullptr;
                if (UMovieScenePropertyTrack* PropertyTrack =
                        Cast<UMovieScenePropertyTrack>(Track))
                {
                    PropertyTrack->SetPropertyNameAndPath(
                        FName(*Operation.PropertyName), Operation.PropertyPath);
                }
                bApplied = Track != nullptr;
            }
            else if (Action == TEXT("add_section"))
            {
                UMovieSceneTrack* Track = FindTrack(
                    MovieScene,
                    LoadTrackClass(Operation.TrackClassPath),
                    Operation.TrackIndex,
                    Operation.BindingGuid,
                    BindingAliases);
                UMovieSceneSection* Section = Track ? Track->CreateNewSection() : nullptr;
                if (Section)
                {
                    Section->SetRange(TRange<FFrameNumber>(
                        FFrameNumber(Operation.StartFrame),
                        FFrameNumber(Operation.EndFrame)));
                    Track->AddSection(*Section);
                }
                bApplied = Section != nullptr;
            }
            else if (Action == TEXT("set_section_range"))
            {
                UMovieSceneSection* Section = FindSection(
                    MovieScene, Operation, BindingAliases);
                if (Section)
                {
                    Section->Modify();
                    Section->SetRange(TRange<FFrameNumber>(
                        FFrameNumber(Operation.StartFrame),
                        FFrameNumber(Operation.EndFrame)));
                }
                bApplied = Section != nullptr;
            }
            else if (Action == TEXT("set_section_state"))
            {
                UMovieSceneSection* Section = FindSection(
                    MovieScene, Operation, BindingAliases);
                if (Section)
                {
                    Section->Modify();
                    Section->SetRowIndex(Operation.RowIndex);
                    Section->SetPreRollFrames(Operation.PreRollFrames);
                    Section->SetPostRollFrames(Operation.PostRollFrames);
                    Section->SetIsActive(Operation.bEnabled);
                    Section->SetIsLocked(Operation.bLocked);
                }
                bApplied = Section != nullptr;
            }
            else if (Action == TEXT("set_channel_key")
                || Action == TEXT("remove_channel_key"))
            {
                UMovieSceneSection* Section = FindSection(
                    MovieScene, Operation, BindingAliases);
                FMovieSceneChannel* Channel = FindSectionChannel(
                    Section, Operation.ChannelType, Operation.ChannelIndex);
                if (Section && Channel)
                {
                    Section->Modify();
                }
                FString ChannelError;
                bApplied = Action == TEXT("set_channel_key")
                    ? (Channel && SetChannelKey(
                        Channel,
                        Operation.ChannelType,
                        Operation.Frame,
                        Operation.Value,
                        Operation.Interpolation,
                        ChannelError))
                    : RemoveChannelKey(Channel, Operation.Frame);
                if (!bApplied && !ChannelError.IsEmpty())
                {
                    Result.Message = ChannelError;
                }
            }
            else if (Action == TEXT("remove_section"))
            {
                UMovieSceneTrack* Track = FindTrack(
                    MovieScene,
                    LoadTrackClass(Operation.TrackClassPath),
                    Operation.TrackIndex,
                    Operation.BindingGuid,
                    BindingAliases);
                UMovieSceneSection* Section = FindSection(
                    MovieScene, Operation, BindingAliases);
                if (Track && Section)
                {
                    Track->Modify();
                    if (UMovieSceneEventSectionBase* EventSection =
                            Cast<UMovieSceneEventSectionBase>(Section))
                    {
                        if (UBlueprint* DirectorBlueprint =
                                Sequence->GetDirectorBlueprint())
                        {
                            FMovieSceneEventUtils::RemoveEndpointsForEventSection(
                                EventSection, DirectorBlueprint);
                            DirectorBlueprintsToCompile.Add(DirectorBlueprint);
                        }
                    }
                    Track->RemoveSection(*Section);
                    bApplied = true;
                }
                else
                {
                    bApplied = false;
                }
            }
            else if (Action == TEXT("remove_camera_cut"))
            {
                UMovieSceneCameraCutTrack* CameraTrack =
                    Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack());
                UMovieSceneCameraCutSection* CameraSection = nullptr;
                if (CameraTrack)
                {
                    for (UMovieSceneSection* Section : CameraTrack->GetAllSections())
                    {
                        if (Section && Section->HasStartFrame()
                            && Section->GetInclusiveStartFrame().Value
                                == Operation.Frame)
                        {
                            CameraSection = Cast<UMovieSceneCameraCutSection>(Section);
                            break;
                        }
                    }
                }
                if (CameraTrack && CameraSection)
                {
                    CameraTrack->Modify();
                    CameraTrack->RemoveSection(*CameraSection);
                    if (CameraTrack->IsEmpty())
                    {
                        MovieScene->RemoveCameraCutTrack();
                    }
                    bApplied = true;
                }
                else
                {
                    bApplied = false;
                }
            }
            else if (Action == TEXT("remove_root_track"))
            {
                UMovieSceneTrack* Track = FindRootTrack(
                    MovieScene,
                    LoadTrackClass(Operation.TrackClassPath),
                    Operation.TrackIndex);
                if (const UMovieSceneEventTrack* EventTrack =
                        Cast<UMovieSceneEventTrack>(Track))
                {
                    if (UBlueprint* DirectorBlueprint =
                            Sequence->GetDirectorBlueprint())
                    {
                        for (UMovieSceneSection* Section
                            : EventTrack->GetAllSections())
                        {
                            if (UMovieSceneEventSectionBase* EventSection =
                                    Cast<UMovieSceneEventSectionBase>(Section))
                            {
                                FMovieSceneEventUtils::RemoveEndpointsForEventSection(
                                    EventSection, DirectorBlueprint);
                            }
                        }
                        DirectorBlueprintsToCompile.Add(DirectorBlueprint);
                    }
                }
                bApplied = Track && MovieScene->RemoveTrack(*Track);
            }
            else if (Action == TEXT("remove_binding_track"))
            {
                UMovieSceneTrack* Track = FindTrack(
                    MovieScene,
                    LoadTrackClass(Operation.TrackClassPath),
                    Operation.TrackIndex,
                    Operation.BindingGuid,
                    BindingAliases);
                bApplied = Track && MovieScene->RemoveTrack(*Track);
            }
            else if (Action == TEXT("remove_binding"))
            {
                FGuid BindingGuid;
                bApplied = ResolveBindingGuid(
                    Operation.BindingGuid, BindingAliases, BindingGuid);
                if (bApplied && MovieScene->GetCameraCutTrack())
                {
                    UMovieSceneTrack* CameraTrack = MovieScene->GetCameraCutTrack();
                    TArray<UMovieSceneSection*> ReferencingSections;
                    for (UMovieSceneSection* Section : CameraTrack->GetAllSections())
                    {
                        const UMovieSceneCameraCutSection* CameraSection =
                            Cast<UMovieSceneCameraCutSection>(Section);
                        if (CameraSection
                            && CameraSection->GetCameraBindingID().GetGuid()
                                == BindingGuid)
                        {
                            ReferencingSections.Add(Section);
                        }
                    }
                    for (UMovieSceneSection* Section : ReferencingSections)
                    {
                        CameraTrack->RemoveSection(*Section);
                    }
                }
                if (bApplied && MovieScene->FindPossessable(BindingGuid))
                {
                    Sequence->UnbindPossessableObjects(BindingGuid);
                    bApplied = MovieScene->RemovePossessable(BindingGuid);
                }
                else if (bApplied && MovieScene->FindSpawnable(BindingGuid))
                {
                    bApplied = MovieScene->RemoveSpawnable(BindingGuid);
                }
                else
                {
                    bApplied = false;
                }
            }
            if (!bApplied)
            {
                Rollback();
                Result.Code = TEXT("apply_failed_rolled_back");
                Result.Message = Operation.Action;
                return Result;
            }
            ++Result.AppliedOperationCount;
        }
        for (UBlueprint* DirectorBlueprint : DirectorBlueprintsToCompile)
        {
            FKismetEditorUtilities::CompileBlueprint(DirectorBlueprint);
            if (DirectorBlueprint->Status == BS_Error)
            {
                Rollback();
                Result.Code = TEXT("director_compile_failed_rolled_back");
                Result.Message = DirectorBlueprint->GetPathName();
                return Result;
            }
        }
        Sequence->PostEditChange();
        Package->MarkPackageDirty();
        if (bSave && !SaveSequence(Sequence))
        {
            Rollback();
            Result.Code = TEXT("save_failed_rolled_back");
            return Result;
        }
        Result.bSaved = bSave;
        Result.bOk = true;
        Result.RevisionAfter = BuildRevision(Sequence);
        return Result;
    }

private:
    void PurgeExpiredPlans()
    {
        const double Now = FPlatformTime::Seconds();
        for (auto It = Plans.CreateIterator(); It; ++It)
        {
            if (Now - It.Value().CreatedAtSeconds > PlanLifetimeSeconds)
            {
                It.RemoveCurrent();
            }
        }
    }

    TMap<FString, FCachedPlan> Plans;
};

IMPLEMENT_MODULE(FThomasEditorCinematicsModule, ThomasEditorCinematics)
