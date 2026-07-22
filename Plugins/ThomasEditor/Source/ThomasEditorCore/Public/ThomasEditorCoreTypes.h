#pragma once

#include "CoreMinimal.h"

#include "ThomasEditorCoreTypes.generated.h"

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasProviderStatus
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Name;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString ModuleName;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bAvailable = false;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bLoaded = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasCoreStatus
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bOk = true;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString ProjectName;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString EngineVersion;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString ArchitectureVersion = TEXT("0.4.0-dev");

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bToolSearchRequired = true;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bAssetsProviderLoadedAtCoreStartup = false;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    int32 LoadedProviderCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    TArray<FThomasProviderStatus> Providers;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAssetRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString AssetPath;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString PackageName;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString ClassPath;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bLoaded = false;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bDirty = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAssetInventoryResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bOk = false;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Code;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Message;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString PackageRoot;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    int32 MatchedCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bTruncated = false;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    TArray<FThomasAssetRecord> Items;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPropertyRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Name;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Type;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Value;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Category;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Tooltip;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString ClampMin;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString ClampMax;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bEditable = false;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bBlueprintVisible = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasObjectDetailsResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bOk = false;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Code;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Message;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString ObjectPath;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString ClassPath;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Revision;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bDirty = false;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bTruncated = false;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    TArray<FThomasPropertyRecord> Properties;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAssetReferencesResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bOk = false;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Code;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString Message;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    FString PackageName;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bDependenciesTruncated = false;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    bool bReferencersTruncated = false;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    TArray<FString> Dependencies;

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor")
    TArray<FString> Referencers;
};

/** One bounded, preconditioned scalar property change on a project asset. */
USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasObjectPatchOperation
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Name;
    /** Optional exact ExportText value observed by GetObjectDetails. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedValue;
    /** ImportText-compatible scalar value. Containers and arbitrary structs are rejected. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Value;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasObjectPatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ObjectPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasObjectPatchOperation> Operations;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasObjectPatchPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ObjectPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasObjectPatchApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ObjectPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

/** One exact project asset/package to save after a guarded mutation. */
USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAssetSaveItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    /** Exact dirty revision returned by the mutation apply or a fresh inspection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAssetSaveRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasAssetSaveItem> Items;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAssetSaveRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Filename;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bWasDirty = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
};

/** Result of an explicit, retryable save inside the already-open canonical Editor process. */
USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAssetSaveResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 SavedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AlreadyCleanCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasAssetSaveRecord> Items;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPluginDetailsResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString FriendlyName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString VersionName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Type;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString DescriptorFile;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bMounted = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bEnabledByDefault = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bExplicitProjectReference = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bProjectReferenceEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCanContainContent = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bExplicitlyLoaded = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bBeta = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bExperimental = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bInstalled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bHidden = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSealed = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Modules;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Dependencies;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPluginChangePlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PluginName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ProjectRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCurrentEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDesiredEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R2");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPluginChangeApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PluginName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRestartRequired = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ProjectRevision;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWorldActorRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ActorPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Label;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Folder;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Location;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Rotation;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Scale;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Mobility;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ComponentCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSelected = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bHiddenInEditor = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bEditorOnly = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWorldInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString WorldType;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString DefaultGameModeClass;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bWorldPartition = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDirty = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bPlayInEditor = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bTruncated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ActorCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 SelectedActorCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasWorldActorRecord> Actors;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasDataLayerRecord
{
    GENERATED_BODY()

    /** Exact UDataLayerInstance object path used by guarded World mutations. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ObjectPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ShortName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString FullName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ParentName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString InitialRuntimeState;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRuntime = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bClientOnly = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bServerOnly = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bVisible = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bInitiallyVisible = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bLoadedInEditor = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ActorCount = 0;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasLandscapeEditLayerRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Index = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ObjectPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Guid;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bVisible = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bLocked = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float HeightmapAlpha = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float WeightmapAlpha = 0.0f;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasLandscapeRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ActorPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Label;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString LandscapeGuid;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MaterialPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString HoleMaterialPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ComponentCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 CollisionComponentCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 NaniteComponentCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ComponentSizeQuads = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 SubsectionSizeQuads = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 NumSubsections = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MinX = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MinY = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MaxX = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MaxY = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 VertexSizeX = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 VertexSizeY = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bHasValidExtent = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bHasLayersContent = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCanHaveLayersContent = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bEnableNanite = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 NaniteLODIndex = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bNaniteSkirtEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float NaniteSkirtDepth = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 NanitePositionPrecision = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float NaniteMaxEdgeLengthFactor = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bUsedForNavigation = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bFillCollisionUnderLandscapeForNavmesh = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString NavigationGeometryGatheringMode;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bUseDynamicMaterialInstance = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 EditLayerCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bEditLayersTruncated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasLandscapeEditLayerRecord> EditLayers;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasLandscapeRegionInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ActorPath;
    /** Optional exact Landscape edit-layer GUID. Empty targets the base layer. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString EditLayerGuid;
    /** Optional LandscapeLayerInfoObject path used for weight inspection. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString LayerInfoPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MinX = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MinY = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MaxX = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MaxY = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Width = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Height = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 SampleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString HeightDataHash;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MinHeightValue = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MaxHeightValue = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double AverageHeightValue = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString WeightDataHash;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MinWeightValue = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MaxWeightValue = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double AverageWeightValue = 0.0;
    /** Row-major samples, bounded by MaxReturnedSamples. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<int32> HeightSamples;
    /** Row-major samples for LayerInfoPath, bounded by MaxReturnedSamples. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<int32> WeightSamples;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSamplesTruncated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasLandscapeLayerInfoCreateRequest
{
    GENERATED_BODY()

    /** Exact /Game/PropHunt package path for the new LandscapeLayerInfoObject. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    /** Creation requires the exact missing revision. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision = TEXT("missing");
    /** Exact Landscape material target-layer name. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString LayerName;
    /** True creates a non-weight-blended target suitable for independent masks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bNoWeightBlend = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasLandscapeLayerInfoCreatePlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ObjectPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString LayerName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bNoWeightBlend = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasLandscapeLayerInfoCreateApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ObjectPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCreated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasFoliageInstanceRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Index = INDEX_NONE;
    /** Exact revision guard used by set_foliage_instance_transform/remove_foliage_instance. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Transform;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float LocationX = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float LocationY = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float LocationZ = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float RotationPitch = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float RotationYaw = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float RotationRoll = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float ScaleX = 1.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float ScaleY = 1.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float ScaleZ = 1.0f;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasFoliageRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ActorPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString FoliageTypePath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString FoliageTypeClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString SourcePath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bFoliageTypeAsset = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 InstanceCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bInstancesTruncated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasFoliageInstanceRecord> Instances;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasHLODActorRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ActorPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Label;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString SourceCellGuid;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ResourcesPackagePath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 LODLevel = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ComponentCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double MinVisibleDistance = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bStandalone = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCustomHLOD = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRequireWarmup = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bBoundsValid = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BoundsCenterX = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BoundsCenterY = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BoundsCenterZ = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BoundsExtentX = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BoundsExtentY = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BoundsExtentZ = 0.0;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasNavigationDataRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ActorPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Label;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RuntimeGeneration;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AgentName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PreferredNavDataClass;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRegistered = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSupportsRuntimeGeneration = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSupportsStreaming = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCanBeMainNavData = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSupportsDefaultAgent = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bNeedsRebuild = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bWorldPartitionedRecast = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bFixedTilePoolSize = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bFullyAsyncGathering = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float AgentRadius = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float AgentHeight = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float AgentStepHeight = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float AgentMaxSlope = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float TileSizeUU = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 TilePoolSize = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 TileCapacity = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ActiveTileCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float DefaultCellSize = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float DefaultCellHeight = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float DefaultAgentMaxStepHeight = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BuilderOverlap = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double QueryExtentX = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double QueryExtentY = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double QueryExtentZ = 0.0;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasNavigationBoundsRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ActorPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Label;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int64 SupportedAgentBits = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bAllowPhysicsOverlap = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bBoundsValid = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BoundsCenterX = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BoundsCenterY = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BoundsCenterZ = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BoundsExtentX = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BoundsExtentY = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double BoundsExtentZ = 0.0;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasEnvironmentInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bWorldPartition = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bWorldPartitionStreamingEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bWorldPartitionStreamingEnabledInEditor = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RuntimeHashClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 HLODActorCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 LandscapeActorCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 FoliageActorCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 FoliageTypeCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 FoliageInstanceCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bNavigationSystemAvailable = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 NavigationDataCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 NavigationBoundsVolumeCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 DataLayerCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDataLayersTruncated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasDataLayerRecord> DataLayers;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasLandscapeRecord> Landscapes;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasFoliageRecord> Foliage;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasHLODActorRecord> HLODDetails;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasNavigationDataRecord> NavigationData;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasNavigationBoundsRecord> NavigationBounds;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> HLODActors;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> NavigationDataActors;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWorldComponentInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ActorPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ComponentPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ComponentName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRegistered = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bActive = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bTruncated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasPropertyRecord> Properties;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMapLifecycleRequest
{
    GENERATED_BODY()

    /** save_current_map, create_blank_map, create_world_partition_map, or open_map. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    /** Exact package path and revision returned by InspectCurrentLevel. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString CurrentMapPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    /** Required for create_blank_map/create_world_partition_map/open_map and restricted to /Game/PropHunt. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString TargetMapPath;
    /** Required before an operation replaces the current Editor world. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmWorldSwitch = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMapLifecyclePlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Action;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString CurrentMapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString TargetMapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMapLifecycleApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Action;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PreviousMapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bWorldSwitched = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRecoveryAttempted = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasEnvironmentBuildRequest
{
    GENERATED_BODY()

    /** build_navigation, build_hlods, or delete_hlods. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    /** Exact World Partition map package and revision returned by InspectEnvironment. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    /** Required because the native builder must temporarily unload the current map. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmWorldSwitch = false;
    /** Required by delete_hlods and destructive navigation package cleanup. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    /** Navigation builder only: emit verbose commandlet diagnostics. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bVerbose = false;
    /** Navigation builder only: remove previous navigation packages before rebuilding. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bCleanPackages = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasEnvironmentBuildPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Action;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString BuilderClass;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R2");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasEnvironmentBuildJobResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString JobId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Action;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    /** running, succeeded, failed, cancelled, or reload_blocked. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Status;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString LogPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString StartedAtUtc;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString FinishedAtUtc;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ProcessId = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ExitCode = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bProcessRunning = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bMapReloaded = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCancelled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWorldOperation
{
    GENERATED_BODY()

    /** spawn_actor, create_landscape, delete_actor, set_transform, set_actor_property, set_component_property, set_actor_label, set_renderer_property, Data Layer, Foliage, or Landscape settings/edit-layer operations. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ActorPath;
    /** Exact owned component object path for set_component_property. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ComponentPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ClassPath;
    /** Exact asset/object path used by domain actions; Landscape edit-layer actions use the inspected layer Guid. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString PropertyName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Value;
    /** Exact foliage instance index returned by InspectEnvironment. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 InstanceIndex = INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float LocationX = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float LocationY = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float LocationZ = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float RotationPitch = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float RotationYaw = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float RotationRoll = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float ScaleX = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float ScaleY = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float ScaleZ = 1.0f;
    /** Typed create_landscape topology. Resulting vertices = components * sections * quads + 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeComponentCountX = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeComponentCountY = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeSectionsPerComponent = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeQuadsPerSection = 63;
    /** Flat uint16 Landscape height used by create_landscape (32768 is zero world height). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeHeightValue = 32768;
    /** Inclusive Landscape vertex rectangle used by typed height/weight operations. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeMinX = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeMinY = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeMaxX = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeMaxY = 0;
    /** Exact Landscape edit-layer GUID. Empty targets the base layer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString LandscapeEditLayerGuid;
    /** Row-major uint16 heights or uint8 weights for a bounded exact region. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<int32> LandscapeValues;
    /** Relative leaf/subpath below Saved/ThomasEditor/Imports/Landscape; absolute paths are refused. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString SourceFile;
    /** Landscape-coordinate brush center used by sculpt_landscape_height/paint_landscape_weight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeBrushCenterX = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeBrushCenterY = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeBrushRadius = 1;
    /** Signed additive height or weight delta at the brush center. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LandscapeBrushStrength = 1;
    /** Fraction [0,1] of the radius occupied by the smooth falloff rim. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float LandscapeBrushFalloff = 0.5f;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWorldPatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasWorldOperation> Operations;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWorldPatchPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWorldPatchApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> CreatedActorPaths;
    /** Object or stable domain handles created by the batch (for foliage: IFA|type|instance index). */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> CreatedObjectPaths;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasLightingInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 LightCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PostProcessVolumeCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bHasSkyLight = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bHasSkyAtmosphere = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bHasExponentialHeightFog = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> LightingActors;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasPropertyRecord> RendererSettings;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPostProcessSettingRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString OverrideName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Type;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Value;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Category;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Tooltip;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bEditable = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bHasOverride = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOverrideEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bLumenRelated = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPostProcessBlendableRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Index = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ObjectPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float Weight = 0.0f;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPostProcessInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ActorPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Label;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bUnbound = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float Priority = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float BlendRadius = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float BlendWeight = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MatchedSettingCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bTruncated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasPostProcessSettingRecord> Settings;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasPostProcessBlendableRecord> Blendables;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPostProcessOperation
{
    GENERATED_BODY()

    /** set_setting, set_override, add_blendable, set_blendable_weight, or remove_blendable. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString SettingName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Value;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bExpectedOverride = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bOverride = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ObjectPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 BlendableIndex = INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float ExpectedWeight = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float Weight = 1.0f;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPostProcessPatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ActorPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasPostProcessOperation> Operations;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPostProcessPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ActorPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPostProcessApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MapPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ActorPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasStaticMeshInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDirty = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bReadOnly = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bNaniteEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bNaniteDataValid = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bAllowCpuAccess = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 LodCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MaterialSlotCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 LOD0VertexCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 LOD0TriangleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 CollisionPrimitiveCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 LightMapCoordinateIndex = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Materials;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasNanitePlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCurrentEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDesiredEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasNaniteApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bNaniteEnabled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMaterialExpressionRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpressionGuid;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ObjectName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Description;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PositionX = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PositionY = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Inputs;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Outputs;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMaterialConnectionRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString FromExpressionGuid;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString FromObjectName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString FromOutput;
    /** expression or material. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString TargetKind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ToExpressionGuid;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ToObjectName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ToInput;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MaterialProperty;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMaterialGraphDiagnostic
{
    GENERATED_BODY()

    /** info, warning, or error. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Severity;
    /** orphan_expression, unreachable_expression, dangling_connection, or overlapping_nodes. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpressionGuid;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ObjectName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMaterialInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MaterialDomain;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString BlendMode;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ShadingModel;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDirty = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bTwoSided = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bTruncated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ExpressionCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ConnectionCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MaterialRootConnectionCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ReachableExpressionCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 UnreachableExpressionCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OrphanExpressionCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OverlapGroupCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasMaterialExpressionRecord> Expressions;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasMaterialConnectionRecord> Connections;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasMaterialGraphDiagnostic> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMaterialOperation
{
    GENERATED_BODY()

    /** create_expression, set_expression_property, connect_expressions, connect_material_property, disconnect_expression, disconnect_material_property, or delete_expression. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Handle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpressionGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString OtherHandle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString OtherExpressionGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString PropertyName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Value;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString FromOutput;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ToInput;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString MaterialProperty;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PositionX = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PositionY = 0;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMaterialPatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bCreateIfMissing = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasMaterialOperation> Operations;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMaterialPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMaterialApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCreated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCompiled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasDomainInventoryResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Domain;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PackageRoot;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MatchedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bTruncated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> ClassCounts;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasAssetRecord> Items;
};

/** Typed, bounded inspection shared by native Animation, Niagara and MetaSound providers. */
USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasSpecializedAssetInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Domain;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDirty = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bTruncated = false;
    /** Stable key=value summaries for domain-specific scalar state. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Metrics;
    /** Bounded structural items such as notifies, montage sections, emitters, parameters or graph nodes. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Items;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasDomainAssetCreateRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision = TEXT("missing");
    /** Provider-specific native asset kind. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetKind;
    /** Optional exact project asset used as factory context, such as a Skeleton, Skeletal Mesh, or IK Rig. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ContextAssetPath;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasDomainAssetCreatePlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString FactoryClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ContextAssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasDomainAssetCreateApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCreated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPCGOperation
{
    GENERATED_BODY()

    /**
     * add_node, set_node_property, set_node_position, set_node_title, connect, disconnect, remove_node,
     * set_graph_usage, add/rename/remove_graph_parameter, set_graph_parameter, or
     * reset_graph_parameter_override.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    /** Batch-local identifier returned as a created handle by add_node. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Handle;
    /** Exact UPCGNode object path returned by inspection, or input/output. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString NodePath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString OtherHandle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString OtherNodePath;
    /** Exact UPCGSettings class path for add_node. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString SettingsClassPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString PropertyName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Value;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString FromPin;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ToPin;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Title;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PositionX = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PositionY = 0;
    /** Exact graph parameter name. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ParameterName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString NewName;
    /** bool, int32, int64, float, double, name, string, text, enum, struct, object, soft_object, class, or soft_class. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ParameterType;
    /** Exact UEnum/UScriptStruct/UClass path when the parameter type requires one. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString TypeObjectPath;
    /** standard, asset, or level for set_graph_usage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString GraphUsage;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPCGPatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasPCGOperation> Operations;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPCGPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPCGApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> CreatedNodePaths;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPCGExecutionPlanRequest
{
    GENERATED_BODY()

    /** Exact PCG Graph or Graph Instance asset under /Game/PropHunt. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 Seed = 42;
    /** Standalone graph execution can run asset-generating nodes and therefore requires explicit R2 confirmation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmExecution = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPCGExecutionPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Seed = 42;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R2");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPCGExecutionStartResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString JobId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString TaskId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Status;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPCGExecutionStatusResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString JobId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString TaskId;
    /** running, cancel_requested, completed, aborted, or source_lost. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Status;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRunning = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCancelRequested = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Seed = 42;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 TaggedDataCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 NullDataCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double StartedAtSeconds = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double CompletedAtSeconds = 0.0;
    /** Bounded output summaries grouped by class and pin. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Outputs;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPCGExecutionCancelResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString JobId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Status;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasInputMappingRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString InputActionPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Key;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bPlayerMappable = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString MappingName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Triggers;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Modifiers;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasEnhancedInputInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ValueType;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDirty = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 MappingCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasInputMappingRecord> Mappings;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasEnhancedInputOperation
{
    GENERATED_BODY()

    /** set_value_type, add_mapping, or remove_mapping. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString InputActionPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Key;
    /** Boolean, Axis1D, Axis2D, or Axis3D. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ValueType;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasEnhancedInputPatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    /** InputAction or InputMappingContext; required only when creating. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bCreateIfMissing = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasEnhancedInputOperation> Operations;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasEnhancedInputPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasEnhancedInputApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCreated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAudioCreateRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision = TEXT("missing");
    /**
     * sound_attenuation, sound_concurrency, sound_cue, sound_class, sound_mix,
     * sound_submix, sound_source_bus, dialogue_voice, dialogue_wave,
     * meta_sound_source or meta_sound_patch. Configure settings through Details after creation.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetKind;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAudioCreatePlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString FactoryClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAudioCreateApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCreated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
};

/** One typed edit in a guarded Niagara System batch. */
USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasNiagaraOperation
{
    GENERATED_BODY()

    /**
     * add_emitter, remove_emitter, rename_emitter, set_emitter_enabled,
     * add_user_parameter, remove_user_parameter, rename_user_parameter, set_user_parameter,
     * add_module, remove_module, set_module_enabled,
     * add_renderer, remove_renderer, set_renderer_enabled, or compile.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    /** Stable emitter handle GUID returned by inspection; empty for system stacks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString EmitterHandleId;
    /** Existing Niagara Emitter asset used by add_emitter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString EmitterAssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString NewName;
    /** float, int, bool, vec2, vec3, vec4, color, position, quat, or matrix4. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Type;
    /** Import-like scalar/vector value, for example 2.5 or X=1 Y=2 Z=3. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Value;
    /** Existing Niagara module script asset used by add_module. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ScriptAssetPath;
    /** system_spawn, system_update, emitter_spawn, emitter_update, particle_spawn, or particle_update. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ScriptUsage;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString UsageId;
    /** Reserved for exact insertion ordering; INDEX_NONE appends or uses ModuleNodeId as the predecessor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 ModuleIndex = INDEX_NONE;
    /** Stable module name returned by Niagara topology inspection; add_module treats it as the predecessor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ModuleNodeId;
    /** sprite, mesh, ribbon, light, decal, component, or volume. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RendererKind;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 RendererIndex = INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bEnabled = true;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasNiagaraPatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasNiagaraOperation> Operations;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasNiagaraPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasNiagaraApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCompiled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

/** One typed edit in a guarded MetaSound graph batch. */
USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMetaSoundOperation
{
    GENERATED_BODY()

    /**
     * add_interface, remove_interface, add_graph_input, add_graph_output, add_graph_variable,
     * remove_graph_input, remove_graph_output, remove_graph_variable,
     * add_node_by_class, add_node_from_asset, remove_node,
     * connect_nodes, disconnect_nodes, connect_graph_input, connect_graph_output,
     * set_node_input_default, set_graph_input_default, or set_node_location.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    /** Existing frontend node GUID or a caller alias produced by an earlier add_node operation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString NodeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString OtherNodeId;
    /** Alias to assign to a newly added node for later operations in the same batch. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ResultId;
    /** Full MetaSound class name accepted by FMetasoundFrontendClassName::Parse. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ClassName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 MajorVersion = 1;
    /** MetaSound Source/Patch asset used by add_node_from_asset. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ReferencedAssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString InterfaceName;
    /** MetaSound data type such as Float, Int32, Bool, String, Trigger, Audio, or UObject type. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString DataType;
    /** default, bool, int, float, string, object, or the corresponding *_array type. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString LiteralType = TEXT("default");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Value;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString InputName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString OutputName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float LocationX = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float LocationY = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConstructor = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMetaSoundPatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasMetaSoundOperation> Operations;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMetaSoundPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasMetaSoundApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRegistered = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> CreatedNodeIds;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

/** One token in a bounded, typed reverse-Polish transition-rule expression. */
USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAnimationTransitionToken
{
    GENERATED_BODY()

    /** bool_variable, float_variable, transition_getter, number, compare, numeric operator, and, or, or not. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Kind;
    /** Variable name, or one of the nine bounded native transition getters. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Name;
    /** Exact state name required only by arbitrary_state_blend_weight. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ContextName;
    /** less, less_equal, greater, greater_equal, equal, or not_equal for compare. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Operator;
    /** Finite numeric literal used by number. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") double Value = 0.0;
};

/** Typed value payload used by bounded Control Rig settings and metadata. */
USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasControlRigValueSpec
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bBoolValue = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float FloatValue = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 IntegerValue = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FVector2D Vector2DValue = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FVector VectorValue = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FVector Location = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FRotator Rotation = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FQuat Quaternion = FQuat::Identity;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FVector Scale = FVector::OneVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FLinearColor ColorValue = FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString NameValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ElementName;
    /** bone, null, control, curve, reference, connector, or socket. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ElementType;
    /** Metadata arrays are bounded to 16 entries by the Control Rig provider. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<bool> BoolArrayValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<float> FloatArrayValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<int32> IntegerArrayValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FString> NameArrayValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FVector> VectorArrayValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FRotator> RotatorArrayValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FQuat> QuaternionArrayValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FTransform> TransformArrayValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FLinearColor> ColorArrayValue;
    /** Parallel exact names/types used by element_key_array. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FString> ElementNames;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FString> ElementTypes;
};

/** One typed edit in an Animation Sequence, Montage, Skeleton, or Anim Blueprint batch. */
USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAnimationOperation
{
    GENERATED_BODY()

    /**
     * set_root_motion, set_additive_settings, add/remove_notify_track, add_notify, add_notify_state,
     * remove_notify_by_name, remove_notify_by_track, add/remove_curve,
     * set/remove_curve_key, add/remove/rename_montage_section, set_next_montage_section,
     * add/remove_montage_slot, add/remove_montage_segment, add/remove_socket,
     * add/rename/remove_state_machine, add/rename/remove_state, set_entry_state,
     * add/set/remove_transition, set/remove_state_sequence_player,
     * set/remove_state_blend_by_bool, set/remove_state_blend_space_player,
     * set/remove_state_blend_list_by_int, set/remove_state_apply_additive,
     * set/remove_state_layered_blend_per_bone,
     * add/remove_state_graph_node, set_state_graph_node_property,
     * resize_state_graph_node_array, resize_state_graph_pose_array,
     * replace_state_graph_constraint_array,
     * set_state_graph_pin_default,
     * add/remove_state_graph_link, or
     * set_transition_rule_constant, set_transition_rule_variable, or
     * remove_transition_rule_variable, set_transition_rule_time_remaining,
     * remove_transition_rule_time_remaining, set_transition_rule_expression, or
     * remove_transition_rule_expression, set_ik_retarget_root,
     * add/remove_ik_goal, add/remove_ik_retarget_chain, add/remove_ik_solver,
     * set_ik_solver_enabled, set_ik_solver_start_bone, set_ik_solver_end_bone,
     * move_ik_solver, add/remove_ik_bone_setting, or
     * connect_ik_goal_to_solver, disconnect_ik_goal_from_solver,
     * set_ik_root_motion_bone, set_ik_bone_excluded, or
     * set_ik_limb_solver_settings, set_ik_solver_setting,
     * set_ik_goal_setting, set_ik_bone_setting, set_ik_retargeter_rig,
     * add_default_ik_retarget_ops, add/remove/rename/move_ik_retarget_op,
     * set_ik_retarget_op_enabled, add/rename/remove_ik_retarget_override_set,
     * set_ik_retarget_override_set_active, set_ik_retarget_override_set_parent,
     * set/remove_ik_retarget_property_override, add/rename/remove_ik_retarget_pose,
     * set_current_ik_retarget_pose, set_ik_retarget_pose_root_offset,
     * add/rename/remove_control_rig_null, set_control_rig_null_transform,
     * add/rename/remove_control_rig_curve, set_control_rig_curve_value,
     * add/rename/remove_control_rig_float_control,
     * set_control_rig_float_value, add/rename/remove_control_rig_control, or
     * set_control_rig_control_value, set_control_rig_control_shape_settings,
     * set_control_rig_control_shape_transform, or
     * set_control_rig_control_limits, add/rename/remove_control_rig_socket,
     * set_control_rig_socket_transform, set_control_rig_socket_settings,
     * add/remove_control_rig_available_space,
     * set_control_rig_available_space_label, or
     * set_control_rig_available_space_index, set_control_rig_metadata,
     * remove_control_rig_metadata, add/rename/remove_control_rig_graph,
     * add/remove_control_rig_unit_node,
     * add/remove_control_rig_event_node,
     * add/remove_control_rig_member_variable,
     * add/remove_control_rig_variable_node,
     * add/remove_control_rig_function,
     * add/remove_control_rig_function_reference_node,
     * add/remove_control_rig_local_variable,
     * add/remove/rename/set_type_control_rig_function_pin,
     * set_index_control_rig_function_pin,
     * set_public/set_mutable/set_category/set_keywords/set_description_control_rig_function,
     * rename/set_default/set_type/set_index_control_rig_local_variable,
     * add/remove_control_rig_template_node,
     * resolve_control_rig_wildcard_pin,
     * unresolve_control_rig_template_node,
     * add/rename/remove_control_rig_comment_node, set_control_rig_comment,
     * set_control_rig_node_position, set_control_rig_node_size,
     * set_control_rig_node_color, set_control_rig_pin_default, or
     * set/add/duplicate/remove_control_rig_array_pin,
     * add/remove_control_rig_aggregate_pin,
     * add/remove_control_rig_link,
     * add_control_rig_free_reroute_node,
     * add_control_rig_reroute_node_on_link, or
     * remove_control_rig_reroute_node,
     * set_control_rig_template_pin_type,
     * collapse_control_rig_nodes, or
     * expand_control_rig_library_node.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString NewName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString TrackName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ClassPath;
    /** Referenced Animation asset, or external Control Rig host for a public RigVM function reference. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ReferencedAssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString SecondaryAssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString BoneName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString StartBoneName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString EndBoneName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString GoalName;
    /** Exact retarget-op name returned by IK Retargeter inspection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RetargetOpName;
    /** source or target for an IK Retargeter rig or pose operation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RetargetSide;
    /** Existing override-set parent, or exact Control Rig Bone/Null parent name. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ParentName;
    /** Exact direct field returned by IK Rig setting inspection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString PropertyName;
    /** Exact current inspected value required by guarded IK or Control Rig edits. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedValue;
    /**
     * bool, float, integer, vector2d, position, scale, rotator, transform,
     * transform_no_scale, euler_transform, or scale_float.
     * Used by the generic Control Rig control actions; float-specific actions
     * remain supported as compatibility aliases.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ControlType = TEXT("float");
    /** Optional Control Rig shape identifier used by shape-settings edits. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ControlShapeName;
    /** Exact Control Rig transform element used by available-space edits. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString SpaceName;
    /** bone, null, control, reference, or socket. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString SpaceType;
    /** Optional bounded label displayed for a Control Rig available space. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString DisplayLabel;
    /** Bounded Control Rig socket description. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Description;
    /** Exact target hierarchy type for a Control Rig metadata edit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ElementType;
    /** Safe metadata identifier for a Control Rig metadata edit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString MetadataName;
    /**
     * bool/float/integer/name/vector/rotator/quat/transform/color/element_key,
     * or the corresponding *_array form.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString MetadataType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FThomasControlRigValueSpec MetadataValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FThomasControlRigValueSpec ControlMinimumValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FThomasControlRigValueSpec ControlMaximumValue;
    /** ImportText value for a bounded IK setting, state-graph property, or complete ConstraintSetup array edit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString SettingValue;
    /** Empty selects the default RigVM model; otherwise use an exact inspected graph path or name. rename/remove_control_rig_graph target this field. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMGraphName;
    /** Exact registered RIGVM_METHOD name used by add_control_rig_unit_node. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMMethodName = TEXT("Execute");
    /** Exact notation returned by ControlRigAvailableTemplate inspection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMTemplateNotation;
    /** Bounded alias: bool, int32, float, double, name, string, vector2d, vector, vector4, quat, rotator, transform, or linear_color. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMType;
    /** Safe variable identifier used by add_control_rig_variable_node. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMVariableName;
    /** Safe function identifier. Function references use the local library unless ReferencedAssetPath selects an external public Control Rig function. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMFunctionName;
    /** Bounded category assigned to a local RigVM function. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMFunctionCategory;
    /** Bounded search keywords assigned to a local RigVM function. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMFunctionKeywords;
    /** Bounded tooltip/description assigned to a local RigVM function. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMFunctionDescription;
    /** input or output for a bounded local-function exposed pin. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMPinDirection;
    /** Creates a mutable local function with execute-context entry and return pins. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bRigVMFunctionMutable = false;
    /** Publishes or privatizes a local RigVM function. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bRigVMFunctionPublic = false;
    /** Creates an array variable using the bounded RigVMType element alias. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bRigVMTypeArray = false;
    /** Getter when true, setter when false. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bRigVMVariableGetter = true;
    /** Exact inspected RigVM pin path used by pin-default, array, aggregate-remove, and link-safe edits. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMPinPath;
    /** Bounded textual default parsed and validated by the native RigVM controller. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMPinDefaultValue;
    /** Bounded target size used by set_control_rig_array_pin_size. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 RigVMArraySize = 0;
    /** Optional bounded custom widget identifier used by a free RigVM reroute. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMCustomWidgetName;
    /** Creates a free RigVM reroute whose value pin is a constant/literal. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bRigVMRerouteConstant = false;
    /** Exact inspected output pin path used by a RigVM link edit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMSourcePinPath;
    /** Exact inspected input pin path used by a RigVM link edit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMTargetPinPath;
    /** Exact direct-node names used by a bounded RigVM collapse or expansion. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FString> RigVMNodeNames;
    /** Exact inspected node states parallel to RigVMNodeNames. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FString> RigVMExpectedNodeStates;
    /** Bounded text used by a native RigVM comment node. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RigVMCommentText;
    /** Positive bounded node size used by comment creation or set_control_rig_node_size. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FVector2D RigVMNodeSize = FVector2D(400.0, 200.0);
    /** Bounded UI font size used by a native RigVM comment node. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 RigVMCommentFontSize = 18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bRigVMCommentBubbleVisible = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bRigVMCommentColorBubble = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString SlotName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString SectionName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString NextSectionName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString MachineName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString StateName;
    /** Exact persistent GUID returned by StateGraphNode inspection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString StateGraphNodeGuid;
    /** Exact persistent GUID returned by StateGraphPin inspection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString StateGraphPinGuid;
    /** Exact output-pin GUID returned by StateGraphPin inspection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString StateGraphSourcePinGuid;
    /** Exact input-pin GUID returned by StateGraphPin inspection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString StateGraphTargetPinGuid;
    /** Bounded string passed through the Animation graph schema for a pin-default edit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString StateGraphPinDefaultValue;
    /** Exact target size: 0..16 for node/constraint arrays, 2..16 for coupled pose arrays. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 StateGraphContainerSize = 0;
    /**
     * Exact enum object path required when creating BlendListByEnum. Native
     * enums use /Script/... and project enum assets use their exact object path.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString StateGraphEnumPath;
    /**
     * Exact visible enum entry names for BlendListByEnum. There is one entry
     * per non-default pose, so the count is target pose count minus one.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FString> StateGraphEnumEntries;
    /**
     * Exact values parallel to a coupled pose or ConstraintSetup array.
     * BlendListByEnum uses blend times, MultiWayBlend uses desired alphas,
     * and replace_state_graph_constraint_array uses constraint weights.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<float> StateGraphCompanionValues;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString FromStateName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ToStateName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString InputXVariableName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString InputYVariableName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString InputIndexVariableName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString InputAlphaVariableName;
    /** time_remaining or time_remaining_fraction. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString TransitionGetter = TEXT("time_remaining_fraction");
    /** less, less_equal, greater, or greater_equal. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ComparisonOperator = TEXT("less");
    /** and or or, used to combine the Transition Getter comparison with Name. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString LogicalOperator = TEXT("and");
    /** Bounded typed reverse-Polish expression used by set_transition_rule_expression. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasAnimationTransitionToken> TransitionExpressionTokens;
    /** One float alpha variable per Layered Blend per Bone overlay pose. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FString> LayerAlphaVariableNames;
    /** One root bone per Layered Blend per Bone overlay pose. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FString> LayerBoneNames;
    /** One branch-filter depth per Layered Blend per Bone overlay pose. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<int32> LayerBlendDepths;
    /** none, local_space, or mesh_space_rotation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AdditiveType = TEXT("local_space");
    /** none, ref_pose, anim_scaled, anim_frame, or local_anim_frame. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AdditiveBasePoseType = TEXT("ref_pose");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AdditiveBasePoseAssetPath;
    /** ref_pose, anim_first_frame, or zero. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RootMotionLock = TEXT("ref_pose");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float Time = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float Duration = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float Value = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float EndTime = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float PlayRate = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float SecondaryPlayRate = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FString> ReferencedAssetPaths;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<float> PlayRates;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<float> BlendTimes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 Index = INDEX_NONE;
    /** Typed values used by generic Control Rig control mutations. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 ControlIntegerValue = 0;
    /** Destination stack index used by move_ik_solver or move_ik_retarget_op. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 TargetIndex = INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 LoopCount = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PositionX = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PositionY = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PriorityOrder = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 AdditiveBaseFrame = 0;
    /** Limb IK solver precision in Unreal units, bounded to [0.001, 100]. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float IKReachPrecision = 0.01f;
    /** Limb IK solver iteration budget, bounded to [1, 64]. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 IKMaxIterations = 12;
    /** Limb IK minimum joint angle in degrees, bounded to [0, 180]. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float IKMinRotationAngle = 15.0f;
    /** Limb IK pull distribution, bounded to [0, 1]. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float IKPullDistribution = 0.5f;
    /** Limb IK reach step alpha, bounded to [0, 1]. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float IKReachStepAlpha = 0.7f;
    /** x, y, or z. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString IKHingeRotationAxis = TEXT("x");
    /** x, y, or z. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString IKEndBoneForwardAxis = TEXT("x");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bControlBoolValue = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bControlShapeVisible = true;
    /**
     * Optional native-channel flags for set_control_rig_control_limits.
     * Both arrays must be empty (legacy all-channel booleans) or exactly match
     * the inspected channel count.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<bool> ControlMinimumLimitEnabledPerChannel;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<bool> ControlMaximumLimitEnabledPerChannel;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bControlMinimumLimitEnabled = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bControlMaximumLimitEnabled = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bDrawControlLimits = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bIKBoneExcluded = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bIKEnableRotationLimit = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bIKAveragePull = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bIKEnableTwistCorrection = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bForceRootLock = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bAlwaysResetOnEntry = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bAutomaticTransitionRule = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bLoopAnimation = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bRuleValue = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bMeshSpaceRotationBlend = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bMeshSpaceScaleBlend = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bBlendRootMotionBasedOnRootBone = true;
    /** Root pose offsets use the controller-persisted Z axis only; X and Y must be zero. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FVector Location = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FVector2D ControlVector2DValue = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FVector ControlVectorValue = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FRotator Rotation = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FVector Scale = FVector::OneVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FLinearColor Color = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAnimationPatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasAnimationOperation> Operations;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAnimationPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAnimationApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasCinematicKeyRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Frame = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Value;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasCinematicChannelRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Type;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Index = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasCinematicKeyRecord> Keys;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasCinematicSectionRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Index = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Id;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bHasStartFrame = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bHasEndFrame = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 StartFrame = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 EndFrame = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 RowIndex = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PreRollFrames = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PostRollFrames = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bActive = true;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bLocked = false;
    /** Local Sequencer binding referenced by a Camera Cut section, when applicable. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString CameraBindingGuid;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bLockPreviousCamera = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasCinematicChannelRecord> Channels;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasCinematicTrackRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Index = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Id;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString DisplayName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString BindingGuid;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasCinematicSectionRecord> Sections;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasCinematicBindingRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Guid;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    /** possessable, spawnable, or binding. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Kind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString TemplateObjectPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double LocationX = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double LocationY = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double LocationZ = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double RotationPitch = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double RotationYaw = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") double RotationRoll = 0.0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCineCamera = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float CurrentFocalLength = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float CurrentAperture = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float ManualFocusDistance = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 TrackCount = 0;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasCinematicInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDirty = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 DisplayRateNumerator = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 DisplayRateDenominator = 1;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 TickResolutionNumerator = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 TickResolutionDenominator = 1;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PlaybackStartFrame = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PlaybackEndFrame = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString DirectorBlueprintPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString DirectorBlueprintStatus;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasCinematicBindingRecord> Bindings;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasCinematicTrackRecord> Tracks;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasCinematicOperation
{
    GENERATED_BODY()

    /** Typed Sequencer action; structural removals and key deletion require bConfirmDestructive. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ResultId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString BindingGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString BindingName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString BindingClassPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString TrackClassPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 TrackIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 SectionIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString PropertyName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString PropertyPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ChannelType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 ChannelIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 Frame = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Value;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Interpolation = TEXT("cubic");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 Numerator = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 Denominator = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 StartFrame = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 EndFrame = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 RowIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PreRollFrames = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PostRollFrames = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bLocked = false;
    /** Actor template transform used by add_spawnable. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") double LocationX = 0.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") double LocationY = 0.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") double LocationZ = 0.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") double RotationPitch = 0.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") double RotationYaw = 0.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") double RotationRoll = 0.0;
    /** Cine camera template settings used when BindingClassPath derives from ACineCameraActor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float FocalLength = 35.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float Aperture = 2.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float FocusDistance = 1000.0f;
    /** Custom event endpoint name used by add_event. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString EventName;
    /** Camera Cut option used by add_camera_cut. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bLockPreviousCamera = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasCinematicPatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bCreateIfMissing = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasCinematicOperation> Operations;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasCinematicPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasCinematicApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCreated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPaperFlipbookFrameRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Index = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString SpritePath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 FrameRun = 1;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPaper2DInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    /** sprite, flipbook, tile_set, or tile_map. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDirty = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString SourceTexturePath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString SourceUV;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString SourceSize;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float PixelsPerUnrealUnit = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float FramesPerSecond = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 TotalFrames = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasPaperFlipbookFrameRecord> Frames;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPaper2DOperation
{
    GENERATED_BODY()

    /** create_paper_asset, set_flipbook_fps, add_flipbook_frame, or remove_flipbook_frame. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ObjectPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float FramesPerSecond = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 FrameIndex = INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 FrameRun = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bUseSourceRegion = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 SourceX = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 SourceY = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 SourceWidth = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 SourceHeight = 0;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPaper2DPatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bCreateIfMissing = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasPaper2DOperation> Operations;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPaper2DPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasPaper2DApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCreated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasDataRowRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    /** Exact UScriptStruct ExportText representation, capped to 8 KiB. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Value;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasDataAssetInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RowStructPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDirty = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bTruncated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 RowCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasDataRowRecord> Rows;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasBlackboardKeyRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString TypeClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Description;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Category;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bInstanceSynced = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bInherited = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAIStateRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Id;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ParentId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Description;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString StateType;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString SelectionBehavior;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bEnabled = true;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ChildCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 EnterConditionCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 TaskCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 TransitionCount = 0;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAINodeRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Id;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString OwnerId;
    /** state_task, enter_condition, transition_condition, evaluator, or global_task. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Role;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString StructPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString InstanceTypePath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PositionX = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PositionY = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bEnabled = true;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAIGraphEdgeRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString FromId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ToId;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAITransitionRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Id;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString OwnerStateId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString TargetStateId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Trigger;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString TransitionType;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bEnabled = true;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ConditionCount = 0;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasAIAssetInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    /** blackboard, behavior_tree, eqs, or state_tree. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ParentBlackboardPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString BlackboardPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RootNodeClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDirty = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 GraphNodeCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OptionCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasBlackboardKeyRecord> BlackboardKeys;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasAIStateRecord> States;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasAINodeRecord> Nodes;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasAITransitionRecord> Transitions;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasAIGraphEdgeRecord> Edges;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasDataAIOperation
{
    GENERATED_BODY()

    /**
     * create_data_asset, create_data_table, set_table_row, remove_table_row,
     * create_ai_asset, set_behavior_tree_blackboard, set_blackboard_parent,
     * add_blackboard_key, set_blackboard_key_property, remove_blackboard_key,
     * add_state, remove_state, rename_state, set_state_description,
     * set_state_enabled, set_state_selection_behavior, add_state_task,
     * add_enter_condition, add_global_task, add_evaluator,
     * add_transition_condition, remove_state_tree_node, set_state_tree_node_property,
     * add_state_transition, remove_state_transition, add_behavior_tree_node,
     * add_behavior_tree_aux_node, connect_ai_graph_nodes,
     * disconnect_ai_graph_nodes, add_eqs_option, add_eqs_test,
     * set_ai_graph_node_property, set_eqs_test_enabled, or remove_ai_graph_node.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString RowStructPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ObjectPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Value;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString KeyTypeClassPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString KeyTypePropertyName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString KeyTypePropertyValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Category;
    /** Existing GUID or an alias produced by ResultId. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString StateId;
    /** Existing parent state GUID or an alias produced by ResultId; empty means top level. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ParentId;
    /** Existing transition target state GUID or an alias produced by ResultId. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString TargetStateId;
    /** Existing StateTree node or transition GUID. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ItemId;
    /** Batch-local alias assigned to the newly created state, node, or transition. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ResultId;
    /** composite, task, decorator, service, generator, or test. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString NodeRole;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString SourceId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString TargetId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PositionX = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PositionY = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString StateType = TEXT("State");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString SelectionBehavior;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString TransitionTrigger = TEXT("OnStateCompleted");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString TransitionType = TEXT("GotoState");
    /** node or instance; used by set_state_tree_node_property. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString PropertyScope = TEXT("instance");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bInstanceSynced = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasDataAIPatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    /** data_asset, data_table, blackboard, behavior_tree, eqs, or state_tree. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bCreateIfMissing = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasDataAIOperation> Operations;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasDataAIPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasDataAIApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCreated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasValidationMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Severity;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ObjectPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasValidationResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Scope;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 RequestedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 CheckedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 ValidCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 InvalidCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 WarningCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 SkippedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bTruncated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasValidationMessage> Messages;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasBlueprintVariableRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Type;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString DefaultValue;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bInstanceEditable = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bExposeOnSpawn = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasBlueprintPinRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Direction;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Type;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString DefaultValue;
    /** Stable targets encoded as NodeGuid:Direction:PinName. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> LinkedTo;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasBlueprintNodeRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString GraphName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString NodeGuid;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Title;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PositionX = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PositionY = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasBlueprintPinRecord> Pins;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWidgetRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ParentName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString SlotClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bVariable = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWidgetAnimationBindingRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Guid;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString WidgetName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 TrackCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRootWidget = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWidgetAnimationKeyRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString WidgetName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PropertyName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 Frame = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") float Value = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Interpolation;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWidgetAnimationRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString DisplayLabel;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 DisplayRateNumerator = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 DisplayRateDenominator = 1;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PlaybackStartFrame = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 PlaybackEndFrame = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasWidgetAnimationBindingRecord> Bindings;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasWidgetAnimationKeyRecord> Keys;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWidgetFunctionBindingRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString WidgetName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PropertyName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString FunctionName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString SourcePropertyName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Kind;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasBlueprintComponentRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ParentName;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString TemplatePath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSceneComponent = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bInherited = false;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasBlueprintInspectionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ParentClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString GeneratedClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString CompileStatus;
    /** normal, widget, interface, editor_utility, or editor_utility_widget. */
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bWidgetBlueprint = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bDirty = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bTruncated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Interfaces;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Components;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasBlueprintComponentRecord> ComponentDetails;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasBlueprintVariableRecord> Variables;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasBlueprintNodeRecord> Nodes;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasWidgetRecord> Widgets;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasWidgetAnimationRecord> WidgetAnimations;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FThomasWidgetFunctionBindingRecord> WidgetBindings;
};

/** One guarded variable/default/graph operation in a Blueprint batch. */
USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasBlueprintOperation
{
    GENERATED_BODY()

    /**
     * add/remove/rename_variable, add/remove_interface, add/remove_component,
     * set_component_property, set_cdo_property, add/remove_function_graph,
     * add_custom_event, add_call_function, add_variable_get, add_variable_set,
     * add_branch, set_pin_default, connect_pins, disconnect_pins, remove_node,
     * add/remove_widget_animation, set_widget_animation_opacity_key, or
     * add/remove_widget_function_binding, add_widget_property_binding, or
     * remove_widget_binding.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Action;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString NewName;
    /** bool/int/int64/float/double/string/name/text or object/class/interface/softobject/softclass/struct/enum:<path>. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Type;
    /** none, array, set, or map. For map, Type is the value type and KeyType is the key type. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ContainerType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString KeyType;
    /** ImportText-compatible value or variable default. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Value;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString PropertyName;
    /** Parent SCS component for add_component. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ParentName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bInstanceEditable = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bExposeOnSpawn = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bPreserveFunctions = false;
    /** Local identifier for a node created earlier in this batch. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Handle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString GraphName;
    /** Existing node reference; mutually exclusive with Handle when resolving a node. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString NodeGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString PinName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString PinDirection;
    /** Second endpoint for connect_pins/disconnect_pins. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString OtherHandle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString OtherNodeGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString OtherPinName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString OtherPinDirection;
    /** Declaring class for add_call_function; empty means the Blueprint generated/skeleton class. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString FunctionName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString SourcePropertyName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AnimationName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString WidgetName;
    /** constant, linear, or cubic for Widget animation keys. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Interpolation = TEXT("cubic");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 Frame = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 StartFrame = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 EndFrame = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 DisplayRateNumerator = 60;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 DisplayRateDenominator = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PositionX = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 PositionY = 0;
};

/** Declarative Widget Tree element. Parents must appear before children. */
USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasWidgetElementSpec
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ClassPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ParentName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bVariable = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Text;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString Color = TEXT("1,1,1,1");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float AnchorMinX = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float AnchorMinY = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float AnchorMaxX = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float AnchorMaxY = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float AlignmentX = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float AlignmentY = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float PositionX = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float PositionY = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float SizeX = 100.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float SizeY = 30.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") float Padding = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") int32 ZOrder = 0;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasBlueprintBatchRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString ParentClassPath;
    /** normal, widget, interface, editor_utility, or editor_utility_widget. Empty preserves the legacy bWidgetBlueprint field. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") FString AssetKind;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bCreateIfMissing = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bWidgetBlueprint = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bReplaceWidgetTree = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") bool bConfirmDestructive = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasBlueprintOperation> Operations;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThomasEditor") TArray<FThomasWidgetElementSpec> WidgetElements;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasBlueprintPlanResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString PlanId;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString ExpectedRevision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 OperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Risk = TEXT("R1");
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Preview;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasBlueprintApplyResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionBefore;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString RevisionAfter;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCreated = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bCompiled = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bRolledBack = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") int32 AppliedOperationCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};

USTRUCT(BlueprintType)
struct THOMASEDITORCORE_API FThomasBlueprintValidationResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") bool bOk = false;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Code;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Message;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString AssetPath;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString Revision;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") FString CompileStatus;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> MissingWidgets;
    UPROPERTY(BlueprintReadOnly, Category = "ThomasEditor") TArray<FString> Diagnostics;
};
