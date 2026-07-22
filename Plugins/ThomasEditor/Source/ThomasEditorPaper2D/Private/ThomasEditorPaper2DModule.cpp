#include "ThomasEditorPaper2DProvider.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookFactory.h"
#include "PaperSprite.h"
#include "PaperSpriteFactory.h"
#include "PaperTileMap.h"
#include "PaperTileMapFactory.h"
#include "PaperTileSet.h"
#include "PaperTileSetFactory.h"
#include "ScopedTransaction.h"
#include "ThomasEditorAssetsProvider.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr int32 MaxOperations = 100;
constexpr int32 MaxFrames = 500;
constexpr double PlanLifetimeSeconds = 300.0;

struct FCachedPlan
{
    FThomasPaper2DPatchRequest Request;
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

UObject* LoadProjectAsset(const FString& InputPath)
{
    const FString PackageName = NormalizePath(InputPath);
    if (!IsAllowedPath(PackageName))
    {
        return nullptr;
    }
    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData AssetData = Registry.GetAssetByObjectPath(
        FSoftObjectPath(ObjectPath(PackageName)));
    return AssetData.IsValid() ? AssetData.GetAsset() : nullptr;
}

FString AssetKind(const UObject* Asset)
{
    if (Asset && Asset->IsA<UPaperSprite>()) return TEXT("sprite");
    if (Asset && Asset->IsA<UPaperFlipbook>()) return TEXT("flipbook");
    if (Asset && Asset->IsA<UPaperTileSet>()) return TEXT("tile_set");
    if (Asset && Asset->IsA<UPaperTileMap>()) return TEXT("tile_map");
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
    const int32 StructuralCount = Asset->IsA<UPaperFlipbook>()
        ? CastChecked<UPaperFlipbook>(Asset)->GetNumKeyFrames() : 0;
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d:%d"),
        *Package->GetName(),
        IFileManager::Get().FileSize(*Filename),
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package->IsDirty() ? 1 : 0,
        StructuralCount);
}

UFactory* CreateFactory(
    const FThomasPaper2DPatchRequest& Request,
    const FThomasPaper2DOperation& Operation,
    FString& OutError)
{
    if (Request.AssetKind == TEXT("sprite"))
    {
        UPaperSpriteFactory* Factory = NewObject<UPaperSpriteFactory>();
        if (!Operation.ObjectPath.IsEmpty())
        {
            Factory->InitialTexture = Cast<UTexture2D>(LoadProjectAsset(Operation.ObjectPath));
            if (!Factory->InitialTexture)
            {
                OutError = TEXT("Sprite source texture is invalid or outside /Game/PropHunt.");
                return nullptr;
            }
        }
        Factory->bUseSourceRegion = Operation.bUseSourceRegion;
        Factory->InitialSourceUV = FIntPoint(Operation.SourceX, Operation.SourceY);
        Factory->InitialSourceDimension = FIntPoint(
            Operation.SourceWidth, Operation.SourceHeight);
        if (Operation.bUseSourceRegion
            && (!Factory->InitialTexture
                || Operation.SourceWidth <= 0 || Operation.SourceHeight <= 0))
        {
            OutError = TEXT("Sprite source region requires a texture and positive size.");
            return nullptr;
        }
        return Factory;
    }
    if (Request.AssetKind == TEXT("flipbook"))
        return NewObject<UPaperFlipbookFactory>();
    if (Request.AssetKind == TEXT("tile_set"))
        return NewObject<UPaperTileSetFactory>();
    if (Request.AssetKind == TEXT("tile_map"))
        return NewObject<UPaperTileMapFactory>();
    OutError = TEXT("Unsupported Paper2D asset kind.");
    return nullptr;
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

class FThomasEditorPaper2DModule final : public IThomasEditorPaper2DProviderModule
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
            TEXT("Paper2D"),
            PackageRoot,
            {
                TEXT("/Script/Paper2D.PaperSprite"),
                TEXT("/Script/Paper2D.PaperFlipbook"),
                TEXT("/Script/Paper2D.PaperTileSet"),
                TEXT("/Script/Paper2D.PaperTileMap"),
                TEXT("/Script/Paper2D.PaperSpriteAtlas")
            },
            MaxResults);
    }

    virtual FThomasPaper2DInspectionResult InspectPaper2DAsset(
        const FString& AssetPath,
        const int32 MaxFrameCount) override
    {
        const FString PackageName = NormalizePath(AssetPath);
        if (!IsAllowedPath(PackageName))
        {
            return MakeError<FThomasPaper2DInspectionResult>(
                TEXT("path_denied"), TEXT("Paper2D asset must remain under /Game/PropHunt."));
        }
        if (MaxFrameCount < 1 || MaxFrameCount > MaxFrames)
        {
            return MakeError<FThomasPaper2DInspectionResult>(
                TEXT("invalid_limit"), TEXT("MaxFrames must be between 1 and 500."));
        }
        UObject* Asset = LoadProjectAsset(PackageName);
        const FString Kind = AssetKind(Asset);
        if (!Asset || Kind.IsEmpty())
        {
            return MakeError<FThomasPaper2DInspectionResult>(
                TEXT("paper_asset_not_found"), PackageName);
        }
        FThomasPaper2DInspectionResult Result;
        Result.bOk = true;
        Result.AssetPath = PackageName;
        Result.AssetKind = Kind;
        Result.ClassPath = Asset->GetClass()->GetPathName();
        Result.Revision = BuildRevision(Asset);
        Result.bDirty = Asset->GetOutermost()->IsDirty();
        if (const UPaperSprite* Sprite = Cast<UPaperSprite>(Asset))
        {
            Result.SourceTexturePath = Sprite->GetSourceTexture()
                ? Sprite->GetSourceTexture()->GetPathName() : FString();
            Result.SourceUV = Sprite->GetSourceUV().ToString();
            Result.SourceSize = Sprite->GetSourceSize().ToString();
            Result.PixelsPerUnrealUnit = Sprite->GetPixelsPerUnrealUnit();
        }
        else if (const UPaperFlipbook* Flipbook = Cast<UPaperFlipbook>(Asset))
        {
            Result.FramesPerSecond = Flipbook->GetFramesPerSecond();
            Result.TotalFrames = Flipbook->GetNumFrames();
            for (int32 Index = 0;
                 Index < Flipbook->GetNumKeyFrames() && Result.Frames.Num() < MaxFrameCount;
                 ++Index)
            {
                const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(Index);
                FThomasPaperFlipbookFrameRecord Record;
                Record.Index = Index;
                Record.SpritePath = KeyFrame.Sprite
                    ? KeyFrame.Sprite->GetPathName() : FString();
                Record.FrameRun = KeyFrame.FrameRun;
                Result.Frames.Add(MoveTemp(Record));
            }
        }
        return Result;
    }

    virtual FThomasPaper2DPlanResult PlanPaper2DPatch(
        const FThomasPaper2DPatchRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasPaper2DPatchRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        Request.AssetKind = Request.AssetKind.TrimStartAndEnd().ToLower();
        if (!IsAllowedPath(Request.AssetPath))
        {
            return MakeError<FThomasPaper2DPlanResult>(
                TEXT("path_denied"), TEXT("Paper2D asset must remain under /Game/PropHunt."));
        }
        if (Request.Operations.IsEmpty() || Request.Operations.Num() > MaxOperations)
        {
            return MakeError<FThomasPaper2DPlanResult>(
                TEXT("invalid_batch_size"), TEXT("Use 1 to 100 Paper2D operations."));
        }
        const TSet<FString> SupportedKinds = {
            TEXT("sprite"), TEXT("flipbook"), TEXT("tile_set"), TEXT("tile_map")};
        if (!SupportedKinds.Contains(Request.AssetKind))
        {
            return MakeError<FThomasPaper2DPlanResult>(
                TEXT("invalid_asset_kind"), Request.AssetKind);
        }
        UObject* Existing = LoadProjectAsset(Request.AssetPath);
        const FString Revision = BuildRevision(Existing);
        if (Existing)
        {
            if (Request.bCreateIfMissing || Request.ExpectedRevision != Revision
                || AssetKind(Existing) != Request.AssetKind)
            {
                return MakeError<FThomasPaper2DPlanResult>(
                    Request.bCreateIfMissing ? TEXT("asset_exists") : TEXT("revision_conflict"),
                    Revision);
            }
            if (Existing->GetOutermost()->IsDirty())
            {
                return MakeError<FThomasPaper2DPlanResult>(
                    TEXT("asset_dirty"), TEXT("Save or revert the Paper2D asset first."));
            }
        }
        else
        {
            if (!Request.bCreateIfMissing
                || (Request.ExpectedRevision != TEXT("missing")
                    && !Request.ExpectedRevision.IsEmpty())
                || Request.Operations[0].Action.ToLower() != TEXT("create_paper_asset"))
            {
                return MakeError<FThomasPaper2DPlanResult>(
                    TEXT("create_operation_required"), TEXT("create_paper_asset must be first."));
            }
            FString FactoryError;
            if (!CreateFactory(Request, Request.Operations[0], FactoryError))
            {
                return MakeError<FThomasPaper2DPlanResult>(
                    TEXT("paper_factory_unavailable"), FactoryError);
            }
            Request.ExpectedRevision = TEXT("missing");
        }

        int32 SimulatedFrames = Existing && Existing->IsA<UPaperFlipbook>()
            ? CastChecked<UPaperFlipbook>(Existing)->GetNumKeyFrames() : 0;
        FThomasPaper2DPlanResult Result;
        Result.AssetPath = Request.AssetPath;
        Result.ExpectedRevision = Request.ExpectedRevision;
        Result.AssetKind = Request.AssetKind;
        for (int32 Index = 0; Index < Request.Operations.Num(); ++Index)
        {
            const FThomasPaper2DOperation& Operation = Request.Operations[Index];
            const FString Action = Operation.Action.ToLower();
            if (Action == TEXT("create_paper_asset"))
            {
                if (Existing || Index != 0)
                {
                    return MakeError<FThomasPaper2DPlanResult>(
                        TEXT("invalid_create_operation"), Operation.Action);
                }
            }
            else if (Action == TEXT("set_flipbook_fps"))
            {
                if (Request.AssetKind != TEXT("flipbook")
                    || Operation.FramesPerSecond < 0.0f
                    || Operation.FramesPerSecond > 1000.0f)
                {
                    return MakeError<FThomasPaper2DPlanResult>(
                        TEXT("invalid_flipbook_fps"), FString::SanitizeFloat(Operation.FramesPerSecond));
                }
            }
            else if (Action == TEXT("add_flipbook_frame"))
            {
                if (Request.AssetKind != TEXT("flipbook")
                    || Operation.FrameRun < 1
                    || !Cast<UPaperSprite>(LoadProjectAsset(Operation.ObjectPath)))
                {
                    return MakeError<FThomasPaper2DPlanResult>(
                        TEXT("invalid_flipbook_frame"), Operation.ObjectPath);
                }
                ++SimulatedFrames;
            }
            else if (Action == TEXT("remove_flipbook_frame"))
            {
                if (Request.AssetKind != TEXT("flipbook")
                    || !Request.bConfirmDestructive
                    || Operation.FrameIndex < 0 || Operation.FrameIndex >= SimulatedFrames)
                {
                    return MakeError<FThomasPaper2DPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("frame_not_found") : TEXT("confirmation_required"),
                        FString::FromInt(Operation.FrameIndex));
                }
                --SimulatedFrames;
                Result.Risk = TEXT("R2");
            }
            else
            {
                return MakeError<FThomasPaper2DPlanResult>(
                    TEXT("unsupported_operation"), Operation.Action);
            }
            Result.Preview.Add(Operation.Action + TEXT(":") + Operation.ObjectPath);
        }
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.OperationCount = Request.Operations.Num();
        FCachedPlan& Plan = Plans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasPaper2DApplyResult ApplyPaper2DPlan(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FCachedPlan Plan;
        if (!Plans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasPaper2DApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        UObject* Asset = LoadProjectAsset(Plan.Request.AssetPath);
        const FString RevisionBefore = BuildRevision(Asset);
        if (RevisionBefore != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasPaper2DApplyResult>(
                TEXT("revision_conflict"), RevisionBefore);
        }
        FThomasPaper2DApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.RevisionBefore = RevisionBefore;
        bool bCreated = false;
        if (!Asset)
        {
            FString FactoryError;
            UFactory* Factory = CreateFactory(
                Plan.Request, Plan.Request.Operations[0], FactoryError);
            UPackage* Package = CreatePackage(*Plan.Request.AssetPath);
            Asset = Factory ? Factory->FactoryCreateNew(
                Factory->SupportedClass,
                Package,
                FName(*FPackageName::GetLongPackageAssetName(Plan.Request.AssetPath)),
                RF_Public | RF_Standalone | RF_Transactional,
                nullptr,
                GWarn) : nullptr;
            if (!Asset)
            {
                return MakeError<FThomasPaper2DApplyResult>(
                    TEXT("create_failed"), FactoryError);
            }
            FAssetRegistryModule::AssetCreated(Asset);
            Asset->GetOutermost()->MarkPackageDirty();
            bCreated = true;
            Result.bCreated = true;
        }
        UPackage* Package = Asset->GetOutermost();
        const FScopedTransaction Transaction(
            NSLOCTEXT("ThomasEditor", "Paper2DPatch", "ThomasEditor Paper2D Patch"));
        Asset->Modify();
        Package->Modify();
        auto Rollback = [&]()
        {
            if (bCreated)
            {
                Result.bRolledBack = ObjectTools::DeleteObjectsUnchecked({Asset}) == 1;
            }
            else
            {
                Result.bRolledBack = UPackageTools::ReloadPackages({Package});
            }
        };

        for (const FThomasPaper2DOperation& Operation : Plan.Request.Operations)
        {
            const FString Action = Operation.Action.ToLower();
            bool bApplied = true;
            if (Action == TEXT("create_paper_asset"))
            {
            }
            else if (Action == TEXT("set_flipbook_fps"))
            {
                UPaperFlipbook* Flipbook = Cast<UPaperFlipbook>(Asset);
                if (Flipbook)
                {
                    FScopedFlipbookMutator Mutator(Flipbook);
                    Mutator.FramesPerSecond = Operation.FramesPerSecond;
                }
                bApplied = Flipbook != nullptr;
            }
            else if (Action == TEXT("add_flipbook_frame"))
            {
                UPaperFlipbook* Flipbook = Cast<UPaperFlipbook>(Asset);
                UPaperSprite* Sprite = Cast<UPaperSprite>(
                    LoadProjectAsset(Operation.ObjectPath));
                if (Flipbook && Sprite)
                {
                    FPaperFlipbookKeyFrame KeyFrame;
                    KeyFrame.Sprite = Sprite;
                    KeyFrame.FrameRun = Operation.FrameRun;
                    FScopedFlipbookMutator Mutator(Flipbook);
                    Mutator.KeyFrames.Add(MoveTemp(KeyFrame));
                }
                bApplied = Flipbook && Sprite;
            }
            else if (Action == TEXT("remove_flipbook_frame"))
            {
                UPaperFlipbook* Flipbook = Cast<UPaperFlipbook>(Asset);
                if (Flipbook && Flipbook->IsValidKeyFrameIndex(Operation.FrameIndex))
                {
                    FScopedFlipbookMutator Mutator(Flipbook);
                    Mutator.KeyFrames.RemoveAt(Operation.FrameIndex);
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
        Asset->PostEditChange();
        Package->MarkPackageDirty();
        if (bSave && !SaveAsset(Asset))
        {
            Rollback();
            Result.Code = TEXT("save_failed_rolled_back");
            return Result;
        }
        Result.bSaved = bSave;
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
            {
                It.RemoveCurrent();
            }
        }
    }

    TMap<FString, FCachedPlan> Plans;
};

IMPLEMENT_MODULE(FThomasEditorPaper2DModule, ThomasEditorPaper2D)
