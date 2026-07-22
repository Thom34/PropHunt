#include "ThomasEditorWorldProvider.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Editor.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/Light.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/RendererSettings.h"
#include "Engine/Selection.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "FoliageType.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "ObjectTools.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionConverter.h"

namespace
{
constexpr int32 MaxActorResults = 500;
constexpr int32 MaxWorldOperations = 50;
constexpr int32 MaxPostProcessSettings = 500;
constexpr int32 MaxPostProcessOperations = 100;
constexpr int32 MaxLandscapeRegionSamples = 257 * 257;
constexpr int32 MaxLandscapeReturnedSamples = 4096;
constexpr double PlanLifetimeSeconds = 300.0;

#if WITH_DEV_AUTOMATION_TESTS
int32 GRendererConfigSaveFailuresRemaining = 0;
#endif

struct FWorldPatchPlan
{
    FThomasWorldPatchRequest Request;
    double CreatedAtSeconds = 0.0;
};

struct FPostProcessPatchPlan
{
    FThomasPostProcessPatchRequest Request;
    double CreatedAtSeconds = 0.0;
};

struct FMapLifecyclePlan
{
    FThomasMapLifecycleRequest Request;
    double CreatedAtSeconds = 0.0;
};

struct FEnvironmentBuildPlan
{
    FThomasEnvironmentBuildRequest Request;
    double CreatedAtSeconds = 0.0;
};

struct FLandscapeLayerInfoCreatePlan
{
    FThomasLandscapeLayerInfoCreateRequest Request;
    double CreatedAtSeconds = 0.0;
};

struct FEnvironmentBuildJob
{
    FThomasEnvironmentBuildRequest Request;
    FString JobId;
    FString Status = TEXT("running");
    FString Code;
    FString Message;
    FString LogPath;
    FString StartedAtUtc;
    FString FinishedAtUtc;
    FString HostMapPath;
    FString HostRevision;
    FProcHandle ProcessHandle;
    uint32 ProcessId = 0;
    int32 ExitCode = INDEX_NONE;
    bool bProcessRunning = true;
    bool bMapReloaded = false;
    bool bCancelled = false;
};

template <typename TResult>
TResult MakeError(const FString& Code, const FString& Message)
{
    TResult Result;
    Result.bOk = false;
    Result.Code = Code;
    Result.Message = Message.Left(1200);
    return Result;
}

UWorld* GetProjectEditorWorld()
{
    if (!GEditor)
    {
        return nullptr;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World || World->WorldType != EWorldType::Editor)
    {
        return nullptr;
    }
    const FString PackageName = World->GetOutermost()->GetName();
    return (PackageName == TEXT("/Game/PropHunt")
        || PackageName.StartsWith(TEXT("/Game/PropHunt/")))
        ? World
        : nullptr;
}

FString BuildWorldRevision(const UWorld* World)
{
    if (!World)
    {
        return FString();
    }
    const UPackage* Package = World->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetMapPackageExtension());
    int32 FoliageTypeCount = 0;
    int32 FoliageInstanceCount = 0;
    for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
    {
        for (const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& Pair
            : It->GetFoliageInfos())
        {
            ++FoliageTypeCount;
            FoliageInstanceCount += Pair.Value->Instances.Num();
        }
    }
    uint32 LandscapeHash = 0;
    for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
    {
        const ALandscapeProxy* Landscape = *It;
        const FString Signature = FString::Printf(
            TEXT("%s|%s|%s|%d|%d|%d|%d|%g|%d|%g|%d|%d|%d"),
            *Landscape->GetPathName(),
            Landscape->GetLandscapeMaterial()
                ? *Landscape->GetLandscapeMaterial()->GetPathName() : TEXT(""),
            Landscape->GetLandscapeHoleMaterial()
                ? *Landscape->GetLandscapeHoleMaterial()->GetPathName() : TEXT(""),
            Landscape->IsNaniteEnabled() ? 1 : 0,
            Landscape->GetNaniteLODIndex(),
            Landscape->IsNaniteSkirtEnabled() ? 1 : 0,
            Landscape->GetNanitePositionPrecision(),
            Landscape->GetNaniteSkirtDepth(),
            Landscape->bUsedForNavigation ? 1 : 0,
            Landscape->GetNaniteMaxEdgeLengthFactor(),
            Landscape->bFillCollisionUnderLandscapeForNavmesh ? 1 : 0,
            Landscape->bUseDynamicMaterialInstance ? 1 : 0,
            Cast<ALandscape>(Landscape)
                ? CastChecked<ALandscape>(Landscape)->GetEditLayersConst().Num() : 0);
        LandscapeHash = HashCombine(LandscapeHash, GetTypeHash(Signature));
        if (const ALandscape* MainLandscape = Cast<ALandscape>(Landscape))
        {
            for (const ULandscapeEditLayerBase* Layer : MainLandscape->GetEditLayersConst())
            {
                if (Layer)
                {
                    LandscapeHash = HashCombine(
                        LandscapeHash,
                        GetTypeHash(FString::Printf(
                            TEXT("%s|%s|%d|%d|%g|%g"),
                            *Layer->GetGuid().ToString(EGuidFormats::Digits),
                            *Layer->GetName().ToString(),
                            Layer->IsVisible() ? 1 : 0,
                            Layer->IsLocked() ? 1 : 0,
                            Layer->GetAlphaForTargetType(ELandscapeToolTargetType::Heightmap),
                            Layer->GetAlphaForTargetType(ELandscapeToolTargetType::Weightmap))));
                }
            }
        }
    }
    uint32 DataLayerHash = 0;
    if (UDataLayerEditorSubsystem* DataLayerSubsystem =
            UDataLayerEditorSubsystem::Get())
    {
        TArray<UDataLayerInstance*> DataLayers = DataLayerSubsystem->GetAllDataLayers();
        DataLayers.Sort(
            [](const UDataLayerInstance& A, const UDataLayerInstance& B)
            {
                return A.GetPathName() < B.GetPathName();
            });
        for (const UDataLayerInstance* DataLayer : DataLayers)
        {
            if (!DataLayer || DataLayer->GetWorld() != World)
            {
                continue;
            }
            const FString Signature = FString::Printf(
                TEXT("%s|%s|%d|%d|%d|%s|%s"),
                *DataLayer->GetPathName(),
                *DataLayer->GetDataLayerShortName(),
                DataLayer->IsVisible() ? 1 : 0,
                DataLayer->IsLoadedInEditor() ? 1 : 0,
                DataLayer->IsInitiallyVisible() ? 1 : 0,
                *UEnum::GetValueAsString(DataLayer->GetInitialRuntimeState()),
                DataLayer->GetParent() ? *DataLayer->GetParent()->GetPathName() : TEXT(""));
            DataLayerHash = HashCombine(DataLayerHash, GetTypeHash(Signature));
        }
        uint32 MembershipHash = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            for (const UDataLayerInstance* DataLayer : It->GetDataLayerInstances())
            {
                if (DataLayer)
                {
                    MembershipHash ^= GetTypeHash(
                        It->GetPathName() + TEXT("|") + DataLayer->GetPathName());
                }
            }
        }
        DataLayerHash = HashCombine(DataLayerHash, MembershipHash);
    }
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d:%d:%d:%d:%u:%u"),
        *Package->GetName(),
        IFileManager::Get().FileSize(*Filename),
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package->IsDirty() ? 1 : 0,
        World->PersistentLevel ? World->PersistentLevel->Actors.Num() : 0,
        FoliageTypeCount,
        FoliageInstanceCount,
        LandscapeHash,
        DataLayerHash);
}

bool IsAllowedProjectMapPath(const FString& MapPath, FString* OutReason = nullptr)
{
    FText Reason;
    const bool bValid = FPackageName::IsValidLongPackageName(
        MapPath, false, &Reason)
        && (MapPath == TEXT("/Game/PropHunt")
            || MapPath.StartsWith(TEXT("/Game/PropHunt/")))
        && !MapPath.Contains(TEXT("/Developers/"));
    if (!bValid && OutReason)
    {
        *OutReason = Reason.IsEmpty()
            ? TEXT("Map path must be a valid /Game/PropHunt package path outside Developers.")
            : Reason.ToString();
    }
    return bValid;
}

FString NormalizeLandscapeLayerInfoPackagePath(const FString& Input)
{
    FString Result = Input.TrimStartAndEnd();
    if (Result.Contains(TEXT(".")))
    {
        Result = FPackageName::ObjectPathToPackageName(Result);
    }
    return Result;
}

bool IsAllowedLandscapeLayerInfoPackagePath(
    const FString& PackagePath,
    FString* OutReason = nullptr)
{
    FText Reason;
    const bool bValid = FPackageName::IsValidLongPackageName(
        PackagePath, false, &Reason)
        && PackagePath.StartsWith(TEXT("/Game/PropHunt/"))
        && !PackagePath.Contains(TEXT("/Developers/"));
    if (!bValid && OutReason)
    {
        *OutReason = Reason.IsEmpty()
            ? TEXT("LayerInfo path must be a valid /Game/PropHunt package path outside Developers.")
            : Reason.ToString();
    }
    return bValid;
}

FString LandscapeLayerInfoObjectPath(const FString& PackagePath)
{
    return PackagePath + TEXT(".")
        + FPackageName::GetLongPackageAssetName(PackagePath);
}

ULandscapeLayerInfoObject* LoadLandscapeLayerInfoAsset(const FString& PackagePath)
{
    const FString ObjectPath = LandscapeLayerInfoObjectPath(PackagePath);
    if (ULandscapeLayerInfoObject* Existing =
            FindObject<ULandscapeLayerInfoObject>(nullptr, *ObjectPath))
    {
        return Existing;
    }
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    if (IFileManager::Get().FileSize(*Filename) < 0)
    {
        return nullptr;
    }
    return LoadObject<ULandscapeLayerInfoObject>(nullptr, *ObjectPath);
}

FString BuildLandscapeLayerInfoRevision(
    const FString& PackagePath,
    const ULandscapeLayerInfoObject* LayerInfo)
{
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    const int64 FileSize = IFileManager::Get().FileSize(*Filename);
    if (!LayerInfo && FileSize < 0)
    {
        return TEXT("missing");
    }
    const UPackage* Package = LayerInfo ? LayerInfo->GetOutermost() : nullptr;
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d:%s:%d"),
        *PackagePath,
        FileSize,
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package && Package->IsDirty() ? 1 : 0,
        LayerInfo ? *LayerInfo->GetLayerName().ToString() : TEXT("unloaded"),
        LayerInfo
            && LayerInfo->GetBlendMethod() == ELandscapeTargetLayerBlendMethod::None
                ? 1 : 0);
}

FString MapFilename(const FString& MapPath)
{
    return FPackageName::LongPackageNameToFilename(
        MapPath, FPackageName::GetMapPackageExtension());
}

FString EnvironmentBuilderClass(const FString& Action)
{
    return Action == TEXT("build_navigation")
        ? TEXT("WorldPartitionNavigationDataBuilder")
        : TEXT("WorldPartitionHLODsBuilder");
}

FString BuildEnvironmentCommandLine(
    const FThomasEnvironmentBuildRequest& Request,
    const FString& AbsoluteLogPath)
{
    const FString ProjectFile = FPaths::ConvertRelativePathToFull(
        FPaths::GetProjectFilePath());
    FString ActionArguments;
    if (Request.Action == TEXT("build_navigation"))
    {
        ActionArguments = FString::Printf(
            TEXT("-AllowCommandletRendering%s%s"),
            Request.bVerbose ? TEXT(" -Verbose") : TEXT(""),
            Request.bCleanPackages ? TEXT(" -CleanPackages") : TEXT(""));
    }
    else if (Request.Action == TEXT("build_hlods"))
    {
        ActionArguments = TEXT("-SetupHLODs -BuildHLODs -AllowCommandletRendering");
    }
    else
    {
        ActionArguments = TEXT("-DeleteHLODs");
    }
    return FString::Printf(
        TEXT("\"%s\" -run=WorldPartitionBuilderCommandlet %s -Builder=%s %s -SCCProvider=None -nop4 -unattended -nosplash -ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False -abslog=\"%s\""),
        *ProjectFile,
        *Request.MapPath,
        *EnvironmentBuilderClass(Request.Action),
        *ActionArguments,
        *AbsoluteLogPath);
}

FThomasEnvironmentBuildJobResult MakeEnvironmentBuildJobResult(
    const FEnvironmentBuildJob& Job)
{
    FThomasEnvironmentBuildJobResult Result;
    Result.bOk = Job.Status == TEXT("running")
        || Job.Status == TEXT("succeeded")
        || Job.Status == TEXT("cancelled");
    Result.Code = Job.Code;
    Result.Message = Job.Message;
    Result.JobId = Job.JobId;
    Result.Action = Job.Request.Action;
    Result.MapPath = Job.Request.MapPath;
    Result.Status = Job.Status;
    Result.LogPath = Job.LogPath;
    Result.StartedAtUtc = Job.StartedAtUtc;
    Result.FinishedAtUtc = Job.FinishedAtUtc;
    Result.ProcessId = static_cast<int32>(Job.ProcessId);
    Result.ExitCode = Job.ExitCode;
    Result.bProcessRunning = Job.bProcessRunning;
    Result.bMapReloaded = Job.bMapReloaded;
    Result.bCancelled = Job.bCancelled;
    if (Job.bProcessRunning)
    {
        Result.Diagnostics.Add(TEXT("Poll GetEnvironmentBuildJob; the editor remains responsive."));
    }
    if (!Job.LogPath.IsEmpty())
    {
        Result.Diagnostics.Add(TEXT("Builder log: ") + Job.LogPath);
    }
    return Result;
}

FTransform MakeOperationTransform(const FThomasWorldOperation& Operation)
{
    return FTransform(
        FRotator(
            Operation.RotationPitch,
            Operation.RotationYaw,
            Operation.RotationRoll),
        FVector(Operation.LocationX, Operation.LocationY, Operation.LocationZ),
        FVector(Operation.ScaleX, Operation.ScaleY, Operation.ScaleZ));
}

bool IsValidOperationTransform(const FThomasWorldOperation& Operation)
{
    const FTransform Transform = MakeOperationTransform(Operation);
    return !Transform.ContainsNaN()
        && !Transform.GetScale3D().IsNearlyZero();
}

bool IsValidLandscapeCreateOperation(
    const FThomasWorldOperation& Operation,
    int32& OutSizeX,
    int32& OutSizeY)
{
    static const TSet<int32> ValidQuadsPerSection = {
        7, 15, 31, 63, 127, 255
    };
    if (!IsValidOperationTransform(Operation)
        || Operation.LandscapeComponentCountX < 1
        || Operation.LandscapeComponentCountX > 32
        || Operation.LandscapeComponentCountY < 1
        || Operation.LandscapeComponentCountY > 32
        || (Operation.LandscapeSectionsPerComponent != 1
            && Operation.LandscapeSectionsPerComponent != 2)
        || !ValidQuadsPerSection.Contains(Operation.LandscapeQuadsPerSection)
        || Operation.LandscapeHeightValue < 0
        || Operation.LandscapeHeightValue > MAX_uint16)
    {
        return false;
    }
    const int32 QuadsPerComponent =
        Operation.LandscapeSectionsPerComponent * Operation.LandscapeQuadsPerSection;
    OutSizeX = Operation.LandscapeComponentCountX * QuadsPerComponent + 1;
    OutSizeY = Operation.LandscapeComponentCountY * QuadsPerComponent + 1;
    return OutSizeX <= 4096 && OutSizeY <= 4096
        && static_cast<int64>(OutSizeX) * OutSizeY <= 16ll * 1024ll * 1024ll;
}

bool ResolveFoliageTarget(
    UWorld* World,
    const FThomasWorldOperation& Operation,
    AInstancedFoliageActor*& OutActor,
    UFoliageType*& OutType,
    FFoliageInfo*& OutInfo)
{
    AInstancedFoliageActor* Candidate =
        FindObject<AInstancedFoliageActor>(nullptr, *Operation.ActorPath);
    OutActor = Candidate && Candidate->GetWorld() == World ? Candidate : nullptr;
    OutType = FindObject<UFoliageType>(nullptr, *Operation.AssetPath);
    OutInfo = OutActor && OutType ? OutActor->FindInfo(OutType) : nullptr;
    return OutActor && OutType && OutInfo;
}

UDataLayerInstance* ResolveDataLayer(UWorld* World, const FString& ObjectPath)
{
    UDataLayerInstance* Candidate =
        FindObject<UDataLayerInstance>(nullptr, *ObjectPath);
    if (!Candidate || Candidate->GetWorld() != World)
    {
        return nullptr;
    }
    UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get();
    return Subsystem && Subsystem->GetAllDataLayers().Contains(Candidate)
        ? Candidate : nullptr;
}

FString BoolText(const bool bValue)
{
    return bValue ? TEXT("true") : TEXT("false");
}

bool ParseBoolStrict(const FString& Value, bool& bOutValue)
{
    if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1"))
    {
        bOutValue = true;
        return true;
    }
    if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Value == TEXT("0"))
    {
        bOutValue = false;
        return true;
    }
    return false;
}

FString DataLayerPropertyValue(
    const UDataLayerInstance* DataLayer,
    const FString& PropertyName)
{
    if (!DataLayer) return FString();
    if (PropertyName.Equals(TEXT("ShortName"), ESearchCase::IgnoreCase))
        return DataLayer->GetDataLayerShortName();
    if (PropertyName.Equals(TEXT("Visible"), ESearchCase::IgnoreCase))
        return BoolText(DataLayer->IsVisible());
    if (PropertyName.Equals(TEXT("LoadedInEditor"), ESearchCase::IgnoreCase))
        return BoolText(DataLayer->IsLoadedInEditor());
    if (PropertyName.Equals(TEXT("InitiallyVisible"), ESearchCase::IgnoreCase))
        return BoolText(DataLayer->IsInitiallyVisible());
    if (PropertyName.Equals(TEXT("InitialRuntimeState"), ESearchCase::IgnoreCase))
        return UEnum::GetValueAsString(DataLayer->GetInitialRuntimeState());
    if (PropertyName.Equals(TEXT("Parent"), ESearchCase::IgnoreCase))
        return DataLayer->GetParent() ? DataLayer->GetParent()->GetPathName() : FString();
    return FString();
}

int32 CountDataLayerActors(UWorld* World, const UDataLayerInstance* DataLayer)
{
    int32 Count = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetDataLayerInstances().Contains(DataLayer))
        {
            ++Count;
        }
    }
    return Count;
}

AActor* ResolveActor(UWorld* World, const FString& ActorPath);
bool IsSupportedActorProperty(const FProperty* Property);

ALandscapeProxy* ResolveLandscape(UWorld* World, const FString& ActorPath)
{
    return Cast<ALandscapeProxy>(ResolveActor(World, ActorPath));
}

ULandscapeEditLayerBase* ResolveLandscapeEditLayer(
    ALandscape* Landscape,
    const FString& GuidText,
    int32* OutIndex = nullptr)
{
    FGuid Guid;
    if (!Landscape || !FGuid::Parse(GuidText, Guid))
    {
        return nullptr;
    }
    const int32 Index = Landscape->GetLayerIndex(Guid);
    if (OutIndex)
    {
        *OutIndex = Index;
    }
    return Index != INDEX_NONE ? Landscape->GetEditLayer(Index) : nullptr;
}

ULandscapeLayerInfoObject* ResolveLandscapeLayerInfo(const FString& ObjectPath)
{
    if (ObjectPath.IsEmpty())
    {
        return nullptr;
    }
    const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
    const bool bAllowedProjectPath = PackageName == TEXT("/Game/PropHunt")
        || PackageName.StartsWith(TEXT("/Game/PropHunt/"));
    const bool bAllowedEngineVisibilityLayer = ObjectPath
        == TEXT("/Engine/EngineResources/LandscapeVisibilityLayerInfo.LandscapeVisibilityLayerInfo");
    if ((!bAllowedProjectPath && !bAllowedEngineVisibilityLayer)
        || PackageName.Contains(TEXT("/Developers/")))
    {
        return nullptr;
    }
    if (ULandscapeLayerInfoObject* Existing =
            FindObject<ULandscapeLayerInfoObject>(nullptr, *ObjectPath))
    {
        return Existing;
    }
    return LoadObject<ULandscapeLayerInfoObject>(nullptr, *ObjectPath);
}

bool ResolveLandscapeDataTarget(
    UWorld* World,
    const FString& ActorPath,
    const FString& EditLayerGuidText,
    const bool bRequireWritableLayer,
    ALandscapeProxy*& OutLandscape,
    ULandscapeInfo*& OutInfo,
    FGuid& OutEditLayerGuid,
    FString& OutReason)
{
    OutLandscape = ResolveLandscape(World, ActorPath);
    OutInfo = OutLandscape ? OutLandscape->GetLandscapeInfo() : nullptr;
    OutEditLayerGuid.Invalidate();
    if (!OutLandscape || !OutInfo)
    {
        OutReason = TEXT("Landscape actor or LandscapeInfo was not found in the open map.");
        return false;
    }
    if (EditLayerGuidText.IsEmpty())
    {
        return true;
    }
    ALandscape* MainLandscape = OutInfo->LandscapeActor.Get();
    if (!MainLandscape
        || !FGuid::Parse(EditLayerGuidText, OutEditLayerGuid))
    {
        OutReason = TEXT("The edit-layer GUID is invalid for this Landscape.");
        return false;
    }
    ULandscapeEditLayerBase* EditLayer = MainLandscape->GetEditLayer(OutEditLayerGuid);
    if (!EditLayer)
    {
        OutReason = TEXT("The edit-layer GUID does not belong to this Landscape.");
        return false;
    }
    if (bRequireWritableLayer && EditLayer->IsLocked())
    {
        OutReason = TEXT("The requested Landscape edit layer is locked.");
        return false;
    }
    return true;
}

bool ValidateLandscapeRegion(
    ULandscapeInfo* LandscapeInfo,
    const int32 MinX,
    const int32 MinY,
    const int32 MaxX,
    const int32 MaxY,
    int32& OutWidth,
    int32& OutHeight,
    FString& OutReason)
{
    FIntRect Extent;
    if (!LandscapeInfo || !LandscapeInfo->GetLandscapeExtent(Extent))
    {
        OutReason = TEXT("Landscape extent is unavailable.");
        return false;
    }
    const int64 Width = static_cast<int64>(MaxX) - MinX + 1;
    const int64 Height = static_cast<int64>(MaxY) - MinY + 1;
    if (Width < 1 || Height < 1
        || MinX < Extent.Min.X || MinY < Extent.Min.Y
        || MaxX > Extent.Max.X || MaxY > Extent.Max.Y
        || Width * Height > MaxLandscapeRegionSamples)
    {
        OutReason = FString::Printf(
            TEXT("Region [%d,%d]-[%d,%d] must stay inside [%d,%d]-[%d,%d] and contain at most %d vertices."),
            MinX,
            MinY,
            MaxX,
            MaxY,
            Extent.Min.X,
            Extent.Min.Y,
            Extent.Max.X,
            Extent.Max.Y,
            MaxLandscapeRegionSamples);
        return false;
    }
    OutWidth = static_cast<int32>(Width);
    OutHeight = static_cast<int32>(Height);
    return true;
}

FString LandscapeDataHash(const void* Data, const int64 ByteCount)
{
    if (!Data || ByteCount <= 0 || ByteCount > MAX_int32)
    {
        return FString();
    }
    return FString::Printf(
        TEXT("%08X"),
        FCrc::MemCrc32(Data, static_cast<int32>(ByteCount)));
}

bool ReadLandscapeRegionData(
    ULandscapeInfo* LandscapeInfo,
    const FGuid& EditLayerGuid,
    ULandscapeLayerInfoObject* LayerInfo,
    const int32 MinX,
    const int32 MinY,
    const int32 MaxX,
    const int32 MaxY,
    TArray<uint16>& OutHeights,
    TArray<uint8>& OutWeights,
    FString& OutReason)
{
    int32 Width = 0;
    int32 Height = 0;
    if (!ValidateLandscapeRegion(
            LandscapeInfo, MinX, MinY, MaxX, MaxY, Width, Height, OutReason))
    {
        return false;
    }
    const int32 Count = Width * Height;
    OutHeights.Init(0, Count);
    {
        FLandscapeEditDataInterface LandscapeEdit(
            LandscapeInfo, EditLayerGuid, false);
        LandscapeEdit.GetHeightDataFast(
            MinX,
            MinY,
            MaxX,
            MaxY,
            OutHeights.GetData(),
            Width,
            nullptr,
            nullptr);
    }
    OutWeights.Reset();
    if (LayerInfo)
    {
        OutWeights.Init(0, Count);
        TAlphamapAccessor<false> AlphamapAccessor(
            LandscapeInfo, LayerInfo);
        AlphamapAccessor.SetEditLayer(EditLayerGuid);
        AlphamapAccessor.GetDataFast(
            MinX,
            MinY,
            MaxX,
            MaxY,
            OutWeights.GetData());
    }
    return true;
}

bool ResolveLandscapeImportFile(
    const FString& RelativeSourceFile,
    FString& OutAbsolutePath)
{
    if (RelativeSourceFile.IsEmpty() || !FPaths::IsRelative(RelativeSourceFile))
    {
        return false;
    }
    FString NormalizedRelative = RelativeSourceFile;
    FPaths::NormalizeFilename(NormalizedRelative);
    if (NormalizedRelative.StartsWith(TEXT("/"))
        || NormalizedRelative.Contains(TEXT(".."))
        || NormalizedRelative.Contains(TEXT(":")))
    {
        return false;
    }
    FString Root = FPaths::ConvertRelativePathToFull(
        FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("ThomasEditor/Imports/Landscape")));
    FPaths::NormalizeDirectoryName(Root);
    OutAbsolutePath = FPaths::ConvertRelativePathToFull(
        FPaths::Combine(Root, NormalizedRelative));
    FPaths::NormalizeFilename(OutAbsolutePath);
    return OutAbsolutePath.StartsWith(
        Root + TEXT("/"), ESearchCase::IgnoreCase);
}

bool LoadLandscapeRawValues(
    const FThomasWorldOperation& Operation,
    const bool bHeightData,
    const int32 ExpectedCount,
    TArray<int32>& OutValues,
    FString& OutReason)
{
    FString SourcePath;
    if (!ResolveLandscapeImportFile(Operation.SourceFile, SourcePath))
    {
        OutReason = TEXT("SourceFile must be relative to Saved/ThomasEditor/Imports/Landscape.");
        return false;
    }
    const FString Extension = FPaths::GetExtension(SourcePath, true).ToLower();
    if ((bHeightData && Extension != TEXT(".r16"))
        || (!bHeightData && Extension != TEXT(".r8")))
    {
        OutReason = bHeightData
            ? TEXT("Height imports require a little-endian .r16 file.")
            : TEXT("Weight imports require an .r8 file.");
        return false;
    }
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *SourcePath))
    {
        OutReason = TEXT("The guarded Landscape import file could not be read.");
        return false;
    }
    const int32 BytesPerValue = bHeightData ? 2 : 1;
    if (Bytes.Num() != ExpectedCount * BytesPerValue)
    {
        OutReason = FString::Printf(
            TEXT("Import byte count %d does not match the exact region (%d expected)."),
            Bytes.Num(),
            ExpectedCount * BytesPerValue);
        return false;
    }
    OutValues.SetNumUninitialized(ExpectedCount);
    for (int32 Index = 0; Index < ExpectedCount; ++Index)
    {
        OutValues[Index] = bHeightData
            ? static_cast<int32>(Bytes[Index * 2])
                | (static_cast<int32>(Bytes[Index * 2 + 1]) << 8)
            : static_cast<int32>(Bytes[Index]);
    }
    return true;
}

struct FPreparedLandscapeDataOperation
{
    ALandscapeProxy* Landscape = nullptr;
    ULandscapeInfo* LandscapeInfo = nullptr;
    ULandscapeLayerInfoObject* LayerInfo = nullptr;
    FGuid EditLayerGuid;
    int32 MinX = 0;
    int32 MinY = 0;
    int32 MaxX = 0;
    int32 MaxY = 0;
    int32 Width = 0;
    int32 Height = 0;
    bool bHeightOperation = false;
    TArray<uint16> CurrentHeights;
    TArray<uint8> CurrentWeights;
    TArray<uint16> NewHeights;
    TArray<uint8> NewWeights;
    FString CurrentHash;
    FString NewHash;
};

bool IsLandscapeDataAction(const FString& Action)
{
    return Action == TEXT("set_landscape_height_region")
        || Action == TEXT("import_landscape_height_r16")
        || Action == TEXT("sculpt_landscape_height")
        || Action == TEXT("set_landscape_weight_region")
        || Action == TEXT("import_landscape_weight_r8")
        || Action == TEXT("paint_landscape_weight");
}

bool IsLandscapeHeightAction(const FString& Action)
{
    return Action == TEXT("set_landscape_height_region")
        || Action == TEXT("import_landscape_height_r16")
        || Action == TEXT("sculpt_landscape_height");
}

bool PrepareLandscapeDataOperation(
    UWorld* World,
    const FThomasWorldOperation& Operation,
    FPreparedLandscapeDataOperation& OutPrepared,
    FString& OutReason)
{
    const FString Action = Operation.Action.ToLower();
    if (!IsLandscapeDataAction(Action))
    {
        OutReason = TEXT("Unsupported Landscape data operation.");
        return false;
    }
    if (!ResolveLandscapeDataTarget(
            World,
            Operation.ActorPath,
            Operation.LandscapeEditLayerGuid,
            true,
            OutPrepared.Landscape,
            OutPrepared.LandscapeInfo,
            OutPrepared.EditLayerGuid,
            OutReason))
    {
        return false;
    }
    OutPrepared.bHeightOperation = IsLandscapeHeightAction(Action);
    if (!OutPrepared.bHeightOperation)
    {
        OutPrepared.LayerInfo = ResolveLandscapeLayerInfo(Operation.AssetPath);
        if (!OutPrepared.LayerInfo)
        {
            OutReason = TEXT("Weight operations require an allowed LandscapeLayerInfoObject path.");
            return false;
        }
    }

    const bool bBrushAction = Action == TEXT("sculpt_landscape_height")
        || Action == TEXT("paint_landscape_weight");
    if (bBrushAction)
    {
        if (Operation.LandscapeBrushRadius < 1
            || Operation.LandscapeBrushRadius > 256
            || Operation.LandscapeBrushStrength == 0
            || !FMath::IsFinite(Operation.LandscapeBrushFalloff)
            || Operation.LandscapeBrushFalloff < 0.0f
            || Operation.LandscapeBrushFalloff > 1.0f
            || (OutPrepared.bHeightOperation
                && FMath::Abs(Operation.LandscapeBrushStrength) > 32768)
            || (!OutPrepared.bHeightOperation
                && FMath::Abs(Operation.LandscapeBrushStrength) > 255))
        {
            OutReason = TEXT("Brush radius must be 1..256, falloff 0..1, and signed strength must be non-zero and in range.");
            return false;
        }
        FIntRect Extent;
        if (!OutPrepared.LandscapeInfo->GetLandscapeExtent(Extent))
        {
            OutReason = TEXT("Landscape extent is unavailable.");
            return false;
        }
        OutPrepared.MinX = FMath::Max(
            Extent.Min.X,
            Operation.LandscapeBrushCenterX - Operation.LandscapeBrushRadius);
        OutPrepared.MinY = FMath::Max(
            Extent.Min.Y,
            Operation.LandscapeBrushCenterY - Operation.LandscapeBrushRadius);
        OutPrepared.MaxX = FMath::Min(
            Extent.Max.X,
            Operation.LandscapeBrushCenterX + Operation.LandscapeBrushRadius);
        OutPrepared.MaxY = FMath::Min(
            Extent.Max.Y,
            Operation.LandscapeBrushCenterY + Operation.LandscapeBrushRadius);
    }
    else
    {
        OutPrepared.MinX = Operation.LandscapeMinX;
        OutPrepared.MinY = Operation.LandscapeMinY;
        OutPrepared.MaxX = Operation.LandscapeMaxX;
        OutPrepared.MaxY = Operation.LandscapeMaxY;
    }
    if (!ValidateLandscapeRegion(
            OutPrepared.LandscapeInfo,
            OutPrepared.MinX,
            OutPrepared.MinY,
            OutPrepared.MaxX,
            OutPrepared.MaxY,
            OutPrepared.Width,
            OutPrepared.Height,
            OutReason))
    {
        return false;
    }
    if (!ReadLandscapeRegionData(
            OutPrepared.LandscapeInfo,
            OutPrepared.EditLayerGuid,
            OutPrepared.LayerInfo,
            OutPrepared.MinX,
            OutPrepared.MinY,
            OutPrepared.MaxX,
            OutPrepared.MaxY,
            OutPrepared.CurrentHeights,
            OutPrepared.CurrentWeights,
            OutReason))
    {
        return false;
    }
    OutPrepared.CurrentHash = OutPrepared.bHeightOperation
        ? LandscapeDataHash(
            OutPrepared.CurrentHeights.GetData(),
            static_cast<int64>(OutPrepared.CurrentHeights.Num()) * sizeof(uint16))
        : LandscapeDataHash(
            OutPrepared.CurrentWeights.GetData(),
            static_cast<int64>(OutPrepared.CurrentWeights.Num()) * sizeof(uint8));
    if (Operation.ExpectedValue.IsEmpty()
        || Operation.ExpectedValue != OutPrepared.CurrentHash)
    {
        OutReason = TEXT("data_hash_conflict|") + OutPrepared.CurrentHash;
        return false;
    }

    const int32 Count = OutPrepared.Width * OutPrepared.Height;
    TArray<int32> InputValues;
    if (Action == TEXT("import_landscape_height_r16")
        || Action == TEXT("import_landscape_weight_r8"))
    {
        if (!LoadLandscapeRawValues(
                Operation,
                OutPrepared.bHeightOperation,
                Count,
                InputValues,
                OutReason))
        {
            return false;
        }
    }
    else if (Action == TEXT("set_landscape_height_region")
        || Action == TEXT("set_landscape_weight_region"))
    {
        InputValues = Operation.LandscapeValues;
        if (InputValues.Num() != Count)
        {
            OutReason = FString::Printf(
                TEXT("LandscapeValues has %d entries; exact region requires %d."),
                InputValues.Num(),
                Count);
            return false;
        }
    }

    if (OutPrepared.bHeightOperation)
    {
        OutPrepared.NewHeights = OutPrepared.CurrentHeights;
        if (bBrushAction)
        {
            const double Radius = Operation.LandscapeBrushRadius;
            const double InnerRadius = Radius
                * (1.0 - Operation.LandscapeBrushFalloff);
            for (int32 Y = OutPrepared.MinY; Y <= OutPrepared.MaxY; ++Y)
            {
                for (int32 X = OutPrepared.MinX; X <= OutPrepared.MaxX; ++X)
                {
                    const double Distance = FVector2D::Distance(
                        FVector2D(X, Y),
                        FVector2D(
                            Operation.LandscapeBrushCenterX,
                            Operation.LandscapeBrushCenterY));
                    if (Distance > Radius)
                    {
                        continue;
                    }
                    double Influence = 1.0;
                    if (Operation.LandscapeBrushFalloff > 0.0f
                        && Distance > InnerRadius)
                    {
                        Influence = 1.0 - (Distance - InnerRadius)
                            / FMath::Max(UE_DOUBLE_SMALL_NUMBER, Radius - InnerRadius);
                    }
                    const int32 Index = (Y - OutPrepared.MinY) * OutPrepared.Width
                        + (X - OutPrepared.MinX);
                    const int32 Delta = FMath::RoundToInt(
                        Operation.LandscapeBrushStrength * Influence);
                    OutPrepared.NewHeights[Index] = static_cast<uint16>(FMath::Clamp(
                        static_cast<int32>(OutPrepared.CurrentHeights[Index]) + Delta,
                        0,
                        static_cast<int32>(MAX_uint16)));
                }
            }
        }
        else
        {
            for (int32 Index = 0; Index < Count; ++Index)
            {
                if (InputValues[Index] < 0 || InputValues[Index] > MAX_uint16)
                {
                    OutReason = FString::Printf(
                        TEXT("Height value %d at index %d is outside uint16."),
                        InputValues[Index],
                        Index);
                    return false;
                }
                OutPrepared.NewHeights[Index] = static_cast<uint16>(InputValues[Index]);
            }
        }
        OutPrepared.NewHash = LandscapeDataHash(
            OutPrepared.NewHeights.GetData(),
            static_cast<int64>(OutPrepared.NewHeights.Num()) * sizeof(uint16));
    }
    else
    {
        OutPrepared.NewWeights = OutPrepared.CurrentWeights;
        if (bBrushAction)
        {
            const double Radius = Operation.LandscapeBrushRadius;
            const double InnerRadius = Radius
                * (1.0 - Operation.LandscapeBrushFalloff);
            for (int32 Y = OutPrepared.MinY; Y <= OutPrepared.MaxY; ++Y)
            {
                for (int32 X = OutPrepared.MinX; X <= OutPrepared.MaxX; ++X)
                {
                    const double Distance = FVector2D::Distance(
                        FVector2D(X, Y),
                        FVector2D(
                            Operation.LandscapeBrushCenterX,
                            Operation.LandscapeBrushCenterY));
                    if (Distance > Radius)
                    {
                        continue;
                    }
                    double Influence = 1.0;
                    if (Operation.LandscapeBrushFalloff > 0.0f
                        && Distance > InnerRadius)
                    {
                        Influence = 1.0 - (Distance - InnerRadius)
                            / FMath::Max(UE_DOUBLE_SMALL_NUMBER, Radius - InnerRadius);
                    }
                    const int32 Index = (Y - OutPrepared.MinY) * OutPrepared.Width
                        + (X - OutPrepared.MinX);
                    const int32 Delta = FMath::RoundToInt(
                        Operation.LandscapeBrushStrength * Influence);
                    OutPrepared.NewWeights[Index] = static_cast<uint8>(FMath::Clamp(
                        static_cast<int32>(OutPrepared.CurrentWeights[Index]) + Delta,
                        0,
                        static_cast<int32>(MAX_uint8)));
                }
            }
        }
        else
        {
            for (int32 Index = 0; Index < Count; ++Index)
            {
                if (InputValues[Index] < 0 || InputValues[Index] > MAX_uint8)
                {
                    OutReason = FString::Printf(
                        TEXT("Weight value %d at index %d is outside uint8."),
                        InputValues[Index],
                        Index);
                    return false;
                }
                OutPrepared.NewWeights[Index] = static_cast<uint8>(InputValues[Index]);
            }
        }
        OutPrepared.NewHash = LandscapeDataHash(
            OutPrepared.NewWeights.GetData(),
            static_cast<int64>(OutPrepared.NewWeights.Num()) * sizeof(uint8));
    }
    if (OutPrepared.NewHash == OutPrepared.CurrentHash)
    {
        OutReason = TEXT("Landscape data operation would be a no-op.");
        return false;
    }
    return true;
}

bool ApplyLandscapeDataOperation(
    FPreparedLandscapeDataOperation& Prepared,
    FString& OutReason)
{
    if (!Prepared.Landscape || !Prepared.LandscapeInfo)
    {
        OutReason = TEXT("Landscape data target disappeared.");
        return false;
    }
    Prepared.Landscape->Modify();
    Prepared.LandscapeInfo->Modify();
    if (Prepared.LayerInfo)
    {
        Prepared.LandscapeInfo->CreateTargetLayerSettingsFor(Prepared.LayerInfo);
    }
    if (Prepared.bHeightOperation)
    {
        FLandscapeEditDataInterface LandscapeEdit(
            Prepared.LandscapeInfo,
            Prepared.EditLayerGuid,
            true);
            LandscapeEdit.SetHeightData(
                Prepared.MinX,
                Prepared.MinY,
                Prepared.MaxX,
                Prepared.MaxY,
                Prepared.NewHeights.GetData(),
                Prepared.Width,
                true,
                nullptr,
                nullptr,
                nullptr,
                false,
                nullptr,
                nullptr,
                true,
                true,
                true);
        LandscapeEdit.Flush();
    }
    else
    {
        TAlphamapAccessor<false> AlphamapAccessor(
            Prepared.LandscapeInfo, Prepared.LayerInfo);
        AlphamapAccessor.SetEditLayer(Prepared.EditLayerGuid);
        AlphamapAccessor.SetData(
            Prepared.MinX,
            Prepared.MinY,
            Prepared.MaxX,
            Prepared.MaxY,
            Prepared.NewWeights.GetData(),
            ELandscapeLayerPaintingRestriction::None);
        AlphamapAccessor.Flush();
    }
    Prepared.LandscapeInfo->DirtyRuntimeVirtualTextureForLandscapeArea(
        Prepared.MinX,
        Prepared.MinY,
        Prepared.MaxX,
        Prepared.MaxY);
    Prepared.Landscape->MarkComponentsRenderStateDirty();
    Prepared.Landscape->MarkPackageDirty();
    if (GEditor)
    {
        GEditor->RedrawLevelEditingViewports();
    }

    TArray<uint16> AppliedHeights;
    TArray<uint8> AppliedWeights;
    if (!ReadLandscapeRegionData(
            Prepared.LandscapeInfo,
            Prepared.EditLayerGuid,
            Prepared.LayerInfo,
            Prepared.MinX,
            Prepared.MinY,
            Prepared.MaxX,
            Prepared.MaxY,
            AppliedHeights,
            AppliedWeights,
            OutReason))
    {
        return false;
    }
    const FString AppliedHash = Prepared.bHeightOperation
        ? LandscapeDataHash(
            AppliedHeights.GetData(),
            static_cast<int64>(AppliedHeights.Num()) * sizeof(uint16))
        : LandscapeDataHash(
            AppliedWeights.GetData(),
            static_cast<int64>(AppliedWeights.Num()) * sizeof(uint8));
    if (AppliedHash != Prepared.NewHash)
    {
        OutReason = TEXT("Landscape data verification failed|") + AppliedHash
            + TEXT("|") + Prepared.NewHash;
        return false;
    }
    return true;
}

bool IsSupportedLandscapeProperty(const FProperty* Property)
{
    static const TSet<FName> Allowed = {
        TEXT("bEnableNanite"),
        TEXT("NaniteLODIndex"),
        TEXT("bNaniteSkirtEnabled"),
        TEXT("NaniteSkirtDepth"),
        TEXT("NanitePositionPrecision"),
        TEXT("NaniteMaxEdgeLengthFactor"),
        TEXT("bUsedForNavigation"),
        TEXT("bFillCollisionUnderLandscapeForNavmesh"),
        TEXT("NavigationGeometryGatheringMode"),
        TEXT("bUseDynamicMaterialInstance"),
        TEXT("bUseLandscapeForCullingInvisibleHLODVertices"),
        TEXT("StreamingDistanceMultiplier"),
        TEXT("NegativeZBoundsExtension"),
        TEXT("PositiveZBoundsExtension")
    };
    return Property && Allowed.Contains(Property->GetFName())
        && IsSupportedActorProperty(Property);
}

bool CanImportPropertyValue(
    const FProperty* Property,
    const FString& Value,
    UObject* Owner)
{
    if (!Property)
    {
        return false;
    }
    void* Storage = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
    Property->InitializeValue(Storage);
    const bool bOk = Property->ImportText_Direct(
        *Value, Storage, Owner, PPF_None) != nullptr;
    Property->DestroyValue(Storage);
    FMemory::Free(Storage);
    return bOk;
}

FString LandscapeEditLayerPropertyValue(
    const ULandscapeEditLayerBase* Layer,
    const FString& PropertyName)
{
    if (!Layer) return FString();
    if (PropertyName.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
        return Layer->GetName().ToString();
    if (PropertyName.Equals(TEXT("Visible"), ESearchCase::IgnoreCase))
        return BoolText(Layer->IsVisible());
    if (PropertyName.Equals(TEXT("Locked"), ESearchCase::IgnoreCase))
        return BoolText(Layer->IsLocked());
    if (PropertyName.Equals(TEXT("HeightmapAlpha"), ESearchCase::IgnoreCase))
        return FString::SanitizeFloat(
            Layer->GetAlphaForTargetType(ELandscapeToolTargetType::Heightmap));
    if (PropertyName.Equals(TEXT("WeightmapAlpha"), ESearchCase::IgnoreCase))
        return FString::SanitizeFloat(
            Layer->GetAlphaForTargetType(ELandscapeToolTargetType::Weightmap));
    return FString();
}

bool IsValidLandscapeEditLayerValue(
    ALandscape* Landscape,
    ULandscapeEditLayerBase* Layer,
    const FString& PropertyName,
    const FString& Value)
{
    if (!Landscape || !Layer) return false;
    if (PropertyName.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
    {
        const FName Name(*Value.TrimStartAndEnd());
        return !Name.IsNone()
            && (Name == Layer->GetName() || Landscape->IsLayerNameUnique(Name));
    }
    bool bBool = false;
    if (PropertyName.Equals(TEXT("Visible"), ESearchCase::IgnoreCase)
        || PropertyName.Equals(TEXT("Locked"), ESearchCase::IgnoreCase))
    {
        return ParseBoolStrict(Value, bBool);
    }
    float Alpha = 0.0f;
    const bool bHeight = PropertyName.Equals(
        TEXT("HeightmapAlpha"), ESearchCase::IgnoreCase);
    const bool bWeight = PropertyName.Equals(
        TEXT("WeightmapAlpha"), ESearchCase::IgnoreCase);
    if ((bHeight || bWeight)
        && LexTryParseString(Alpha, *Value)
        && FMath::IsFinite(Alpha))
    {
        const FFloatInterval Range = Layer->GetAlphaRangeForTargetType(
            bHeight
                ? ELandscapeToolTargetType::Heightmap
                : ELandscapeToolTargetType::Weightmap);
        return Range.Contains(Alpha);
    }
    return false;
}

bool ParseRuntimeState(const FString& Value, EDataLayerRuntimeState& OutState)
{
    const UEnum* Enum = StaticEnum<EDataLayerRuntimeState>();
    int64 EnumValue = Enum ? Enum->GetValueByNameString(Value) : INDEX_NONE;
    if (EnumValue == INDEX_NONE && Enum)
    {
        EnumValue = Enum->GetValueByNameString(
            TEXT("EDataLayerRuntimeState::") + Value);
    }
    if (EnumValue == INDEX_NONE)
    {
        return false;
    }
    OutState = static_cast<EDataLayerRuntimeState>(EnumValue);
    return true;
}

FString MobilityText(const USceneComponent* Component)
{
    if (!Component)
    {
        return TEXT("none");
    }
    switch (Component->Mobility)
    {
    case EComponentMobility::Static: return TEXT("Static");
    case EComponentMobility::Stationary: return TEXT("Stationary");
    case EComponentMobility::Movable: return TEXT("Movable");
    default: return TEXT("Unknown");
    }
}

FThomasPropertyRecord ReadProperty(const UObject* Object, const FName Name)
{
    FThomasPropertyRecord Result;
    Result.Name = Name.ToString();
    if (!Object)
    {
        return Result;
    }
    const FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), Name);
    if (!Property)
    {
        Result.Value = TEXT("<unavailable>");
        return Result;
    }
    Result.Type = Property->GetCPPType();
    Property->ExportText_InContainer(
        0,
        Result.Value,
        Object,
        Object,
        const_cast<UObject*>(Object),
        PPF_None);
    Result.Value = Result.Value.Left(1200);
    Result.Category = Property->GetMetaData(TEXT("Category"));
    Result.Tooltip = Property->GetMetaData(TEXT("ToolTip")).Left(1200);
    Result.ClampMin = Property->GetMetaData(TEXT("ClampMin"));
    Result.ClampMax = Property->GetMetaData(TEXT("ClampMax"));
    Result.bEditable = Property->HasAnyPropertyFlags(CPF_Edit);
    Result.bBlueprintVisible = Property->HasAnyPropertyFlags(CPF_BlueprintVisible);
    return Result;
}

AActor* ResolveActor(UWorld* World, const FString& ActorPath)
{
    AActor* Actor = FindObject<AActor>(nullptr, *ActorPath);
    return Actor && Actor->GetWorld() == World ? Actor : nullptr;
}

bool IsProtectedEditorActor(const AActor* Actor)
{
    return !Actor
        || Actor->IsA<AWorldSettings>()
        || Actor->IsA<ALevelScriptActor>()
        || Actor->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists);
}

bool IsLumenRelated(const FProperty* Property);

bool IsSupportedActorProperty(const FProperty* Property)
{
    return Property
        && Property->HasAnyPropertyFlags(CPF_Edit)
        && !Property->HasAnyPropertyFlags(
            CPF_Transient | CPF_EditConst | CPF_DisableEditOnInstance)
        && (Property->IsA<FBoolProperty>()
            || Property->IsA<FNumericProperty>()
            || Property->IsA<FEnumProperty>()
            || Property->IsA<FNameProperty>()
            || Property->IsA<FStrProperty>()
            || Property->IsA<FTextProperty>());
}

bool IsSupportedComponentProperty(const FProperty* Property)
{
    return Property
        && Property->HasAnyPropertyFlags(CPF_Edit)
        && !Property->HasAnyPropertyFlags(
            CPF_Transient | CPF_EditConst | CPF_DisableEditOnInstance | CPF_Deprecated)
        && (Property->IsA<FBoolProperty>()
            || Property->IsA<FNumericProperty>()
            || Property->IsA<FEnumProperty>()
            || Property->IsA<FNameProperty>()
            || Property->IsA<FStrProperty>()
            || Property->IsA<FTextProperty>()
            || Property->IsA<FObjectPropertyBase>()
            || Property->IsA<FSoftObjectProperty>()
            || Property->IsA<FSoftClassProperty>()
            || Property->IsA<FStructProperty>());
}

UActorComponent* ResolveOwnedComponent(
    AActor* Actor,
    const FString& ComponentPath)
{
    if (!Actor || ComponentPath.IsEmpty())
    {
        return nullptr;
    }
    UActorComponent* Component = FindObject<UActorComponent>(nullptr, *ComponentPath);
    return Component && Component->GetOwner() == Actor
        && Actor->GetComponents().Contains(Component)
        ? Component : nullptr;
}

FString ExportComponentProperty(
    const UActorComponent* Component,
    const FProperty* Property)
{
    FString Value;
    if (Component && Property)
    {
        Property->ExportText_InContainer(
            0,
            Value,
            Component,
            Component,
            const_cast<UActorComponent*>(Component),
            PPF_None);
    }
    return Value;
}

bool CanImportComponentProperty(
    UActorComponent* Component,
    FProperty* Property,
    const FString& Value)
{
    UActorComponent* Candidate = Component
        ? DuplicateObject<UActorComponent>(Component, GetTransientPackage())
        : nullptr;
    return Candidate && Property
        && Property->ImportText_InContainer(
            *Value, Candidate, Candidate, PPF_None) != nullptr;
}

bool IsSupportedRendererProperty(const FProperty* Property)
{
    return Property
        && Property->HasAnyPropertyFlags(CPF_Edit | CPF_Config)
        && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst)
        && IsLumenRelated(Property)
        && (Property->IsA<FBoolProperty>()
            || Property->IsA<FNumericProperty>()
            || Property->IsA<FEnumProperty>()
            || Property->IsA<FNameProperty>()
            || Property->IsA<FStrProperty>());
}

FString ExportObjectProperty(const UObject* Object, const FProperty* Property)
{
    FString Value;
    if (Object && Property)
    {
        Property->ExportText_InContainer(
            0, Value, Object, Object, const_cast<UObject*>(Object), PPF_None);
    }
    return Value;
}

bool HasMixedRendererAndWorldOperations(const TArray<FThomasWorldOperation>& Operations)
{
    bool bHasRendererOperation = false;
    bool bHasWorldOperation = false;
    for (const FThomasWorldOperation& Operation : Operations)
    {
        if (Operation.Action.Equals(TEXT("set_renderer_property"), ESearchCase::IgnoreCase))
        {
            bHasRendererOperation = true;
        }
        else
        {
            bHasWorldOperation = true;
        }
    }
    return bHasRendererOperation && bHasWorldOperation;
}

bool PersistRendererConfig(URendererSettings* Settings)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (GRendererConfigSaveFailuresRemaining > 0)
    {
        --GRendererConfigSaveFailuresRemaining;
        return false;
    }
#endif
    return Settings && Settings->TryUpdateDefaultConfigFile();
}

struct FRendererConfigSnapshot
{
    FString Filename;
    TArray64<uint8> Bytes;
    bool bExisted = false;
    bool bCaptured = false;
};

bool CaptureRendererConfig(
    const URendererSettings* Settings,
    FRendererConfigSnapshot& OutSnapshot)
{
    if (!Settings)
    {
        return false;
    }
    OutSnapshot.Filename = FPaths::ConvertRelativePathToFull(
        Settings->GetDefaultConfigFilename());
    OutSnapshot.bExisted = IFileManager::Get().FileExists(*OutSnapshot.Filename);
    OutSnapshot.bCaptured = !OutSnapshot.bExisted
        || FFileHelper::LoadFileToArray(
            OutSnapshot.Bytes, *OutSnapshot.Filename, FILEREAD_Silent);
    return OutSnapshot.bCaptured;
}

bool RestoreRendererConfig(const FRendererConfigSnapshot& Snapshot)
{
    if (!Snapshot.bCaptured || Snapshot.Filename.IsEmpty())
    {
        return false;
    }
    if (Snapshot.bExisted)
    {
        return FFileHelper::SaveArrayToFile(
            Snapshot.Bytes, *Snapshot.Filename);
    }
    return !IFileManager::Get().FileExists(*Snapshot.Filename)
        || IFileManager::Get().Delete(*Snapshot.Filename, false, true, true);
}

bool RestoreRendererProperties(
    URendererSettings* Settings,
    const TMap<FName, FString>& PreviousValues,
    const bool bPersistConfig)
{
    if (!Settings)
    {
        return false;
    }
    for (const TPair<FName, FString>& Pair : PreviousValues)
    {
        FProperty* Property = FindFProperty<FProperty>(
            URendererSettings::StaticClass(), Pair.Key);
        if (!IsSupportedRendererProperty(Property)
            || !Property->ImportText_InContainer(
                *Pair.Value, Settings, Settings, PPF_None))
        {
            return false;
        }
        FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
        Settings->PostEditChangeProperty(Event);
    }
    return !bPersistConfig || PersistRendererConfig(Settings);
}

bool IsSupportedPostProcessProperty(const FProperty* Property)
{
    return Property
        && Property->HasAnyPropertyFlags(CPF_Edit)
        && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst)
        && !Property->IsA<FArrayProperty>()
        && !Property->IsA<FMapProperty>()
        && !Property->IsA<FSetProperty>()
        && !Property->IsA<FDelegateProperty>()
        && !Property->IsA<FMulticastDelegateProperty>()
        && Property->GetFName() != TEXT("WeightedBlendables");
}

FString ExportStructProperty(const void* Container, const FProperty* Property)
{
    FString Value;
    if (Container && Property)
    {
        const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Container);
        Property->ExportTextItem_Direct(Value, PropertyValue, nullptr, nullptr, PPF_None);
    }
    return Value.Left(4000);
}

bool ImportStructProperty(
    void* Container,
    const FProperty* Property,
    const FString& Value,
    UObject* Owner)
{
    if (!Container || !Property)
    {
        return false;
    }
    void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Container);
    return Property->ImportText_Direct(*Value, PropertyValue, Owner, PPF_None) != nullptr;
}

FBoolProperty* FindOverrideProperty(const FProperty* Property)
{
    return Property
        ? FindFProperty<FBoolProperty>(
            FPostProcessSettings::StaticStruct(),
            FName(*(TEXT("bOverride_") + Property->GetName())))
        : nullptr;
}

bool IsLumenRelated(const FProperty* Property)
{
    if (!Property)
    {
        return false;
    }
    const FString Search = (
        Property->GetName()
        + TEXT(" ") + Property->GetMetaData(TEXT("Category"))
        + TEXT(" ") + Property->GetMetaData(TEXT("ToolTip"))).ToLower();
    return Search.Contains(TEXT("lumen"))
        || Search.Contains(TEXT("global illumination"))
        || Search.Contains(TEXT("globalillumination"))
        || Search.Contains(TEXT("reflection"))
        || Search.Contains(TEXT("surface cache"))
        || Search.Contains(TEXT("surfacecache"))
        || Search.Contains(TEXT("distance field"))
        || Search.Contains(TEXT("distancefield"))
        || Search.Contains(TEXT("hardware ray tracing"))
        || Search.Contains(TEXT("hardwareraytracing"));
}

APostProcessVolume* ResolvePostProcessVolume(UWorld* World, const FString& ActorPath)
{
    return Cast<APostProcessVolume>(ResolveActor(World, ActorPath));
}

bool IsAllowedBlendablePath(const FString& ObjectPath)
{
    const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
    return (PackageName == TEXT("/Game/PropHunt")
        || PackageName.StartsWith(TEXT("/Game/PropHunt/")))
        && !PackageName.Contains(TEXT(".."))
        && !PackageName.StartsWith(TEXT("/Game/Developers/"));
}

UMaterialInterface* LoadPostProcessMaterial(const FString& ObjectPath)
{
    if (!IsAllowedBlendablePath(ObjectPath))
    {
        return nullptr;
    }
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
    UMaterial* BaseMaterial = Material ? Material->GetMaterial() : nullptr;
    return BaseMaterial && BaseMaterial->MaterialDomain == MD_PostProcess ? Material : nullptr;
}

FString ExportActorProperty(const AActor* Actor, const FProperty* Property)
{
    FString Value;
    if (Actor && Property)
    {
        Property->ExportText_InContainer(
            0, Value, Actor, Actor, const_cast<AActor*>(Actor), PPF_None);
    }
    return Value;
}

bool SaveWorld(UWorld* World)
{
    if (!World)
    {
        return false;
    }
    UPackage* Package = World->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetMapPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    Args.Error = GError;
    return UPackage::SavePackage(Package, World, *Filename, Args);
}
}

class FThomasEditorWorldModule final : public IThomasEditorWorldProviderModule
{
public:
#if WITH_DEV_AUTOMATION_TESTS
    virtual void SetForceRendererConfigSaveFailureForTests(
        const bool bForceFailure) override
    {
        GRendererConfigSaveFailuresRemaining = bForceFailure ? 1 : 0;
    }
#endif

    virtual void ShutdownModule() override
    {
        for (TPair<FString, FEnvironmentBuildJob>& Pair : EnvironmentJobs)
        {
            FEnvironmentBuildJob& Job = Pair.Value;
            if (Job.bProcessRunning && Job.ProcessHandle.IsValid())
            {
                FPlatformProcess::TerminateProc(Job.ProcessHandle, true);
                FPlatformProcess::CloseProc(Job.ProcessHandle);
            }
        }
        EnvironmentJobs.Reset();
        EnvironmentBuildPlans.Reset();
        LandscapeLayerInfoPlans.Reset();
        Plans.Reset();
        PostProcessPlans.Reset();
        MapPlans.Reset();
    }

    virtual FThomasWorldInspectionResult InspectCurrentLevel(
        const FString& ClassFilter,
        const bool bSelectedOnly,
        const int32 MaxActors) override
    {
        if (MaxActors < 1 || MaxActors > MaxActorResults)
        {
            return MakeError<FThomasWorldInspectionResult>(
                TEXT("invalid_limit"), TEXT("MaxActors must be between 1 and 500."));
        }
        UWorld* World = GetProjectEditorWorld();
        if (!World)
        {
            return MakeError<FThomasWorldInspectionResult>(
                TEXT("project_editor_world_unavailable"),
                TEXT("Open a /Game/PropHunt level in the canonical Editor."));
        }

        FThomasWorldInspectionResult Result;
        Result.bOk = true;
        Result.MapPath = World->GetOutermost()->GetName();
        Result.Revision = BuildWorldRevision(World);
        Result.WorldType = TEXT("Editor");
        Result.bWorldPartition = World->GetWorldPartition() != nullptr;
        Result.bDirty = World->GetOutermost()->IsDirty();
        Result.bPlayInEditor = GEditor && GEditor->PlayWorld != nullptr;
        if (const AWorldSettings* Settings = World->GetWorldSettings())
        {
            if (Settings->DefaultGameMode)
            {
                Result.DefaultGameModeClass = Settings->DefaultGameMode->GetPathName();
            }
        }

        const USelection* Selection = GEditor ? GEditor->GetSelectedActors() : nullptr;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!Actor)
            {
                continue;
            }
            const bool bSelected = Selection && Selection->IsSelected(Actor);
            Result.SelectedActorCount += bSelected ? 1 : 0;
            ++Result.ActorCount;

            const FString ClassPath = Actor->GetClass()->GetPathName();
            if ((bSelectedOnly && !bSelected)
                || (!ClassFilter.IsEmpty()
                    && !ClassPath.Contains(ClassFilter, ESearchCase::IgnoreCase)))
            {
                continue;
            }
            if (Result.Actors.Num() >= MaxActors)
            {
                Result.bTruncated = true;
                continue;
            }

            FThomasWorldActorRecord Record;
            Record.ActorPath = Actor->GetPathName();
            Record.Label = Actor->GetActorLabel();
            Record.ClassPath = ClassPath;
            Record.Folder = Actor->GetFolderPath().ToString();
            Record.Location = Actor->GetActorLocation().ToCompactString();
            Record.Rotation = Actor->GetActorRotation().ToCompactString();
            Record.Scale = Actor->GetActorScale3D().ToCompactString();
            Record.Mobility = MobilityText(Actor->GetRootComponent());
            Record.ComponentCount = Actor->GetComponents().Num();
            Record.bSelected = bSelected;
            Record.bHiddenInEditor = Actor->IsHiddenEd();
            Record.bEditorOnly = Actor->IsEditorOnly();
            Result.Actors.Add(MoveTemp(Record));
        }
        return Result;
    }

    virtual FThomasWorldComponentInspectionResult InspectActorComponent(
        const FString& ActorPath,
        const FString& ComponentPath,
        const TArray<FString>& PropertyNames,
        const int32 MaxProperties) override
    {
        if (MaxProperties < 1 || MaxProperties > 100 || PropertyNames.Num() > 100)
        {
            return MakeError<FThomasWorldComponentInspectionResult>(
                TEXT("invalid_limit"), TEXT("Component Details are capped at 100 properties."));
        }
        UWorld* World = GetProjectEditorWorld();
        AActor* Actor = ResolveActor(World, ActorPath);
        UActorComponent* Component = ResolveOwnedComponent(Actor, ComponentPath);
        if (IsProtectedEditorActor(Actor) || !Component)
        {
            return MakeError<FThomasWorldComponentInspectionResult>(
                TEXT("component_not_found"), ComponentPath);
        }

        TArray<FProperty*> Properties;
        if (PropertyNames.IsEmpty())
        {
            for (TFieldIterator<FProperty> It(Component->GetClass()); It; ++It)
            {
                if ((*It)->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
                {
                    Properties.Add(*It);
                }
            }
            Properties.Sort([](const FProperty& Left, const FProperty& Right)
            {
                return Left.GetName() < Right.GetName();
            });
        }
        else
        {
            TSet<FString> Seen;
            for (const FString& PropertyName : PropertyNames)
            {
                FProperty* Property = FindFProperty<FProperty>(
                    Component->GetClass(), FName(*PropertyName));
                if (PropertyName.IsEmpty() || Seen.Contains(PropertyName) || !Property)
                {
                    return MakeError<FThomasWorldComponentInspectionResult>(
                        Property ? TEXT("duplicate_property") : TEXT("property_not_found"),
                        PropertyName);
                }
                Seen.Add(PropertyName);
                Properties.Add(Property);
            }
        }

        FThomasWorldComponentInspectionResult Result;
        Result.bOk = true;
        Result.MapPath = World->GetOutermost()->GetName();
        Result.Revision = BuildWorldRevision(World);
        Result.ActorPath = Actor->GetPathName();
        Result.ComponentPath = Component->GetPathName();
        Result.ComponentName = Component->GetName();
        Result.ClassPath = Component->GetClass()->GetPathName();
        Result.bRegistered = Component->IsRegistered();
        Result.bActive = Component->IsActive();
        Result.bTruncated = Properties.Num() > MaxProperties;
        const int32 Count = FMath::Min(MaxProperties, Properties.Num());
        for (int32 Index = 0; Index < Count; ++Index)
        {
            FProperty* Property = Properties[Index];
            FThomasPropertyRecord Record;
            Record.Name = Property->GetName();
            Record.Type = Property->GetCPPType();
            Record.Value = ExportComponentProperty(Component, Property).Left(1200);
            Record.Category = Property->GetMetaData(TEXT("Category"));
            Record.Tooltip = Property->GetMetaData(TEXT("ToolTip")).Left(1200);
            Record.ClampMin = Property->GetMetaData(TEXT("ClampMin"));
            Record.ClampMax = Property->GetMetaData(TEXT("ClampMax"));
            Record.bEditable = Property->HasAnyPropertyFlags(CPF_Edit);
            Record.bBlueprintVisible = Property->HasAnyPropertyFlags(CPF_BlueprintVisible);
            Result.Properties.Add(MoveTemp(Record));
        }
        return Result;
    }

    virtual FThomasLightingInspectionResult InspectLighting() override
    {
        UWorld* World = GetProjectEditorWorld();
        if (!World)
        {
            return MakeError<FThomasLightingInspectionResult>(
                TEXT("project_editor_world_unavailable"),
                TEXT("Open a /Game/PropHunt level in the canonical Editor."));
        }

        FThomasLightingInspectionResult Result;
        Result.bOk = true;
        Result.MapPath = World->GetOutermost()->GetName();

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (Actor->IsA<ALight>())
            {
                ++Result.LightCount;
                Result.LightingActors.Add(FString::Printf(
                    TEXT("%s|%s|%s"),
                    *Actor->GetActorLabel(),
                    *Actor->GetClass()->GetPathName(),
                    *MobilityText(Actor->GetRootComponent())));
            }
            if (Actor->IsA<APostProcessVolume>())
            {
                ++Result.PostProcessVolumeCount;
                Result.LightingActors.Add(FString::Printf(
                    TEXT("%s|%s"),
                    *Actor->GetActorLabel(),
                    *Actor->GetClass()->GetPathName()));
            }
            const FString ClassName = Actor->GetClass()->GetName();
            Result.bHasSkyLight |= Actor->IsA<ASkyLight>();
            Result.bHasSkyAtmosphere |= ClassName == TEXT("SkyAtmosphere");
            Result.bHasExponentialHeightFog |= Actor->IsA<AExponentialHeightFog>();
        }

        const URendererSettings* Settings = GetDefault<URendererSettings>();
        const TSet<FName> CoreRendererSettings = {
            TEXT("DynamicGlobalIllumination"), TEXT("Reflections"), TEXT("ShadowMapMethod"),
            TEXT("bGenerateMeshDistanceFields"), TEXT("bVirtualTextures"),
            TEXT("bVirtualTexturedLightmaps")
        };
        for (TFieldIterator<FProperty> It(URendererSettings::StaticClass()); It; ++It)
        {
            const FProperty* Property = *It;
            if (CoreRendererSettings.Contains(Property->GetFName()) || IsLumenRelated(Property))
            {
                Result.RendererSettings.Add(ReadProperty(Settings, Property->GetFName()));
            }
        }

        const FThomasPropertyRecord* Gi = Result.RendererSettings.FindByPredicate(
            [](const FThomasPropertyRecord& Item)
            {
                return Item.Name == TEXT("DynamicGlobalIllumination");
            });
        const FThomasPropertyRecord* Reflections = Result.RendererSettings.FindByPredicate(
            [](const FThomasPropertyRecord& Item)
            {
                return Item.Name == TEXT("Reflections");
            });
        if (!Gi || !Gi->Value.Contains(TEXT("Lumen"), ESearchCase::IgnoreCase))
        {
            Result.Diagnostics.Add(TEXT("Project dynamic GI is not configured as Lumen."));
        }
        if (!Reflections
            || !Reflections->Value.Contains(TEXT("Lumen"), ESearchCase::IgnoreCase))
        {
            Result.Diagnostics.Add(TEXT("Project reflections are not configured as Lumen."));
        }
        if (Result.PostProcessVolumeCount == 0)
        {
            Result.Diagnostics.Add(TEXT("No PostProcessVolume is present in the current level."));
        }
        return Result;
    }

    virtual FThomasEnvironmentInspectionResult InspectEnvironment(
        const int32 MaxItems) override
    {
        if (MaxItems < 1 || MaxItems > MaxActorResults)
        {
            return MakeError<FThomasEnvironmentInspectionResult>(
                TEXT("invalid_limit"), TEXT("MaxItems must be between 1 and 500."));
        }
        UWorld* World = GetProjectEditorWorld();
        if (!World)
        {
            return MakeError<FThomasEnvironmentInspectionResult>(
                TEXT("project_editor_world_unavailable"),
                TEXT("Open a /Game/PropHunt level in the canonical Editor."));
        }

        FThomasEnvironmentInspectionResult Result;
        Result.bOk = true;
        Result.MapPath = World->GetOutermost()->GetName();
        Result.Revision = BuildWorldRevision(World);
        if (UWorldPartition* WorldPartition = World->GetWorldPartition())
        {
            Result.bWorldPartition = true;
            Result.bWorldPartitionStreamingEnabled =
                WorldPartition->IsStreamingEnabled();
            Result.bWorldPartitionStreamingEnabledInEditor =
                WorldPartition->IsStreamingEnabledInEditor();
            const FObjectPropertyBase* RuntimeHashProperty =
                FindFProperty<FObjectPropertyBase>(
                    WorldPartition->GetClass(), FName(TEXT("RuntimeHash")));
            const UObject* RuntimeHash = RuntimeHashProperty
                ? RuntimeHashProperty->GetObjectPropertyValue_InContainer(WorldPartition)
                : nullptr;
            Result.RuntimeHashClassPath = RuntimeHash
                ? RuntimeHash->GetClass()->GetPathName() : FString();
        }
        else
        {
            Result.Diagnostics.Add(TEXT("world_partition_disabled"));
        }

        TMap<const UDataLayerInstance*, int32> DataLayerActorCounts;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            for (const UDataLayerInstance* DataLayer : It->GetDataLayerInstances())
            {
                if (DataLayer)
                {
                    ++DataLayerActorCounts.FindOrAdd(DataLayer);
                }
            }
        }
        if (UDataLayerEditorSubsystem* DataLayerSubsystem =
                UDataLayerEditorSubsystem::Get())
        {
            for (UDataLayerInstance* DataLayer : DataLayerSubsystem->GetAllDataLayers())
            {
                if (!DataLayer)
                {
                    continue;
                }
                ++Result.DataLayerCount;
                if (Result.DataLayers.Num() >= MaxItems)
                {
                    Result.bDataLayersTruncated = true;
                    continue;
                }
                FThomasDataLayerRecord Record;
                Record.ObjectPath = DataLayer->GetPathName();
                Record.Name = DataLayer->GetName();
                Record.ShortName = DataLayer->GetDataLayerShortName();
                Record.FullName = DataLayer->GetDataLayerFullName();
                Record.AssetPath = DataLayer->GetAsset()
                    ? DataLayer->GetAsset()->GetPathName() : FString();
                Record.ParentName = DataLayer->GetParent()
                    ? DataLayer->GetParent()->GetName() : FString();
                Record.InitialRuntimeState = UEnum::GetValueAsString(
                    DataLayer->GetInitialRuntimeState());
                Record.bRuntime = DataLayer->IsRuntime();
                Record.bClientOnly = DataLayer->IsClientOnly();
                Record.bServerOnly = DataLayer->IsServerOnly();
                Record.bVisible = DataLayer->IsVisible();
                Record.bInitiallyVisible = DataLayer->IsInitiallyVisible();
                Record.bLoadedInEditor = DataLayer->IsLoadedInEditor();
                Record.ActorCount = DataLayerActorCounts.FindRef(DataLayer);
                Result.DataLayers.Add(MoveTemp(Record));
            }
        }

        if (UNavigationSystemV1* NavigationSystem =
                FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
        {
            Result.bNavigationSystemAvailable = true;
            Result.NavigationDataCount = NavigationSystem->NavDataSet.Num();
            for (const ANavigationData* NavData : NavigationSystem->NavDataSet)
            {
                if (NavData && Result.NavigationDataActors.Num() < MaxItems)
                {
                    Result.NavigationDataActors.Add(FString::Printf(
                        TEXT("%s|%s"),
                        *NavData->GetPathName(),
                        *NavData->GetClass()->GetPathName()));

                    FThomasNavigationDataRecord Record;
                    Record.ActorPath = NavData->GetPathName();
                    Record.Label = NavData->GetActorLabel();
                    Record.ClassPath = NavData->GetClass()->GetPathName();
                    Record.RuntimeGeneration = UEnum::GetValueAsString(
                        NavData->GetRuntimeGenerationMode());
                    const FNavDataConfig& Config = NavData->GetConfig();
                    Record.AgentName = Config.Name.ToString();
                    Record.PreferredNavDataClass = Config.PreferredNavData.ToString();
                    Record.AgentRadius = Config.AgentRadius;
                    Record.AgentHeight = Config.AgentHeight;
                    Record.AgentStepHeight = Config.AgentStepHeight;
                    Record.QueryExtentX = Config.DefaultQueryExtent.X;
                    Record.QueryExtentY = Config.DefaultQueryExtent.Y;
                    Record.QueryExtentZ = Config.DefaultQueryExtent.Z;
                    Record.bRegistered = NavData->IsRegistered();
                    Record.bSupportsRuntimeGeneration = NavData->SupportsRuntimeGeneration();
                    Record.bSupportsStreaming = NavData->SupportsStreaming();
                    Record.bCanBeMainNavData = NavData->CanBeMainNavData();
                    Record.bSupportsDefaultAgent = NavData->IsSupportingDefaultAgent();
                    Record.bNeedsRebuild = NavData->NeedsRebuild();
                    Record.BuilderOverlap =
                        NavData->GetWorldPartitionNavigationDataBuilderOverlap();
                    if (const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(NavData))
                    {
                        Record.bWorldPartitionedRecast = Recast->bIsWorldPartitioned;
                        Record.bFixedTilePoolSize = Recast->bFixedTilePoolSize;
                        Record.bFullyAsyncGathering = Recast->bDoFullyAsyncNavDataGathering;
                        Record.AgentMaxSlope = Recast->AgentMaxSlope;
                        Record.TileSizeUU = Recast->GetTileSizeUU();
                        Record.TilePoolSize = Recast->TilePoolSize;
                        Record.TileCapacity = Recast->GetNavMeshTilesCount();
                        Record.ActiveTileCount = Recast->GetNumActiveTiles();
                        Record.DefaultCellSize = Recast->GetCellSize(
                            ENavigationDataResolution::Default);
                        Record.DefaultCellHeight = Recast->GetCellHeight(
                            ENavigationDataResolution::Default);
                        Record.DefaultAgentMaxStepHeight = Recast->GetAgentMaxStepHeight(
                            ENavigationDataResolution::Default);
                    }
                    Result.NavigationData.Add(MoveTemp(Record));
                }
            }
        }
        else
        {
            Result.Diagnostics.Add(TEXT("navigation_system_unavailable"));
        }

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(Actor))
            {
                ++Result.LandscapeActorCount;
                if (Result.Landscapes.Num() < MaxItems)
                {
                    FThomasLandscapeRecord Record;
                    Record.ActorPath = Landscape->GetPathName();
                    Record.Label = Landscape->GetActorLabel();
                    Record.ClassPath = Landscape->GetClass()->GetPathName();
                    Record.LandscapeGuid = Landscape->GetLandscapeGuid().ToString(
                        EGuidFormats::DigitsWithHyphens);
                    Record.MaterialPath = Landscape->GetLandscapeMaterial()
                        ? Landscape->GetLandscapeMaterial()->GetPathName() : FString();
                    Record.HoleMaterialPath = Landscape->GetLandscapeHoleMaterial()
                        ? Landscape->GetLandscapeHoleMaterial()->GetPathName() : FString();
                    Record.ComponentCount = Landscape->LandscapeComponents.Num();
                    Record.CollisionComponentCount = Landscape->CollisionComponents.Num();
                    Record.NaniteComponentCount = Landscape->NaniteComponents.Num();
                    Record.ComponentSizeQuads = Landscape->ComponentSizeQuads;
                    Record.SubsectionSizeQuads = Landscape->SubsectionSizeQuads;
                    Record.NumSubsections = Landscape->NumSubsections;
                    if (ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo())
                    {
                        FIntRect Extent;
                        Record.bHasValidExtent = LandscapeInfo->GetLandscapeExtent(
                            Landscape, Extent);
                        if (Record.bHasValidExtent)
                        {
                            Record.MinX = Extent.Min.X;
                            Record.MinY = Extent.Min.Y;
                            Record.MaxX = Extent.Max.X;
                            Record.MaxY = Extent.Max.Y;
                            Record.VertexSizeX = Extent.Width() + 1;
                            Record.VertexSizeY = Extent.Height() + 1;
                        }
                    }
                    Record.bEnableNanite = Landscape->IsNaniteEnabled();
                    Record.NaniteLODIndex = Landscape->GetNaniteLODIndex();
                    Record.bNaniteSkirtEnabled = Landscape->IsNaniteSkirtEnabled();
                    Record.NaniteSkirtDepth = Landscape->GetNaniteSkirtDepth();
                    Record.NanitePositionPrecision = Landscape->GetNanitePositionPrecision();
                    Record.NaniteMaxEdgeLengthFactor =
                        Landscape->GetNaniteMaxEdgeLengthFactor();
                    Record.bUsedForNavigation = Landscape->bUsedForNavigation;
                    Record.bFillCollisionUnderLandscapeForNavmesh =
                        Landscape->bFillCollisionUnderLandscapeForNavmesh;
                    Record.NavigationGeometryGatheringMode = UEnum::GetValueAsString(
                        Landscape->NavigationGeometryGatheringMode);
                    Record.bUseDynamicMaterialInstance =
                        Landscape->bUseDynamicMaterialInstance;
                    if (const ALandscape* MainLandscape = Cast<ALandscape>(Landscape))
                    {
                        const TArray<const ULandscapeEditLayerBase*> EditLayers =
                            MainLandscape->GetEditLayersConst();
                        Record.bCanHaveLayersContent = true;
                        Record.bHasLayersContent = !EditLayers.IsEmpty();
                        Record.EditLayerCount = EditLayers.Num();
                        const int32 LayerLimit = FMath::Min(EditLayers.Num(), MaxItems);
                        for (int32 LayerIndex = 0; LayerIndex < LayerLimit; ++LayerIndex)
                        {
                            const ULandscapeEditLayerBase* Layer = EditLayers[LayerIndex];
                            if (!Layer) continue;
                            FThomasLandscapeEditLayerRecord LayerRecord;
                            LayerRecord.Index = LayerIndex;
                            LayerRecord.ObjectPath = Layer->GetPathName();
                            LayerRecord.Guid = Layer->GetGuid().ToString(EGuidFormats::Digits);
                            LayerRecord.Name = Layer->GetName().ToString();
                            LayerRecord.ClassPath = Layer->GetClass()->GetPathName();
                            LayerRecord.bVisible = Layer->IsVisible();
                            LayerRecord.bLocked = Layer->IsLocked();
                            LayerRecord.HeightmapAlpha = Layer->GetAlphaForTargetType(
                                ELandscapeToolTargetType::Heightmap);
                            LayerRecord.WeightmapAlpha = Layer->GetAlphaForTargetType(
                                ELandscapeToolTargetType::Weightmap);
                            Record.EditLayers.Add(MoveTemp(LayerRecord));
                        }
                        Record.bEditLayersTruncated = EditLayers.Num() > LayerLimit;
                    }
                    Result.Landscapes.Add(MoveTemp(Record));
                }
            }
            if (AInstancedFoliageActor* FoliageActor =
                    Cast<AInstancedFoliageActor>(Actor))
            {
                ++Result.FoliageActorCount;
                for (const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& Pair
                    : FoliageActor->GetFoliageInfos())
                {
                    ++Result.FoliageTypeCount;
                    const int32 InstanceCount = Pair.Value->Instances.Num();
                    Result.FoliageInstanceCount += InstanceCount;
                    if (Result.Foliage.Num() < MaxItems)
                    {
                        FThomasFoliageRecord Record;
                        Record.ActorPath = FoliageActor->GetPathName();
                        Record.FoliageTypePath = Pair.Key
                            ? Pair.Key->GetPathName() : FString();
                        Record.FoliageTypeClassPath = Pair.Key
                            ? Pair.Key->GetClass()->GetPathName() : FString();
                        Record.SourcePath = Pair.Key && Pair.Key->GetSource()
                            ? Pair.Key->GetSource()->GetPathName() : FString();
                        Record.bFoliageTypeAsset = Pair.Key && Pair.Key->IsAsset();
                        Record.InstanceCount = InstanceCount;
                        const int32 InstanceLimit = FMath::Min(InstanceCount, MaxItems);
                        for (int32 InstanceIndex = 0;
                            InstanceIndex < InstanceLimit;
                            ++InstanceIndex)
                        {
                            const FTransform Transform =
                                Pair.Value->Instances[InstanceIndex].GetInstanceWorldTransform();
                            const FVector Location = Transform.GetLocation();
                            const FRotator Rotation = Transform.Rotator();
                            const FVector Scale = Transform.GetScale3D();
                            FThomasFoliageInstanceRecord InstanceRecord;
                            InstanceRecord.Index = InstanceIndex;
                            InstanceRecord.Transform = Transform.ToString();
                            InstanceRecord.LocationX = Location.X;
                            InstanceRecord.LocationY = Location.Y;
                            InstanceRecord.LocationZ = Location.Z;
                            InstanceRecord.RotationPitch = Rotation.Pitch;
                            InstanceRecord.RotationYaw = Rotation.Yaw;
                            InstanceRecord.RotationRoll = Rotation.Roll;
                            InstanceRecord.ScaleX = Scale.X;
                            InstanceRecord.ScaleY = Scale.Y;
                            InstanceRecord.ScaleZ = Scale.Z;
                            Record.Instances.Add(MoveTemp(InstanceRecord));
                        }
                        Record.bInstancesTruncated = InstanceCount > InstanceLimit;
                        Result.Foliage.Add(MoveTemp(Record));
                    }
                }
            }
            if (const AWorldPartitionHLOD* HLOD = Cast<AWorldPartitionHLOD>(Actor))
            {
                ++Result.HLODActorCount;
                if (Result.HLODActors.Num() < MaxItems)
                {
                    Result.HLODActors.Add(HLOD->GetPathName());
                    FThomasHLODActorRecord Record;
                    Record.ActorPath = HLOD->GetPathName();
                    Record.Label = HLOD->GetActorLabel();
                    Record.SourceCellGuid = HLOD->GetSourceCellGuid().ToString(
                        EGuidFormats::DigitsWithHyphens);
                    Record.ResourcesPackagePath = HLOD->GetHLODResourcesPackagePath().ToString();
                    Record.LODLevel = static_cast<int32>(HLOD->GetLODLevel());
                    Record.ComponentCount = HLOD->GetComponents().Num();
                    Record.MinVisibleDistance = HLOD->GetMinVisibleDistance();
                    Record.bStandalone = HLOD->IsStandalone();
                    Record.bCustomHLOD = HLOD->IsCustomHLOD();
                    Record.bRequireWarmup = HLOD->DoesRequireWarmup();
                    const FBox Bounds = HLOD->GetHLODBounds();
                    Record.bBoundsValid = Bounds.IsValid != 0;
                    if (Record.bBoundsValid)
                    {
                        const FVector Center = Bounds.GetCenter();
                        const FVector Extent = Bounds.GetExtent();
                        Record.BoundsCenterX = Center.X;
                        Record.BoundsCenterY = Center.Y;
                        Record.BoundsCenterZ = Center.Z;
                        Record.BoundsExtentX = Extent.X;
                        Record.BoundsExtentY = Extent.Y;
                        Record.BoundsExtentZ = Extent.Z;
                    }
                    Result.HLODDetails.Add(MoveTemp(Record));
                }
            }
            if (const ANavMeshBoundsVolume* BoundsVolume =
                    Cast<ANavMeshBoundsVolume>(Actor))
            {
                ++Result.NavigationBoundsVolumeCount;
                if (Result.NavigationBounds.Num() < MaxItems)
                {
                    FThomasNavigationBoundsRecord Record;
                    Record.ActorPath = BoundsVolume->GetPathName();
                    Record.Label = BoundsVolume->GetActorLabel();
                    Record.SupportedAgentBits = BoundsVolume->SupportedAgents.GetAgentBits();
                    Record.bAllowPhysicsOverlap = BoundsVolume->bAllowPhysicsOverlap;
                    const FBox Bounds = BoundsVolume->GetComponentsBoundingBox(true);
                    Record.bBoundsValid = Bounds.IsValid != 0;
                    if (Record.bBoundsValid)
                    {
                        const FVector Center = Bounds.GetCenter();
                        const FVector Extent = Bounds.GetExtent();
                        Record.BoundsCenterX = Center.X;
                        Record.BoundsCenterY = Center.Y;
                        Record.BoundsCenterZ = Center.Z;
                        Record.BoundsExtentX = Extent.X;
                        Record.BoundsExtentY = Extent.Y;
                        Record.BoundsExtentZ = Extent.Z;
                    }
                    Result.NavigationBounds.Add(MoveTemp(Record));
                }
            }
        }
        return Result;
    }

    virtual FThomasLandscapeRegionInspectionResult InspectLandscapeRegion(
        const FString& ActorPath,
        const int32 MinX,
        const int32 MinY,
        const int32 MaxX,
        const int32 MaxY,
        const FString& EditLayerGuid,
        const FString& LayerInfoPath,
        const int32 MaxReturnedSamples) override
    {
        UWorld* World = GetProjectEditorWorld();
        if (!World)
        {
            return MakeError<FThomasLandscapeRegionInspectionResult>(
                TEXT("project_editor_world_unavailable"),
                TEXT("Open a /Game/PropHunt level in the canonical Editor."));
        }
        if (MaxReturnedSamples < 0
            || MaxReturnedSamples > MaxLandscapeReturnedSamples)
        {
            return MakeError<FThomasLandscapeRegionInspectionResult>(
                TEXT("invalid_sample_limit"),
                FString::Printf(
                    TEXT("MaxReturnedSamples must be between 0 and %d."),
                    MaxLandscapeReturnedSamples));
        }

        ALandscapeProxy* Landscape = nullptr;
        ULandscapeInfo* LandscapeInfo = nullptr;
        FGuid ParsedEditLayerGuid;
        FString Reason;
        if (!ResolveLandscapeDataTarget(
                World,
                ActorPath,
                EditLayerGuid,
                false,
                Landscape,
                LandscapeInfo,
                ParsedEditLayerGuid,
                Reason))
        {
            return MakeError<FThomasLandscapeRegionInspectionResult>(
                TEXT("landscape_data_target_invalid"), Reason);
        }
        ULandscapeLayerInfoObject* LayerInfo = nullptr;
        if (!LayerInfoPath.IsEmpty())
        {
            LayerInfo = ResolveLandscapeLayerInfo(LayerInfoPath);
            if (!LayerInfo)
            {
                return MakeError<FThomasLandscapeRegionInspectionResult>(
                    TEXT("landscape_layer_info_invalid"), LayerInfoPath);
            }
        }

        int32 Width = 0;
        int32 Height = 0;
        if (!ValidateLandscapeRegion(
                LandscapeInfo,
                MinX,
                MinY,
                MaxX,
                MaxY,
                Width,
                Height,
                Reason))
        {
            return MakeError<FThomasLandscapeRegionInspectionResult>(
                TEXT("landscape_region_invalid"), Reason);
        }
        TArray<uint16> Heights;
        TArray<uint8> Weights;
        if (!ReadLandscapeRegionData(
                LandscapeInfo,
                ParsedEditLayerGuid,
                LayerInfo,
                MinX,
                MinY,
                MaxX,
                MaxY,
                Heights,
                Weights,
                Reason))
        {
            return MakeError<FThomasLandscapeRegionInspectionResult>(
                TEXT("landscape_region_read_failed"), Reason);
        }

        FThomasLandscapeRegionInspectionResult Result;
        Result.bOk = true;
        Result.MapPath = World->GetOutermost()->GetName();
        Result.Revision = BuildWorldRevision(World);
        Result.ActorPath = Landscape->GetPathName();
        Result.EditLayerGuid = EditLayerGuid;
        Result.LayerInfoPath = LayerInfo ? LayerInfo->GetPathName() : FString();
        Result.MinX = MinX;
        Result.MinY = MinY;
        Result.MaxX = MaxX;
        Result.MaxY = MaxY;
        Result.Width = Width;
        Result.Height = Height;
        Result.SampleCount = Heights.Num();
        Result.HeightDataHash = LandscapeDataHash(
            Heights.GetData(),
            static_cast<int64>(Heights.Num()) * sizeof(uint16));
        uint64 HeightTotal = 0;
        Result.MinHeightValue = MAX_uint16;
        Result.MaxHeightValue = 0;
        for (const uint16 HeightValue : Heights)
        {
            Result.MinHeightValue = FMath::Min(
                Result.MinHeightValue, static_cast<int32>(HeightValue));
            Result.MaxHeightValue = FMath::Max(
                Result.MaxHeightValue, static_cast<int32>(HeightValue));
            HeightTotal += HeightValue;
        }
        Result.AverageHeightValue = Heights.IsEmpty()
            ? 0.0 : static_cast<double>(HeightTotal) / Heights.Num();
        if (!Weights.IsEmpty())
        {
            Result.WeightDataHash = LandscapeDataHash(
                Weights.GetData(),
                static_cast<int64>(Weights.Num()) * sizeof(uint8));
            uint64 WeightTotal = 0;
            Result.MinWeightValue = MAX_uint8;
            Result.MaxWeightValue = 0;
            for (const uint8 WeightValue : Weights)
            {
                Result.MinWeightValue = FMath::Min(
                    Result.MinWeightValue, static_cast<int32>(WeightValue));
                Result.MaxWeightValue = FMath::Max(
                    Result.MaxWeightValue, static_cast<int32>(WeightValue));
                WeightTotal += WeightValue;
            }
            Result.AverageWeightValue = static_cast<double>(WeightTotal) / Weights.Num();
        }
        const int32 ReturnedCount = FMath::Min(
            MaxReturnedSamples, Heights.Num());
        Result.HeightSamples.Reserve(ReturnedCount);
        Result.WeightSamples.Reserve(LayerInfo ? ReturnedCount : 0);
        for (int32 Index = 0; Index < ReturnedCount; ++Index)
        {
            Result.HeightSamples.Add(Heights[Index]);
            if (LayerInfo)
            {
                Result.WeightSamples.Add(Weights[Index]);
            }
        }
        Result.bSamplesTruncated = ReturnedCount < Heights.Num();
        if (Result.bSamplesTruncated)
        {
            Result.Diagnostics.Add(FString::Printf(
                TEXT("Samples truncated to %d; hashes cover all %d vertices."),
                ReturnedCount,
                Heights.Num()));
        }
        if (EditLayerGuid.IsEmpty())
        {
            Result.Diagnostics.Add(TEXT("Base Landscape layer inspected explicitly."));
        }
        return Result;
    }

    virtual FThomasLandscapeLayerInfoCreatePlanResult PlanLandscapeLayerInfoCreate(
        const FThomasLandscapeLayerInfoCreateRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasLandscapeLayerInfoCreateRequest Request = InputRequest;
        Request.AssetPath = NormalizeLandscapeLayerInfoPackagePath(Request.AssetPath);
        Request.LayerName = Request.LayerName.TrimStartAndEnd();
        FString PathReason;
        if (!IsAllowedLandscapeLayerInfoPackagePath(Request.AssetPath, &PathReason))
        {
            return MakeError<FThomasLandscapeLayerInfoCreatePlanResult>(
                TEXT("path_denied"), PathReason);
        }
        if (Request.LayerName.IsEmpty() || Request.LayerName.Len() > NAME_SIZE)
        {
            return MakeError<FThomasLandscapeLayerInfoCreatePlanResult>(
                TEXT("invalid_layer_name"),
                TEXT("LayerName must contain 1 to 1024 characters."));
        }

        ULandscapeLayerInfoObject* Existing =
            LoadLandscapeLayerInfoAsset(Request.AssetPath);
        const FString Revision = BuildLandscapeLayerInfoRevision(
            Request.AssetPath, Existing);
        if (Revision != TEXT("missing"))
        {
            return MakeError<FThomasLandscapeLayerInfoCreatePlanResult>(
                TEXT("asset_exists"), Revision);
        }
        if (Request.ExpectedRevision != Revision)
        {
            return MakeError<FThomasLandscapeLayerInfoCreatePlanResult>(
                TEXT("revision_conflict"), Revision);
        }

        FThomasLandscapeLayerInfoCreatePlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.AssetPath = Request.AssetPath;
        Result.ObjectPath = LandscapeLayerInfoObjectPath(Request.AssetPath);
        Result.ExpectedRevision = Revision;
        Result.LayerName = Request.LayerName;
        Result.bNoWeightBlend = Request.bNoWeightBlend;
        Result.Preview.Add(FString::Printf(
            TEXT("Create LandscapeLayerInfoObject %s for target '%s' (NoWeightBlend=%d)"),
            *Result.ObjectPath,
            *Request.LayerName,
            Request.bNoWeightBlend ? 1 : 0));

        FLandscapeLayerInfoCreatePlan Plan;
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        LandscapeLayerInfoPlans.Add(Result.PlanId, MoveTemp(Plan));
        return Result;
    }

    virtual FThomasLandscapeLayerInfoCreateApplyResult ApplyLandscapeLayerInfoCreate(
        const FString& PlanId) override
    {
        FLandscapeLayerInfoCreatePlan Plan;
        if (FLandscapeLayerInfoCreatePlan* Found = LandscapeLayerInfoPlans.Find(PlanId))
        {
            Plan = MoveTemp(*Found);
            LandscapeLayerInfoPlans.Remove(PlanId);
        }
        else
        {
            return MakeError<FThomasLandscapeLayerInfoCreateApplyResult>(
                TEXT("plan_not_found"),
                TEXT("Plan is missing, expired, or already consumed."));
        }
        if (FPlatformTime::Seconds() - Plan.CreatedAtSeconds > PlanLifetimeSeconds)
        {
            return MakeError<FThomasLandscapeLayerInfoCreateApplyResult>(
                TEXT("plan_expired"), TEXT("Create a fresh plan."));
        }

        const FString RevisionBefore = BuildLandscapeLayerInfoRevision(
            Plan.Request.AssetPath,
            LoadLandscapeLayerInfoAsset(Plan.Request.AssetPath));
        if (RevisionBefore != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasLandscapeLayerInfoCreateApplyResult>(
                TEXT("revision_conflict"), RevisionBefore);
        }

        FThomasLandscapeLayerInfoCreateApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.ObjectPath = LandscapeLayerInfoObjectPath(Plan.Request.AssetPath);
        Result.RevisionBefore = RevisionBefore;

        FScopedTransaction Transaction(NSLOCTEXT(
            "ThomasEditor",
            "CreateLandscapeLayerInfo",
            "ThomasEditor Create Landscape Layer Info"));
        UPackage* Package = CreatePackage(*Plan.Request.AssetPath);
        if (!Package)
        {
            Transaction.Cancel();
            return MakeError<FThomasLandscapeLayerInfoCreateApplyResult>(
                TEXT("package_create_failed"), Plan.Request.AssetPath);
        }
        Package->Modify();
        ULandscapeLayerInfoObject* LayerInfo =
            NewObject<ULandscapeLayerInfoObject>(
                Package,
                FName(*FPackageName::GetLongPackageAssetName(Plan.Request.AssetPath)),
                RF_Public | RF_Standalone | RF_Transactional);
        if (!LayerInfo)
        {
            Transaction.Cancel();
            return MakeError<FThomasLandscapeLayerInfoCreateApplyResult>(
                TEXT("asset_create_failed"), Plan.Request.AssetPath);
        }
        LayerInfo->Modify();
        LayerInfo->SetLayerName(FName(*Plan.Request.LayerName), false);
        LayerInfo->SetBlendMethod(
            Plan.Request.bNoWeightBlend
                ? ELandscapeTargetLayerBlendMethod::None
                : static_cast<ELandscapeTargetLayerBlendMethod>(1),
            false);
        LayerInfo->PostEditChange();
        FAssetRegistryModule::AssetCreated(LayerInfo);
        Package->MarkPackageDirty();
        Result.bCreated = true;

        const FString Filename = FPackageName::LongPackageNameToFilename(
            Plan.Request.AssetPath, FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        SaveArgs.Error = GError;
        Result.bSaved = UPackage::SavePackage(
            Package, LayerInfo, *Filename, SaveArgs);
        if (!Result.bSaved)
        {
            Result.bRolledBack = ObjectTools::DeleteObjectsUnchecked({LayerInfo}) == 1;
            Package->SetDirtyFlag(false);
            Transaction.Cancel();
            Result.Code = TEXT("save_failed");
            Result.Message = TEXT("LayerInfo save failed; the new asset was removed.");
            Result.RevisionAfter = BuildLandscapeLayerInfoRevision(
                Plan.Request.AssetPath, nullptr);
            return Result;
        }

        Result.RevisionAfter = BuildLandscapeLayerInfoRevision(
            Plan.Request.AssetPath, LayerInfo);
        if (Result.RevisionAfter == TEXT("missing"))
        {
            ObjectTools::DeleteObjectsUnchecked({LayerInfo});
            Package->SetDirtyFlag(false);
            Transaction.Cancel();
            Result.bRolledBack = true;
            Result.bSaved = false;
            Result.Code = TEXT("verification_failed");
            Result.Message = TEXT("LayerInfo did not persist after save.");
            return Result;
        }
        Result.bOk = true;
        return Result;
    }

    virtual FThomasPostProcessInspectionResult InspectPostProcessVolume(
        const FString& ActorPath,
        const FString& SettingFilter,
        const int32 MaxSettings) override
    {
        if (MaxSettings < 1 || MaxSettings > MaxPostProcessSettings)
        {
            return MakeError<FThomasPostProcessInspectionResult>(
                TEXT("invalid_limit"), TEXT("MaxSettings must be between 1 and 500."));
        }
        UWorld* World = GetProjectEditorWorld();
        APostProcessVolume* Volume = ResolvePostProcessVolume(World, ActorPath);
        if (!World || !Volume)
        {
            return MakeError<FThomasPostProcessInspectionResult>(
                TEXT("post_process_volume_not_found"), ActorPath);
        }

        FThomasPostProcessInspectionResult Result;
        Result.bOk = true;
        Result.MapPath = World->GetOutermost()->GetName();
        Result.Revision = BuildWorldRevision(World);
        Result.ActorPath = Volume->GetPathName();
        Result.Label = Volume->GetActorLabel();
        Result.bEnabled = Volume->bEnabled;
        Result.bUnbound = Volume->bUnbound;
        Result.Priority = Volume->Priority;
        Result.BlendRadius = Volume->BlendRadius;
        Result.BlendWeight = Volume->BlendWeight;

        const FString Filter = SettingFilter.TrimStartAndEnd().ToLower();
        for (TFieldIterator<FProperty> It(FPostProcessSettings::StaticStruct()); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property
                || Property->GetName().StartsWith(TEXT("bOverride_"))
                || Property->GetFName() == TEXT("WeightedBlendables"))
            {
                continue;
            }
            const FString Search = (
                Property->GetName()
                + TEXT(" ") + Property->GetMetaData(TEXT("Category"))
                + TEXT(" ") + Property->GetMetaData(TEXT("ToolTip"))).ToLower();
            if (!Filter.IsEmpty()
                && !Search.Contains(Filter)
                && !(Filter == TEXT("lumen") && IsLumenRelated(Property)))
            {
                continue;
            }
            ++Result.MatchedSettingCount;
            if (Result.Settings.Num() >= MaxSettings)
            {
                Result.bTruncated = true;
                continue;
            }
            FThomasPostProcessSettingRecord Record;
            Record.Name = Property->GetName();
            Record.Type = Property->GetCPPType();
            Record.Value = ExportStructProperty(&Volume->Settings, Property);
            Record.Category = Property->GetMetaData(TEXT("Category"));
            Record.Tooltip = Property->GetMetaData(TEXT("ToolTip")).Left(1200);
            Record.bEditable = IsSupportedPostProcessProperty(Property);
            Record.bLumenRelated = IsLumenRelated(Property);
            if (FBoolProperty* OverrideProperty = FindOverrideProperty(Property))
            {
                Record.OverrideName = OverrideProperty->GetName();
                Record.bHasOverride = true;
                Record.bOverrideEnabled = OverrideProperty->GetPropertyValue_InContainer(
                    &Volume->Settings);
            }
            Result.Settings.Add(MoveTemp(Record));
        }

        const TArray<FWeightedBlendable>& Blendables = Volume->Settings.WeightedBlendables.Array;
        for (int32 Index = 0; Index < Blendables.Num() && Index < 100; ++Index)
        {
            const FWeightedBlendable& Blendable = Blendables[Index];
            UObject* Object = Blendable.Object.Get();
            FThomasPostProcessBlendableRecord Record;
            Record.Index = Index;
            Record.ObjectPath = Object ? Object->GetPathName() : FString();
            Record.ClassPath = Object ? Object->GetClass()->GetPathName() : FString();
            Record.Weight = Blendable.Weight;
            Result.Blendables.Add(MoveTemp(Record));
        }
        Result.bTruncated |= Blendables.Num() > 100;
        return Result;
    }

    virtual FThomasPostProcessPlanResult PlanPostProcessPatch(
        const FThomasPostProcessPatchRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        UWorld* World = GetProjectEditorWorld();
        if (!World)
        {
            return MakeError<FThomasPostProcessPlanResult>(
                TEXT("project_editor_world_unavailable"),
                TEXT("Open a /Game/PropHunt level in the canonical Editor."));
        }
        if (GEditor && GEditor->PlayWorld)
        {
            return MakeError<FThomasPostProcessPlanResult>(
                TEXT("pie_active"), TEXT("Stop PIE before planning Post Process mutations."));
        }
        FThomasPostProcessPatchRequest Request = InputRequest;
        const FString CurrentMap = World->GetOutermost()->GetName();
        const FString CurrentRevision = BuildWorldRevision(World);
        if (Request.MapPath != CurrentMap)
        {
            return MakeError<FThomasPostProcessPlanResult>(TEXT("map_mismatch"), CurrentMap);
        }
        if (Request.ExpectedRevision.IsEmpty() || Request.ExpectedRevision != CurrentRevision)
        {
            return MakeError<FThomasPostProcessPlanResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }
        if (Request.Operations.Num() < 1 || Request.Operations.Num() > MaxPostProcessOperations)
        {
            return MakeError<FThomasPostProcessPlanResult>(
                TEXT("invalid_batch_size"), TEXT("Post Process batch must contain 1 to 100 operations."));
        }
        APostProcessVolume* Volume = ResolvePostProcessVolume(World, Request.ActorPath);
        if (!Volume)
        {
            return MakeError<FThomasPostProcessPlanResult>(
                TEXT("post_process_volume_not_found"), Request.ActorPath);
        }

        bool bDestructive = false;
        TArray<FString> Preview;
        FPostProcessSettings Candidate = Volume->Settings;
        for (const FThomasPostProcessOperation& Operation : Request.Operations)
        {
            const FString Action = Operation.Action.ToLower();
            if (Action == TEXT("set_setting"))
            {
                FProperty* Property = FindFProperty<FProperty>(
                    FPostProcessSettings::StaticStruct(), FName(*Operation.SettingName));
                const FString Current = ExportStructProperty(&Candidate, Property);
                if (!IsSupportedPostProcessProperty(Property))
                {
                    return MakeError<FThomasPostProcessPlanResult>(
                        TEXT("setting_not_editable"), Operation.SettingName);
                }
                if (Operation.ExpectedValue.IsEmpty() || Operation.ExpectedValue != Current)
                {
                    return MakeError<FThomasPostProcessPlanResult>(
                        TEXT("setting_conflict"), Current);
                }
                if (!ImportStructProperty(&Candidate, Property, Operation.Value, Volume))
                {
                    return MakeError<FThomasPostProcessPlanResult>(
                        TEXT("setting_value_invalid"), Operation.Value);
                }
                Preview.Add(TEXT("Set Settings.") + Operation.SettingName);
            }
            else if (Action == TEXT("set_override"))
            {
                FProperty* Property = FindFProperty<FProperty>(
                    FPostProcessSettings::StaticStruct(), FName(*Operation.SettingName));
                FBoolProperty* OverrideProperty = FindOverrideProperty(Property);
                if (!IsSupportedPostProcessProperty(Property) || !OverrideProperty)
                {
                    return MakeError<FThomasPostProcessPlanResult>(
                        TEXT("override_not_found"), Operation.SettingName);
                }
                const bool bCurrent = OverrideProperty->GetPropertyValue_InContainer(&Candidate);
                if (bCurrent != Operation.bExpectedOverride)
                {
                    return MakeError<FThomasPostProcessPlanResult>(
                        TEXT("override_conflict"), bCurrent ? TEXT("true") : TEXT("false"));
                }
                OverrideProperty->SetPropertyValue_InContainer(&Candidate, Operation.bOverride);
                Preview.Add(TEXT("Set override Settings.") + Operation.SettingName);
            }
            else if (Action == TEXT("add_blendable"))
            {
                UMaterialInterface* Material = LoadPostProcessMaterial(Operation.ObjectPath);
                if (!Material || !FMath::IsFinite(Operation.Weight) || Operation.Weight < 0.0f)
                {
                    return MakeError<FThomasPostProcessPlanResult>(
                        TEXT("invalid_post_process_blendable"), Operation.ObjectPath);
                }
                const bool bAlreadyPresent = Candidate.WeightedBlendables.Array.ContainsByPredicate(
                    [Material](const FWeightedBlendable& Item)
                    {
                        return Item.Object.Get() == Material;
                    });
                if (bAlreadyPresent)
                {
                    return MakeError<FThomasPostProcessPlanResult>(
                        TEXT("blendable_already_present"), Operation.ObjectPath);
                }
                FWeightedBlendable Blendable;
                Blendable.Object = Material;
                Blendable.Weight = Operation.Weight;
                Candidate.WeightedBlendables.Array.Add(MoveTemp(Blendable));
                Preview.Add(TEXT("Add blendable ") + Operation.ObjectPath);
            }
            else if (Action == TEXT("set_blendable_weight")
                || Action == TEXT("remove_blendable"))
            {
                if (!Candidate.WeightedBlendables.Array.IsValidIndex(Operation.BlendableIndex))
                {
                    return MakeError<FThomasPostProcessPlanResult>(
                        TEXT("blendable_index_invalid"), FString::FromInt(Operation.BlendableIndex));
                }
                FWeightedBlendable& Blendable =
                    Candidate.WeightedBlendables.Array[Operation.BlendableIndex];
                UObject* Object = Blendable.Object.Get();
                const FString CurrentPath = Object ? Object->GetPathName() : FString();
                if (CurrentPath != Operation.ObjectPath
                    || !FMath::IsNearlyEqual(Blendable.Weight, Operation.ExpectedWeight))
                {
                    return MakeError<FThomasPostProcessPlanResult>(
                        TEXT("blendable_conflict"), CurrentPath);
                }
                if (Action == TEXT("set_blendable_weight"))
                {
                    if (!FMath::IsFinite(Operation.Weight) || Operation.Weight < 0.0f)
                    {
                        return MakeError<FThomasPostProcessPlanResult>(
                            TEXT("invalid_blendable_weight"), FString::SanitizeFloat(Operation.Weight));
                    }
                    Blendable.Weight = Operation.Weight;
                    Preview.Add(TEXT("Set blendable weight ") + Operation.ObjectPath);
                }
                else
                {
                    Candidate.WeightedBlendables.Array.RemoveAt(Operation.BlendableIndex);
                    bDestructive = true;
                    Preview.Add(TEXT("Remove blendable ") + Operation.ObjectPath);
                }
            }
            else
            {
                return MakeError<FThomasPostProcessPlanResult>(
                    TEXT("unsupported_operation"), Operation.Action);
            }
        }
        if (bDestructive && !Request.bConfirmDestructive)
        {
            return MakeError<FThomasPostProcessPlanResult>(
                TEXT("confirmation_required"),
                TEXT("Removing a Post Process blendable requires bConfirmDestructive=true."));
        }

        FThomasPostProcessPlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.MapPath = CurrentMap;
        Result.ExpectedRevision = CurrentRevision;
        Result.ActorPath = Volume->GetPathName();
        Result.Risk = bDestructive ? TEXT("R2") : TEXT("R1");
        Result.OperationCount = Request.Operations.Num();
        Result.Preview = MoveTemp(Preview);
        FPostProcessPatchPlan& Plan = PostProcessPlans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasPostProcessApplyResult ApplyPostProcessPlan(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FPostProcessPatchPlan Plan;
        if (!PostProcessPlans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasPostProcessApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        UWorld* World = GetProjectEditorWorld();
        if (!World || (GEditor && GEditor->PlayWorld))
        {
            return MakeError<FThomasPostProcessApplyResult>(
                TEXT("world_unavailable"), TEXT("The canonical Editor world must be open outside PIE."));
        }
        const FString CurrentRevision = BuildWorldRevision(World);
        APostProcessVolume* Volume = ResolvePostProcessVolume(World, Plan.Request.ActorPath);
        if (!Volume
            || World->GetOutermost()->GetName() != Plan.Request.MapPath
            || CurrentRevision != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasPostProcessApplyResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }

        FThomasPostProcessApplyResult Result;
        Result.MapPath = Plan.Request.MapPath;
        Result.ActorPath = Plan.Request.ActorPath;
        Result.RevisionBefore = CurrentRevision;
        bool bApplied = true;
        FString Error;
        {
            const FScopedTransaction Transaction(
                NSLOCTEXT("ThomasEditor", "ApplyPostProcessPatch", "ThomasEditor Post Process Patch"));
            World->Modify();
            if (World->PersistentLevel) World->PersistentLevel->Modify();
            Volume->Modify();
            for (const FThomasPostProcessOperation& Operation : Plan.Request.Operations)
            {
                const FString Action = Operation.Action.ToLower();
                if (Action == TEXT("set_setting"))
                {
                    FProperty* Property = FindFProperty<FProperty>(
                        FPostProcessSettings::StaticStruct(), FName(*Operation.SettingName));
                    const FString Current = ExportStructProperty(&Volume->Settings, Property);
                    bApplied = IsSupportedPostProcessProperty(Property)
                        && Current == Operation.ExpectedValue
                        && ImportStructProperty(&Volume->Settings, Property, Operation.Value, Volume);
                }
                else if (Action == TEXT("set_override"))
                {
                    FProperty* Property = FindFProperty<FProperty>(
                        FPostProcessSettings::StaticStruct(), FName(*Operation.SettingName));
                    FBoolProperty* OverrideProperty = FindOverrideProperty(Property);
                    bApplied = IsSupportedPostProcessProperty(Property) && OverrideProperty
                        && OverrideProperty->GetPropertyValue_InContainer(&Volume->Settings)
                            == Operation.bExpectedOverride;
                    if (bApplied)
                    {
                        OverrideProperty->SetPropertyValue_InContainer(
                            &Volume->Settings, Operation.bOverride);
                    }
                }
                else if (Action == TEXT("add_blendable"))
                {
                    UMaterialInterface* Material = LoadPostProcessMaterial(Operation.ObjectPath);
                    bApplied = Material && FMath::IsFinite(Operation.Weight)
                        && Operation.Weight >= 0.0f
                        && !Volume->Settings.WeightedBlendables.Array.ContainsByPredicate(
                            [Material](const FWeightedBlendable& Item)
                            {
                                return Item.Object.Get() == Material;
                            });
                    if (bApplied)
                    {
                        FWeightedBlendable Blendable;
                        Blendable.Object = Material;
                        Blendable.Weight = Operation.Weight;
                        Volume->Settings.WeightedBlendables.Array.Add(MoveTemp(Blendable));
                    }
                }
                else if (Action == TEXT("set_blendable_weight")
                    || Action == TEXT("remove_blendable"))
                {
                    bApplied = Volume->Settings.WeightedBlendables.Array.IsValidIndex(
                        Operation.BlendableIndex);
                    if (bApplied)
                    {
                        FWeightedBlendable& Blendable =
                            Volume->Settings.WeightedBlendables.Array[Operation.BlendableIndex];
                        UObject* Object = Blendable.Object.Get();
                        const FString CurrentPath = Object ? Object->GetPathName() : FString();
                        bApplied = CurrentPath == Operation.ObjectPath
                            && FMath::IsNearlyEqual(Blendable.Weight, Operation.ExpectedWeight);
                        if (bApplied && Action == TEXT("set_blendable_weight"))
                        {
                            bApplied = FMath::IsFinite(Operation.Weight) && Operation.Weight >= 0.0f;
                            if (bApplied) Blendable.Weight = Operation.Weight;
                        }
                        else if (bApplied)
                        {
                            Volume->Settings.WeightedBlendables.Array.RemoveAt(
                                Operation.BlendableIndex);
                        }
                    }
                }
                if (!bApplied)
                {
                    Error = Operation.Action + TEXT(" failed for ") + Operation.SettingName;
                    break;
                }
                ++Result.AppliedOperationCount;
            }
            if (bApplied)
            {
                Volume->PostEditChange();
                World->MarkPackageDirty();
            }
        }
        if (!bApplied)
        {
            Result.bRolledBack = GEditor && GEditor->UndoTransaction();
            Result.Code = TEXT("apply_failed");
            Result.Message = Error.Left(1200);
            Result.RevisionAfter = BuildWorldRevision(World);
            return Result;
        }
        if (bSave)
        {
            Result.bSaved = SaveWorld(World);
            if (!Result.bSaved)
            {
                Result.bRolledBack = GEditor && GEditor->UndoTransaction();
                Result.Code = TEXT("save_failed");
                Result.Message = TEXT("Map save failed; the Post Process transaction was undone.");
                Result.RevisionAfter = BuildWorldRevision(World);
                return Result;
            }
        }
        if (GEditor) GEditor->RedrawLevelEditingViewports(true);
        Result.bOk = true;
        Result.RevisionAfter = BuildWorldRevision(World);
        return Result;
    }

    virtual FThomasMapLifecyclePlanResult PlanMapLifecycle(
        const FThomasMapLifecycleRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        UWorld* World = GetProjectEditorWorld();
        if (!World)
        {
            return MakeError<FThomasMapLifecyclePlanResult>(
                TEXT("project_editor_world_unavailable"),
                TEXT("Open a /Game/PropHunt level in the canonical Editor."));
        }
        if (GEditor && GEditor->PlayWorld)
        {
            return MakeError<FThomasMapLifecyclePlanResult>(
                TEXT("pie_active"), TEXT("Stop PIE before planning a map lifecycle operation."));
        }

        FThomasMapLifecycleRequest Request = InputRequest;
        Request.Action = Request.Action.ToLower();
        const FString CurrentMap = World->GetOutermost()->GetName();
        const FString CurrentRevision = BuildWorldRevision(World);
        if (Request.CurrentMapPath != CurrentMap)
        {
            return MakeError<FThomasMapLifecyclePlanResult>(TEXT("map_mismatch"), CurrentMap);
        }
        if (Request.ExpectedRevision.IsEmpty() || Request.ExpectedRevision != CurrentRevision)
        {
            return MakeError<FThomasMapLifecyclePlanResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }

        TArray<FString> Preview;
        FString Risk = TEXT("R1");
        if (Request.Action == TEXT("save_current_map"))
        {
            if (!Request.TargetMapPath.IsEmpty())
            {
                return MakeError<FThomasMapLifecyclePlanResult>(
                    TEXT("target_not_allowed"),
                    TEXT("save_current_map saves the exact current package; Save As is not implicit."));
            }
            Preview.Add(TEXT("Save current map ") + CurrentMap);
        }
        else if (Request.Action == TEXT("create_blank_map")
            || Request.Action == TEXT("create_world_partition_map")
            || Request.Action == TEXT("open_map"))
        {
            FString PathReason;
            if (!IsAllowedProjectMapPath(Request.TargetMapPath, &PathReason))
            {
                return MakeError<FThomasMapLifecyclePlanResult>(
                    TEXT("target_map_path_invalid"), PathReason);
            }
            if (Request.TargetMapPath == CurrentMap)
            {
                return MakeError<FThomasMapLifecyclePlanResult>(
                    TEXT("target_is_current_map"), CurrentMap);
            }
            if (World->GetOutermost()->IsDirty())
            {
                return MakeError<FThomasMapLifecyclePlanResult>(
                    TEXT("current_map_dirty"),
                    TEXT("Save or undo current map changes before switching worlds."));
            }
            if (!Request.bConfirmWorldSwitch)
            {
                return MakeError<FThomasMapLifecyclePlanResult>(
                    TEXT("confirmation_required"),
                    TEXT("Replacing the current Editor world requires bConfirmWorldSwitch=true."));
            }
            const bool bTargetExists = IFileManager::Get().FileExists(
                *MapFilename(Request.TargetMapPath));
            if ((Request.Action == TEXT("create_blank_map")
                    || Request.Action == TEXT("create_world_partition_map"))
                && bTargetExists)
            {
                return MakeError<FThomasMapLifecyclePlanResult>(
                    TEXT("target_map_exists"), Request.TargetMapPath);
            }
            if (Request.Action == TEXT("open_map") && !bTargetExists)
            {
                return MakeError<FThomasMapLifecyclePlanResult>(
                    TEXT("target_map_missing"), Request.TargetMapPath);
            }
            Risk = TEXT("R2");
            Preview.Add((Request.Action == TEXT("create_blank_map")
                ? TEXT("Create, save and open blank map ")
                : Request.Action == TEXT("create_world_partition_map")
                    ? TEXT("Create, convert, save and open World Partition map ")
                    : TEXT("Open existing map ")) + Request.TargetMapPath);
            Preview.Add(TEXT("Replace current Editor world ") + CurrentMap);
        }
        else
        {
            return MakeError<FThomasMapLifecyclePlanResult>(
                TEXT("unsupported_operation"), Request.Action);
        }

        FThomasMapLifecyclePlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.Action = Request.Action;
        Result.CurrentMapPath = CurrentMap;
        Result.ExpectedRevision = CurrentRevision;
        Result.TargetMapPath = Request.TargetMapPath;
        Result.Risk = Risk;
        Result.Preview = MoveTemp(Preview);
        FMapLifecyclePlan& Plan = MapPlans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasMapLifecycleApplyResult ApplyMapLifecyclePlan(
        const FString& PlanId) override
    {
        PurgeExpiredPlans();
        FMapLifecyclePlan Plan;
        if (!MapPlans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasMapLifecycleApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        UWorld* World = GetProjectEditorWorld();
        if (!World || (GEditor && GEditor->PlayWorld))
        {
            return MakeError<FThomasMapLifecycleApplyResult>(
                TEXT("world_unavailable"),
                TEXT("The canonical Editor world must be open outside PIE."));
        }
        const FString CurrentMap = World->GetOutermost()->GetName();
        const FString CurrentRevision = BuildWorldRevision(World);
        if (CurrentMap != Plan.Request.CurrentMapPath
            || CurrentRevision != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasMapLifecycleApplyResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }

        FThomasMapLifecycleApplyResult Result;
        Result.Action = Plan.Request.Action;
        Result.PreviousMapPath = CurrentMap;
        Result.MapPath = CurrentMap;
        Result.RevisionBefore = CurrentRevision;
        if (Plan.Request.Action == TEXT("save_current_map"))
        {
            Result.bSaved = UEditorLoadingAndSavingUtils::SaveMap(World, CurrentMap);
            if (!Result.bSaved)
            {
                Result.Code = TEXT("save_failed");
                Result.Message = TEXT("The current map could not be saved.");
                Result.RevisionAfter = BuildWorldRevision(World);
                return Result;
            }
            Result.bOk = true;
            Result.RevisionAfter = BuildWorldRevision(World);
            return Result;
        }

        if (World->GetOutermost()->IsDirty())
        {
            Result.Code = TEXT("current_map_dirty");
            Result.Message = TEXT("The current map changed after planning; world switch refused.");
            Result.RevisionAfter = CurrentRevision;
            return Result;
        }
        const bool bTargetExists = IFileManager::Get().FileExists(
            *MapFilename(Plan.Request.TargetMapPath));
        if (((Plan.Request.Action == TEXT("create_blank_map")
                || Plan.Request.Action == TEXT("create_world_partition_map"))
                && bTargetExists)
            || (Plan.Request.Action == TEXT("open_map") && !bTargetExists))
        {
            Result.Code = TEXT("target_state_conflict");
            Result.Message = Plan.Request.TargetMapPath;
            Result.RevisionAfter = CurrentRevision;
            return Result;
        }

        UWorld* TargetWorld = nullptr;
        if (Plan.Request.Action == TEXT("open_map"))
        {
            TargetWorld = UEditorLoadingAndSavingUtils::LoadMap(
                MapFilename(Plan.Request.TargetMapPath));
        }
        else if (Plan.Request.Action == TEXT("create_blank_map")
            || Plan.Request.Action == TEXT("create_world_partition_map"))
        {
            TargetWorld = UEditorLoadingAndSavingUtils::NewBlankMap(false);
            bool bPrepared = TargetWorld != nullptr;
            if (bPrepared && Plan.Request.Action == TEXT("create_world_partition_map"))
            {
                FWorldPartitionConverter::FParameters Parameters;
                Parameters.bConvertSubLevels = false;
                Parameters.bEnableStreaming = true;
                Parameters.bEnableLoadingInEditor = true;
                Parameters.bUseActorFolders = true;
                bPrepared = FWorldPartitionConverter::Convert(TargetWorld, Parameters)
                    && TargetWorld->GetWorldPartition() != nullptr;
            }
            if (bPrepared)
            {
                Result.bSaved = UEditorLoadingAndSavingUtils::SaveMap(
                    TargetWorld, Plan.Request.TargetMapPath);
            }
            if (!bPrepared || !Result.bSaved)
            {
                Result.bRecoveryAttempted = true;
                UEditorLoadingAndSavingUtils::LoadMap(MapFilename(CurrentMap));
                Result.Code = TEXT("create_or_save_failed");
                Result.Message = TEXT("Blank map creation/save failed; reopening the previous map was attempted.");
                if (UWorld* RecoveredWorld = GetProjectEditorWorld())
                {
                    Result.MapPath = RecoveredWorld->GetOutermost()->GetName();
                    Result.RevisionAfter = BuildWorldRevision(RecoveredWorld);
                }
                return Result;
            }
        }

        UWorld* ActiveWorld = GEditor
            ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!TargetWorld || !ActiveWorld
            || ActiveWorld->GetOutermost()->GetName() != Plan.Request.TargetMapPath)
        {
            Result.Code = TEXT("world_switch_failed");
            Result.Message = Plan.Request.TargetMapPath;
            if (ActiveWorld)
            {
                Result.MapPath = ActiveWorld->GetOutermost()->GetName();
                Result.RevisionAfter = BuildWorldRevision(ActiveWorld);
            }
            return Result;
        }
        Result.bOk = true;
        Result.bWorldSwitched = true;
        Result.MapPath = Plan.Request.TargetMapPath;
        Result.RevisionAfter = BuildWorldRevision(ActiveWorld);
        return Result;
    }

    virtual FThomasEnvironmentBuildPlanResult PlanEnvironmentBuild(
        const FThomasEnvironmentBuildRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        UWorld* World = GetProjectEditorWorld();
        if (!World || (GEditor && GEditor->PlayWorld))
        {
            return MakeError<FThomasEnvironmentBuildPlanResult>(
                TEXT("world_unavailable"),
                TEXT("Open the canonical project map outside PIE before planning a builder job."));
        }
        for (const TPair<FString, FEnvironmentBuildJob>& Pair : EnvironmentJobs)
        {
            if (Pair.Value.bProcessRunning)
            {
                return MakeError<FThomasEnvironmentBuildPlanResult>(
                    TEXT("environment_job_running"), Pair.Key);
            }
        }

        FThomasEnvironmentBuildRequest Request = InputRequest;
        Request.Action = Request.Action.ToLower();
        const FString CurrentMap = World->GetOutermost()->GetName();
        const FString CurrentRevision = BuildWorldRevision(World);
        if (Request.MapPath != CurrentMap)
        {
            return MakeError<FThomasEnvironmentBuildPlanResult>(
                TEXT("map_mismatch"), CurrentMap);
        }
        if (Request.ExpectedRevision.IsEmpty() || Request.ExpectedRevision != CurrentRevision)
        {
            return MakeError<FThomasEnvironmentBuildPlanResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }
        if (!World->GetWorldPartition())
        {
            return MakeError<FThomasEnvironmentBuildPlanResult>(
                TEXT("world_partition_required"),
                TEXT("These external builders are restricted to World Partition maps."));
        }
        if (World->GetOutermost()->IsDirty())
        {
            return MakeError<FThomasEnvironmentBuildPlanResult>(
                TEXT("current_map_dirty"),
                TEXT("Save or undo the current map before starting an external builder."));
        }
        if (!IFileManager::Get().FileExists(*MapFilename(CurrentMap)))
        {
            return MakeError<FThomasEnvironmentBuildPlanResult>(
                TEXT("map_not_saved"), CurrentMap);
        }
        if (!Request.bConfirmWorldSwitch)
        {
            return MakeError<FThomasEnvironmentBuildPlanResult>(
                TEXT("confirmation_required"),
                TEXT("The native builder unloads the current map; set bConfirmWorldSwitch=true."));
        }
        if (Request.Action != TEXT("build_navigation")
            && Request.Action != TEXT("build_hlods")
            && Request.Action != TEXT("delete_hlods"))
        {
            return MakeError<FThomasEnvironmentBuildPlanResult>(
                TEXT("unsupported_operation"), Request.Action);
        }
        if (Request.Action != TEXT("build_navigation")
            && (Request.bVerbose || Request.bCleanPackages))
        {
            return MakeError<FThomasEnvironmentBuildPlanResult>(
                TEXT("navigation_option_not_allowed"),
                TEXT("bVerbose and bCleanPackages only apply to build_navigation."));
        }
        if ((Request.Action == TEXT("delete_hlods") || Request.bCleanPackages)
            && !Request.bConfirmDestructive)
        {
            return MakeError<FThomasEnvironmentBuildPlanResult>(
                TEXT("destructive_confirmation_required"),
                TEXT("HLOD deletion and navigation package cleanup require bConfirmDestructive=true."));
        }
        const FString ProjectFile = FPaths::ConvertRelativePathToFull(
            FPaths::GetProjectFilePath());
        if (ProjectFile.IsEmpty() || !IFileManager::Get().FileExists(*ProjectFile))
        {
            return MakeError<FThomasEnvironmentBuildPlanResult>(
                TEXT("project_file_unavailable"), ProjectFile);
        }

        FThomasEnvironmentBuildPlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.Action = Request.Action;
        Result.MapPath = CurrentMap;
        Result.ExpectedRevision = CurrentRevision;
        Result.BuilderClass = EnvironmentBuilderClass(Request.Action);
        Result.Risk = TEXT("R2");
        Result.Preview.Add(TEXT("Unload saved Editor map ") + CurrentMap);
        Result.Preview.Add(TEXT("Run native builder ") + Result.BuilderClass
            + TEXT(" in a hidden Unreal process"));
        Result.Preview.Add(TEXT("Write a dedicated log under Saved/Logs/ThomasEditor/EnvironmentJobs"));
        Result.Preview.Add(TEXT("Reload the exact map only if the temporary host world is unchanged"));
        FEnvironmentBuildPlan& Plan = EnvironmentBuildPlans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasEnvironmentBuildJobResult StartEnvironmentBuildJob(
        const FString& PlanId) override
    {
        PurgeExpiredPlans();
        FEnvironmentBuildPlan Plan;
        if (!EnvironmentBuildPlans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasEnvironmentBuildJobResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        for (const TPair<FString, FEnvironmentBuildJob>& Pair : EnvironmentJobs)
        {
            if (Pair.Value.bProcessRunning)
            {
                return MakeError<FThomasEnvironmentBuildJobResult>(
                    TEXT("environment_job_running"), Pair.Key);
            }
        }
        UWorld* World = GetProjectEditorWorld();
        if (!World || (GEditor && GEditor->PlayWorld))
        {
            return MakeError<FThomasEnvironmentBuildJobResult>(
                TEXT("world_unavailable"), TEXT("The planned World Partition map is not open."));
        }
        const FString CurrentRevision = BuildWorldRevision(World);
        if (World->GetOutermost()->GetName() != Plan.Request.MapPath
            || CurrentRevision != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasEnvironmentBuildJobResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }
        if (!World->GetWorldPartition() || World->GetOutermost()->IsDirty())
        {
            return MakeError<FThomasEnvironmentBuildJobResult>(
                TEXT("map_state_conflict"),
                TEXT("The World Partition map must remain saved and clean after planning."));
        }

        FEnvironmentBuildJob Job;
        Job.Request = Plan.Request;
        Job.JobId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Job.StartedAtUtc = FDateTime::UtcNow().ToIso8601();
        const FString JobDirectory = FPaths::ConvertRelativePathToFull(
            FPaths::ProjectLogDir() / TEXT("ThomasEditor/EnvironmentJobs"));
        if (!IFileManager::Get().MakeDirectory(*JobDirectory, true))
        {
            return MakeError<FThomasEnvironmentBuildJobResult>(
                TEXT("log_directory_failed"), JobDirectory);
        }
        Job.LogPath = JobDirectory / FString::Printf(
            TEXT("ThomasEditor_%s_%s.log"), *Plan.Request.Action, *Job.JobId);

        UWorld* HostWorld = UEditorLoadingAndSavingUtils::NewBlankMap(false);
        if (!HostWorld)
        {
            return MakeError<FThomasEnvironmentBuildJobResult>(
                TEXT("world_unload_failed"), Plan.Request.MapPath);
        }
        Job.HostMapPath = HostWorld->GetOutermost()->GetName();
        Job.HostRevision = BuildWorldRevision(HostWorld);

        const FString Arguments = BuildEnvironmentCommandLine(Plan.Request, Job.LogPath);
        Job.ProcessHandle = FPlatformProcess::CreateProc(
            FPlatformProcess::ExecutablePath(),
            *Arguments,
            true,
            true,
            true,
            &Job.ProcessId,
            0,
            nullptr,
            nullptr);
        if (!Job.ProcessHandle.IsValid())
        {
            UEditorLoadingAndSavingUtils::LoadMap(MapFilename(Plan.Request.MapPath));
            return MakeError<FThomasEnvironmentBuildJobResult>(
                TEXT("process_start_failed"), EnvironmentBuilderClass(Plan.Request.Action));
        }

        FEnvironmentBuildJob& StoredJob = EnvironmentJobs.Add(Job.JobId, MoveTemp(Job));
        return MakeEnvironmentBuildJobResult(StoredJob);
    }

    virtual FThomasEnvironmentBuildJobResult GetEnvironmentBuildJob(
        const FString& JobId,
        const bool bReloadMapWhenComplete) override
    {
        FEnvironmentBuildJob* Job = EnvironmentJobs.Find(JobId);
        if (!Job)
        {
            return MakeError<FThomasEnvironmentBuildJobResult>(
                TEXT("job_not_found"), JobId);
        }
        RefreshEnvironmentBuildJob(*Job, bReloadMapWhenComplete);
        return MakeEnvironmentBuildJobResult(*Job);
    }

    virtual FThomasEnvironmentBuildJobResult CancelEnvironmentBuildJob(
        const FString& JobId,
        const bool bConfirmCancel) override
    {
        FEnvironmentBuildJob* Job = EnvironmentJobs.Find(JobId);
        if (!Job)
        {
            return MakeError<FThomasEnvironmentBuildJobResult>(
                TEXT("job_not_found"), JobId);
        }
        if (!bConfirmCancel)
        {
            return MakeError<FThomasEnvironmentBuildJobResult>(
                TEXT("confirmation_required"),
                TEXT("Terminating an environment builder requires bConfirmCancel=true."));
        }
        if (!Job->bProcessRunning || !Job->ProcessHandle.IsValid())
        {
            return MakeError<FThomasEnvironmentBuildJobResult>(
                TEXT("job_not_running"), Job->Status);
        }
        FPlatformProcess::TerminateProc(Job->ProcessHandle, true);
        FPlatformProcess::CloseProc(Job->ProcessHandle);
        Job->bProcessRunning = false;
        Job->bCancelled = true;
        Job->Status = TEXT("cancelled");
        Job->Code = TEXT("cancelled");
        Job->Message = TEXT("The builder process was explicitly terminated.");
        Job->FinishedAtUtc = FDateTime::UtcNow().ToIso8601();
        ReloadEnvironmentBuildMap(*Job);
        return MakeEnvironmentBuildJobResult(*Job);
    }

    virtual FThomasWorldPatchPlanResult PlanWorldPatch(
        const FThomasWorldPatchRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        UWorld* World = GetProjectEditorWorld();
        if (!World)
        {
            return MakeError<FThomasWorldPatchPlanResult>(
                TEXT("project_editor_world_unavailable"),
                TEXT("Open a /Game/PropHunt level in the canonical Editor."));
        }
        if (GEditor && GEditor->PlayWorld)
        {
            return MakeError<FThomasWorldPatchPlanResult>(
                TEXT("pie_active"), TEXT("Stop PIE before planning level mutations."));
        }

        FThomasWorldPatchRequest Request = InputRequest;
        const FString CurrentMap = World->GetOutermost()->GetName();
        const FString CurrentRevision = BuildWorldRevision(World);
        if (Request.MapPath != CurrentMap)
        {
            return MakeError<FThomasWorldPatchPlanResult>(
                TEXT("map_mismatch"), CurrentMap);
        }
        if (Request.ExpectedRevision.IsEmpty() || Request.ExpectedRevision != CurrentRevision)
        {
            return MakeError<FThomasWorldPatchPlanResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }
        if (Request.Operations.Num() < 1 || Request.Operations.Num() > MaxWorldOperations)
        {
            return MakeError<FThomasWorldPatchPlanResult>(
                TEXT("invalid_batch_size"), TEXT("World batch must contain 1 to 50 operations."));
        }
        if (HasMixedRendererAndWorldOperations(Request.Operations))
        {
            return MakeError<FThomasWorldPatchPlanResult>(
                TEXT("mixed_renderer_world_batch_denied"),
                TEXT("Renderer config and map mutations require separate plans so each can roll back atomically."));
        }

        bool bDestructive = false;
        TArray<FString> Preview;
        URendererSettings* RendererCandidate = DuplicateObject<URendererSettings>(
            GetMutableDefault<URendererSettings>(), GetTransientPackage());
        for (FThomasWorldOperation& Operation : Request.Operations)
        {
            const FString Action = Operation.Action.ToLower();
            if (Action == TEXT("set_renderer_property"))
            {
                FProperty* Property = FindFProperty<FProperty>(
                    URendererSettings::StaticClass(), FName(*Operation.PropertyName));
                const FString CurrentValue = ExportObjectProperty(RendererCandidate, Property);
                if (!RendererCandidate || !IsSupportedRendererProperty(Property))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("renderer_property_not_editable"), Operation.PropertyName);
                }
                if (Operation.ExpectedValue.IsEmpty() || Operation.ExpectedValue != CurrentValue)
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("renderer_property_conflict"), CurrentValue);
                }
                if (!Property->ImportText_InContainer(
                    *Operation.Value, RendererCandidate, RendererCandidate, PPF_None))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("renderer_value_invalid"), Operation.Value);
                }
                bDestructive = true;
                Preview.Add(TEXT("Set renderer/Lumen setting ") + Operation.PropertyName);
            }
            else if (Action == TEXT("create_landscape"))
            {
                int32 SizeX = 0;
                int32 SizeY = 0;
                UMaterialInterface* Material = Operation.AssetPath.IsEmpty()
                    ? nullptr : LoadObject<UMaterialInterface>(nullptr, *Operation.AssetPath);
                if (!IsValidLandscapeCreateOperation(Operation, SizeX, SizeY)
                    || (!Operation.AssetPath.IsEmpty() && !Material))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("invalid_landscape_create_parameters"),
                        FString::Printf(TEXT("%dx%d components, %d sections, %d quads"),
                            Operation.LandscapeComponentCountX,
                            Operation.LandscapeComponentCountY,
                            Operation.LandscapeSectionsPerComponent,
                            Operation.LandscapeQuadsPerSection));
                }
                bDestructive = true;
                Preview.Add(FString::Printf(
                    TEXT("Create flat Landscape %dx%d vertices (%dx%d components)"),
                    SizeX,
                    SizeY,
                    Operation.LandscapeComponentCountX,
                    Operation.LandscapeComponentCountY));
            }
            else if (Action == TEXT("spawn_actor"))
            {
                UClass* ActorClass = LoadObject<UClass>(nullptr, *Operation.ClassPath);
                if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass())
                    || ActorClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("invalid_actor_class"), Operation.ClassPath);
                }
                if (ActorClass->IsChildOf(ALandscapeProxy::StaticClass()))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("landscape_requires_typed_create"),
                        TEXT("Use create_landscape so Import assigns a valid GUID and component topology."));
                }
                bDestructive = true;
                Preview.Add(TEXT("Spawn ") + Operation.ClassPath);
            }
            else if (Action == TEXT("add_foliage_instance"))
            {
                UObject* Source = LoadObject<UObject>(nullptr, *Operation.AssetPath);
                UFoliageType* FoliageType = Cast<UFoliageType>(Source);
                UStaticMesh* StaticMesh = Cast<UStaticMesh>(Source);
                if ((!FoliageType && !StaticMesh) || !IsValidOperationTransform(Operation))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("invalid_foliage_source_or_transform"), Operation.AssetPath);
                }
                if (World->IsPartitionedWorld()
                    && (!FoliageType || !FoliageType->IsAsset()))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("partitioned_world_requires_foliage_type_asset"),
                        TEXT("Create or select a FoliageType asset; raw Static Mesh local types are not valid in World Partition."));
                }
                bDestructive = true;
                Preview.Add(TEXT("Add foliage instance from ") + Operation.AssetPath);
            }
            else if (Action == TEXT("create_private_data_layer"))
            {
                UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get();
                if (!World->GetWorldPartition() || !World->GetWorldDataLayers() || !Subsystem
                    || Operation.Label.TrimStartAndEnd().IsEmpty())
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("private_data_layer_creation_unavailable"), Operation.Label);
                }
                const bool bDuplicate = Subsystem->GetAllDataLayers().ContainsByPredicate(
                    [&Operation](const UDataLayerInstance* DataLayer)
                    {
                        return DataLayer
                            && DataLayer->GetDataLayerShortName().Equals(
                                Operation.Label, ESearchCase::IgnoreCase);
                    });
                if (bDuplicate)
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("data_layer_name_conflict"), Operation.Label);
                }
                bDestructive = true;
                Preview.Add(TEXT("Create private Data Layer ") + Operation.Label);
            }
            else if (Action == TEXT("set_data_layer_property"))
            {
                UDataLayerInstance* DataLayer = ResolveDataLayer(World, Operation.AssetPath);
                const FString CurrentValue = DataLayerPropertyValue(
                    DataLayer, Operation.PropertyName);
                if (!DataLayer || Operation.ExpectedValue != CurrentValue)
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("data_layer_property_conflict"), CurrentValue);
                }
                const bool bShortName = Operation.PropertyName.Equals(
                    TEXT("ShortName"), ESearchCase::IgnoreCase);
                const bool bParent = Operation.PropertyName.Equals(
                    TEXT("Parent"), ESearchCase::IgnoreCase);
                const bool bRuntimeState = Operation.PropertyName.Equals(
                    TEXT("InitialRuntimeState"), ESearchCase::IgnoreCase);
                const bool bBoolProperty =
                    Operation.PropertyName.Equals(TEXT("Visible"), ESearchCase::IgnoreCase)
                    || Operation.PropertyName.Equals(TEXT("LoadedInEditor"), ESearchCase::IgnoreCase)
                    || Operation.PropertyName.Equals(TEXT("InitiallyVisible"), ESearchCase::IgnoreCase);
                bool bParsedBool = false;
                EDataLayerRuntimeState ParsedState = EDataLayerRuntimeState::Unloaded;
                if ((bShortName && Operation.Value.TrimStartAndEnd().IsEmpty())
                    || (bParent && !Operation.Value.IsEmpty()
                        && !ResolveDataLayer(World, Operation.Value))
                    || (bRuntimeState
                        && (!DataLayer->IsRuntime()
                            || !ParseRuntimeState(Operation.Value, ParsedState)))
                    || (bBoolProperty && !ParseBoolStrict(Operation.Value, bParsedBool))
                    || (!bShortName && !bParent && !bRuntimeState && !bBoolProperty))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("invalid_data_layer_property_value"),
                        Operation.PropertyName + TEXT("=") + Operation.Value);
                }
                bDestructive = bDestructive
                    || bParent
                    || bRuntimeState
                    || Operation.PropertyName.Equals(
                        TEXT("LoadedInEditor"), ESearchCase::IgnoreCase);
                Preview.Add(TEXT("Set Data Layer ") + Operation.PropertyName
                    + TEXT(" on ") + Operation.AssetPath);
            }
            else if (Action == TEXT("add_actor_to_data_layer")
                || Action == TEXT("remove_actor_from_data_layer"))
            {
                AActor* Actor = ResolveActor(World, Operation.ActorPath);
                UDataLayerInstance* DataLayer = ResolveDataLayer(World, Operation.AssetPath);
                if (IsProtectedEditorActor(Actor) || !DataLayer)
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("data_layer_assignment_target_invalid"),
                        Operation.ActorPath + TEXT("|") + Operation.AssetPath);
                }
                const bool bAssigned = Actor->GetDataLayerInstances().Contains(DataLayer);
                if (Operation.ExpectedValue != BoolText(bAssigned)
                    || (Action == TEXT("add_actor_to_data_layer") && bAssigned)
                    || (Action == TEXT("remove_actor_from_data_layer") && !bAssigned))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("data_layer_assignment_conflict"), BoolText(bAssigned));
                }
                bDestructive = true;
                Preview.Add(Operation.Action + TEXT(" ") + Operation.ActorPath);
            }
            else if (Action == TEXT("delete_data_layer"))
            {
                UDataLayerInstance* DataLayer = ResolveDataLayer(World, Operation.AssetPath);
                const FString CurrentActorCount = DataLayer
                    ? FString::FromInt(CountDataLayerActors(World, DataLayer)) : FString();
                if (!DataLayer || !DataLayer->CanBeRemoved()
                    || Operation.ExpectedValue != CurrentActorCount)
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("data_layer_delete_conflict"), CurrentActorCount);
                }
                bDestructive = true;
                Preview.Add(TEXT("Delete Data Layer ") + Operation.AssetPath);
            }
            else if (IsLandscapeDataAction(Action))
            {
                FPreparedLandscapeDataOperation Prepared;
                FString Reason;
                if (!PrepareLandscapeDataOperation(
                        World, Operation, Prepared, Reason))
                {
                    const bool bHashConflict = Reason.StartsWith(
                        TEXT("data_hash_conflict|"));
                    return MakeError<FThomasWorldPatchPlanResult>(
                        bHashConflict
                            ? TEXT("landscape_data_hash_conflict")
                            : TEXT("landscape_data_preflight_failed"),
                        Reason);
                }
                Operation.Value = Prepared.NewHash;
                bDestructive = true;
                Preview.Add(FString::Printf(
                    TEXT("%s %s [%d,%d]-[%d,%d] %s -> %s"),
                    *Operation.Action,
                    *Operation.ActorPath,
                    Prepared.MinX,
                    Prepared.MinY,
                    Prepared.MaxX,
                    Prepared.MaxY,
                    *Prepared.CurrentHash,
                    *Prepared.NewHash));
            }
            else if (Action == TEXT("set_landscape_material"))
            {
                ALandscapeProxy* Landscape = ResolveLandscape(World, Operation.ActorPath);
                const FString CurrentMaterial = Landscape && Landscape->GetLandscapeMaterial()
                    ? Landscape->GetLandscapeMaterial()->GetPathName() : FString();
                UMaterialInterface* NewMaterial = Operation.Value.IsEmpty()
                    ? nullptr : LoadObject<UMaterialInterface>(nullptr, *Operation.Value);
                if (!Landscape || Operation.ExpectedValue != CurrentMaterial
                    || (!Operation.Value.IsEmpty() && !NewMaterial))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("landscape_material_conflict_or_invalid"), CurrentMaterial);
                }
                Preview.Add(TEXT("Set Landscape material on ") + Operation.ActorPath);
            }
            else if (Action == TEXT("set_landscape_property"))
            {
                ALandscapeProxy* Landscape = ResolveLandscape(World, Operation.ActorPath);
                FProperty* Property = Landscape
                    ? FindFProperty<FProperty>(
                        Landscape->GetClass(), FName(*Operation.PropertyName))
                    : nullptr;
                const FString CurrentValue = ExportActorProperty(Landscape, Property);
                if (!Landscape || !IsSupportedLandscapeProperty(Property)
                    || Operation.ExpectedValue != CurrentValue)
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("landscape_property_conflict"), CurrentValue);
                }
                if (!CanImportPropertyValue(Property, Operation.Value, Landscape))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("landscape_property_value_invalid"), Operation.Value);
                }
                const FString LowerProperty = Operation.PropertyName.ToLower();
                bDestructive = bDestructive
                    || LowerProperty.Contains(TEXT("nanite"))
                    || LowerProperty.Contains(TEXT("navigation"))
                    || LowerProperty.Contains(TEXT("navmesh"));
                Preview.Add(TEXT("Set Landscape property ")
                    + Operation.PropertyName + TEXT(" on ") + Operation.ActorPath);
            }
            else if (Action == TEXT("create_landscape_edit_layer"))
            {
                ALandscape* Landscape = Cast<ALandscape>(
                    ResolveLandscape(World, Operation.ActorPath));
                const FString CurrentCount = Landscape
                    ? FString::FromInt(Landscape->GetEditLayersConst().Num()) : FString();
                const FName LayerName(*Operation.Label.TrimStartAndEnd());
                if (!Landscape || LayerName.IsNone()
                    || Operation.ExpectedValue != CurrentCount
                    || !Landscape->IsLayerNameUnique(LayerName))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("landscape_edit_layer_create_conflict"), CurrentCount);
                }
                bDestructive = true;
                Preview.Add(TEXT("Create Landscape edit layer ") + Operation.Label);
            }
            else if (Action == TEXT("set_landscape_edit_layer_property"))
            {
                ALandscape* Landscape = Cast<ALandscape>(
                    ResolveLandscape(World, Operation.ActorPath));
                ULandscapeEditLayerBase* Layer = ResolveLandscapeEditLayer(
                    Landscape, Operation.AssetPath);
                const FString CurrentValue = LandscapeEditLayerPropertyValue(
                    Layer, Operation.PropertyName);
                const bool bUnlocking = Operation.PropertyName.Equals(
                    TEXT("Locked"), ESearchCase::IgnoreCase);
                if (!Layer || Operation.ExpectedValue != CurrentValue
                    || (Layer->IsLocked() && !bUnlocking)
                    || !IsValidLandscapeEditLayerValue(
                        Landscape, Layer, Operation.PropertyName, Operation.Value))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("landscape_edit_layer_property_conflict"), CurrentValue);
                }
                Preview.Add(TEXT("Set Landscape edit layer ")
                    + Operation.PropertyName + TEXT(" on ") + Operation.AssetPath);
            }
            else if (Action == TEXT("move_landscape_edit_layer")
                || Action == TEXT("delete_landscape_edit_layer"))
            {
                ALandscape* Landscape = Cast<ALandscape>(
                    ResolveLandscape(World, Operation.ActorPath));
                int32 CurrentIndex = INDEX_NONE;
                ULandscapeEditLayerBase* Layer = ResolveLandscapeEditLayer(
                    Landscape, Operation.AssetPath, &CurrentIndex);
                if (!Layer || Layer->IsLocked())
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("landscape_edit_layer_not_editable"), Operation.AssetPath);
                }
                if (Action == TEXT("move_landscape_edit_layer"))
                {
                    if (Operation.ExpectedValue != FString::FromInt(CurrentIndex)
                        || !Landscape->GetEditLayersConst().IsValidIndex(Operation.InstanceIndex)
                        || Operation.InstanceIndex == CurrentIndex)
                    {
                        return MakeError<FThomasWorldPatchPlanResult>(
                            TEXT("landscape_edit_layer_move_conflict"),
                            FString::FromInt(CurrentIndex));
                    }
                    Preview.Add(FString::Printf(
                        TEXT("Move Landscape edit layer %s from %d to %d"),
                        *Operation.AssetPath, CurrentIndex, Operation.InstanceIndex));
                }
                else
                {
                    if (CurrentIndex <= 0
                        || Operation.ExpectedValue != Layer->GetName().ToString())
                    {
                        return MakeError<FThomasWorldPatchPlanResult>(
                            TEXT("landscape_edit_layer_delete_conflict"),
                            Layer->GetName().ToString());
                    }
                    Preview.Add(TEXT("Delete Landscape edit layer ") + Operation.AssetPath);
                }
                bDestructive = true;
            }
            else if (Action == TEXT("set_foliage_instance_transform")
                || Action == TEXT("remove_foliage_instance")
                || Action == TEXT("remove_foliage_type"))
            {
                AInstancedFoliageActor* FoliageActor = nullptr;
                UFoliageType* FoliageType = nullptr;
                FFoliageInfo* FoliageInfo = nullptr;
                if (!ResolveFoliageTarget(
                    World, Operation, FoliageActor, FoliageType, FoliageInfo))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("foliage_target_not_found"),
                        Operation.ActorPath + TEXT("|") + Operation.AssetPath);
                }
                if (Action == TEXT("remove_foliage_type"))
                {
                    const FString CurrentCount = FString::FromInt(FoliageInfo->Instances.Num());
                    if (Operation.ExpectedValue.IsEmpty()
                        || Operation.ExpectedValue != CurrentCount)
                    {
                        return MakeError<FThomasWorldPatchPlanResult>(
                            TEXT("foliage_count_conflict"), CurrentCount);
                    }
                    bDestructive = true;
                    Preview.Add(TEXT("Remove foliage type ") + Operation.AssetPath);
                }
                else
                {
                    if (!FoliageInfo->Instances.IsValidIndex(Operation.InstanceIndex))
                    {
                        return MakeError<FThomasWorldPatchPlanResult>(
                            TEXT("foliage_instance_not_found"),
                            FString::FromInt(Operation.InstanceIndex));
                    }
                    const FString CurrentTransform =
                        FoliageInfo->Instances[Operation.InstanceIndex]
                            .GetInstanceWorldTransform().ToString();
                    if (Operation.ExpectedValue.IsEmpty()
                        || Operation.ExpectedValue != CurrentTransform)
                    {
                        return MakeError<FThomasWorldPatchPlanResult>(
                            TEXT("foliage_transform_conflict"), CurrentTransform);
                    }
                    if (Action == TEXT("set_foliage_instance_transform")
                        && !IsValidOperationTransform(Operation))
                    {
                        return MakeError<FThomasWorldPatchPlanResult>(
                            TEXT("invalid_foliage_transform"), Operation.Value);
                    }
                    if (Action == TEXT("remove_foliage_instance"))
                    {
                        bDestructive = true;
                    }
                    Preview.Add(Operation.Action + TEXT(" ") + Operation.AssetPath);
                }
            }
            else if (Action == TEXT("delete_actor")
                || Action == TEXT("set_transform")
                || Action == TEXT("set_actor_label")
                || Action == TEXT("set_actor_property")
                || Action == TEXT("set_component_property"))
            {
                AActor* Actor = ResolveActor(World, Operation.ActorPath);
                if (IsProtectedEditorActor(Actor))
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("actor_not_found_or_protected"), Operation.ActorPath);
                }
                if (Action == TEXT("delete_actor"))
                {
                    bDestructive = true;
                }
                else if (Action == TEXT("set_actor_label") && Operation.Label.IsEmpty())
                {
                    return MakeError<FThomasWorldPatchPlanResult>(
                        TEXT("invalid_label"), Operation.ActorPath);
                }
                else if (Action == TEXT("set_actor_property"))
                {
                    FProperty* Property = FindFProperty<FProperty>(
                        Actor->GetClass(), FName(*Operation.PropertyName));
                    if (!IsSupportedActorProperty(Property))
                    {
                        return MakeError<FThomasWorldPatchPlanResult>(
                            TEXT("property_not_editable"), Operation.PropertyName);
                    }
                    const FString CurrentValue = ExportActorProperty(Actor, Property);
                    if (Operation.ExpectedValue.IsEmpty()
                        || Operation.ExpectedValue != CurrentValue)
                    {
                        return MakeError<FThomasWorldPatchPlanResult>(
                            TEXT("property_conflict"), CurrentValue);
                    }
                }
                else if (Action == TEXT("set_component_property"))
                {
                    UActorComponent* Component = ResolveOwnedComponent(
                        Actor, Operation.ComponentPath);
                    FProperty* Property = Component
                        ? FindFProperty<FProperty>(
                            Component->GetClass(), FName(*Operation.PropertyName))
                        : nullptr;
                    if (!Component || !IsSupportedComponentProperty(Property))
                    {
                        return MakeError<FThomasWorldPatchPlanResult>(
                            TEXT("component_property_not_editable"),
                            Operation.ComponentPath + TEXT("|") + Operation.PropertyName);
                    }
                    const FString CurrentValue = ExportComponentProperty(Component, Property);
                    if (Operation.ExpectedValue.IsEmpty()
                        || Operation.ExpectedValue != CurrentValue)
                    {
                        return MakeError<FThomasWorldPatchPlanResult>(
                            TEXT("component_property_conflict"), CurrentValue);
                    }
                    if (!CanImportComponentProperty(Component, Property, Operation.Value))
                    {
                        return MakeError<FThomasWorldPatchPlanResult>(
                            TEXT("component_property_value_invalid"), Operation.Value);
                    }
                }
                Preview.Add(Operation.Action + TEXT(" ") + Actor->GetActorLabel());
            }
            else
            {
                return MakeError<FThomasWorldPatchPlanResult>(
                    TEXT("unsupported_operation"), Operation.Action);
            }
        }
        if (bDestructive && !Request.bConfirmDestructive)
        {
            return MakeError<FThomasWorldPatchPlanResult>(
                TEXT("confirmation_required"),
                TEXT("Structural world, foliage, renderer, and delete operations require bConfirmDestructive=true."));
        }

        FThomasWorldPatchPlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.MapPath = CurrentMap;
        Result.ExpectedRevision = CurrentRevision;
        Result.Risk = bDestructive ? TEXT("R2") : TEXT("R1");
        Result.OperationCount = Request.Operations.Num();
        Result.Preview = MoveTemp(Preview);
        FWorldPatchPlan& Plan = Plans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasWorldPatchApplyResult ApplyWorldPlan(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FWorldPatchPlan Plan;
        if (!Plans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasWorldPatchApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        UWorld* World = GetProjectEditorWorld();
        if (!World || (GEditor && GEditor->PlayWorld))
        {
            return MakeError<FThomasWorldPatchApplyResult>(
                TEXT("world_unavailable"), TEXT("The canonical Editor world must be open outside PIE."));
        }
        const FString CurrentRevision = BuildWorldRevision(World);
        if (World->GetOutermost()->GetName() != Plan.Request.MapPath
            || CurrentRevision != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasWorldPatchApplyResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }
        if (HasMixedRendererAndWorldOperations(Plan.Request.Operations))
        {
            return MakeError<FThomasWorldPatchApplyResult>(
                TEXT("mixed_renderer_world_batch_denied"),
                TEXT("Renderer config and map mutations require separate plans so each can roll back atomically."));
        }

        FThomasWorldPatchApplyResult Result;
        Result.MapPath = Plan.Request.MapPath;
        Result.RevisionBefore = CurrentRevision;
        bool bApplied = true;
        bool bMapChanged = false;
        bool bRendererChanged = false;
        TMap<FName, FString> PreviousRendererValues;
        FRendererConfigSnapshot PreviousRendererConfig;
        FString Error;
        const bool bHasRendererOperation = Plan.Request.Operations.ContainsByPredicate(
            [](const FThomasWorldOperation& Operation)
            {
                return Operation.Action.Equals(
                    TEXT("set_renderer_property"), ESearchCase::IgnoreCase);
            });
        if (bHasRendererOperation
            && !CaptureRendererConfig(
                GetDefault<URendererSettings>(), PreviousRendererConfig))
        {
            return MakeError<FThomasWorldPatchApplyResult>(
                TEXT("config_snapshot_failed"),
                TEXT("DefaultEngine.ini could not be captured before the renderer mutation."));
        }
        {
            const FScopedTransaction Transaction(
                NSLOCTEXT("ThomasEditor", "ApplyWorldPatch", "ThomasEditor World Patch"));
            World->Modify();
            if (World->PersistentLevel)
            {
                World->PersistentLevel->Modify();
            }
            for (const FThomasWorldOperation& Operation : Plan.Request.Operations)
            {
                const FString Action = Operation.Action.ToLower();
                if (Action == TEXT("set_renderer_property"))
                {
                    URendererSettings* Settings = GetMutableDefault<URendererSettings>();
                    FProperty* Property = FindFProperty<FProperty>(
                        URendererSettings::StaticClass(), FName(*Operation.PropertyName));
                    const FString CurrentValue = ExportObjectProperty(Settings, Property);
                    bApplied = Settings && IsSupportedRendererProperty(Property)
                        && CurrentValue == Operation.ExpectedValue;
                    if (bApplied)
                    {
                        PreviousRendererValues.FindOrAdd(
                            Property->GetFName(), CurrentValue);
                        Settings->Modify();
                        bApplied = Property->ImportText_InContainer(
                            *Operation.Value, Settings, Settings, PPF_None) != nullptr;
                        if (bApplied)
                        {
                            FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
                            Settings->PostEditChangeProperty(Event);
                            bRendererChanged = true;
                        }
                    }
                }
                else if (Action == TEXT("create_landscape"))
                {
                    int32 SizeX = 0;
                    int32 SizeY = 0;
                    UMaterialInterface* Material = Operation.AssetPath.IsEmpty()
                        ? nullptr : LoadObject<UMaterialInterface>(nullptr, *Operation.AssetPath);
                    bApplied = IsValidLandscapeCreateOperation(Operation, SizeX, SizeY)
                        && (Operation.AssetPath.IsEmpty() || Material);
                    ALandscape* Landscape = nullptr;
                    if (bApplied)
                    {
                        TArray<uint16> HeightData;
                        HeightData.Init(
                            static_cast<uint16>(Operation.LandscapeHeightValue),
                            SizeX * SizeY);
                        TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
                        HeightDataPerLayers.Add(FGuid(), MoveTemp(HeightData));
                        TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayers;
                        MaterialLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

                        FActorSpawnParameters Parameters;
                        Parameters.ObjectFlags |= RF_Transactional;
                        Landscape = World->SpawnActor<ALandscape>(
                            ALandscape::StaticClass(),
                            MakeOperationTransform(Operation),
                            Parameters);
                        bApplied = Landscape != nullptr;
                        if (Landscape)
                        {
                            Landscape->LandscapeMaterial = Material;
                            if (!Operation.Label.IsEmpty())
                            {
                                Landscape->SetActorLabel(Operation.Label, true);
                            }
                            const FGuid LandscapeGuid = FGuid::NewGuid();
                            Landscape->Import(
                                LandscapeGuid,
                                0,
                                0,
                                SizeX - 1,
                                SizeY - 1,
                                Operation.LandscapeSectionsPerComponent,
                                Operation.LandscapeQuadsPerSection,
                                HeightDataPerLayers,
                                TEXT(""),
                                MaterialLayers,
                                ELandscapeImportAlphamapType::Additive,
                                TArrayView<const FLandscapeLayer>());
                            bApplied = Landscape->GetLandscapeGuid() == LandscapeGuid
                                && Landscape->LandscapeComponents.Num()
                                    == Operation.LandscapeComponentCountX
                                        * Operation.LandscapeComponentCountY;
                        }
                    }
                    if (!bApplied)
                    {
                        if (Landscape && Landscape->GetLandscapeGuid().IsValid())
                        {
                            World->EditorDestroyActor(Landscape, true);
                        }
                        Error = TEXT("Typed Landscape creation/import failed.");
                        break;
                    }
                    Result.CreatedActorPaths.Add(Landscape->GetPathName());
                    bMapChanged = true;
                }
                else if (Action == TEXT("spawn_actor"))
                {
                    UClass* ActorClass = LoadObject<UClass>(nullptr, *Operation.ClassPath);
                    const FTransform Transform(
                        FRotator(
                            Operation.RotationPitch,
                            Operation.RotationYaw,
                            Operation.RotationRoll),
                        FVector(Operation.LocationX, Operation.LocationY, Operation.LocationZ),
                        FVector(Operation.ScaleX, Operation.ScaleY, Operation.ScaleZ));
                    FActorSpawnParameters Parameters;
                    Parameters.ObjectFlags |= RF_Transactional;
                    AActor* Actor = ActorClass
                        ? World->SpawnActor<AActor>(ActorClass, Transform, Parameters)
                        : nullptr;
                    if (!Actor)
                    {
                        Error = TEXT("Actor spawn failed: ") + Operation.ClassPath;
                        bApplied = false;
                        break;
                    }
                    if (!Operation.Label.IsEmpty())
                    {
                        Actor->SetActorLabel(Operation.Label, true);
                    }
                    Result.CreatedActorPaths.Add(Actor->GetPathName());
                    bMapChanged = true;
                }
                else if (Action == TEXT("add_foliage_instance"))
                {
                    UObject* Source = LoadObject<UObject>(nullptr, *Operation.AssetPath);
                    UFoliageType* FoliageType = Cast<UFoliageType>(Source);
                    UStaticMesh* StaticMesh = Cast<UStaticMesh>(Source);
                    const FTransform Transform = MakeOperationTransform(Operation);
                    AInstancedFoliageActor* TargetActor = AInstancedFoliageActor::Get(
                        World, true, World->PersistentLevel, Transform.GetLocation());
                    UFoliageType* TargetType = FoliageType;
                    FFoliageInfo* TargetInfo = nullptr;
                    if (StaticMesh)
                    {
                        if (TargetActor)
                        {
                            TargetActor->Modify();
                            TargetType = TargetActor->GetLocalFoliageTypeForSource(
                                StaticMesh, &TargetInfo);
                            if (!TargetType)
                            {
                                TargetInfo = TargetActor->AddMesh(StaticMesh, &TargetType);
                            }
                        }
                    }
                    else if (TargetActor && TargetType)
                    {
                        TargetActor->Modify();
                        TargetType = TargetActor->AddFoliageType(TargetType, &TargetInfo);
                    }
                    bApplied = TargetActor && TargetType && TargetInfo;
                    if (bApplied)
                    {
                        FFoliageInstance Instance;
                        Instance.SetInstanceWorldTransform(Transform);
                        const int32 NewInstanceIndex = TargetInfo->Instances.Num();
                        TargetInfo->AddInstance(TargetType, Instance);
                        bApplied = TargetInfo->Instances.Num() == NewInstanceIndex + 1;
                        if (bApplied)
                        {
                            Result.CreatedObjectPaths.Add(FString::Printf(
                                TEXT("%s|%s|%d"),
                                *TargetActor->GetPathName(),
                                *TargetType->GetPathName(),
                                NewInstanceIndex));
                        }
                    }
                    if (!bApplied)
                    {
                        Error = TEXT("Foliage instance add failed: ") + Operation.AssetPath;
                        break;
                    }
                    bMapChanged = true;
                }
                else if (Action == TEXT("create_private_data_layer"))
                {
                    UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get();
                    FDataLayerCreationParameters Parameters;
                    Parameters.bIsPrivate = true;
                    Parameters.WorldDataLayers = World->GetWorldDataLayers();
                    UDataLayerInstance* DataLayer = Subsystem
                        ? Subsystem->CreateDataLayerInstance(Parameters) : nullptr;
                    bApplied = DataLayer
                        && Subsystem->SetDataLayerShortName(DataLayer, Operation.Label);
                    if (!bApplied && DataLayer && Subsystem)
                    {
                        Subsystem->DeleteDataLayer(DataLayer);
                    }
                    if (bApplied)
                    {
                        Result.CreatedObjectPaths.Add(DataLayer->GetPathName());
                    }
                    else
                    {
                        Error = TEXT("Private Data Layer creation failed: ") + Operation.Label;
                        break;
                    }
                    bMapChanged = true;
                }
                else if (Action == TEXT("set_data_layer_property"))
                {
                    UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get();
                    UDataLayerInstance* DataLayer = ResolveDataLayer(World, Operation.AssetPath);
                    bApplied = Subsystem && DataLayer
                        && DataLayerPropertyValue(DataLayer, Operation.PropertyName)
                            == Operation.ExpectedValue;
                    if (bApplied)
                    {
                        DataLayer->Modify();
                        bool bValue = false;
                        EDataLayerRuntimeState RuntimeState = EDataLayerRuntimeState::Unloaded;
                        if (Operation.PropertyName.Equals(TEXT("ShortName"), ESearchCase::IgnoreCase))
                            bApplied = Subsystem->SetDataLayerShortName(DataLayer, Operation.Value);
                        else if (Operation.PropertyName.Equals(TEXT("Visible"), ESearchCase::IgnoreCase)
                            && ParseBoolStrict(Operation.Value, bValue))
                            Subsystem->SetDataLayerVisibility(DataLayer, bValue);
                        else if (Operation.PropertyName.Equals(TEXT("LoadedInEditor"), ESearchCase::IgnoreCase)
                            && ParseBoolStrict(Operation.Value, bValue))
                            bApplied = Subsystem->SetDataLayerIsLoadedInEditor(DataLayer, bValue, true);
                        else if (Operation.PropertyName.Equals(TEXT("InitiallyVisible"), ESearchCase::IgnoreCase)
                            && ParseBoolStrict(Operation.Value, bValue))
                            Subsystem->SetDataLayerIsInitiallyVisible(DataLayer, bValue);
                        else if (Operation.PropertyName.Equals(TEXT("InitialRuntimeState"), ESearchCase::IgnoreCase)
                            && ParseRuntimeState(Operation.Value, RuntimeState))
                            Subsystem->SetDataLayerInitialRuntimeState(DataLayer, RuntimeState);
                        else if (Operation.PropertyName.Equals(TEXT("Parent"), ESearchCase::IgnoreCase))
                            bApplied = Subsystem->SetParentDataLayer(
                                DataLayer,
                                Operation.Value.IsEmpty()
                                    ? nullptr : ResolveDataLayer(World, Operation.Value));
                        else
                            bApplied = false;
                        bApplied = bApplied
                            && DataLayerPropertyValue(DataLayer, Operation.PropertyName)
                                == Operation.Value;
                    }
                    if (!bApplied)
                    {
                        Error = TEXT("Data Layer property mutation failed: ")
                            + Operation.AssetPath + TEXT("|") + Operation.PropertyName;
                        break;
                    }
                    bMapChanged = true;
                }
                else if (Action == TEXT("add_actor_to_data_layer")
                    || Action == TEXT("remove_actor_from_data_layer"))
                {
                    UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get();
                    AActor* Actor = ResolveActor(World, Operation.ActorPath);
                    UDataLayerInstance* DataLayer = ResolveDataLayer(World, Operation.AssetPath);
                    const bool bAssigned = Actor && DataLayer
                        && Actor->GetDataLayerInstances().Contains(DataLayer);
                    bApplied = Subsystem && Actor && DataLayer
                        && Operation.ExpectedValue == BoolText(bAssigned);
                    if (bApplied)
                    {
                        Actor->Modify();
                        bApplied = Action == TEXT("add_actor_to_data_layer")
                            ? Subsystem->AddActorToDataLayer(Actor, DataLayer)
                            : Subsystem->RemoveActorFromDataLayer(Actor, DataLayer);
                    }
                    if (!bApplied)
                    {
                        Error = Operation.Action + TEXT(" failed for ")
                            + Operation.ActorPath + TEXT("|") + Operation.AssetPath;
                        break;
                    }
                    bMapChanged = true;
                }
                else if (Action == TEXT("delete_data_layer"))
                {
                    UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get();
                    UDataLayerInstance* DataLayer = ResolveDataLayer(World, Operation.AssetPath);
                    bApplied = Subsystem && DataLayer && DataLayer->CanBeRemoved()
                        && Operation.ExpectedValue
                            == FString::FromInt(CountDataLayerActors(World, DataLayer));
                    if (bApplied)
                    {
                        Subsystem->DeleteDataLayer(DataLayer);
                        bApplied = !Subsystem->GetAllDataLayers().Contains(DataLayer);
                    }
                    if (!bApplied)
                    {
                        Error = TEXT("Data Layer deletion failed: ") + Operation.AssetPath;
                        break;
                    }
                    bMapChanged = true;
                }
                else if (IsLandscapeDataAction(Action))
                {
                    FPreparedLandscapeDataOperation Prepared;
                    FString Reason;
                    bApplied = PrepareLandscapeDataOperation(
                        World, Operation, Prepared, Reason);
                    if (bApplied && Operation.Value != Prepared.NewHash)
                    {
                        bApplied = false;
                        Reason = TEXT("planned_landscape_data_changed|")
                            + Operation.Value + TEXT("|") + Prepared.NewHash;
                    }
                    if (bApplied)
                    {
                        bApplied = ApplyLandscapeDataOperation(Prepared, Reason);
                    }
                    if (!bApplied)
                    {
                        Error = Operation.Action + TEXT(" failed: ") + Reason;
                        break;
                    }
                    bMapChanged = true;
                }
                else if (Action == TEXT("set_landscape_material"))
                {
                    ALandscapeProxy* Landscape = ResolveLandscape(World, Operation.ActorPath);
                    const FString CurrentMaterial = Landscape && Landscape->GetLandscapeMaterial()
                        ? Landscape->GetLandscapeMaterial()->GetPathName() : FString();
                    UMaterialInterface* NewMaterial = Operation.Value.IsEmpty()
                        ? nullptr : LoadObject<UMaterialInterface>(nullptr, *Operation.Value);
                    bApplied = Landscape && CurrentMaterial == Operation.ExpectedValue
                        && (Operation.Value.IsEmpty() || NewMaterial);
                    if (bApplied)
                    {
                        Landscape->Modify();
                        Landscape->LandscapeMaterial = NewMaterial;
                        FProperty* Property = FindFProperty<FProperty>(
                            Landscape->GetClass(), FName(TEXT("LandscapeMaterial")));
                        FPropertyChangedEvent Event(
                            Property, EPropertyChangeType::ValueSet);
                        Landscape->PostEditChangeProperty(Event);
                        Landscape->UpdateAllComponentMaterialInstances(true);
                        const FString AppliedMaterial = Landscape->GetLandscapeMaterial()
                            ? Landscape->GetLandscapeMaterial()->GetPathName() : FString();
                        bApplied = AppliedMaterial == Operation.Value;
                    }
                    if (!bApplied)
                    {
                        Error = TEXT("Landscape material mutation failed: ")
                            + Operation.ActorPath;
                        break;
                    }
                    bMapChanged = true;
                }
                else if (Action == TEXT("set_landscape_property"))
                {
                    ALandscapeProxy* Landscape = ResolveLandscape(World, Operation.ActorPath);
                    FProperty* Property = Landscape
                        ? FindFProperty<FProperty>(
                            Landscape->GetClass(), FName(*Operation.PropertyName))
                        : nullptr;
                    bApplied = Landscape && IsSupportedLandscapeProperty(Property)
                        && ExportActorProperty(Landscape, Property) == Operation.ExpectedValue;
                    if (bApplied)
                    {
                        Landscape->Modify();
                        bApplied = Property->ImportText_InContainer(
                            *Operation.Value, Landscape, Landscape, PPF_None) != nullptr;
                    }
                    if (bApplied)
                    {
                        FPropertyChangedEvent Event(
                            Property, EPropertyChangeType::ValueSet);
                        Landscape->PostEditChangeProperty(Event);
                        bApplied = ExportActorProperty(Landscape, Property) == Operation.Value;
                    }
                    if (!bApplied)
                    {
                        Error = TEXT("Landscape property mutation failed: ")
                            + Operation.ActorPath + TEXT("|") + Operation.PropertyName;
                        break;
                    }
                    bMapChanged = true;
                }
                else if (Action == TEXT("create_landscape_edit_layer"))
                {
                    ALandscape* Landscape = Cast<ALandscape>(
                        ResolveLandscape(World, Operation.ActorPath));
                    const FName LayerName(*Operation.Label.TrimStartAndEnd());
                    bApplied = Landscape && !LayerName.IsNone()
                        && Operation.ExpectedValue
                            == FString::FromInt(Landscape->GetEditLayersConst().Num())
                        && Landscape->IsLayerNameUnique(LayerName);
                    int32 NewLayerIndex = INDEX_NONE;
                    if (bApplied)
                    {
                        Landscape->Modify();
                        NewLayerIndex = Landscape->CreateLayer(LayerName);
                        bApplied = NewLayerIndex != INDEX_NONE
                            && Landscape->GetEditLayer(NewLayerIndex)
                            && Landscape->GetEditLayer(NewLayerIndex)->GetName() == LayerName;
                    }
                    if (bApplied)
                    {
                        Result.CreatedObjectPaths.Add(
                            Landscape->GetEditLayer(NewLayerIndex)
                                ->GetGuid().ToString(EGuidFormats::Digits));
                    }
                    else
                    {
                        Error = TEXT("Landscape edit layer creation failed: ")
                            + Operation.ActorPath + TEXT("|") + Operation.Label;
                        break;
                    }
                    bMapChanged = true;
                }
                else if (Action == TEXT("set_landscape_edit_layer_property"))
                {
                    ALandscape* Landscape = Cast<ALandscape>(
                        ResolveLandscape(World, Operation.ActorPath));
                    ULandscapeEditLayerBase* Layer = ResolveLandscapeEditLayer(
                        Landscape, Operation.AssetPath);
                    bApplied = Layer
                        && LandscapeEditLayerPropertyValue(Layer, Operation.PropertyName)
                            == Operation.ExpectedValue
                        && IsValidLandscapeEditLayerValue(
                            Landscape, Layer, Operation.PropertyName, Operation.Value);
                    if (bApplied)
                    {
                        bool bValue = false;
                        float Alpha = 0.0f;
                        if (Operation.PropertyName.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
                            Layer->SetName(FName(*Operation.Value), true);
                        else if (Operation.PropertyName.Equals(TEXT("Visible"), ESearchCase::IgnoreCase)
                            && ParseBoolStrict(Operation.Value, bValue))
                            Layer->SetVisible(bValue, true);
                        else if (Operation.PropertyName.Equals(TEXT("Locked"), ESearchCase::IgnoreCase)
                            && ParseBoolStrict(Operation.Value, bValue))
                            Layer->SetLocked(bValue, true);
                        else if (Operation.PropertyName.Equals(TEXT("HeightmapAlpha"), ESearchCase::IgnoreCase)
                            && LexTryParseString(Alpha, *Operation.Value))
                            Layer->SetAlphaForTargetType(
                                ELandscapeToolTargetType::Heightmap,
                                Alpha,
                                true,
                                EPropertyChangeType::ValueSet);
                        else if (Operation.PropertyName.Equals(TEXT("WeightmapAlpha"), ESearchCase::IgnoreCase)
                            && LexTryParseString(Alpha, *Operation.Value))
                            Layer->SetAlphaForTargetType(
                                ELandscapeToolTargetType::Weightmap,
                                Alpha,
                                true,
                                EPropertyChangeType::ValueSet);
                        else
                            bApplied = false;
                    }
                    bApplied = bApplied
                        && LandscapeEditLayerPropertyValue(Layer, Operation.PropertyName)
                            == Operation.Value;
                    if (!bApplied)
                    {
                        Error = TEXT("Landscape edit layer property mutation failed: ")
                            + Operation.AssetPath + TEXT("|") + Operation.PropertyName;
                        break;
                    }
                    bMapChanged = true;
                }
                else if (Action == TEXT("move_landscape_edit_layer")
                    || Action == TEXT("delete_landscape_edit_layer"))
                {
                    ALandscape* Landscape = Cast<ALandscape>(
                        ResolveLandscape(World, Operation.ActorPath));
                    int32 CurrentIndex = INDEX_NONE;
                    ULandscapeEditLayerBase* Layer = ResolveLandscapeEditLayer(
                        Landscape, Operation.AssetPath, &CurrentIndex);
                    bApplied = Layer && !Layer->IsLocked();
                    if (bApplied && Action == TEXT("move_landscape_edit_layer"))
                    {
                        bApplied = Operation.ExpectedValue == FString::FromInt(CurrentIndex)
                            && Landscape->GetEditLayersConst().IsValidIndex(Operation.InstanceIndex)
                            && Landscape->ReorderLayer(CurrentIndex, Operation.InstanceIndex)
                            && Landscape->GetLayerIndex(Layer->GetGuid()) == Operation.InstanceIndex;
                    }
                    else if (bApplied)
                    {
                        const FGuid LayerGuid = Layer->GetGuid();
                        bApplied = CurrentIndex > 0
                            && Operation.ExpectedValue == Layer->GetName().ToString()
                            && Landscape->DeleteLayer(CurrentIndex)
                            && Landscape->GetLayerIndex(LayerGuid) == INDEX_NONE;
                    }
                    if (!bApplied)
                    {
                        Error = Operation.Action + TEXT(" failed for ")
                            + Operation.ActorPath + TEXT("|") + Operation.AssetPath;
                        break;
                    }
                    bMapChanged = true;
                }
                else if (Action == TEXT("set_foliage_instance_transform")
                    || Action == TEXT("remove_foliage_instance")
                    || Action == TEXT("remove_foliage_type"))
                {
                    AInstancedFoliageActor* FoliageActor = nullptr;
                    UFoliageType* FoliageType = nullptr;
                    FFoliageInfo* FoliageInfo = nullptr;
                    bApplied = ResolveFoliageTarget(
                        World, Operation, FoliageActor, FoliageType, FoliageInfo);
                    if (bApplied)
                    {
                        FoliageActor->Modify();
                        if (Action == TEXT("remove_foliage_type"))
                        {
                            bApplied = Operation.ExpectedValue
                                == FString::FromInt(FoliageInfo->Instances.Num());
                            if (bApplied)
                            {
                                UFoliageType* MutableType = FoliageType;
                                FoliageActor->RemoveFoliageType(&MutableType, 1);
                                bApplied = FoliageActor->FindInfo(FoliageType) == nullptr;
                            }
                        }
                        else if (FoliageInfo->Instances.IsValidIndex(Operation.InstanceIndex))
                        {
                            const FString CurrentTransform =
                                FoliageInfo->Instances[Operation.InstanceIndex]
                                    .GetInstanceWorldTransform().ToString();
                            bApplied = CurrentTransform == Operation.ExpectedValue;
                            if (bApplied && Action == TEXT("set_foliage_instance_transform"))
                            {
                                const FTransform Transform = MakeOperationTransform(Operation);
                                const int32 Index = Operation.InstanceIndex;
                                const TArrayView<const int32> InstanceView =
                                    MakeArrayView(&Index, 1);
                                FoliageInfo->PreMoveInstances(InstanceView);
                                FoliageInfo->Instances[Index]
                                    .SetInstanceWorldTransform(Transform);
                                FoliageInfo->PostMoveInstances(InstanceView, true);
                                bApplied = FoliageInfo->Instances[Operation.InstanceIndex]
                                    .GetInstanceWorldTransform().Equals(Transform, 0.01f);
                            }
                            else if (bApplied)
                            {
                                const int32 PreviousCount = FoliageInfo->Instances.Num();
                                const int32 Index = Operation.InstanceIndex;
                                FoliageInfo->RemoveInstances(
                                    MakeArrayView(&Index, 1), true);
                                bApplied = FoliageInfo->Instances.Num() == PreviousCount - 1;
                            }
                        }
                        else
                        {
                            bApplied = false;
                        }
                    }
                    if (!bApplied)
                    {
                        Error = Operation.Action + TEXT(" failed for ")
                            + Operation.ActorPath + TEXT("|") + Operation.AssetPath;
                        break;
                    }
                    bMapChanged = true;
                }
                else
                {
                    AActor* Actor = ResolveActor(World, Operation.ActorPath);
                    if (IsProtectedEditorActor(Actor))
                    {
                        Error = TEXT("Actor disappeared or is protected: ") + Operation.ActorPath;
                        bApplied = false;
                        break;
                    }
                    Actor->Modify();
                    if (Action == TEXT("delete_actor"))
                    {
                        bApplied = World->EditorDestroyActor(Actor, true);
                    }
                    else if (Action == TEXT("set_transform"))
                    {
                        const FTransform Transform(
                            FRotator(
                                Operation.RotationPitch,
                                Operation.RotationYaw,
                                Operation.RotationRoll),
                            FVector(Operation.LocationX, Operation.LocationY, Operation.LocationZ),
                            FVector(Operation.ScaleX, Operation.ScaleY, Operation.ScaleZ));
                        bApplied = Actor->SetActorTransform(
                            Transform, false, nullptr, ETeleportType::TeleportPhysics);
                        Actor->PostEditMove(true);
                    }
                    else if (Action == TEXT("set_actor_label"))
                    {
                        Actor->SetActorLabel(Operation.Label, true);
                    }
                    else if (Action == TEXT("set_actor_property"))
                    {
                        FProperty* Property = FindFProperty<FProperty>(
                            Actor->GetClass(), FName(*Operation.PropertyName));
                        const FString CurrentValue = ExportActorProperty(Actor, Property);
                        if (!IsSupportedActorProperty(Property)
                            || CurrentValue != Operation.ExpectedValue
                            || !Property->ImportText_InContainer(
                                *Operation.Value, Actor, Actor, PPF_None))
                        {
                            bApplied = false;
                        }
                        else
                        {
                            Actor->PostEditChange();
                        }
                    }
                    else if (Action == TEXT("set_component_property"))
                    {
                        UActorComponent* Component = ResolveOwnedComponent(
                            Actor, Operation.ComponentPath);
                        FProperty* Property = Component
                            ? FindFProperty<FProperty>(
                                Component->GetClass(), FName(*Operation.PropertyName))
                            : nullptr;
                        const FString CurrentValue = ExportComponentProperty(Component, Property);
                        if (!Component || !IsSupportedComponentProperty(Property)
                            || CurrentValue != Operation.ExpectedValue)
                        {
                            bApplied = false;
                        }
                        else
                        {
                            Component->Modify();
                            bApplied = Property->ImportText_InContainer(
                                *Operation.Value, Component, Component, PPF_None) != nullptr;
                            if (bApplied)
                            {
                                FPropertyChangedEvent Event(
                                    Property, EPropertyChangeType::ValueSet);
                                Component->PostEditChangeProperty(Event);
                                Component->MarkRenderStateDirty();
                                Actor->PostEditChange();
                            }
                        }
                    }
                    if (!bApplied)
                    {
                        Error = Operation.Action + TEXT(" failed for ") + Operation.ActorPath;
                        break;
                    }
                    bMapChanged = true;
                }
                if (!bApplied)
                {
                    Error = Operation.Action + TEXT(" failed");
                    break;
                }
                ++Result.AppliedOperationCount;
            }
        }

        if (!bApplied)
        {
            if (!PreviousRendererValues.IsEmpty())
            {
                Result.bRolledBack = RestoreRendererProperties(
                    GetMutableDefault<URendererSettings>(),
                    PreviousRendererValues,
                    false);
            }
            else
            {
                Result.bRolledBack = GEditor && GEditor->UndoTransaction();
            }
            Result.Code = Result.bRolledBack
                ? TEXT("apply_failed")
                : TEXT("rollback_failed");
            Result.Message = Result.bRolledBack
                ? Error.Left(1200)
                : TEXT("World apply failed and the previous state could not be fully restored.");
            Result.RevisionAfter = BuildWorldRevision(World);
            return Result;
        }
        if (bSave)
        {
            bool bPersisted = true;
            if (bRendererChanged)
            {
                bPersisted = PersistRendererConfig(
                    GetMutableDefault<URendererSettings>());
            }
            if (bPersisted && bMapChanged)
            {
                bPersisted = SaveWorld(World);
            }
            Result.bSaved = bPersisted;
            if (!Result.bSaved)
            {
                if (bRendererChanged)
                {
                    const bool bPropertiesRestored = RestoreRendererProperties(
                        GetMutableDefault<URendererSettings>(),
                        PreviousRendererValues,
                        false);
                    const bool bConfigRestored = RestoreRendererConfig(
                        PreviousRendererConfig);
                    Result.bRolledBack = bPropertiesRestored && bConfigRestored;
                    Result.Code = Result.bRolledBack
                        ? TEXT("save_failed")
                        : TEXT("rollback_failed");
                    Result.Message = Result.bRolledBack
                        ? TEXT("Renderer config save failed; previous values were restored in memory and config.")
                        : TEXT("Renderer config save failed and the previous config could not be fully restored.");
                }
                else
                {
                    Result.bRolledBack = GEditor && GEditor->UndoTransaction();
                    Result.Code = Result.bRolledBack
                        ? TEXT("save_failed")
                        : TEXT("rollback_failed");
                    Result.Message = Result.bRolledBack
                        ? TEXT("Map save failed; the scoped world edit was undone.")
                        : TEXT("Map save failed and the scoped world edit could not be undone.");
                }
                Result.RevisionAfter = BuildWorldRevision(World);
                return Result;
            }
        }
        Result.bOk = true;
        Result.RevisionAfter = BuildWorldRevision(World);
        return Result;
    }

private:
    void RefreshEnvironmentBuildJob(
        FEnvironmentBuildJob& Job,
        const bool bReloadMapWhenComplete)
    {
        if (Job.bProcessRunning && Job.ProcessHandle.IsValid()
            && !FPlatformProcess::IsProcRunning(Job.ProcessHandle))
        {
            int32 ReturnCode = INDEX_NONE;
            const bool bHasReturnCode = FPlatformProcess::GetProcReturnCode(
                Job.ProcessHandle, &ReturnCode);
            FPlatformProcess::CloseProc(Job.ProcessHandle);
            Job.bProcessRunning = false;
            Job.ExitCode = bHasReturnCode ? ReturnCode : INDEX_NONE;
            Job.FinishedAtUtc = FDateTime::UtcNow().ToIso8601();
            if (bHasReturnCode && ReturnCode == 0)
            {
                Job.Status = TEXT("succeeded");
                Job.Code.Reset();
                Job.Message = TEXT("The native environment builder completed successfully.");
            }
            else
            {
                Job.Status = TEXT("failed");
                Job.Code = TEXT("process_failed");
                Job.Message = bHasReturnCode
                    ? FString::Printf(TEXT("Builder exited with code %d."), ReturnCode)
                    : TEXT("Builder ended without a readable process return code.");
            }
        }
        if (!Job.bProcessRunning && !Job.bMapReloaded && bReloadMapWhenComplete)
        {
            ReloadEnvironmentBuildMap(Job);
        }
    }

    void ReloadEnvironmentBuildMap(FEnvironmentBuildJob& Job)
    {
        UWorld* HostWorld = GEditor
            ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!HostWorld
            || HostWorld->GetOutermost()->GetName() != Job.HostMapPath
            || BuildWorldRevision(HostWorld) != Job.HostRevision)
        {
            if (Job.Status == TEXT("succeeded"))
            {
                Job.Status = TEXT("reload_blocked");
                Job.Code = TEXT("host_world_changed");
                Job.Message = TEXT("The temporary host world changed; automatic map reload was refused.");
            }
            return;
        }

        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
        AssetRegistry.ScanModifiedAssetFiles({MapFilename(Job.Request.MapPath)});
        AssetRegistry.ScanPathsSynchronous(
            ULevel::GetExternalObjectsPaths(Job.Request.MapPath), true);
        UWorld* ReloadedWorld = UEditorLoadingAndSavingUtils::LoadMap(
            MapFilename(Job.Request.MapPath));
        if (!ReloadedWorld
            || ReloadedWorld->GetOutermost()->GetName() != Job.Request.MapPath)
        {
            if (Job.Status == TEXT("succeeded"))
            {
                Job.Status = TEXT("reload_blocked");
                Job.Code = TEXT("map_reload_failed");
                Job.Message = TEXT("The builder completed, but the exact source map could not be reloaded.");
            }
            return;
        }
        Job.bMapReloaded = true;
        if (Job.Status == TEXT("reload_blocked"))
        {
            Job.Status = Job.ExitCode == 0 ? TEXT("succeeded") : TEXT("failed");
            Job.Code = Job.ExitCode == 0 ? FString() : TEXT("process_failed");
        }
    }

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
        for (auto It = PostProcessPlans.CreateIterator(); It; ++It)
        {
            if (Now - It.Value().CreatedAtSeconds > PlanLifetimeSeconds)
            {
                It.RemoveCurrent();
            }
        }
        for (auto It = MapPlans.CreateIterator(); It; ++It)
        {
            if (Now - It.Value().CreatedAtSeconds > PlanLifetimeSeconds)
            {
                It.RemoveCurrent();
            }
        }
        for (auto It = EnvironmentBuildPlans.CreateIterator(); It; ++It)
        {
            if (Now - It.Value().CreatedAtSeconds > PlanLifetimeSeconds)
            {
                It.RemoveCurrent();
            }
        }
        for (auto It = LandscapeLayerInfoPlans.CreateIterator(); It; ++It)
        {
            if (Now - It.Value().CreatedAtSeconds > PlanLifetimeSeconds)
            {
                It.RemoveCurrent();
            }
        }
    }

    TMap<FString, FWorldPatchPlan> Plans;
    TMap<FString, FPostProcessPatchPlan> PostProcessPlans;
    TMap<FString, FMapLifecyclePlan> MapPlans;
    TMap<FString, FEnvironmentBuildPlan> EnvironmentBuildPlans;
    TMap<FString, FLandscapeLayerInfoCreatePlan> LandscapeLayerInfoPlans;
    TMap<FString, FEnvironmentBuildJob> EnvironmentJobs;
};

IMPLEMENT_MODULE(FThomasEditorWorldModule, ThomasEditorWorld)
