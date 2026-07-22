#include "ThomasEditorRenderProvider.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "ScopedTransaction.h"
#include "StaticMeshEditorSubsystem.h"
#include "StaticMeshResources.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr double PlanLifetimeSeconds = 300.0;

struct FNanitePlan
{
    FString AssetPath;
    FString ExpectedRevision;
    bool bEnabled = false;
    double CreatedAtSeconds = 0.0;
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

FString NormalizeObjectPath(const FString& Input)
{
    FString Path = Input.TrimStartAndEnd();
    if (!Path.Contains(TEXT(".")))
    {
        Path += TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
    }
    return Path;
}

bool IsAllowedPath(const FString& ObjectPath)
{
    const FString Package = FPackageName::ObjectPathToPackageName(ObjectPath);
    return Package.StartsWith(TEXT("/Game/PropHunt/"))
        && !Package.StartsWith(TEXT("/Game/Developers/"))
        && !Package.Contains(TEXT(".."));
}

UStaticMesh* LoadStaticMesh(const FString& Input)
{
    const FString ObjectPath = NormalizeObjectPath(Input);
    if (!IsAllowedPath(ObjectPath))
    {
        return nullptr;
    }
    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
    return Asset.IsValid() ? Cast<UStaticMesh>(Asset.GetAsset()) : nullptr;
}

FString MeshRevision(const UStaticMesh* Mesh)
{
    if (!Mesh)
    {
        return FString();
    }
    const UPackage* Package = Mesh->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d:%d"),
        *Package->GetName(),
        IFileManager::Get().FileSize(*Filename),
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package->IsDirty() ? 1 : 0,
        Mesh->IsNaniteEnabled() ? 1 : 0);
}

FString MeshFilename(const UStaticMesh* Mesh)
{
    return Mesh
        ? FPackageName::LongPackageNameToFilename(
            Mesh->GetOutermost()->GetName(),
            FPackageName::GetAssetPackageExtension())
        : FString();
}
}

class FThomasEditorRenderModule final : public IThomasEditorRenderProviderModule
{
public:
    virtual FThomasStaticMeshInspectionResult InspectStaticMesh(
        const FString& AssetPath) override
    {
        const FString ObjectPath = NormalizeObjectPath(AssetPath);
        if (!IsAllowedPath(ObjectPath))
        {
            return MakeError<FThomasStaticMeshInspectionResult>(
                TEXT("path_denied"), TEXT("Mesh must remain under /Game/PropHunt."));
        }
        UStaticMesh* Mesh = LoadStaticMesh(ObjectPath);
        if (!Mesh)
        {
            return MakeError<FThomasStaticMeshInspectionResult>(
                TEXT("static_mesh_not_found"), ObjectPath);
        }

        FThomasStaticMeshInspectionResult Result;
        Result.bOk = true;
        Result.AssetPath = Mesh->GetPathName();
        Result.Revision = MeshRevision(Mesh);
        Result.bDirty = Mesh->GetOutermost()->IsDirty();
        Result.bReadOnly = IFileManager::Get().IsReadOnly(*MeshFilename(Mesh));
        Result.bNaniteEnabled = Mesh->IsNaniteEnabled();
        Result.bNaniteDataValid = Mesh->HasValidNaniteData();
        Result.bAllowCpuAccess = Mesh->bAllowCPUAccess;
        Result.LodCount = Mesh->GetNumLODs();
        Result.MaterialSlotCount = Mesh->GetStaticMaterials().Num();
        Result.LightMapCoordinateIndex = Mesh->GetLightMapCoordinateIndex();
        if (Result.LodCount > 0)
        {
            Result.LOD0VertexCount = Mesh->GetNumVertices(0);
            if (const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
                RenderData && !RenderData->LODResources.IsEmpty())
            {
                Result.LOD0TriangleCount = RenderData->LODResources[0].GetNumTriangles();
            }
        }
        if (const UBodySetup* Body = Mesh->GetBodySetup())
        {
            Result.CollisionPrimitiveCount = Body->AggGeom.GetElementCount();
        }
        for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
        {
            Result.Materials.Add(FString::Printf(
                TEXT("%s|%s"),
                *Slot.MaterialSlotName.ToString(),
                Slot.MaterialInterface
                    ? *Slot.MaterialInterface->GetPathName()
                    : TEXT("None")));
        }
        if (Result.bNaniteEnabled && !Result.bNaniteDataValid)
        {
            Result.Diagnostics.Add(TEXT("Nanite is enabled but valid Nanite render data is unavailable."));
        }
        if (Result.LOD0TriangleCount < 1000 && Result.bNaniteEnabled)
        {
            Result.Diagnostics.Add(TEXT("Nanite is enabled on a very small mesh; measure whether it is beneficial."));
        }
        if (Result.CollisionPrimitiveCount == 0)
        {
            Result.Diagnostics.Add(TEXT("No simple collision primitive is authored."));
        }
        return Result;
    }

    virtual FThomasNanitePlanResult PlanNanite(
        const FString& AssetPath,
        const FString& ExpectedRevision,
        const bool bEnabled) override
    {
        UStaticMesh* Mesh = LoadStaticMesh(AssetPath);
        if (!Mesh)
        {
            return MakeError<FThomasNanitePlanResult>(
                TEXT("static_mesh_not_found"), NormalizeObjectPath(AssetPath));
        }
        if (Mesh->GetOutermost()->IsDirty())
        {
            return MakeError<FThomasNanitePlanResult>(
                TEXT("dirty_asset"), TEXT("Save or revert existing human changes first."));
        }
        if (IFileManager::Get().IsReadOnly(*MeshFilename(Mesh)))
        {
            return MakeError<FThomasNanitePlanResult>(
                TEXT("read_only_asset"), MeshFilename(Mesh));
        }
        const FString Revision = MeshRevision(Mesh);
        if (ExpectedRevision.IsEmpty() || ExpectedRevision != Revision)
        {
            return MakeError<FThomasNanitePlanResult>(
                TEXT("revision_conflict"), Revision);
        }
        if (Mesh->IsNaniteEnabled() == bEnabled)
        {
            return MakeError<FThomasNanitePlanResult>(
                TEXT("no_change"), bEnabled ? TEXT("Nanite is already enabled.") : TEXT("Nanite is already disabled."));
        }

        FThomasNanitePlanResult Result;
        Result.bOk = true;
        Result.AssetPath = Mesh->GetPathName();
        Result.ExpectedRevision = Revision;
        Result.bCurrentEnabled = Mesh->IsNaniteEnabled();
        Result.bDesiredEnabled = bEnabled;
        Result.Warnings.Add(TEXT("Applying this plan rebuilds the static mesh render data."));
        Result.Warnings.Add(TEXT("Nanite suitability still depends on mesh size, material and platform budgets."));
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);

        FNanitePlan Plan;
        Plan.AssetPath = Mesh->GetPathName();
        Plan.ExpectedRevision = Revision;
        Plan.bEnabled = bEnabled;
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        Plans.Add(Result.PlanId, MoveTemp(Plan));
        return Result;
    }

    virtual FThomasNaniteApplyResult ApplyNanitePlan(
        const FString& PlanId,
        const bool bSave) override
    {
        FNanitePlan Plan;
        if (FNanitePlan* Found = Plans.Find(PlanId))
        {
            Plan = MoveTemp(*Found);
            Plans.Remove(PlanId);
        }
        else
        {
            return MakeError<FThomasNaniteApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        if (FPlatformTime::Seconds() - Plan.CreatedAtSeconds > PlanLifetimeSeconds)
        {
            return MakeError<FThomasNaniteApplyResult>(
                TEXT("plan_expired"), TEXT("Create a fresh plan."));
        }

        UStaticMesh* Mesh = LoadStaticMesh(Plan.AssetPath);
        if (!Mesh
            || Mesh->GetOutermost()->IsDirty()
            || MeshRevision(Mesh) != Plan.ExpectedRevision)
        {
            return MakeError<FThomasNaniteApplyResult>(
                TEXT("revision_conflict"), Mesh ? MeshRevision(Mesh) : Plan.AssetPath);
        }

        FThomasNaniteApplyResult Result;
        Result.AssetPath = Mesh->GetPathName();
        Result.RevisionBefore = MeshRevision(Mesh);
        const FMeshNaniteSettings PreviousSettings = Mesh->GetNaniteSettings();

        FScopedTransaction Transaction(
            NSLOCTEXT("ThomasEditor", "NaniteChange", "ThomasEditor Nanite Change"));
        UStaticMeshEditorSubsystem* Subsystem =
            GEditor ? GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>() : nullptr;
        if (!Subsystem)
        {
            Transaction.Cancel();
            return MakeError<FThomasNaniteApplyResult>(
                TEXT("static_mesh_editor_unavailable"), TEXT("StaticMeshEditorSubsystem is unavailable."));
        }

        FMeshNaniteSettings NewSettings = PreviousSettings;
        NewSettings.bEnabled = Plan.bEnabled;
        Subsystem->SetNaniteSettings(Mesh, NewSettings, true);
        if (Mesh->IsNaniteEnabled() != Plan.bEnabled)
        {
            Subsystem->SetNaniteSettings(Mesh, PreviousSettings, true);
            Mesh->GetOutermost()->SetDirtyFlag(false);
            Transaction.Cancel();
            Result.Code = TEXT("post_validation_failed");
            Result.Message = TEXT("Nanite state did not match the plan; previous settings were restored.");
            Result.bRolledBack = true;
            Result.RevisionAfter = MeshRevision(Mesh);
            return Result;
        }

        if (bSave)
        {
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            SaveArgs.SaveFlags = SAVE_NoError;
            SaveArgs.Error = GError;
            if (!UPackage::SavePackage(
                    Mesh->GetOutermost(), Mesh, *MeshFilename(Mesh), SaveArgs))
            {
                Subsystem->SetNaniteSettings(Mesh, PreviousSettings, true);
                Mesh->GetOutermost()->SetDirtyFlag(false);
                Transaction.Cancel();
                Result.Code = TEXT("save_failed_rolled_back");
                Result.Message = TEXT("Previous Nanite settings were restored in memory.");
                Result.bRolledBack = true;
                Result.RevisionAfter = MeshRevision(Mesh);
                return Result;
            }
            Result.bSaved = true;
        }

        Result.bOk = true;
        Result.bNaniteEnabled = Mesh->IsNaniteEnabled();
        Result.RevisionAfter = MeshRevision(Mesh);
        if (Result.bNaniteEnabled && !Mesh->HasValidNaniteData())
        {
            Result.Diagnostics.Add(TEXT("Nanite render data is not yet reported as valid after rebuild."));
        }
        return Result;
    }

private:
    TMap<FString, FNanitePlan> Plans;
};

IMPLEMENT_MODULE(FThomasEditorRenderModule, ThomasEditorRender)
