#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "Toolsets/ThomasWorldToolset.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FThomasEditorEnvironmentBuilderSmokeTest,
    "PropHunt.Environment.ThomasEditorBuilderSmoke",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FThomasEditorEnvironmentBuilderSmokeTest::RunTest(const FString& Parameters)
{
    const FThomasWorldInspectionResult OriginalInspection =
        UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
    TestTrue(TEXT("Builder smoke starts from an inspectable project map"), OriginalInspection.bOk);
    TestFalse(TEXT("Builder smoke starts from a clean project map"), OriginalInspection.bDirty);
    if (!OriginalInspection.bOk || OriginalInspection.bDirty)
    {
        return false;
    }

    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FAssetData> StaleAssets;
    Registry.GetAssetsByPath(
        FName(TEXT("/Game/PropHunt/Tests/ThomasEditor")), StaleAssets, false, false);
    TArray<UObject*> StaleWorlds;
    for (const FAssetData& AssetData : StaleAssets)
    {
        if (AssetData.AssetName.ToString().StartsWith(TEXT("L_TE_WPBuilder_")))
        {
            if (UObject* Asset = AssetData.GetAsset())
            {
                StaleWorlds.Add(Asset);
            }
        }
    }
    if (!StaleWorlds.IsEmpty())
    {
        TestTrue(
            TEXT("Interrupted builder-smoke maps cleaned through Unreal API"),
            ObjectTools::DeleteObjectsUnchecked(StaleWorlds) >= StaleWorlds.Num());
    }

    const FString TestMapPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/L_TE_WPBuilder_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FThomasMapLifecycleRequest CreateRequest;
    CreateRequest.Action = TEXT("create_world_partition_map");
    CreateRequest.CurrentMapPath = OriginalInspection.MapPath;
    CreateRequest.ExpectedRevision = OriginalInspection.Revision;
    CreateRequest.TargetMapPath = TestMapPath;
    CreateRequest.bConfirmWorldSwitch = true;
    const FThomasMapLifecyclePlanResult CreatePlan =
        UThomasWorldToolset::PlanMapLifecycle(CreateRequest);
    TestTrue(TEXT("Disposable World Partition map plan succeeds"), CreatePlan.bOk);
    const FThomasMapLifecycleApplyResult CreateApply = CreatePlan.bOk
        ? UThomasWorldToolset::ApplyMapLifecyclePlan(CreatePlan.PlanId)
        : FThomasMapLifecycleApplyResult();
    TestTrue(TEXT("Disposable World Partition map create/save/open succeeds"), CreateApply.bOk);

    const FThomasEnvironmentInspectionResult CreatedEnvironment =
        UThomasWorldToolset::InspectEnvironment(50);
    const FThomasWorldInspectionResult CreatedWorld =
        UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
    TestTrue(TEXT("Disposable environment is inspectable"), CreatedEnvironment.bOk);
    TestTrue(TEXT("Disposable environment is World Partition"), CreatedEnvironment.bWorldPartition);
    TestFalse(TEXT("Disposable environment is clean after save"), CreatedWorld.bDirty);

    auto RunBuilder = [this, &TestMapPath](
        const FString& Action,
        const bool bConfirmDestructive) -> bool
    {
        const FThomasEnvironmentInspectionResult Before =
            UThomasWorldToolset::InspectEnvironment(50);
        const FThomasWorldInspectionResult BeforeWorld =
            UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
        TestTrue(*FString::Printf(TEXT("%s preflight inspection succeeds"), *Action), Before.bOk);
        if (!Before.bOk || !BeforeWorld.bOk
            || Before.MapPath != TestMapPath || BeforeWorld.bDirty)
        {
            return false;
        }

        FThomasEnvironmentBuildRequest Request;
        Request.Action = Action;
        Request.MapPath = Before.MapPath;
        Request.ExpectedRevision = Before.Revision;
        Request.bConfirmWorldSwitch = true;
        Request.bConfirmDestructive = bConfirmDestructive;
        const FThomasEnvironmentBuildPlanResult Plan =
            UThomasWorldToolset::PlanEnvironmentBuild(Request);
        TestTrue(*FString::Printf(TEXT("%s builder plan succeeds"), *Action), Plan.bOk);
        if (!Plan.bOk)
        {
            return false;
        }

        FThomasEnvironmentBuildJobResult Job =
            UThomasWorldToolset::StartEnvironmentBuildJob(Plan.PlanId);
        TestTrue(*FString::Printf(TEXT("%s builder process starts"), *Action), Job.bOk);
        TestTrue(*FString::Printf(TEXT("%s builder returns a PID"), *Action), Job.ProcessId > 0);
        if (!Job.bOk || Job.JobId.IsEmpty())
        {
            return false;
        }

        const double Deadline = FPlatformTime::Seconds() + 180.0;
        while (Job.bProcessRunning && FPlatformTime::Seconds() < Deadline)
        {
            FPlatformProcess::Sleep(0.20f);
            Job = UThomasWorldToolset::GetEnvironmentBuildJob(Job.JobId, false);
        }
        if (Job.bProcessRunning)
        {
            UThomasWorldToolset::CancelEnvironmentBuildJob(Job.JobId, true);
            AddError(FString::Printf(TEXT("%s builder exceeded 180 seconds"), *Action));
            return false;
        }

        Job = UThomasWorldToolset::GetEnvironmentBuildJob(Job.JobId, true);
        TestTrue(*FString::Printf(TEXT("%s builder succeeds"), *Action), Job.bOk);
        TestEqual(*FString::Printf(TEXT("%s builder status"), *Action), Job.Status, FString(TEXT("succeeded")));
        TestEqual(*FString::Printf(TEXT("%s builder exit code"), *Action), Job.ExitCode, 0);
        TestTrue(*FString::Printf(TEXT("%s builder reloads the exact map"), *Action), Job.bMapReloaded);
        TestTrue(*FString::Printf(TEXT("%s builder writes a log"), *Action),
            !Job.LogPath.IsEmpty() && IFileManager::Get().FileExists(*Job.LogPath));
        return Job.bOk && Job.Status == TEXT("succeeded")
            && Job.ExitCode == 0 && Job.bMapReloaded;
    };

    bool bAllBuildersSucceeded = CreatedEnvironment.bOk
        && CreatedEnvironment.bWorldPartition
        && CreatedWorld.bOk
        && !CreatedWorld.bDirty;
    if (bAllBuildersSucceeded)
    {
        bAllBuildersSucceeded &= RunBuilder(TEXT("build_navigation"), false);
    }
    if (bAllBuildersSucceeded)
    {
        bAllBuildersSucceeded &= RunBuilder(TEXT("build_hlods"), false);
    }
    if (bAllBuildersSucceeded)
    {
        bAllBuildersSucceeded &= RunBuilder(TEXT("delete_hlods"), true);
    }

    FThomasWorldInspectionResult CurrentInspection =
        UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
    if (CurrentInspection.bOk && CurrentInspection.MapPath == TestMapPath)
    {
        FThomasMapLifecycleRequest ReopenRequest;
        ReopenRequest.Action = TEXT("open_map");
        ReopenRequest.CurrentMapPath = CurrentInspection.MapPath;
        ReopenRequest.ExpectedRevision = CurrentInspection.Revision;
        ReopenRequest.TargetMapPath = OriginalInspection.MapPath;
        ReopenRequest.bConfirmWorldSwitch = true;
        const FThomasMapLifecyclePlanResult ReopenPlan =
            UThomasWorldToolset::PlanMapLifecycle(ReopenRequest);
        TestTrue(TEXT("Builder smoke original-map reopen plan succeeds"), ReopenPlan.bOk);
        const FThomasMapLifecycleApplyResult ReopenApply = ReopenPlan.bOk
            ? UThomasWorldToolset::ApplyMapLifecyclePlan(ReopenPlan.PlanId)
            : FThomasMapLifecycleApplyResult();
        TestTrue(TEXT("Builder smoke returns to the exact original map"),
            ReopenApply.bOk && ReopenApply.MapPath == OriginalInspection.MapPath);
    }
    else if (!CurrentInspection.bOk
        || CurrentInspection.MapPath != OriginalInspection.MapPath)
    {
        UWorld* RecoveredWorld = UEditorLoadingAndSavingUtils::LoadMap(
            FPackageName::LongPackageNameToFilename(
                OriginalInspection.MapPath,
                FPackageName::GetMapPackageExtension()));
        TestTrue(TEXT("Builder smoke fail-safe reopens the original map"),
            RecoveredWorld
                && RecoveredWorld->GetOutermost()->GetName() == OriginalInspection.MapPath);
    }

    Registry.ScanPathsSynchronous({TEXT("/Game/PropHunt/Tests/ThomasEditor")}, true);
    TArray<FAssetData> CleanupAssetData;
    Registry.GetAssetsByPath(
        FName(TEXT("/Game/PropHunt/Tests/ThomasEditor")), CleanupAssetData, false, false);
    const FString TestMapAssetName = FPackageName::GetLongPackageAssetName(TestMapPath);
    TArray<UObject*> CleanupAssets;
    for (const FAssetData& AssetData : CleanupAssetData)
    {
        if (AssetData.AssetName.ToString().StartsWith(TestMapAssetName))
        {
            if (UObject* Asset = AssetData.GetAsset())
            {
                CleanupAssets.Add(Asset);
            }
        }
    }
    TestTrue(TEXT("Disposable World Partition map and generated HLOD layers found for cleanup"),
        CleanupAssets.Num() >= 3);
    TestTrue(
        TEXT("Disposable World Partition assets deleted through Unreal API"),
        !CleanupAssets.IsEmpty()
            && ObjectTools::DeleteObjectsUnchecked(CleanupAssets) >= CleanupAssets.Num());
    CollectGarbage(RF_NoFlags);
    const FString TestAssetDirectory = FPackageName::LongPackageNameToFilename(
        FPackageName::GetLongPackagePath(TestMapPath));
    bool bGeneratedAssetFileFound = false;
    IFileManager::Get().IterateDirectoryRecursively(
        *TestAssetDirectory,
        [&bGeneratedAssetFileFound, TestMapAssetName](const TCHAR* Filename, const bool bIsDirectory)
        {
            bGeneratedAssetFileFound |= !bIsDirectory
                && FPaths::GetBaseFilename(Filename).StartsWith(TestMapAssetName);
            return !bGeneratedAssetFileFound;
        });
    TestFalse(TEXT("No disposable World Partition map or HLOD Layer file remains"),
        bGeneratedAssetFileFound);
    for (const FString& ExternalPath : ULevel::GetExternalObjectsPaths(TestMapPath))
    {
        const FString Directory = FPackageName::LongPackageNameToFilename(ExternalPath);
        bool bFileFound = false;
        IFileManager::Get().IterateDirectoryRecursively(
            *Directory,
            [&bFileFound](const TCHAR*, const bool bIsDirectory)
            {
                bFileFound |= !bIsDirectory;
                return !bFileFound;
            });
        TestFalse(
            *FString::Printf(TEXT("No disposable external-object files remain under %s"), *ExternalPath),
            bFileFound);
    }

    return bAllBuildersSucceeded && !HasAnyErrors();
}

#endif
