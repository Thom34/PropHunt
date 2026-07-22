#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/ThomasEditorV02ParityTests.h"

#include "Asset/ThomasEditorAssetService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Game/PHMatchRulesDataAsset.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
bool SaveTestAsset(UPHMatchRulesDataAsset* Asset)
{
    UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
    if (!Package)
    {
        return false;
    }

    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    Args.Error = GError;
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    return UPackage::SavePackage(Package, Asset, *Filename, Args);
}

TSharedPtr<FJsonObject> ParseResult(const FString& Json)
{
    TSharedPtr<FJsonObject> Result;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    FJsonSerializer::Deserialize(Reader, Result);
    return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FThomasEditorV02DataAssetParityTest,
    "PropHunt.ThomasEditor.LegacyV02.DataAssetMutationGates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FThomasEditorV02DataAssetParityTest::RunTest(const FString& Parameters)
{
    const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString PackageName =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/DA_TE_Parity_") + Suffix;
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    UPackage* Package = CreatePackage(*PackageName);
    UPHMatchRulesDataAsset* Asset = NewObject<UPHMatchRulesDataAsset>(
        Package,
        *AssetName,
        RF_Public | RF_Standalone | RF_Transactional);
    TestNotNull(TEXT("Temporary test Data Asset created"), Asset);
    if (!Asset)
    {
        return false;
    }

    FAssetRegistryModule::AssetCreated(Asset);
    Asset->MarkPackageDirty();
    TestTrue(TEXT("Initial test asset saved"), SaveTestAsset(Asset));
    const FString AssetPath = Asset->GetPathName();

    const TSharedPtr<FJsonObject> Summary = ParseResult(
        FThomasEditorAssetService::BuildSummaryJson(
            AssetPath, TEXT("[\"ObjectiveScore\",\"MinimumPropPlayers\",\"MaximumPropPlayers\"]")));
    TestTrue(
        TEXT("Summary succeeds"),
        Summary.IsValid() && Summary->GetBoolField(TEXT("ok")));

    const TSharedPtr<FJsonObject> InMemoryPatch = ParseResult(
        FThomasEditorAssetService::ApplyPatchJson(
            AssetPath,
            TEXT("[{\"name\":\"ObjectiveScore\",\"expected\":1000,\"value\":1001}]"),
            false,
            false));
    TestTrue(
        TEXT("Successful in-memory patch"),
        InMemoryPatch.IsValid() && InMemoryPatch->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("In-memory value changed"), Asset->ObjectiveScore, 1001);

    const TSharedPtr<FJsonObject> InMemoryRestore = ParseResult(
        FThomasEditorAssetService::ApplyPatchJson(
            AssetPath,
            TEXT("[{\"name\":\"ObjectiveScore\",\"expected\":1001,\"value\":1000}]"),
            true,
            false));
    TestTrue(
        TEXT("In-memory value restored"),
        InMemoryRestore.IsValid() && InMemoryRestore->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("Restored original value"), Asset->ObjectiveScore, 1000);
    Package->SetDirtyFlag(false);

    const TSharedPtr<FJsonObject> SavedPatch = ParseResult(
        FThomasEditorAssetService::ApplyPatchJson(
            AssetPath,
            TEXT("[{\"name\":\"ObjectiveScore\",\"expected\":1000,\"value\":1001}]"),
            false,
            true));
    TestTrue(
        TEXT("Voluntary save succeeds"),
        SavedPatch.IsValid()
            && SavedPatch->GetBoolField(TEXT("ok"))
            && SavedPatch->GetBoolField(TEXT("saved")));
    TestEqual(TEXT("Saved value changed"), Asset->ObjectiveScore, 1001);

    const TSharedPtr<FJsonObject> SavedRestore = ParseResult(
        FThomasEditorAssetService::ApplyPatchJson(
            AssetPath,
            TEXT("[{\"name\":\"ObjectiveScore\",\"expected\":1001,\"value\":1000}]"),
            false,
            true));
    TestTrue(
        TEXT("Voluntary save restore succeeds"),
        SavedRestore.IsValid()
            && SavedRestore->GetBoolField(TEXT("ok"))
            && SavedRestore->GetBoolField(TEXT("saved")));
    TestEqual(TEXT("Saved value restored"), Asset->ObjectiveScore, 1000);

    const TSharedPtr<FJsonObject> ValidationRollback = ParseResult(
        FThomasEditorAssetService::ApplyPatchJson(
            AssetPath,
            TEXT("[{\"name\":\"MinimumPropPlayers\",\"expected\":1,\"value\":4},"
                 "{\"name\":\"MaximumPropPlayers\",\"expected\":4,\"value\":1}]"),
            false,
            false));
    TestTrue(
        TEXT("Business-rule rollback reported"),
        ValidationRollback.IsValid()
            && !ValidationRollback->GetBoolField(TEXT("ok"))
            && ValidationRollback->GetStringField(TEXT("code")) == TEXT("post_validation_failed"));
    TestEqual(TEXT("Minimum player count rolled back"), Asset->MinimumPropPlayers, 1);
    TestEqual(TEXT("Maximum player count rolled back"), Asset->MaximumPropPlayers, 4);
    TestFalse(TEXT("Rollback restored clean package"), Package->IsDirty());

    FThomasEditorAssetService::SetForceSaveFailureForTests(true);
    const TSharedPtr<FJsonObject> SaveFailure = ParseResult(
        FThomasEditorAssetService::ApplyPatchJson(
            AssetPath,
            TEXT("[{\"name\":\"ObjectiveScore\",\"expected\":1000,\"value\":1001}]"),
            false,
            true));
    FThomasEditorAssetService::SetForceSaveFailureForTests(false);
    TestTrue(
        TEXT("Forced disk failure reports rollback status"),
        SaveFailure.IsValid()
            && !SaveFailure->GetBoolField(TEXT("ok"))
            && SaveFailure->GetStringField(TEXT("code")) == TEXT("rollback_failed")
            && SaveFailure->GetBoolField(TEXT("rollback_validated"))
            && !SaveFailure->GetBoolField(TEXT("rollback_saved")));
    TestEqual(TEXT("Failed save restored in-memory value"), Asset->ObjectiveScore, 1000);

    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
        .Get()
        .ScanModifiedAssetFiles({Filename});

    TestEqual(
        TEXT("Temporary asset deleted through Unreal Editor API"),
        ObjectTools::DeleteObjectsUnchecked({ Asset }),
        1);
    CollectGarbage(RF_NoFlags);
    TestFalse(
        TEXT("Temporary test package removed from disk"),
        IFileManager::Get().FileExists(*Filename));

    return !HasAnyErrors();
}

bool RunThomasEditorV02MutationGates()
{
    class FEmbeddedParityTest final : public FThomasEditorV02DataAssetParityTest
    {
    public:
        FEmbeddedParityTest()
            : FThomasEditorV02DataAssetParityTest(TEXT("ThomasEditorEmbeddedV02Parity"))
        {
        }

        bool Execute()
        {
            return RunTest(TEXT(""));
        }
    };

    FEmbeddedParityTest Test;
    return Test.Execute();
}

#endif
