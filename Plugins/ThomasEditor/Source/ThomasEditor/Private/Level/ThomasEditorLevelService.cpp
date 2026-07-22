#include "Level/ThomasEditorLevelService.h"

#include "Components/LightComponentBase.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace
{
constexpr int32 MaxFilters = 16;
constexpr int32 MaxActorResults = 100;
constexpr int32 MaxActorsScanned = 10000;

const TSet<FString>& AllowedMaps()
{
    static const TSet<FString> Maps = {
        TEXT("/Game/PropHunt/Tests/L_PH_NetworkTest"),
        TEXT("/Game/PropHunt/Tests/L_PH_PhysicsPropTest"),
        TEXT("/Game/PropHunt/Tests/L_PH_CaptureTest"),
        TEXT("/Game/PropHunt/Tests/L_PH_ObjectiveTest"),
        TEXT("/Game/PropHunt/Tests/L_PH_Intro"),
        TEXT("/Game/PropHunt/Maps/L_PH_Results"),
        TEXT("/Game/PropHunt/Maps/L_PH_Lobby"),
        TEXT("/Game/PropHunt/Maps/L_PH_WaitingRoom"),
        TEXT("/Game/PropHunt/Maps/L_PH_CampCaper_Graybox")
    };
    return Maps;
}

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

bool ParseStrings(const FString& Json, TArray<FString>& OutValues)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json.IsEmpty() ? TEXT("[]") : Json);
    if (!FJsonSerializer::Deserialize(Reader, Values) || Values.Num() > MaxFilters)
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

FString PackagePath(const FString& Input)
{
    FString Path = Input.TrimStartAndEnd();
    FString Left;
    FString Right;
    if (Path.Split(TEXT("."), &Left, &Right))
    {
        return Left;
    }
    return Path;
}

TSharedPtr<FJsonObject> TransformJson(const FTransform& Transform)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    const FVector Location = Transform.GetLocation();
    const FRotator Rotation = Transform.Rotator();
    const FVector Scale = Transform.GetScale3D();
    Result->SetArrayField(TEXT("location"), {
        MakeShared<FJsonValueNumber>(Location.X), MakeShared<FJsonValueNumber>(Location.Y), MakeShared<FJsonValueNumber>(Location.Z)});
    Result->SetArrayField(TEXT("rotation"), {
        MakeShared<FJsonValueNumber>(Rotation.Pitch), MakeShared<FJsonValueNumber>(Rotation.Yaw), MakeShared<FJsonValueNumber>(Rotation.Roll)});
    Result->SetArrayField(TEXT("scale"), {
        MakeShared<FJsonValueNumber>(Scale.X), MakeShared<FJsonValueNumber>(Scale.Y), MakeShared<FJsonValueNumber>(Scale.Z)});
    return Result;
}
}

FString FThomasEditorLevelService::BuildAuditJson(
    const FString& MapPath,
    const FString& ClassFiltersJson,
    const FString& FoldersJson,
    const FString& RequiredFlagsJson,
    const bool bIncludeActors,
    const int32 MaxResults)
{
    if (GEditor && GEditor->PlayWorld)
    {
        return MakeErrorResponse(TEXT("pie_active"), TEXT("Level audit is disabled while PIE is active."));
    }
    if (MaxResults < 1 || MaxResults > MaxActorResults)
    {
        return MakeErrorResponse(TEXT("too_many_results"), TEXT("max_results must be between 1 and 100."));
    }

    TArray<FString> ClassFilterPaths;
    TArray<FString> FolderFilters;
    TArray<FString> RequiredFlags;
    if (!ParseStrings(ClassFiltersJson, ClassFilterPaths)
        || !ParseStrings(FoldersJson, FolderFilters)
        || !ParseStrings(RequiredFlagsJson, RequiredFlags))
    {
        return MakeErrorResponse(TEXT("too_many_filters"), TEXT("Each filter list is capped at 16 strings."));
    }

    const TSet<FString> KnownFlags = { TEXT("has_light"), TEXT("has_sky_light"), TEXT("has_world_settings") };
    for (const FString& Flag : RequiredFlags)
    {
        if (!KnownFlags.Contains(Flag))
        {
            return MakeErrorResponse(TEXT("unknown_audit_flag"), Flag);
        }
    }

    TArray<UClass*> ClassFilters;
    for (const FString& ClassPath : ClassFilterPaths)
    {
        if (!ClassPath.StartsWith(TEXT("/Script/")) && !ClassPath.StartsWith(TEXT("/Game/PropHunt/")))
        {
            return MakeErrorResponse(TEXT("path_denied"), ClassPath);
        }
        UClass* Class = LoadObject<UClass>(nullptr, *ClassPath);
        if (!Class || !Class->IsChildOf(AActor::StaticClass()))
        {
            return MakeErrorResponse(TEXT("class_not_found"), ClassPath);
        }
        ClassFilters.Add(Class);
    }

    UWorld* World = nullptr;
    FString AuditedPath;
    const FString RequestedMap = PackagePath(MapPath);
    if (RequestedMap.IsEmpty())
    {
        World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        AuditedPath = World ? World->GetOutermost()->GetName() : FString();
    }
    else
    {
        if (!AllowedMaps().Contains(RequestedMap))
        {
            return MakeErrorResponse(TEXT("path_denied"), RequestedMap);
        }
        UPackage* Package = LoadPackage(nullptr, *RequestedMap, LOAD_None);
        World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
        AuditedPath = RequestedMap;
    }
    if (!World || !World->PersistentLevel)
    {
        return MakeErrorResponse(TEXT("level_not_found"), AuditedPath);
    }
    if (World->GetWorldPartition())
    {
        return MakeErrorResponse(TEXT("world_partition_unsupported"), AuditedPath);
    }

    TMap<FString, int32> CountsByClass;
    TMap<FString, int32> CountsByFolder;
    TArray<TSharedPtr<FJsonValue>> Actors;
    int32 ActorCount = 0;
    int32 MatchingActorCount = 0;
    int32 LightCount = 0;
    int32 InactiveLightCount = 0;
    int32 DirectionalCount = 0;
    int32 PointCount = 0;
    int32 SpotCount = 0;
    int32 RectCount = 0;
    int32 SkyCount = 0;

    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (!IsValid(Actor))
        {
            continue;
        }
        if (++ActorCount > MaxActorsScanned)
        {
            return MakeErrorResponse(TEXT("too_many_results"), TEXT("The level contains more than 10000 actors."));
        }
        CountsByClass.FindOrAdd(Actor->GetClass()->GetPathName())++;

        TInlineComponentArray<ULightComponentBase*> Lights;
        Actor->GetComponents(Lights);
        for (ULightComponentBase* Light : Lights)
        {
            if (!Light)
            {
                continue;
            }
            if (!Light->IsVisible() || !Light->bAffectsWorld)
            {
                ++InactiveLightCount;
                continue;
            }
            ++LightCount;
            DirectionalCount += Light->IsA<UDirectionalLightComponent>();
            PointCount += Light->IsA<UPointLightComponent>();
            SpotCount += Light->IsA<USpotLightComponent>();
            RectCount += Light->IsA<URectLightComponent>();
            SkyCount += Light->IsA<USkyLightComponent>();
        }

        bool bClassMatches = ClassFilters.Num() == 0;
        for (UClass* Filter : ClassFilters)
        {
            bClassMatches |= Actor->IsA(Filter);
        }
        const FString Folder = Actor->GetFolderPath().ToString();
        CountsByFolder.FindOrAdd(Folder)++;
        bool bFolderMatches = FolderFilters.Num() == 0;
        for (const FString& Filter : FolderFilters)
        {
            bFolderMatches |= Folder.StartsWith(Filter);
        }
        if (!bClassMatches || !bFolderMatches)
        {
            continue;
        }
        ++MatchingActorCount;
        if (bIncludeActors && Actors.Num() < MaxResults)
        {
            TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("path"), Actor->GetPathName());
            Item->SetStringField(TEXT("label"), Actor->GetActorLabel());
            Item->SetStringField(TEXT("class"), Actor->GetClass()->GetPathName());
            Item->SetStringField(TEXT("folder"), Folder);
            Item->SetObjectField(TEXT("transform"), TransformJson(Actor->GetActorTransform()));
            Actors.Add(MakeShared<FJsonValueObject>(Item));
        }
    }

    const bool bHasWorldSettings = World->GetWorldSettings() != nullptr;
    TArray<TSharedPtr<FJsonValue>> MissingFlags;
    for (const FString& Flag : RequiredFlags)
    {
        if ((Flag == TEXT("has_light") && LightCount == 0)
            || (Flag == TEXT("has_sky_light") && SkyCount == 0)
            || (Flag == TEXT("has_world_settings") && !bHasWorldSettings))
        {
            MissingFlags.Add(MakeShared<FJsonValueString>(Flag));
        }
    }

    TArray<TSharedPtr<FJsonValue>> ClassCounts;
    for (const TPair<FString, int32>& Pair : CountsByClass)
    {
        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("class_path"), Pair.Key);
        Item->SetNumberField(TEXT("count"), Pair.Value);
        ClassCounts.Add(MakeShared<FJsonValueObject>(Item));
    }
    TArray<TSharedPtr<FJsonValue>> FolderCounts;
    for (const TPair<FString, int32>& Pair : CountsByFolder)
    {
        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("folder"), Pair.Key);
        Item->SetNumberField(TEXT("count"), Pair.Value);
        FolderCounts.Add(MakeShared<FJsonValueObject>(Item));
    }
    TSharedRef<FJsonObject> Lights = MakeShared<FJsonObject>();
    Lights->SetNumberField(TEXT("active"), LightCount);
    Lights->SetNumberField(TEXT("inactive"), InactiveLightCount);
    Lights->SetNumberField(TEXT("directional"), DirectionalCount);
    Lights->SetNumberField(TEXT("point"), PointCount);
    Lights->SetNumberField(TEXT("spot"), SpotCount);
    Lights->SetNumberField(TEXT("rect"), RectCount);
    Lights->SetNumberField(TEXT("sky"), SkyCount);

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("map_path"), AuditedPath);
    Result->SetBoolField(TEXT("dirty"), World->GetOutermost()->IsDirty());
    Result->SetNumberField(TEXT("actor_count"), ActorCount);
    Result->SetArrayField(TEXT("class_counts"), ClassCounts);
    Result->SetArrayField(TEXT("folder_counts"), FolderCounts);
    Result->SetObjectField(TEXT("lights"), Lights);
    TSharedRef<FJsonObject> WorldSettings = MakeShared<FJsonObject>();
    WorldSettings->SetBoolField(TEXT("present"), bHasWorldSettings);
    WorldSettings->SetStringField(TEXT("class_path"), bHasWorldSettings
        ? World->GetWorldSettings()->GetClass()->GetPathName() : TEXT(""));
    Result->SetObjectField(TEXT("world_settings"), WorldSettings);
    Result->SetArrayField(TEXT("missing_flags"), MissingFlags);
    if (bIncludeActors)
    {
        Result->SetArrayField(TEXT("actors"), Actors);
    }
    Result->SetBoolField(TEXT("truncated"), bIncludeActors && MatchingActorCount > MaxResults);
    return CompactJson(Result);
}
