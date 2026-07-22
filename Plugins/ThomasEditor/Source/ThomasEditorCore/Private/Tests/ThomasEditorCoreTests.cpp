#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Animation/BlendProfile.h"
#include "Animation/MirrorDataTable.h"
#include "Components/ActorComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "ThomasEditorCoreModule.h"
#include "ThomasEditorLegacyProvider.h"
#include "ThomasEditorWorldProvider.h"
#include "Toolsets/ThomasAssetsToolset.h"
#include "Toolsets/ThomasBlueprintToolset.h"
#include "Toolsets/ThomasCompatibilityToolset.h"
#include "Toolsets/ThomasDiscoveryToolset.h"
#include "Toolsets/ThomasDomainsToolset.h"
#include "Toolsets/ThomasMaterialsToolset.h"
#include "Toolsets/ThomasProjectToolset.h"
#include "Toolsets/ThomasRenderToolset.h"
#include "Toolsets/ThomasValidationToolset.h"
#include "Toolsets/ThomasWorldToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FThomasEditorNativeCoreTest,
    "PropHunt.ThomasEditor.NativeCore.LazyAssetsProvider",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

static void RunControlRigTypedControlMutationTests(
    FThomasEditorNativeCoreTest& Test,
    const FString& ControlRigPath,
    const FString& ControlRigRootBone,
    const int32 ExpectedControlRigBoneCount,
    const TFunction<FThomasSpecializedAssetInspectionResult(
        const FString&,
        const FThomasAnimationOperation&,
        bool)>& ApplyControlRigOperationAndReload)
{
    struct FTypedControlCase
    {
        FString Type;
        FString EnumName;
        FString Name;
    };
    const TArray<FTypedControlCase> Cases = {
        {TEXT("bool"), TEXT("Bool"), TEXT("TE_BoolControl")},
        {TEXT("float"), TEXT("Float"), TEXT("TE_GenericFloatControl")},
        {TEXT("integer"), TEXT("Integer"), TEXT("TE_IntegerControl")},
        {TEXT("vector2d"), TEXT("Vector2D"), TEXT("TE_Vector2DControl")},
        {TEXT("position"), TEXT("Position"), TEXT("TE_PositionControl")},
        {TEXT("scale"), TEXT("Scale"), TEXT("TE_ScaleControl")},
        {TEXT("rotator"), TEXT("Rotator"), TEXT("TE_RotatorControl")},
        {TEXT("transform"), TEXT("Transform"), TEXT("TE_TransformControl")},
        {TEXT("transform_no_scale"), TEXT("TransformNoScale"),
            TEXT("TE_TransformNoScaleControl")},
        {TEXT("euler_transform"), TEXT("EulerTransform"),
            TEXT("TE_EulerTransformControl")},
        {TEXT("scale_float"), TEXT("ScaleFloat"),
            TEXT("TE_ScaleFloatControl")}
    };

    auto ConfigureValue = [](
        FThomasAnimationOperation& Operation,
        const FString& Type,
        const bool bUpdated)
    {
        if (Type == TEXT("bool"))
        {
            Operation.bControlBoolValue = bUpdated;
        }
        else if (Type == TEXT("float") || Type == TEXT("scale_float"))
        {
            Operation.Value = Type == TEXT("scale_float")
                ? (bUpdated ? 1.75f : 1.25f)
                : (bUpdated ? 0.625f : 0.125f);
        }
        else if (Type == TEXT("integer"))
        {
            Operation.ControlIntegerValue = bUpdated ? 17 : 7;
        }
        else if (Type == TEXT("vector2d"))
        {
            Operation.ControlVector2DValue = bUpdated
                ? FVector2D(30.0, 40.0)
                : FVector2D(10.0, 20.0);
        }
        else if (Type == TEXT("position"))
        {
            Operation.ControlVectorValue = bUpdated
                ? FVector(4.0, 5.0, 6.0)
                : FVector(1.0, 2.0, 3.0);
        }
        else if (Type == TEXT("scale"))
        {
            Operation.ControlVectorValue = bUpdated
                ? FVector(0.8, 0.9, 1.0)
                : FVector(1.1, 1.2, 1.3);
        }
        else if (Type == TEXT("rotator"))
        {
            Operation.Rotation = bUpdated
                ? FRotator(40.0, 50.0, 60.0)
                : FRotator(10.0, 20.0, 30.0);
        }
        else if (Type == TEXT("transform")
            || Type == TEXT("euler_transform"))
        {
            Operation.Location = bUpdated
                ? FVector(7.0, 8.0, 9.0)
                : FVector(1.0, 3.0, 5.0);
            Operation.Rotation = bUpdated
                ? FRotator(35.0, 45.0, 55.0)
                : FRotator(5.0, 15.0, 25.0);
            Operation.Scale = bUpdated
                ? FVector(0.7, 0.8, 0.9)
                : FVector(1.1, 1.2, 1.3);
        }
        else if (Type == TEXT("transform_no_scale"))
        {
            Operation.Location = bUpdated
                ? FVector(8.0, 9.0, 10.0)
                : FVector(2.0, 4.0, 6.0);
            Operation.Rotation = bUpdated
                ? FRotator(45.0, 55.0, 65.0)
                : FRotator(15.0, 25.0, 35.0);
            Operation.Scale = FVector::OneVector;
        }
    };
    auto GetControlItem = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& Name)
    {
        for (const FString& Item : Inspection.Items)
        {
            if (Item.Contains(
                TEXT("Name=") + Name + TEXT(" Type=Control")))
            {
                return Item;
            }
        }
        return FString();
    };
    auto GetControlValue = [](
        const FString& Item,
        const bool bInitial)
    {
        const FString InitialMarker = TEXT(" InitialValue=");
        const FString CurrentMarker = TEXT(" CurrentValue=");
        const int32 InitialStart = Item.Find(InitialMarker);
        const int32 CurrentStart = Item.Find(CurrentMarker);
        if (InitialStart == INDEX_NONE || CurrentStart == INDEX_NONE)
        {
            return FString();
        }
        if (bInitial)
        {
            const int32 ValueStart = InitialStart + InitialMarker.Len();
            return Item.Mid(ValueStart, CurrentStart - ValueStart);
        }
        return Item.Mid(CurrentStart + CurrentMarker.Len());
    };
    auto GetDelimitedField = [](
        const FString& Item,
        const FString& Marker,
        const FString& NextMarker)
    {
        const int32 Start = Item.Find(Marker);
        if (Start == INDEX_NONE)
        {
            return FString();
        }
        const int32 ValueStart = Start + Marker.Len();
        const int32 End = Item.Find(NextMarker, ESearchCase::CaseSensitive,
            ESearchDir::FromStart, ValueStart);
        return End == INDEX_NONE
            ? Item.Mid(ValueStart)
            : Item.Mid(ValueStart, End - ValueStart);
    };

    for (const FTypedControlCase& Case : Cases)
    {
        FString CurrentName = Case.Name;
        FThomasAnimationOperation AddControl;
        AddControl.Action = TEXT("add_control_rig_control");
        AddControl.Name = CurrentName;
        AddControl.ParentName = ControlRigRootBone;
        AddControl.ControlType = Case.Type;
        AddControl.Color = FLinearColor(0.15f, 0.45f, 0.75f, 1.0f);
        ConfigureValue(AddControl, Case.Type, false);
        FThomasSpecializedAssetInspectionResult AfterAdd =
            ApplyControlRigOperationAndReload(
                TEXT("Control Rig generic ") + Case.Type
                    + TEXT(" control add"),
                AddControl,
                false);
        FString Item = GetControlItem(AfterAdd, CurrentName);
        FString InitialValue = GetControlValue(Item, true);
        const FString CurrentValue = GetControlValue(Item, false);
        Test.TestTrue(
            TEXT("Control Rig generic ") + Case.Type
                + TEXT(" control survives save and reload"),
            AfterAdd.bOk
                && AfterAdd.Metrics.Contains(TEXT("ControlCount=1"))
                && AfterAdd.Metrics.Contains(FString::Printf(
                    TEXT("ElementCount=%d"),
                    ExpectedControlRigBoneCount + 1))
                && Item.Contains(TEXT("Parent=") + ControlRigRootBone)
                && Item.Contains(TEXT("ControlType=") + Case.EnumName)
                && !InitialValue.IsEmpty()
                && InitialValue == CurrentValue);

        if (Case.Type == TEXT("float"))
        {
            FThomasAnimationOperation PlannedSet;
            PlannedSet.Action = TEXT("set_control_rig_control_value");
            PlannedSet.Name = CurrentName;
            PlannedSet.ControlType = Case.Type;
            PlannedSet.ExpectedValue = InitialValue;
            ConfigureValue(PlannedSet, Case.Type, true);
            FThomasAnimationPatchRequest PlannedSetRequest;
            PlannedSetRequest.AssetPath = ControlRigPath;
            PlannedSetRequest.ExpectedRevision = AfterAdd.Revision;
            PlannedSetRequest.Operations.Add(PlannedSet);
            const FThomasAnimationPlanResult PlannedSetPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    PlannedSetRequest);
            Test.TestTrue(
                TEXT("Control Rig rollback regression set plan succeeds"),
                PlannedSetPlan.bOk);

            FThomasAnimationOperation UnsavedRename;
            UnsavedRename.Action = TEXT("rename_control_rig_control");
            UnsavedRename.Name = CurrentName;
            UnsavedRename.NewName = CurrentName + TEXT("Unsaved");
            UnsavedRename.ControlType = Case.Type;
            UnsavedRename.ExpectedValue = InitialValue;
            FThomasAnimationPatchRequest UnsavedRenameRequest;
            UnsavedRenameRequest.AssetPath = ControlRigPath;
            UnsavedRenameRequest.ExpectedRevision = AfterAdd.Revision;
            UnsavedRenameRequest.bConfirmDestructive = true;
            UnsavedRenameRequest.Operations.Add(UnsavedRename);
            const FThomasAnimationPlanResult UnsavedRenamePlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    UnsavedRenameRequest);
            Test.TestTrue(
                TEXT("Control Rig rollback regression rename plan succeeds"),
                UnsavedRenamePlan.bOk);
            const FThomasAnimationApplyResult UnsavedRenameApply =
                UThomasDomainsToolset::ApplyAnimationPlan(
                    UnsavedRenamePlan.PlanId, false);
            Test.TestTrue(
                TEXT("Control Rig rollback regression creates unsaved mutation"),
                UnsavedRenameApply.bOk && !UnsavedRenameApply.bSaved);
            UObject* MutatedControlRig = LoadObject<UObject>(
                nullptr,
                *(ControlRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(
                        ControlRigPath)));
            if (MutatedControlRig)
            {
                MutatedControlRig->GetOutermost()->SetDirtyFlag(false);
            }
            const FThomasAnimationApplyResult FailedSetApply =
                UThomasDomainsToolset::ApplyAnimationPlan(
                    PlannedSetPlan.PlanId, true);
            const FThomasSpecializedAssetInspectionResult AfterRollback =
                UThomasDomainsToolset::InspectAnimationAsset(
                    ControlRigPath, 1000);
            UObject* RolledBackControlRig = LoadObject<UObject>(
                nullptr,
                *(ControlRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(
                        ControlRigPath)));
            Test.AddInfo(FString::Printf(
                TEXT("Control Rig rollback readback code=%s rolled_back=%s package_dirty=%s original_restored=%s"),
                *FailedSetApply.Code,
                FailedSetApply.bRolledBack ? TEXT("true") : TEXT("false"),
                RolledBackControlRig
                        && RolledBackControlRig->GetOutermost()->IsDirty()
                    ? TEXT("true") : TEXT("false"),
                !GetControlItem(AfterRollback, CurrentName).IsEmpty()
                    ? TEXT("true") : TEXT("false")));
            Test.TestTrue(
                TEXT("Control Rig failed apply reloads disk and clears dirty state"),
                FailedSetApply.Code == TEXT("animation_operation_failed")
                    && FailedSetApply.bRolledBack
                    && RolledBackControlRig
                    && !RolledBackControlRig->GetOutermost()->IsDirty()
                    && !GetControlItem(
                        AfterRollback, CurrentName).IsEmpty()
                    && GetControlItem(
                        AfterRollback,
                        UnsavedRename.NewName).IsEmpty());
        }

        if (Case.Type == TEXT("transform"))
        {
            const FString* ShapeLibraryItemPtr = AfterAdd.Items
                .FindByPredicate([](const FString& Candidate)
                {
                    return Candidate.StartsWith(
                        TEXT("ControlRigShapeLibrary Index="));
                });
            const FString ShapeLibraryItem = ShapeLibraryItemPtr
                ? *ShapeLibraryItemPtr : FString();
            Test.AddInfo(TEXT("Control Rig shape library readback ")
                + ShapeLibraryItem);
            Test.TestTrue(
                TEXT("Control Rig shape library is loaded and exposes Circle_Thin"),
                AfterAdd.Metrics.Contains(
                    TEXT("ShapeLibraryReferenceCount=1"))
                    && AfterAdd.Metrics.Contains(
                        TEXT("LoadedShapeLibraryCount=1"))
                    && ShapeLibraryItem.Contains(TEXT("Loaded=True"))
                    && ShapeLibraryItem.Contains(TEXT("Circle_Thin")));
            const FString ShapeSettings = GetDelimitedField(
                Item,
                TEXT(" ShapeSettings="),
                TEXT(" LimitSettings="));
            FThomasAnimationOperation SetShapeSettings;
            SetShapeSettings.Action =
                TEXT("set_control_rig_control_shape_settings");
            SetShapeSettings.Name = CurrentName;
            SetShapeSettings.ControlType = Case.Type;
            SetShapeSettings.ExpectedValue = ShapeSettings;
            SetShapeSettings.ControlShapeName = TEXT("Circle_Thin");
            SetShapeSettings.Color =
                FLinearColor(0.8f, 0.2f, 0.1f, 1.0f);
            SetShapeSettings.bControlShapeVisible = false;
            FThomasAnimationOperation MissingShape = SetShapeSettings;
            MissingShape.ControlShapeName = TEXT("TE_MissingShape");
            FThomasAnimationPatchRequest MissingShapeRequest;
            MissingShapeRequest.AssetPath = ControlRigPath;
            MissingShapeRequest.ExpectedRevision = AfterAdd.Revision;
            MissingShapeRequest.Operations.Add(MissingShape);
            Test.TestEqual(
                TEXT("Control Rig rejects a shape absent from its libraries"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    MissingShapeRequest).Code,
                FString(TEXT("control_rig_shape_not_found")));
            const FThomasSpecializedAssetInspectionResult
                AfterShapeSettings = ApplyControlRigOperationAndReload(
                    TEXT("Control Rig control shape settings"),
                    SetShapeSettings,
                    false);
            Item = GetControlItem(AfterShapeSettings, CurrentName);
            const FString UpdatedShapeSettings = GetDelimitedField(
                Item,
                TEXT(" ShapeSettings="),
                TEXT(" LimitSettings="));
            Test.TestTrue(
                TEXT("Control Rig shape settings survive save and reload"),
                AfterShapeSettings.bOk
                    && UpdatedShapeSettings.Contains(
                        TEXT("ShapeName=Circle_Thin"))
                    && UpdatedShapeSettings.Contains(
                        TEXT("ShapeVisible=False"))
                    && UpdatedShapeSettings != ShapeSettings);

            const FString ShapeInitial = GetDelimitedField(
                Item,
                TEXT(" ShapeInitialLocal="),
                TEXT(" ShapeCurrentLocal="));
            FThomasAnimationOperation SetShapeTransform;
            SetShapeTransform.Action =
                TEXT("set_control_rig_control_shape_transform");
            SetShapeTransform.Name = CurrentName;
            SetShapeTransform.ControlType = Case.Type;
            SetShapeTransform.ExpectedValue = ShapeInitial;
            SetShapeTransform.Location = FVector(2.0, 4.0, 6.0);
            SetShapeTransform.Rotation = FRotator(10.0, 20.0, 30.0);
            SetShapeTransform.Scale = FVector(1.5, 1.25, 0.75);
            const FThomasSpecializedAssetInspectionResult
                AfterShapeTransform = ApplyControlRigOperationAndReload(
                    TEXT("Control Rig control shape transform"),
                    SetShapeTransform,
                    false);
            Item = GetControlItem(AfterShapeTransform, CurrentName);
            const FString UpdatedShapeInitial = GetDelimitedField(
                Item,
                TEXT(" ShapeInitialLocal="),
                TEXT(" ShapeCurrentLocal="));
            const FString UpdatedShapeCurrent = GetDelimitedField(
                Item,
                TEXT(" ShapeCurrentLocal="),
                TEXT(" InitialValue="));
            Test.TestTrue(
                TEXT("Control Rig shape transform survives save and reload"),
                AfterShapeTransform.bOk
                    && !UpdatedShapeInitial.IsEmpty()
                    && UpdatedShapeInitial == UpdatedShapeCurrent
                    && UpdatedShapeInitial != ShapeInitial);
        }

        if (Case.Type == TEXT("position"))
        {
            const FString LimitSettings = GetDelimitedField(
                Item,
                TEXT(" LimitSettings="),
                TEXT(" ShapeInitialLocal="));
            FThomasAnimationOperation SetLimits;
            SetLimits.Action = TEXT("set_control_rig_control_limits");
            SetLimits.Name = CurrentName;
            SetLimits.ControlType = Case.Type;
            SetLimits.ExpectedValue = LimitSettings;
            SetLimits.ControlMinimumLimitEnabledPerChannel = {
                true, false, true};
            SetLimits.ControlMaximumLimitEnabledPerChannel = {
                false, true, true};
            SetLimits.bDrawControlLimits = true;
            SetLimits.ControlMinimumValue.VectorValue =
                FVector(-10.0, -20.0, -30.0);
            SetLimits.ControlMaximumValue.VectorValue =
                FVector(10.0, 20.0, 30.0);
            FThomasAnimationOperation InvalidLimitChannels = SetLimits;
            InvalidLimitChannels
                .ControlMinimumLimitEnabledPerChannel = {true, false};
            InvalidLimitChannels
                .ControlMaximumLimitEnabledPerChannel = {false, true};
            FThomasAnimationPatchRequest InvalidLimitRequest;
            InvalidLimitRequest.AssetPath = ControlRigPath;
            InvalidLimitRequest.ExpectedRevision = AfterAdd.Revision;
            InvalidLimitRequest.Operations.Add(InvalidLimitChannels);
            Test.TestEqual(
                TEXT("Control Rig rejects an incomplete limit channel map"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    InvalidLimitRequest).Code,
                FString(TEXT("invalid_control_rig_limit_channels")));
            const FThomasSpecializedAssetInspectionResult AfterLimits =
                ApplyControlRigOperationAndReload(
                    TEXT("Control Rig position control limits"),
                    SetLimits,
                    false);
            Item = GetControlItem(AfterLimits, CurrentName);
            const FString UpdatedLimitSettings = GetDelimitedField(
                Item,
                TEXT(" LimitSettings="),
                TEXT(" ShapeInitialLocal="));
            Test.TestTrue(
                TEXT("Control Rig typed limits survive save and reload"),
                AfterLimits.bOk
                    && UpdatedLimitSettings.Contains(
                        TEXT("Channels=X,Y,Z"))
                    && UpdatedLimitSettings.Contains(TEXT("Draw=True"))
                    && UpdatedLimitSettings.Contains(
                        TEXT("Flags=1:0,0:1,1:1"))
                    && UpdatedLimitSettings.Contains(
                        TEXT("Minimum=X=-10.000 Y=-20.000 Z=-30.000"))
                    && UpdatedLimitSettings.Contains(
                        TEXT("Maximum=X=10.000 Y=20.000 Z=30.000"))
                    && UpdatedLimitSettings != LimitSettings);
        }

        if (Case.Type == TEXT("bool"))
        {
            FThomasAnimationOperation RenameControl;
            RenameControl.Action = TEXT("rename_control_rig_control");
            RenameControl.Name = CurrentName;
            RenameControl.NewName = CurrentName + TEXT("Renamed");
            RenameControl.ControlType = Case.Type;
            RenameControl.ExpectedValue = InitialValue;
            FThomasSpecializedAssetInspectionResult AfterRename =
                ApplyControlRigOperationAndReload(
                    TEXT("Control Rig generic bool control R2 rename"),
                    RenameControl,
                    true);
            CurrentName = RenameControl.NewName;
            Item = GetControlItem(AfterRename, CurrentName);
            Test.TestTrue(
                TEXT("Control Rig generic control rename survives reload"),
                AfterRename.bOk
                    && GetControlValue(Item, true) == InitialValue
                    && !AfterRename.Items.ContainsByPredicate(
                        [&](const FString& Candidate)
                        {
                            return Candidate.Contains(
                                TEXT("Name=") + Case.Name
                                    + TEXT(" Type=Control"));
                        }));
        }

        FThomasAnimationOperation SetControl;
        SetControl.Action = TEXT("set_control_rig_control_value");
        SetControl.Name = CurrentName;
        SetControl.ControlType = Case.Type;
        SetControl.ExpectedValue = InitialValue;
        ConfigureValue(SetControl, Case.Type, true);
        FThomasSpecializedAssetInspectionResult AfterSet =
            ApplyControlRigOperationAndReload(
                TEXT("Control Rig generic ") + Case.Type
                    + TEXT(" control value"),
                SetControl,
                false);
        Item = GetControlItem(AfterSet, CurrentName);
        const FString UpdatedInitialValue =
            GetControlValue(Item, true);
        const FString UpdatedCurrentValue =
            GetControlValue(Item, false);
        Test.TestTrue(
            TEXT("Control Rig generic ") + Case.Type
                + TEXT(" value survives save and reload"),
            AfterSet.bOk
                && !UpdatedInitialValue.IsEmpty()
                && UpdatedInitialValue == UpdatedCurrentValue
                && UpdatedInitialValue != InitialValue);

        FThomasAnimationOperation RemoveControl;
        RemoveControl.Action = TEXT("remove_control_rig_control");
        RemoveControl.Name = CurrentName;
        RemoveControl.ControlType = Case.Type;
        RemoveControl.ExpectedValue = UpdatedInitialValue;
        const FThomasSpecializedAssetInspectionResult AfterRemove =
            ApplyControlRigOperationAndReload(
                TEXT("Control Rig generic ") + Case.Type
                    + TEXT(" control R2 remove"),
                RemoveControl,
                true);
        Test.TestTrue(
            TEXT("Control Rig generic ") + Case.Type
                + TEXT(" cleanup survives save and reload"),
            AfterRemove.bOk
                && AfterRemove.Metrics.Contains(TEXT("ControlCount=0"))
                && AfterRemove.Metrics.Contains(FString::Printf(
                    TEXT("ElementCount=%d"),
                    ExpectedControlRigBoneCount))
                && GetControlItem(AfterRemove, CurrentName).IsEmpty());
    }

    const FThomasSpecializedAssetInspectionResult BeforeInvalidType =
        UThomasDomainsToolset::InspectAnimationAsset(ControlRigPath, 1000);
    FThomasAnimationPatchRequest InvalidTypeRequest;
    InvalidTypeRequest.AssetPath = ControlRigPath;
    InvalidTypeRequest.ExpectedRevision = BeforeInvalidType.Revision;
    FThomasAnimationOperation InvalidTypeControl;
    InvalidTypeControl.Action = TEXT("add_control_rig_control");
    InvalidTypeControl.Name = TEXT("TE_InvalidTypeControl");
    InvalidTypeControl.ControlType = TEXT("unsupported");
    InvalidTypeRequest.Operations.Add(InvalidTypeControl);
    Test.TestEqual(
        TEXT("Control Rig generic control rejects unsupported type"),
        UThomasDomainsToolset::PlanAnimationPatch(
            InvalidTypeRequest).Code,
        FString(TEXT("invalid_control_rig_control_type")));

    const FThomasSpecializedAssetInspectionResult BeforeInvalidNoScale =
        UThomasDomainsToolset::InspectAnimationAsset(ControlRigPath, 1000);
    FThomasAnimationPatchRequest InvalidNoScaleRequest;
    InvalidNoScaleRequest.AssetPath = ControlRigPath;
    InvalidNoScaleRequest.ExpectedRevision = BeforeInvalidNoScale.Revision;
    FThomasAnimationOperation InvalidNoScaleControl;
    InvalidNoScaleControl.Action = TEXT("add_control_rig_control");
    InvalidNoScaleControl.Name = TEXT("TE_InvalidNoScaleControl");
    InvalidNoScaleControl.ControlType = TEXT("transform_no_scale");
    InvalidNoScaleControl.Scale = FVector(2.0, 2.0, 2.0);
    InvalidNoScaleRequest.Operations.Add(InvalidNoScaleControl);
    Test.TestEqual(
        TEXT("Control Rig TransformNoScale rejects a hidden scale"),
        UThomasDomainsToolset::PlanAnimationPatch(
            InvalidNoScaleRequest).Code,
        FString(TEXT("invalid_control_rig_control_value")));
}

static void RunControlRigSocketAndSpaceMutationTests(
    FThomasEditorNativeCoreTest& Test,
    const FString& ControlRigRootBone,
    const int32 ExpectedControlRigBoneCount,
    const TFunction<FThomasSpecializedAssetInspectionResult(
        const FString&,
        const FThomasAnimationOperation&,
        bool)>& ApplyControlRigOperationAndReload)
{
    auto GetElementItem = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& Name,
        const FString& Type)
    {
        for (const FString& Item : Inspection.Items)
        {
            if (Item.Contains(TEXT("Name=") + Name + TEXT(" Type=") + Type))
            {
                return Item;
            }
        }
        return FString();
    };
    auto GetDelimitedField = [](
        const FString& Item,
        const FString& Marker,
        const FString& NextMarker)
    {
        const int32 Start = Item.Find(Marker);
        if (Start == INDEX_NONE)
        {
            return FString();
        }
        const int32 ValueStart = Start + Marker.Len();
        const int32 End = NextMarker.IsEmpty()
            ? INDEX_NONE
            : Item.Find(
                NextMarker,
                ESearchCase::CaseSensitive,
                ESearchDir::FromStart,
                ValueStart);
        return End == INDEX_NONE
            ? Item.Mid(ValueStart)
            : Item.Mid(ValueStart, End - ValueStart);
    };
    auto GetAvailableSpaces = [&](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& ControlName)
    {
        return GetDelimitedField(
            GetElementItem(Inspection, ControlName, TEXT("Control")),
            TEXT(" AvailableSpaces="),
            TEXT(" ShapeSettings="));
    };
    auto GetSocketState = [&](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& SocketName)
    {
        return GetDelimitedField(
            GetElementItem(Inspection, SocketName, TEXT("Socket")),
            TEXT(" SocketState="),
            FString());
    };
    auto GetInitialControlValue = [&](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& ControlName)
    {
        return GetDelimitedField(
            GetElementItem(Inspection, ControlName, TEXT("Control")),
            TEXT(" InitialValue="),
            TEXT(" CurrentValue="));
    };

    const FString ControlName = TEXT("TE_SpaceControl");
    FThomasAnimationOperation AddControl;
    AddControl.Action = TEXT("add_control_rig_control");
    AddControl.Name = ControlName;
    AddControl.ControlType = TEXT("transform");
    AddControl.Color = FLinearColor(0.3f, 0.6f, 0.9f, 1.0f);
    FThomasSpecializedAssetInspectionResult AfterControlAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig available-space control add"),
            AddControl,
            false);
    const FString InitialControlValue =
        GetInitialControlValue(AfterControlAdd, ControlName);
    FString AvailableSpaces =
        GetAvailableSpaces(AfterControlAdd, ControlName);
    Test.TestTrue(
        TEXT("Control Rig available-space control survives reload"),
        AfterControlAdd.bOk
            && AfterControlAdd.Metrics.Contains(TEXT("ControlCount=1"))
            && AvailableSpaces.IsEmpty()
            && !InitialControlValue.IsEmpty());

    const FString SocketName = TEXT("TE_SpaceSocket");
    FThomasAnimationOperation AddSocket;
    AddSocket.Action = TEXT("add_control_rig_socket");
    AddSocket.Name = SocketName;
    AddSocket.ParentName = ControlRigRootBone;
    AddSocket.Location = FVector(11.0, 12.0, 13.0);
    AddSocket.Rotation = FRotator(10.0, 20.0, 30.0);
    AddSocket.Scale = FVector(1.1, 1.2, 1.3);
    AddSocket.Color = FLinearColor(0.2f, 0.4f, 0.6f, 1.0f);
    AddSocket.Description = TEXT("Primary test socket");
    FThomasSpecializedAssetInspectionResult AfterSocketAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig socket add"), AddSocket, false);
    FString SocketState = GetSocketState(AfterSocketAdd, SocketName);
    Test.TestTrue(TEXT("Control Rig socket survives save and reload"),
        AfterSocketAdd.bOk
            && AfterSocketAdd.Metrics.Contains(TEXT("SocketCount=1"))
            && AfterSocketAdd.Metrics.Contains(FString::Printf(
                TEXT("ElementCount=%d"),
                ExpectedControlRigBoneCount + 2))
            && SocketState.Contains(
                TEXT("Parent=Bone:") + ControlRigRootBone)
            && SocketState.Contains(
                TEXT("InitialLocal=11.000000,12.000000,13.000000"))
            && SocketState.Contains(
                TEXT("Description=Primary test socket")));

    FThomasAnimationOperation AddRootSpace;
    AddRootSpace.Action = TEXT("add_control_rig_available_space");
    AddRootSpace.Name = ControlName;
    AddRootSpace.SpaceName = ControlRigRootBone;
    AddRootSpace.SpaceType = TEXT("bone");
    AddRootSpace.DisplayLabel = TEXT("RootSpace");
    AddRootSpace.ExpectedValue = AvailableSpaces;
    FThomasSpecializedAssetInspectionResult AfterRootSpaceAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig root available space add"),
            AddRootSpace,
            false);
    AvailableSpaces = GetAvailableSpaces(
        AfterRootSpaceAdd, ControlName);
    Test.TestTrue(
        TEXT("Control Rig root available space survives reload"),
        AfterRootSpaceAdd.bOk
            && AvailableSpaces.Contains(
                TEXT("0=") + ControlRigRootBone
                    + TEXT(":Bone:RootSpace")));

    FThomasAnimationOperation AddSocketSpace;
    AddSocketSpace.Action = TEXT("add_control_rig_available_space");
    AddSocketSpace.Name = ControlName;
    AddSocketSpace.SpaceName = SocketName;
    AddSocketSpace.SpaceType = TEXT("socket");
    AddSocketSpace.DisplayLabel = TEXT("HandSpace");
    AddSocketSpace.ExpectedValue = AvailableSpaces;
    FThomasSpecializedAssetInspectionResult AfterSocketSpaceAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig socket available space add"),
            AddSocketSpace,
            false);
    AvailableSpaces = GetAvailableSpaces(
        AfterSocketSpaceAdd, ControlName);
    Test.TestTrue(
        TEXT("Control Rig socket available space survives reload"),
        AfterSocketSpaceAdd.bOk
            && AvailableSpaces.Contains(
                TEXT("1=") + SocketName
                    + TEXT(":Socket:HandSpace")));

    FThomasAnimationOperation SetSpaceLabel;
    SetSpaceLabel.Action =
        TEXT("set_control_rig_available_space_label");
    SetSpaceLabel.Name = ControlName;
    SetSpaceLabel.SpaceName = SocketName;
    SetSpaceLabel.SpaceType = TEXT("socket");
    SetSpaceLabel.DisplayLabel = TEXT("GripSpace");
    SetSpaceLabel.ExpectedValue = AvailableSpaces;
    FThomasSpecializedAssetInspectionResult AfterSpaceLabel =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig available space label"),
            SetSpaceLabel,
            false);
    AvailableSpaces = GetAvailableSpaces(AfterSpaceLabel, ControlName);
    Test.TestTrue(
        TEXT("Control Rig available-space label survives reload"),
        AfterSpaceLabel.bOk
            && AvailableSpaces.Contains(
                TEXT("1=") + SocketName
                    + TEXT(":Socket:GripSpace")));

    FThomasAnimationOperation SetSpaceIndex;
    SetSpaceIndex.Action =
        TEXT("set_control_rig_available_space_index");
    SetSpaceIndex.Name = ControlName;
    SetSpaceIndex.SpaceName = SocketName;
    SetSpaceIndex.SpaceType = TEXT("socket");
    SetSpaceIndex.Index = 0;
    SetSpaceIndex.ExpectedValue = AvailableSpaces;
    FThomasSpecializedAssetInspectionResult AfterSpaceIndex =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig available space reorder"),
            SetSpaceIndex,
            false);
    AvailableSpaces = GetAvailableSpaces(AfterSpaceIndex, ControlName);
    Test.TestTrue(
        TEXT("Control Rig available-space order survives reload"),
        AfterSpaceIndex.bOk
            && AvailableSpaces.StartsWith(
                TEXT("0=") + SocketName
                    + TEXT(":Socket:GripSpace")));

    FThomasAnimationOperation RemoveRootSpace;
    RemoveRootSpace.Action =
        TEXT("remove_control_rig_available_space");
    RemoveRootSpace.Name = ControlName;
    RemoveRootSpace.SpaceName = ControlRigRootBone;
    RemoveRootSpace.SpaceType = TEXT("bone");
    RemoveRootSpace.ExpectedValue = AvailableSpaces;
    FThomasSpecializedAssetInspectionResult AfterRootSpaceRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig root available space R2 remove"),
            RemoveRootSpace,
            true);
    AvailableSpaces = GetAvailableSpaces(
        AfterRootSpaceRemove, ControlName);
    Test.TestTrue(
        TEXT("Control Rig root available-space removal survives reload"),
        AfterRootSpaceRemove.bOk
            && !AvailableSpaces.Contains(ControlRigRootBone)
            && AvailableSpaces.Contains(SocketName));

    FThomasAnimationOperation RemoveSocketSpace;
    RemoveSocketSpace.Action =
        TEXT("remove_control_rig_available_space");
    RemoveSocketSpace.Name = ControlName;
    RemoveSocketSpace.SpaceName = SocketName;
    RemoveSocketSpace.SpaceType = TEXT("socket");
    RemoveSocketSpace.ExpectedValue = AvailableSpaces;
    FThomasSpecializedAssetInspectionResult AfterSocketSpaceRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig socket available space R2 remove"),
            RemoveSocketSpace,
            true);
    AvailableSpaces = GetAvailableSpaces(
        AfterSocketSpaceRemove, ControlName);
    Test.TestTrue(
        TEXT("Control Rig available-space cleanup survives reload"),
        AfterSocketSpaceRemove.bOk && AvailableSpaces.IsEmpty());

    const int32 ColorStart = SocketState.Find(TEXT("|Color="));
    FThomasAnimationOperation SetSocketSettings;
    SetSocketSettings.Action = TEXT("set_control_rig_socket_settings");
    SetSocketSettings.Name = SocketName;
    SetSocketSettings.ExpectedValue = ColorStart == INDEX_NONE
        ? FString() : SocketState.Mid(ColorStart + 1);
    SetSocketSettings.Color = FLinearColor(0.8f, 0.3f, 0.1f, 1.0f);
    SetSocketSettings.Description = TEXT("Updated socket");
    FThomasSpecializedAssetInspectionResult AfterSocketSettings =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig socket settings"),
            SetSocketSettings,
            false);
    SocketState = GetSocketState(AfterSocketSettings, SocketName);
    Test.TestTrue(
        TEXT("Control Rig socket settings survive save and reload"),
        AfterSocketSettings.bOk
            && SocketState.Contains(TEXT("R=0.800000"))
            && SocketState.Contains(TEXT("Description=Updated socket")));

    const FString InitialSocketTransform = GetDelimitedField(
        SocketState,
        TEXT("InitialLocal="),
        TEXT("|Color="));
    FThomasAnimationOperation SetSocketTransform;
    SetSocketTransform.Action = TEXT("set_control_rig_socket_transform");
    SetSocketTransform.Name = SocketName;
    SetSocketTransform.ExpectedValue = InitialSocketTransform;
    SetSocketTransform.Location = FVector(21.0, 22.0, 23.0);
    SetSocketTransform.Rotation = FRotator(40.0, 50.0, 60.0);
    SetSocketTransform.Scale = FVector(0.9, 0.8, 0.7);
    FThomasSpecializedAssetInspectionResult AfterSocketTransform =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig socket transform"),
            SetSocketTransform,
            false);
    SocketState = GetSocketState(AfterSocketTransform, SocketName);
    Test.TestTrue(
        TEXT("Control Rig socket transform survives save and reload"),
        AfterSocketTransform.bOk
            && SocketState.Contains(
                TEXT("InitialLocal=21.000000,22.000000,23.000000"))
            && SocketState.Contains(TEXT("Description=Updated socket")));

    const FString RenamedSocketName = TEXT("TE_SpaceSocketRenamed");
    FThomasAnimationOperation RenameSocket;
    RenameSocket.Action = TEXT("rename_control_rig_socket");
    RenameSocket.Name = SocketName;
    RenameSocket.NewName = RenamedSocketName;
    RenameSocket.ExpectedValue = SocketState;
    FThomasSpecializedAssetInspectionResult AfterSocketRename =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig socket R2 rename"),
            RenameSocket,
            true);
    SocketState = GetSocketState(
        AfterSocketRename, RenamedSocketName);
    Test.TestTrue(
        TEXT("Control Rig socket rename survives save and reload"),
        AfterSocketRename.bOk
            && !SocketState.IsEmpty()
            && GetElementItem(
                AfterSocketRename, SocketName, TEXT("Socket")).IsEmpty());

    FThomasAnimationOperation RemoveSocket;
    RemoveSocket.Action = TEXT("remove_control_rig_socket");
    RemoveSocket.Name = RenamedSocketName;
    RemoveSocket.ExpectedValue = SocketState;
    FThomasSpecializedAssetInspectionResult AfterSocketRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig socket R2 remove"),
            RemoveSocket,
            true);
    Test.TestTrue(
        TEXT("Control Rig socket removal survives save and reload"),
        AfterSocketRemove.bOk
            && AfterSocketRemove.Metrics.Contains(TEXT("SocketCount=0"))
            && GetElementItem(
                AfterSocketRemove,
                RenamedSocketName,
                TEXT("Socket")).IsEmpty());

    FThomasAnimationOperation RemoveControl;
    RemoveControl.Action = TEXT("remove_control_rig_control");
    RemoveControl.Name = ControlName;
    RemoveControl.ControlType = TEXT("transform");
    RemoveControl.ExpectedValue = InitialControlValue;
    FThomasSpecializedAssetInspectionResult AfterControlRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig available-space control R2 remove"),
            RemoveControl,
            true);
    Test.TestTrue(
        TEXT("Control Rig socket and available-space cleanup survives reload"),
        AfterControlRemove.bOk
            && AfterControlRemove.Metrics.Contains(TEXT("ControlCount=0"))
            && AfterControlRemove.Metrics.Contains(TEXT("SocketCount=0"))
            && AfterControlRemove.Metrics.Contains(FString::Printf(
                TEXT("ElementCount=%d"), ExpectedControlRigBoneCount)));
}

static void RunControlRigMetadataMutationTests(
    FThomasEditorNativeCoreTest& Test,
    const FString& ControlRigPath,
    const FString& ControlRigRootBone,
    const TFunction<FThomasSpecializedAssetInspectionResult(
        const FString&,
        const FThomasAnimationOperation&,
        bool)>& ApplyControlRigOperationAndReload)
{
    struct FMetadataCase
    {
        FString Type;
        FString EnumName;
    };
    const TArray<FMetadataCase> Cases = {
        {TEXT("bool"), TEXT("Bool")},
        {TEXT("bool_array"), TEXT("BoolArray")},
        {TEXT("float"), TEXT("Float")},
        {TEXT("float_array"), TEXT("FloatArray")},
        {TEXT("integer"), TEXT("Int32")},
        {TEXT("integer_array"), TEXT("Int32Array")},
        {TEXT("name"), TEXT("Name")},
        {TEXT("name_array"), TEXT("NameArray")},
        {TEXT("vector"), TEXT("Vector")},
        {TEXT("vector_array"), TEXT("VectorArray")},
        {TEXT("rotator"), TEXT("Rotator")},
        {TEXT("rotator_array"), TEXT("RotatorArray")},
        {TEXT("quat"), TEXT("Quat")},
        {TEXT("quat_array"), TEXT("QuatArray")},
        {TEXT("transform"), TEXT("Transform")},
        {TEXT("transform_array"), TEXT("TransformArray")},
        {TEXT("color"), TEXT("LinearColor")},
        {TEXT("color_array"), TEXT("LinearColorArray")},
        {TEXT("element_key"), TEXT("RigElementKey")},
        {TEXT("element_key_array"), TEXT("RigElementKeyArray")}
    };
    auto ConfigureValue = [&](
        FThomasControlRigValueSpec& Value,
        const FString& Type)
    {
        if (Type == TEXT("bool"))
        {
            Value.bBoolValue = true;
        }
        else if (Type == TEXT("bool_array"))
        {
            Value.BoolArrayValue = {true, false, true};
        }
        else if (Type == TEXT("float"))
        {
            Value.FloatValue = 1.25f;
        }
        else if (Type == TEXT("float_array"))
        {
            Value.FloatArrayValue = {1.25f, -2.5f, 3.75f};
        }
        else if (Type == TEXT("integer"))
        {
            Value.IntegerValue = 42;
        }
        else if (Type == TEXT("integer_array"))
        {
            Value.IntegerArrayValue = {42, -7, 99};
        }
        else if (Type == TEXT("name"))
        {
            Value.NameValue = TEXT("Gameplay");
        }
        else if (Type == TEXT("name_array"))
        {
            Value.NameArrayValue = {
                TEXT("Gameplay"), TEXT("Presentation")};
        }
        else if (Type == TEXT("vector"))
        {
            Value.VectorValue = FVector(1.0, 2.0, 3.0);
        }
        else if (Type == TEXT("vector_array"))
        {
            Value.VectorArrayValue = {
                FVector(1.0, 2.0, 3.0),
                FVector(-4.0, 5.0, 6.0)};
        }
        else if (Type == TEXT("rotator"))
        {
            Value.Rotation = FRotator(10.0, 20.0, 30.0);
        }
        else if (Type == TEXT("rotator_array"))
        {
            Value.RotatorArrayValue = {
                FRotator(10.0, 20.0, 30.0),
                FRotator(-15.0, 25.0, 35.0)};
        }
        else if (Type == TEXT("quat"))
        {
            Value.Quaternion = FRotator(10.0, 20.0, 30.0).Quaternion();
        }
        else if (Type == TEXT("quat_array"))
        {
            Value.QuaternionArrayValue = {
                FRotator(10.0, 20.0, 30.0).Quaternion(),
                FRotator(-15.0, 25.0, 35.0).Quaternion()};
        }
        else if (Type == TEXT("transform"))
        {
            Value.Location = FVector(4.0, 5.0, 6.0);
            Value.Rotation = FRotator(15.0, 25.0, 35.0);
            Value.Scale = FVector(1.1, 1.2, 1.3);
        }
        else if (Type == TEXT("transform_array"))
        {
            Value.TransformArrayValue = {
                FTransform(
                    FRotator(15.0, 25.0, 35.0),
                    FVector(4.0, 5.0, 6.0),
                    FVector(1.1, 1.2, 1.3)),
                FTransform(
                    FRotator(-5.0, 10.0, 20.0),
                    FVector(-1.0, 2.0, 3.0),
                    FVector(0.9, 0.8, 0.7))};
        }
        else if (Type == TEXT("color"))
        {
            Value.ColorValue = FLinearColor(0.2f, 0.4f, 0.6f, 0.8f);
        }
        else if (Type == TEXT("color_array"))
        {
            Value.ColorArrayValue = {
                FLinearColor(0.2f, 0.4f, 0.6f, 0.8f),
                FLinearColor(1.0f, 0.5f, 0.25f, 1.0f)};
        }
        else if (Type == TEXT("element_key"))
        {
            Value.ElementName = ControlRigRootBone;
            Value.ElementType = TEXT("bone");
        }
        else if (Type == TEXT("element_key_array"))
        {
            Value.ElementNames = {
                ControlRigRootBone, ControlRigRootBone};
            Value.ElementTypes = {TEXT("bone"), TEXT("bone")};
        }
    };
    auto GetRootItem = [&](
        const FThomasSpecializedAssetInspectionResult& Inspection)
    {
        for (const FString& Item : Inspection.Items)
        {
            if (Item.Contains(
                TEXT("Name=") + ControlRigRootBone + TEXT(" Type=Bone")))
            {
                return Item;
            }
        }
        return FString();
    };
    auto GetMetadataEntry = [](
        const FString& Item,
        const FString& MetadataName)
    {
        const FString Marker = MetadataName + TEXT("{");
        const int32 MarkerStart = Item.Find(Marker);
        if (MarkerStart == INDEX_NONE)
        {
            return FString();
        }
        const int32 ValueStart = MarkerStart + Marker.Len();
        const int32 End = Item.Find(
            TEXT("}"),
            ESearchCase::CaseSensitive,
            ESearchDir::FromStart,
            ValueStart);
        return End == INDEX_NONE
            ? FString()
            : Item.Mid(ValueStart, End - ValueStart);
    };

    for (const FMetadataCase& Case : Cases)
    {
        const FString MetadataName =
            TEXT("TE_Meta_") + Case.EnumName;
        FThomasAnimationOperation SetMetadata;
        SetMetadata.Action = TEXT("set_control_rig_metadata");
        SetMetadata.Name = ControlRigRootBone;
        SetMetadata.ElementType = TEXT("bone");
        SetMetadata.MetadataName = MetadataName;
        SetMetadata.MetadataType = Case.Type;
        SetMetadata.ExpectedValue = TEXT("Missing");
        ConfigureValue(SetMetadata.MetadataValue, Case.Type);
        FThomasSpecializedAssetInspectionResult AfterSet =
            ApplyControlRigOperationAndReload(
                TEXT("Control Rig ") + Case.Type
                    + TEXT(" metadata set"),
                SetMetadata,
                false);
        const FString RootItem = GetRootItem(AfterSet);
        const FString MetadataEntry = GetMetadataEntry(
            RootItem, MetadataName);
        Test.AddInfo(FString::Printf(
            TEXT("Control Rig metadata readback name=%s entry=%s"),
            *MetadataName,
            *MetadataEntry));
        Test.TestTrue(
            TEXT("Control Rig ") + Case.Type
                + TEXT(" metadata survives save and reload"),
            AfterSet.bOk
                && MetadataEntry.StartsWith(
                    TEXT("Type=") + Case.EnumName + TEXT("|Value=")));

        if (Case.Type == TEXT("float_array"))
        {
            FThomasAnimationOperation OversizedMetadata = SetMetadata;
            OversizedMetadata.ExpectedValue = MetadataEntry;
            OversizedMetadata.MetadataValue.FloatArrayValue.Init(
                1.0f, 17);
            FThomasAnimationPatchRequest OversizedRequest;
            OversizedRequest.AssetPath = ControlRigPath;
            OversizedRequest.ExpectedRevision = AfterSet.Revision;
            OversizedRequest.Operations.Add(OversizedMetadata);
            Test.TestEqual(
                TEXT("Control Rig rejects metadata arrays over 16 items"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    OversizedRequest).Code,
                FString(TEXT("invalid_control_rig_metadata_value")));
        }

        if (Case.Type == TEXT("quat"))
        {
            FThomasAnimationOperation InvalidQuat = SetMetadata;
            InvalidQuat.ExpectedValue = MetadataEntry;
            InvalidQuat.MetadataValue.Quaternion = FQuat(0, 0, 0, 0);
            FThomasAnimationPatchRequest InvalidQuatRequest;
            InvalidQuatRequest.AssetPath = ControlRigPath;
            InvalidQuatRequest.ExpectedRevision = AfterSet.Revision;
            InvalidQuatRequest.Operations.Add(InvalidQuat);
            Test.TestEqual(
                TEXT("Control Rig rejects a zero metadata quaternion"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    InvalidQuatRequest).Code,
                FString(TEXT("invalid_control_rig_metadata_value")));
        }

        if (Case.Type == TEXT("float"))
        {
            FThomasAnimationOperation StaleMetadata = SetMetadata;
            StaleMetadata.ExpectedValue = TEXT("stale");
            FThomasAnimationPatchRequest StaleRequest;
            StaleRequest.AssetPath = ControlRigPath;
            StaleRequest.ExpectedRevision = AfterSet.Revision;
            StaleRequest.Operations.Add(StaleMetadata);
            Test.TestEqual(
                TEXT("Control Rig metadata rejects stale expected value"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    StaleRequest).Code,
                FString(TEXT("control_rig_metadata_conflict")));
        }

        FThomasAnimationOperation RemoveMetadata;
        RemoveMetadata.Action = TEXT("remove_control_rig_metadata");
        RemoveMetadata.Name = ControlRigRootBone;
        RemoveMetadata.ElementType = TEXT("bone");
        RemoveMetadata.MetadataName = MetadataName;
        RemoveMetadata.MetadataType = Case.Type;
        RemoveMetadata.ExpectedValue = MetadataEntry;
        FThomasSpecializedAssetInspectionResult AfterRemove =
            ApplyControlRigOperationAndReload(
                TEXT("Control Rig ") + Case.Type
                    + TEXT(" metadata R2 remove"),
                RemoveMetadata,
                true);
        Test.TestTrue(
            TEXT("Control Rig ") + Case.Type
                + TEXT(" metadata removal survives reload"),
            AfterRemove.bOk
                && GetMetadataEntry(
                    GetRootItem(AfterRemove), MetadataName).IsEmpty());
    }
}

static void RunControlRigGraphMutationTests(
    FThomasEditorNativeCoreTest& Test,
    const FString& ControlRigPath,
    const FString& ExternalControlRigPath,
    const TFunction<FThomasSpecializedAssetInspectionResult(
        const FString&,
        const FThomasAnimationOperation&,
        bool)>& ApplyControlRigOperationAndReload)
{
    auto ApplyExternalControlRigOperationAndReload =
        [&Test, &ExternalControlRigPath](
            const FString& Label,
            const FThomasAnimationOperation& Operation,
            const bool bConfirmDestructive)
    {
        const FThomasSpecializedAssetInspectionResult Before =
            UThomasDomainsToolset::InspectAnimationAsset(
                ExternalControlRigPath, 1000);
        FThomasAnimationPatchRequest Request;
        Request.AssetPath = ExternalControlRigPath;
        Request.ExpectedRevision = Before.Revision;
        Request.bConfirmDestructive = bConfirmDestructive;
        Request.Operations.Add(Operation);
        const FThomasAnimationPlanResult Plan =
            UThomasDomainsToolset::PlanAnimationPatch(Request);
        Test.TestTrue(Label + TEXT(" plan succeeds (code=")
                + Plan.Code + TEXT(" message=")
                + Plan.Message + TEXT(")"),
            Plan.bOk);
        if (!Plan.bOk)
        {
            return FThomasSpecializedAssetInspectionResult();
        }
        const FThomasAnimationApplyResult Apply =
            UThomasDomainsToolset::ApplyAnimationPlan(
                Plan.PlanId, true);
        Test.TestTrue(Label + TEXT(" apply saves (code=")
                + Apply.Code + TEXT(" message=")
                + Apply.Message + TEXT(")"),
            Apply.bOk && Apply.bSaved);
        if (!Apply.bOk || !Apply.bSaved)
        {
            return FThomasSpecializedAssetInspectionResult();
        }
        UObject* ReloadAsset = LoadObject<UObject>(
            nullptr,
            *(ExternalControlRigPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    ExternalControlRigPath)));
        FText ReloadError;
        Test.TestTrue(Label + TEXT(" reloads from saved file"),
            ReloadAsset
                && UPackageTools::ReloadPackages(
                    {ReloadAsset->GetOutermost()},
                    ReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        FThomasSpecializedAssetInspectionResult Inspection =
            UThomasDomainsToolset::InspectAnimationAsset(
                ExternalControlRigPath, 1000);
        TArray<FString> TestElements;
        for (const FString& Item : Inspection.Items)
        {
            if (Item.Contains(TEXT("TE_")))
            {
                TestElements.Add(Item);
            }
        }
        Test.AddInfo(Label + TEXT(" readback metrics=")
            + FString::Join(Inspection.Metrics, TEXT(";"))
            + TEXT(" elements=")
            + FString::Join(TestElements, TEXT(";")));
        return Inspection;
    };

    auto FindNodeItem = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& NodeName)
    {
        const FString Marker = TEXT(" Name=") + NodeName + TEXT(" State=");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(TEXT("ControlRigNode Graph="))
                && Item.Contains(Marker))
            {
                return Item;
            }
        }
        return FString();
    };
    auto FindNodeItemInGraph = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& GraphName,
        const FString& NodeName)
    {
        const FString Marker = TEXT("ControlRigNode Graph=")
            + GraphName + TEXT(" Name=") + NodeName + TEXT(" State=");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(Marker))
            {
                return Item;
            }
        }
        return FString();
    };
    auto FindMemberVariableItem = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& Name)
    {
        const FString Marker = TEXT("ControlRigMemberVariable ");
        const FString NameMarker = TEXT(" Name=") + Name + TEXT(" ");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(Marker) && Item.Contains(NameMarker))
            {
                return Item;
            }
        }
        return FString();
    };
    auto FindFunctionItem = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& Name)
    {
        const FString Marker = TEXT("ControlRigFunction Name=")
            + Name + TEXT(" State=");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(Marker))
            {
                return Item;
            }
        }
        return FString();
    };
    auto FindLocalVariableItem = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& Graph,
        const FString& Name)
    {
        const FString Marker = TEXT("ControlRigLocalVariable Graph=")
            + Graph + TEXT(" Name=") + Name + TEXT(" State=");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(Marker))
            {
                return Item;
            }
        }
        return FString();
    };
    auto FindFunctionPinItem = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& Function,
        const FString& Name)
    {
        const FString Marker = TEXT("ControlRigFunctionPin Function=")
            + Function + TEXT(" Name=") + Name + TEXT(" State=");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(Marker))
            {
                return Item;
            }
        }
        return FString();
    };
    auto FindGraphItem = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& GraphName)
    {
        const FString Marker = TEXT("ControlRigGraph Name=")
            + GraphName + TEXT(" ");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(Marker)
                || (Item.StartsWith(TEXT("ControlRigGraph Name="))
                    && Item.Contains(GraphName)))
            {
                return Item;
            }
        }
        return FString();
    };
    auto GraphState = [&](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& GraphName)
    {
        const FString Item = FindGraphItem(Inspection, GraphName);
        const FString Marker = TEXT(" State=");
        const int32 Start = Item.Find(Marker);
        return Start == INDEX_NONE
            ? FString()
            : Item.Mid(Start + Marker.Len());
    };
    auto FunctionPinState = [&](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& Function,
        const FString& Name)
    {
        const FString Item = FindFunctionPinItem(
            Inspection, Function, Name);
        const FString Marker = TEXT(" State=");
        const int32 Start = Item.Find(Marker);
        return Start == INDEX_NONE
            ? FString()
            : Item.Mid(Start + Marker.Len());
    };
    auto NodeState = [&](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& NodeName)
    {
        const FString Item = FindNodeItem(Inspection, NodeName);
        const FString Marker = TEXT(" State=");
        const int32 Start = Item.Find(Marker);
        return Start == INDEX_NONE
            ? FString()
            : Item.Mid(Start + Marker.Len());
    };
    auto NodeStateInGraph = [&](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& GraphName,
        const FString& NodeName)
    {
        const FString Item = FindNodeItemInGraph(
            Inspection, GraphName, NodeName);
        const FString Marker = TEXT(" State=");
        const int32 Start = Item.Find(Marker);
        return Start == INDEX_NONE
            ? FString()
            : Item.Mid(Start + Marker.Len());
    };
    auto FindPinItem = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& PinPath)
    {
        const FString Marker = TEXT(" Path=") + PinPath + TEXT(" ");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(TEXT("ControlRigPin Graph="))
                && Item.Contains(Marker))
            {
                return Item;
            }
        }
        return FString();
    };
    auto PinDefault = [&](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& PinPath)
    {
        const FString Item = FindPinItem(Inspection, PinPath);
        const FString StartMarker = TEXT(" Default=");
        const FString EndMarker = TEXT(" SourceLinks=");
        const int32 Start = Item.Find(StartMarker);
        const int32 End = Item.Find(EndMarker);
        return Start == INDEX_NONE || End == INDEX_NONE || End <= Start
            ? FString()
            : Item.Mid(
                Start + StartMarker.Len(),
                End - Start - StartMarker.Len());
    };
    auto HasLink = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& SourcePinPath,
        const FString& TargetPinPath)
    {
        return Inspection.Items.ContainsByPredicate(
            [&](const FString& Item)
            {
                return Item.StartsWith(TEXT("ControlRigLink Graph="))
                    && Item.Contains(TEXT(" Source=") + SourcePinPath)
                    && Item.Contains(TEXT(" Target=") + TargetPinPath);
            });
    };
    auto GetDelimitedValue = [](
        const FString& Item,
        const FString& StartMarker,
        const FString& EndMarker)
    {
        const int32 Start = Item.Find(StartMarker);
        if (Start == INDEX_NONE)
        {
            return FString();
        }
        const int32 ValueStart = Start + StartMarker.Len();
        const int32 End = EndMarker.IsEmpty()
            ? Item.Len()
            : Item.Find(EndMarker, ESearchCase::CaseSensitive,
                ESearchDir::FromStart, ValueStart);
        return End == INDEX_NONE || End < ValueStart
            ? FString()
            : Item.Mid(ValueStart, End - ValueStart);
    };
    auto AggregatePinNames = [&](const FString& NodeStateValue)
    {
        TArray<FString> Names;
        const FString Value = GetDelimitedValue(
            NodeStateValue,
            TEXT("|AggregatePins="),
            TEXT("|AggregatePinCount="));
        Value.ParseIntoArray(Names, TEXT(","), true);
        return Names;
    };
    auto FindEditableArrayPinItem = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& NodeName)
    {
        const FString PathPrefix = TEXT(" Path=")
            + NodeName + TEXT(".");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(TEXT("ControlRigPin Graph="))
                && Item.Contains(PathPrefix)
                && (Item.Contains(TEXT(" Direction=Input "))
                    || Item.Contains(TEXT(" Direction=IO ")))
                && Item.Contains(TEXT(" IsDynamicArray=true ")))
            {
                return Item;
            }
        }
        return FString();
    };
    auto FindWildcardPinItem = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& NodeName)
    {
        const FString PathPrefix = TEXT(" Path=")
            + NodeName + TEXT(".");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(TEXT("ControlRigPin Graph="))
                && Item.Contains(PathPrefix)
                && (Item.Contains(TEXT(" Direction=Input "))
                    || Item.Contains(TEXT(" Direction=IO ")))
                && Item.Contains(TEXT(" IsWildCard=true ")))
            {
                return Item;
            }
        }
        return FString();
    };
    auto ArrayStateSize = [](const FString& State)
    {
        const int32 End = State.Find(TEXT("|Defaults="));
        return State.StartsWith(TEXT("Size=")) && End != INDEX_NONE
            ? FCString::Atoi(*State.Mid(5, End - 5))
            : INDEX_NONE;
    };

    const FString AuxiliaryGraphName = TEXT("TE_AuxiliaryGraph");
    FThomasAnimationOperation AddAuxiliaryGraph;
    AddAuxiliaryGraph.Action = TEXT("add_control_rig_graph");
    AddAuxiliaryGraph.Name = AuxiliaryGraphName;
    FThomasSpecializedAssetInspectionResult AfterAuxiliaryGraphAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add top-level graph"),
            AddAuxiliaryGraph,
            false);
    const FString AuxiliaryGraphItem = FindGraphItem(
        AfterAuxiliaryGraphAdd,
        AuxiliaryGraphName);
    const FString AuxiliaryGraphIdentifier = GetDelimitedValue(
        AuxiliaryGraphItem,
        TEXT("ControlRigGraph Name="),
        TEXT(" NodeCount="));
    Test.TestTrue(TEXT("Control Rig top-level graph survives reload"),
        AfterAuxiliaryGraphAdd.bOk
            && AfterAuxiliaryGraphAdd.Metrics.Contains(
                TEXT("RigVMGraphCount=3"))
            && !AuxiliaryGraphIdentifier.IsEmpty()
            && AuxiliaryGraphItem.Contains(
                    TEXT("NodeCount=0 LinkCount=0 LocalVariableCount=0"))
            && AuxiliaryGraphItem.Contains(TEXT("|ContentHash=")));

    FThomasAnimationPatchRequest DuplicateAuxiliaryGraphRequest;
    DuplicateAuxiliaryGraphRequest.AssetPath = ControlRigPath;
    DuplicateAuxiliaryGraphRequest.ExpectedRevision =
        AfterAuxiliaryGraphAdd.Revision;
    DuplicateAuxiliaryGraphRequest.Operations.Add(AddAuxiliaryGraph);
    Test.TestEqual(TEXT("Control Rig top-level graph rejects duplicate"),
        UThomasDomainsToolset::PlanAnimationPatch(
            DuplicateAuxiliaryGraphRequest).Code,
        FString(TEXT("control_rig_graph_exists")));

    const FString AuxiliaryCommentName = TEXT("TE_AuxiliaryComment");
    FThomasAnimationOperation AddAuxiliaryComment;
    AddAuxiliaryComment.Action = TEXT("add_control_rig_comment_node");
    AddAuxiliaryComment.RigVMGraphName = AuxiliaryGraphIdentifier;
    AddAuxiliaryComment.Name = AuxiliaryCommentName;
    AddAuxiliaryComment.RigVMCommentText =
        TEXT("Top-level graph addressability");
    AddAuxiliaryComment.PositionX = 80;
    AddAuxiliaryComment.PositionY = 120;
    AddAuxiliaryComment.RigVMNodeSize = FVector2D(420.0, 180.0);
    AddAuxiliaryComment.Color = FLinearColor(0.15f, 0.25f, 0.35f, 1.0f);
    FThomasSpecializedAssetInspectionResult AfterAuxiliaryCommentAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM author top-level graph"),
            AddAuxiliaryComment,
            false);
    Test.TestTrue(TEXT("Control Rig top-level graph is addressable"),
        AfterAuxiliaryCommentAdd.bOk
            && FindGraphItem(
                AfterAuxiliaryCommentAdd,
                AuxiliaryGraphIdentifier).Contains(TEXT("NodeCount=1"))
            && NodeStateInGraph(
                AfterAuxiliaryCommentAdd,
                AuxiliaryGraphIdentifier,
                AuxiliaryCommentName).Contains(
                    TEXT("Comment=Text=Top-level graph addressability")));

    const FString RenamedAuxiliaryGraphName =
        TEXT("TE_AuxiliaryGraphRenamed");
    FThomasAnimationOperation RenameAuxiliaryGraph;
    RenameAuxiliaryGraph.Action = TEXT("rename_control_rig_graph");
    RenameAuxiliaryGraph.RigVMGraphName = AuxiliaryGraphIdentifier;
    RenameAuxiliaryGraph.NewName = RenamedAuxiliaryGraphName;
    RenameAuxiliaryGraph.ExpectedValue = GraphState(
        AfterAuxiliaryCommentAdd,
        AuxiliaryGraphIdentifier);
    FThomasAnimationPatchRequest UnconfirmedAuxiliaryGraphRenameRequest;
    UnconfirmedAuxiliaryGraphRenameRequest.AssetPath = ControlRigPath;
    UnconfirmedAuxiliaryGraphRenameRequest.ExpectedRevision =
        AfterAuxiliaryCommentAdd.Revision;
    UnconfirmedAuxiliaryGraphRenameRequest.Operations.Add(
        RenameAuxiliaryGraph);
    Test.TestEqual(TEXT("Control Rig top-level graph rename requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedAuxiliaryGraphRenameRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasAnimationPatchRequest StaleAuxiliaryGraphRenameRequest =
        UnconfirmedAuxiliaryGraphRenameRequest;
    StaleAuxiliaryGraphRenameRequest.bConfirmDestructive = true;
    StaleAuxiliaryGraphRenameRequest.Operations[0].ExpectedValue +=
        TEXT("|Stale=true");
    Test.TestEqual(TEXT("Control Rig top-level graph rename rejects stale state"),
        UThomasDomainsToolset::PlanAnimationPatch(
            StaleAuxiliaryGraphRenameRequest).Code,
        FString(TEXT("control_rig_graph_conflict")));
    FThomasSpecializedAssetInspectionResult AfterAuxiliaryGraphRename =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 rename populated top-level graph"),
            RenameAuxiliaryGraph,
            true);
    const FString RenamedAuxiliaryGraphItem = FindGraphItem(
        AfterAuxiliaryGraphRename,
        RenamedAuxiliaryGraphName);
    const FString RenamedAuxiliaryGraphIdentifier = GetDelimitedValue(
        RenamedAuxiliaryGraphItem,
        TEXT("ControlRigGraph Name="),
        TEXT(" NodeCount="));
    Test.TestTrue(TEXT("Control Rig populated top-level graph rename survives reload"),
        AfterAuxiliaryGraphRename.bOk
            && AfterAuxiliaryGraphRename.Metrics.Contains(
                TEXT("RigVMGraphCount=3"))
            && !RenamedAuxiliaryGraphIdentifier.IsEmpty()
            && FindGraphItem(
                AfterAuxiliaryGraphRename,
                AuxiliaryGraphIdentifier).IsEmpty()
            && RenamedAuxiliaryGraphItem.Contains(TEXT("NodeCount=1"))
            && NodeStateInGraph(
                AfterAuxiliaryGraphRename,
                RenamedAuxiliaryGraphIdentifier,
                AuxiliaryCommentName).Contains(
                    TEXT("Comment=Text=Top-level graph addressability")));

    FThomasAnimationOperation RemoveAuxiliaryGraph;
    RemoveAuxiliaryGraph.Action = TEXT("remove_control_rig_graph");
    RemoveAuxiliaryGraph.RigVMGraphName =
        RenamedAuxiliaryGraphIdentifier;
    RemoveAuxiliaryGraph.ExpectedValue = GraphState(
        AfterAuxiliaryGraphRename,
        RenamedAuxiliaryGraphIdentifier);
    FThomasAnimationPatchRequest UnconfirmedAuxiliaryGraphRemoveRequest;
    UnconfirmedAuxiliaryGraphRemoveRequest.AssetPath = ControlRigPath;
    UnconfirmedAuxiliaryGraphRemoveRequest.ExpectedRevision =
        AfterAuxiliaryGraphRename.Revision;
    UnconfirmedAuxiliaryGraphRemoveRequest.Operations.Add(
        RemoveAuxiliaryGraph);
    Test.TestEqual(TEXT("Control Rig top-level graph removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedAuxiliaryGraphRemoveRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterAuxiliaryGraphRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove populated top-level graph"),
            RemoveAuxiliaryGraph,
            true);
    Test.TestTrue(TEXT("Control Rig top-level graph cleanup survives reload"),
        AfterAuxiliaryGraphRemove.bOk
            && AfterAuxiliaryGraphRemove.Metrics.Contains(
                TEXT("RigVMGraphCount=2"))
            && FindGraphItem(
                AfterAuxiliaryGraphRemove,
                RenamedAuxiliaryGraphIdentifier).IsEmpty()
            && FindNodeItemInGraph(
                AfterAuxiliaryGraphRemove,
                RenamedAuxiliaryGraphIdentifier,
                AuxiliaryCommentName).IsEmpty());

    const FString CommentName = TEXT("TE_RigVMComment");
    const FString RenamedCommentName = TEXT("TE_RigVMCommentVerified");
    FThomasAnimationOperation AddComment;
    AddComment.Action = TEXT("add_control_rig_comment_node");
    AddComment.Name = CommentName;
    AddComment.RigVMCommentText = TEXT("RigVM authoring");
    AddComment.PositionX = 20;
    AddComment.PositionY = 40;
    AddComment.RigVMNodeSize = FVector2D(500.0, 220.0);
    AddComment.Color = FLinearColor(0.1f, 0.2f, 0.3f, 1.0f);
    AddComment.RigVMCommentFontSize = 18;
    FThomasSpecializedAssetInspectionResult AfterCommentAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add comment node"),
            AddComment,
            false);
    Test.TestTrue(TEXT("Control Rig RigVM comment survives reload"),
        AfterCommentAdd.bOk
            && AfterCommentAdd.Metrics.Contains(TEXT("RigVMNodeCount=1"))
            && NodeState(AfterCommentAdd, CommentName).Contains(
                TEXT("Class=/Script/RigVMDeveloper.RigVMCommentNode"))
            && NodeState(AfterCommentAdd, CommentName).Contains(
                TEXT("Size=X=500.000000,Y=220.000000"))
            && NodeState(AfterCommentAdd, CommentName).Contains(
                TEXT("Color=R=0.100000,G=0.200000,B=0.300000,A=1.000000"))
            && NodeState(AfterCommentAdd, CommentName).Contains(
                TEXT("Comment=Text=RigVM authoring|FontSize=18")));

    FThomasAnimationOperation SetComment = AddComment;
    SetComment.Action = TEXT("set_control_rig_comment");
    SetComment.ExpectedValue = NodeState(AfterCommentAdd, CommentName);
    SetComment.RigVMCommentText = TEXT("RigVM authoring verified");
    SetComment.RigVMCommentFontSize = 22;
    SetComment.bRigVMCommentBubbleVisible = true;
    SetComment.bRigVMCommentColorBubble = true;
    FThomasSpecializedAssetInspectionResult AfterCommentSet =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM set comment"),
            SetComment,
            false);
    Test.TestTrue(TEXT("Control Rig RigVM comment properties survive reload"),
        NodeState(AfterCommentSet, CommentName).Contains(
            TEXT("Comment=Text=RigVM authoring verified|FontSize=22|BubbleVisible=true|ColorBubble=true")));

    FThomasAnimationOperation StaleComment = SetComment;
    StaleComment.RigVMCommentText = TEXT("stale");
    FThomasAnimationPatchRequest StaleCommentRequest;
    StaleCommentRequest.AssetPath = ControlRigPath;
    StaleCommentRequest.ExpectedRevision = AfterCommentSet.Revision;
    StaleCommentRequest.Operations.Add(StaleComment);
    Test.TestEqual(TEXT("Control Rig RigVM comment rejects stale state"),
        UThomasDomainsToolset::PlanAnimationPatch(StaleCommentRequest).Code,
        FString(TEXT("control_rig_comment_node_conflict")));

    FThomasAnimationOperation SetCommentSize;
    SetCommentSize.Action = TEXT("set_control_rig_node_size");
    SetCommentSize.Name = CommentName;
    SetCommentSize.ExpectedValue = TEXT("X=500.000000,Y=220.000000");
    SetCommentSize.RigVMNodeSize = FVector2D(540.0, 260.0);
    FThomasSpecializedAssetInspectionResult AfterCommentSize =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM set node size"),
            SetCommentSize,
            false);
    Test.TestTrue(TEXT("Control Rig RigVM node size survives reload"),
        NodeState(AfterCommentSize, CommentName).Contains(
            TEXT("Size=X=540.000000,Y=260.000000")));

    FThomasAnimationOperation SetCommentColor;
    SetCommentColor.Action = TEXT("set_control_rig_node_color");
    SetCommentColor.Name = CommentName;
    SetCommentColor.ExpectedValue =
        TEXT("R=0.100000,G=0.200000,B=0.300000,A=1.000000");
    SetCommentColor.Color = FLinearColor(0.4f, 0.5f, 0.6f, 1.0f);
    FThomasSpecializedAssetInspectionResult AfterCommentColor =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM set node color"),
            SetCommentColor,
            false);
    Test.TestTrue(TEXT("Control Rig RigVM node color survives reload"),
        NodeState(AfterCommentColor, CommentName).Contains(
            TEXT("Color=R=0.400000,G=0.500000,B=0.600000,A=1.000000")));

    FThomasAnimationOperation RenameComment;
    RenameComment.Action = TEXT("rename_control_rig_comment_node");
    RenameComment.Name = CommentName;
    RenameComment.NewName = RenamedCommentName;
    RenameComment.ExpectedValue = NodeState(AfterCommentColor, CommentName);
    FThomasSpecializedAssetInspectionResult AfterCommentRename =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 rename comment"),
            RenameComment,
            true);
    Test.TestTrue(TEXT("Control Rig RigVM comment rename survives reload"),
        FindNodeItem(AfterCommentRename, CommentName).IsEmpty()
            && !FindNodeItem(
                AfterCommentRename, RenamedCommentName).IsEmpty());

    FThomasAnimationOperation RemoveComment;
    RemoveComment.Action = TEXT("remove_control_rig_comment_node");
    RemoveComment.Name = RenamedCommentName;
    RemoveComment.ExpectedValue =
        NodeState(AfterCommentRename, RenamedCommentName);
    FThomasAnimationPatchRequest UnconfirmedCommentRemoveRequest;
    UnconfirmedCommentRemoveRequest.AssetPath = ControlRigPath;
    UnconfirmedCommentRemoveRequest.ExpectedRevision =
        AfterCommentRename.Revision;
    UnconfirmedCommentRemoveRequest.Operations.Add(RemoveComment);
    Test.TestEqual(TEXT("Control Rig RigVM comment removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedCommentRemoveRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterCommentRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove comment"),
            RemoveComment,
            true);
    Test.TestTrue(TEXT("Control Rig RigVM comment cleanup survives reload"),
        AfterCommentRemove.bOk
            && AfterCommentRemove.Metrics.Contains(TEXT("RigVMNodeCount=0"))
            && FindNodeItem(
                AfterCommentRemove, RenamedCommentName).IsEmpty());

    const FString AddNodeName = TEXT("TE_RigVMAdd");
    const FString MultiplyNodeName = TEXT("TE_RigVMMultiply");
    FThomasAnimationOperation AddNode;
    AddNode.Action = TEXT("add_control_rig_unit_node");
    AddNode.Name = AddNodeName;
    AddNode.ClassPath = TEXT("/Script/ControlRig.RigUnit_Add_FloatFloat");
    AddNode.RigVMMethodName = TEXT("Execute");
    AddNode.PositionX = 100;
    AddNode.PositionY = 200;
    FThomasSpecializedAssetInspectionResult AfterAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add unit node"),
            AddNode,
            false);
    Test.TestTrue(TEXT("Control Rig RigVM unit node survives reload"),
        AfterAdd.bOk
            && AfterAdd.Metrics.Contains(TEXT("RigVMNodeCount=1"))
            && !FindNodeItem(AfterAdd, AddNodeName).IsEmpty()
            && NodeState(AfterAdd, AddNodeName).Contains(
                TEXT("Struct=/Script/ControlRig.RigUnit_Add_FloatFloat"))
            && NodeState(AfterAdd, AddNodeName).Contains(
                TEXT("Position=X=100.000000,Y=200.000000")));

    FThomasAnimationOperation AddMultiply = AddNode;
    AddMultiply.Name = MultiplyNodeName;
    AddMultiply.ClassPath =
        TEXT("/Script/ControlRig.RigUnit_Multiply_FloatFloat");
    AddMultiply.PositionX = 400;
    AddMultiply.PositionY = 200;
    FThomasSpecializedAssetInspectionResult AfterMultiplyAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add second unit node"),
            AddMultiply,
            false);
    Test.TestTrue(TEXT("Second Control Rig RigVM unit survives reload"),
        AfterMultiplyAdd.bOk
            && AfterMultiplyAdd.Metrics.Contains(TEXT("RigVMNodeCount=2"))
            && !FindNodeItem(
                AfterMultiplyAdd, MultiplyNodeName).IsEmpty());

    const FString AddInputPin = AddNodeName + TEXT(".Argument0");
    const FString AddResultPin = AddNodeName + TEXT(".Result");
    const FString MultiplyInputPin = MultiplyNodeName + TEXT(".Argument0");
    const FString InitialAddDefault = PinDefault(
        AfterMultiplyAdd, AddInputPin);
    Test.TestTrue(TEXT("Control Rig RigVM inspection exposes exact pin default"),
        !InitialAddDefault.IsEmpty());

    const FString FreeRerouteName = TEXT("TE_RigVMFreeReroute");
    const FString FreeReroutePinPath =
        FreeRerouteName + TEXT(".Value");
    const FString AddResultTypeState = GetDelimitedValue(
        FindPinItem(AfterMultiplyAdd, AddResultPin),
        TEXT(" TypeState="), TEXT(" ArrayState="));
    FThomasAnimationOperation AddFreeReroute;
    AddFreeReroute.Action = TEXT("add_control_rig_free_reroute_node");
    AddFreeReroute.Name = FreeRerouteName;
    AddFreeReroute.RigVMType = TEXT("float");
    AddFreeReroute.RigVMPinDefaultValue = TEXT("3.500000");
    AddFreeReroute.RigVMCustomWidgetName = TEXT("TE_TestWidget");
    AddFreeReroute.RigVMSourcePinPath = AddResultPin;
    AddFreeReroute.ExpectedValue = AddResultTypeState;
    AddFreeReroute.PositionX = 300;
    AddFreeReroute.PositionY = 100;
    FThomasSpecializedAssetInspectionResult AfterFreeRerouteAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add typed free reroute"),
            AddFreeReroute,
            false);
    Test.TestTrue(TEXT("Control Rig typed free reroute survives reload"),
        AfterFreeRerouteAdd.bOk
            && AfterFreeRerouteAdd.Metrics.Contains(
                TEXT("RigVMNodeCount=3"))
            && AfterFreeRerouteAdd.Metrics.Contains(
                TEXT("RigVMLinkCount=1"))
            && NodeState(
                AfterFreeRerouteAdd, FreeRerouteName).Contains(
                    TEXT("|Reroute=true|RerouteType=float"))
            && NodeState(
                AfterFreeRerouteAdd, FreeRerouteName).Contains(
                    TEXT("|RerouteConstant=false"))
            && NodeState(
                AfterFreeRerouteAdd, FreeRerouteName).Contains(
                    TEXT("|RerouteWidget=TE_TestWidget"))
            && NodeState(
                AfterFreeRerouteAdd, FreeRerouteName).Contains(
                    TEXT("|RerouteDefault=3.500000"))
            && FindPinItem(
                AfterFreeRerouteAdd, FreeReroutePinPath).Contains(
                    TEXT(" CPPType=float Default=3.500000 "))
            && HasLink(
                AfterFreeRerouteAdd,
                AddResultPin,
                FreeReroutePinPath));

    FThomasAnimationOperation RemoveFreeReroute;
    RemoveFreeReroute.Action = TEXT("remove_control_rig_reroute_node");
    RemoveFreeReroute.Name = FreeRerouteName;
    RemoveFreeReroute.ExpectedValue = NodeState(
        AfterFreeRerouteAdd, FreeRerouteName);
    FThomasSpecializedAssetInspectionResult AfterFreeRerouteRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove typed free reroute"),
            RemoveFreeReroute,
            true);
    Test.TestTrue(TEXT("Control Rig typed free reroute cleanup survives reload"),
        AfterFreeRerouteRemove.bOk
            && AfterFreeRerouteRemove.Metrics.Contains(
                TEXT("RigVMNodeCount=2"))
            && AfterFreeRerouteRemove.Metrics.Contains(
                TEXT("RigVMLinkCount=0"))
            && FindNodeItem(
                AfterFreeRerouteRemove, FreeRerouteName).IsEmpty());

    FThomasAnimationOperation SetPinDefault;
    SetPinDefault.Action = TEXT("set_control_rig_pin_default");
    SetPinDefault.RigVMPinPath = AddInputPin;
    SetPinDefault.ExpectedValue = InitialAddDefault;
    SetPinDefault.RigVMPinDefaultValue = TEXT("1.250000");
    FThomasSpecializedAssetInspectionResult AfterPinDefault =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM pin default"),
            SetPinDefault,
            false);
    Test.TestEqual(TEXT("Control Rig RigVM pin default survives reload"),
        PinDefault(AfterPinDefault, AddInputPin),
        FString(TEXT("1.250000")));

    FThomasAnimationOperation StalePinDefault = SetPinDefault;
    StalePinDefault.ExpectedValue = InitialAddDefault;
    StalePinDefault.RigVMPinDefaultValue = TEXT("2.500000");
    FThomasAnimationPatchRequest StalePinRequest;
    StalePinRequest.AssetPath = ControlRigPath;
    StalePinRequest.ExpectedRevision = AfterPinDefault.Revision;
    StalePinRequest.Operations.Add(StalePinDefault);
    Test.TestEqual(TEXT("Control Rig RigVM pin edit rejects stale default"),
        UThomasDomainsToolset::PlanAnimationPatch(StalePinRequest).Code,
        FString(TEXT("control_rig_pin_default_conflict")));

    FThomasAnimationOperation MoveMultiply;
    MoveMultiply.Action = TEXT("set_control_rig_node_position");
    MoveMultiply.Name = MultiplyNodeName;
    MoveMultiply.ExpectedValue = TEXT("X=400.000000,Y=200.000000");
    MoveMultiply.PositionX = 450;
    MoveMultiply.PositionY = 250;
    FThomasSpecializedAssetInspectionResult AfterMove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM node position"),
            MoveMultiply,
            false);
    Test.TestTrue(TEXT("Control Rig RigVM node position survives reload"),
        NodeState(AfterMove, MultiplyNodeName).Contains(
            TEXT("Position=X=450.000000,Y=250.000000")));

    FThomasAnimationOperation AddLink;
    AddLink.Action = TEXT("add_control_rig_link");
    AddLink.RigVMSourcePinPath = AddResultPin;
    AddLink.RigVMTargetPinPath = MultiplyInputPin;
    FThomasSpecializedAssetInspectionResult AfterLink =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add link"),
            AddLink,
            false);
    Test.TestTrue(TEXT("Control Rig RigVM link survives reload"),
        AfterLink.Metrics.Contains(TEXT("RigVMLinkCount=1"))
            && HasLink(AfterLink, AddResultPin, MultiplyInputPin));

    const FString RerouteNodeName = TEXT("TE_RigVMReroute");
    FThomasAnimationOperation AddReroute;
    AddReroute.Action = TEXT("add_control_rig_reroute_node_on_link");
    AddReroute.Name = RerouteNodeName;
    AddReroute.RigVMSourcePinPath = AddResultPin;
    AddReroute.RigVMTargetPinPath = MultiplyInputPin;
    AddReroute.ExpectedValue =
        AddResultPin + TEXT(" -> ") + MultiplyInputPin;
    AddReroute.PositionX = 300;
    AddReroute.PositionY = 300;
    FThomasAnimationPatchRequest UnconfirmedRerouteRequest;
    UnconfirmedRerouteRequest.AssetPath = ControlRigPath;
    UnconfirmedRerouteRequest.ExpectedRevision = AfterLink.Revision;
    UnconfirmedRerouteRequest.Operations.Add(AddReroute);
    Test.TestEqual(TEXT("Control Rig RigVM reroute insertion requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedRerouteRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterRerouteAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 add reroute on link"),
            AddReroute,
            true);
    Test.TestTrue(TEXT("Control Rig RigVM reroute survives reload"),
        AfterRerouteAdd.Metrics.Contains(TEXT("RigVMNodeCount=3"))
            && AfterRerouteAdd.Metrics.Contains(TEXT("RigVMLinkCount=2"))
            && NodeState(AfterRerouteAdd, RerouteNodeName).Contains(
                TEXT("Class=/Script/RigVMDeveloper.RigVMRerouteNode"))
            && NodeState(AfterRerouteAdd, RerouteNodeName).Contains(
                TEXT("Position=X=300.000000,Y=300.000000"))
            && !HasLink(
                AfterRerouteAdd, AddResultPin, MultiplyInputPin));

    FThomasAnimationOperation RemoveReroute;
    RemoveReroute.Action = TEXT("remove_control_rig_reroute_node");
    RemoveReroute.Name = RerouteNodeName;
    RemoveReroute.ExpectedValue =
        NodeState(AfterRerouteAdd, RerouteNodeName);
    FThomasSpecializedAssetInspectionResult AfterRerouteRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove reroute"),
            RemoveReroute,
            true);
    Test.TestTrue(TEXT("Control Rig RigVM reroute cleanup survives reload"),
        AfterRerouteRemove.Metrics.Contains(TEXT("RigVMNodeCount=2"))
            && AfterRerouteRemove.Metrics.Contains(TEXT("RigVMLinkCount=0"))
            && FindNodeItem(
                AfterRerouteRemove, RerouteNodeName).IsEmpty());

    FThomasSpecializedAssetInspectionResult AfterRestoredLink =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM restore direct link"),
            AddLink,
            false);
    Test.TestTrue(TEXT("Control Rig RigVM direct link restores after reroute"),
        AfterRestoredLink.Metrics.Contains(TEXT("RigVMLinkCount=1"))
            && HasLink(
                AfterRestoredLink, AddResultPin, MultiplyInputPin));

    const FString RootGraphName = GetDelimitedValue(
        FindNodeItem(AfterRestoredLink, AddNodeName),
        TEXT("ControlRigNode Graph="), TEXT(" Name="));
    const FString CollapseNodeName = TEXT("TE_RigVMCollapse");
    FThomasAnimationOperation CollapseNodes;
    CollapseNodes.Action = TEXT("collapse_control_rig_nodes");
    CollapseNodes.Name = CollapseNodeName;
    CollapseNodes.RigVMNodeNames = {AddNodeName, MultiplyNodeName};
    CollapseNodes.RigVMExpectedNodeStates = {
        NodeState(AfterRestoredLink, AddNodeName),
        NodeState(AfterRestoredLink, MultiplyNodeName)};
    FThomasAnimationPatchRequest UnconfirmedCollapseRequest;
    UnconfirmedCollapseRequest.AssetPath = ControlRigPath;
    UnconfirmedCollapseRequest.ExpectedRevision =
        AfterRestoredLink.Revision;
    UnconfirmedCollapseRequest.Operations.Add(CollapseNodes);
    Test.TestEqual(TEXT("Control Rig RigVM collapse requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedCollapseRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterCollapse =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 collapse nodes"),
            CollapseNodes,
            true);
    const FString CollapseState = NodeState(
        AfterCollapse, CollapseNodeName);
    const FString ContainedGraphName = GetDelimitedValue(
        CollapseState,
        TEXT("|ContainedGraph="), TEXT("|ContainedNodeCount="));
    Test.TestTrue(TEXT("Control Rig RigVM collapse survives reload"),
        AfterCollapse.bOk
            && AfterCollapse.Metrics.Contains(
                TEXT("RigVMCollapseNodeCount=1"))
            && CollapseState.Contains(TEXT("|Collapse=true"))
            && CollapseState.Contains(TEXT("|GraphFunction=false"))
            && !ContainedGraphName.IsEmpty()
            && FindNodeItemInGraph(
                AfterCollapse, RootGraphName, AddNodeName).IsEmpty()
            && FindNodeItemInGraph(
                AfterCollapse, RootGraphName, MultiplyNodeName).IsEmpty()
            && !FindNodeItemInGraph(
                AfterCollapse, ContainedGraphName, AddNodeName).IsEmpty()
            && !FindNodeItemInGraph(
                AfterCollapse,
                ContainedGraphName,
                MultiplyNodeName).IsEmpty());

    const FString InvalidNestedLocalVariableName =
        TEXT("TE_CollapseInvalidLocal");
    FThomasAnimationOperation InvalidNestedLocalVariable;
    InvalidNestedLocalVariable.Action =
        TEXT("add_control_rig_local_variable");
    InvalidNestedLocalVariable.RigVMGraphName = ContainedGraphName;
    InvalidNestedLocalVariable.RigVMVariableName =
        InvalidNestedLocalVariableName;
    InvalidNestedLocalVariable.RigVMType = TEXT("float");
    InvalidNestedLocalVariable.RigVMPinDefaultValue = TEXT("4.250000");
    FThomasAnimationPatchRequest InvalidNestedLocalVariableRequest;
    InvalidNestedLocalVariableRequest.AssetPath = ControlRigPath;
    InvalidNestedLocalVariableRequest.ExpectedRevision =
        AfterCollapse.Revision;
    InvalidNestedLocalVariableRequest.Operations.Add(
        InvalidNestedLocalVariable);
    Test.TestEqual(
        TEXT("Control Rig plain collapse rejects function-only local variable"),
        UThomasDomainsToolset::PlanAnimationPatch(
            InvalidNestedLocalVariableRequest).Code,
        FString(TEXT("invalid_control_rig_local_variable")));

    const FString NestedMemberVariableName = TEXT("TE_CollapseShared");
    FThomasAnimationOperation AddNestedMemberVariable;
    AddNestedMemberVariable.Action =
        TEXT("add_control_rig_member_variable");
    AddNestedMemberVariable.RigVMVariableName = NestedMemberVariableName;
    AddNestedMemberVariable.RigVMType = TEXT("float");
    AddNestedMemberVariable.RigVMPinDefaultValue = TEXT("4.250000");
    FThomasSpecializedAssetInspectionResult AfterNestedMemberVariableAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add collapse member variable"),
            AddNestedMemberVariable,
            false);
    const FString NestedMemberVariableState = GetDelimitedValue(
        FindMemberVariableItem(
            AfterNestedMemberVariableAdd,
            NestedMemberVariableName),
        TEXT(" State="),
        FString());
    Test.TestTrue(
        TEXT("Control Rig collapse member variable survives reload"),
        AfterNestedMemberVariableAdd.bOk
            && AfterNestedMemberVariableAdd.Metrics.Contains(
                TEXT("RigVMMemberVariableCount=1"))
            && NestedMemberVariableState.Contains(
                TEXT("CPPType=float|CPPTypeObject=None|Default=4.250000")));

    const FString NestedMemberGetterName = TEXT("TE_CollapseSharedGet");
    FThomasAnimationOperation AddNestedMemberGetter;
    AddNestedMemberGetter.Action = TEXT("add_control_rig_variable_node");
    AddNestedMemberGetter.RigVMGraphName = ContainedGraphName;
    AddNestedMemberGetter.Name = NestedMemberGetterName;
    AddNestedMemberGetter.RigVMVariableName = NestedMemberVariableName;
    AddNestedMemberGetter.RigVMType = TEXT("float");
    AddNestedMemberGetter.RigVMPinDefaultValue = TEXT("4.250000");
    AddNestedMemberGetter.bRigVMVariableGetter = true;
    AddNestedMemberGetter.PositionX = 100;
    AddNestedMemberGetter.PositionY = 400;
    FThomasSpecializedAssetInspectionResult AfterNestedMemberGetterAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add collapse member getter"),
            AddNestedMemberGetter,
            false);
    Test.TestTrue(
        TEXT("Control Rig collapse member getter survives reload"),
        AfterNestedMemberGetterAdd.bOk
            && NodeStateInGraph(
                AfterNestedMemberGetterAdd,
                ContainedGraphName,
                NestedMemberGetterName).Contains(
                    TEXT("|Variable=true|VariableName=TE_CollapseShared|CPPType=float"))
            && NodeStateInGraph(
                AfterNestedMemberGetterAdd,
                ContainedGraphName,
                NestedMemberGetterName).Contains(
                    TEXT("|Getter=true|External=true|Local=false")));

    const FString NestedMemberSetterName = TEXT("TE_CollapseSharedSet");
    FThomasAnimationOperation AddNestedMemberSetter = AddNestedMemberGetter;
    AddNestedMemberSetter.Name = NestedMemberSetterName;
    AddNestedMemberSetter.bRigVMVariableGetter = false;
    AddNestedMemberSetter.PositionX = 350;
    FThomasSpecializedAssetInspectionResult AfterNestedMemberSetterAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add collapse member setter"),
            AddNestedMemberSetter,
            false);
    Test.TestTrue(
        TEXT("Control Rig collapse member setter survives reload"),
        AfterNestedMemberSetterAdd.bOk
            && NodeStateInGraph(
                AfterNestedMemberSetterAdd,
                ContainedGraphName,
                NestedMemberSetterName).Contains(
                    TEXT("|Variable=true|VariableName=TE_CollapseShared|CPPType=float"))
            && NodeStateInGraph(
                AfterNestedMemberSetterAdd,
                ContainedGraphName,
                NestedMemberSetterName).Contains(
                    TEXT("|Getter=false|External=true|Local=false")));

    FThomasAnimationOperation RemoveReferencedNestedMemberVariable;
    RemoveReferencedNestedMemberVariable.Action =
        TEXT("remove_control_rig_member_variable");
    RemoveReferencedNestedMemberVariable.RigVMVariableName =
        NestedMemberVariableName;
    RemoveReferencedNestedMemberVariable.ExpectedValue = GetDelimitedValue(
        FindMemberVariableItem(
            AfterNestedMemberSetterAdd,
            NestedMemberVariableName),
        TEXT(" State="),
        FString());
    FThomasAnimationPatchRequest RemoveReferencedNestedMemberRequest;
    RemoveReferencedNestedMemberRequest.AssetPath = ControlRigPath;
    RemoveReferencedNestedMemberRequest.ExpectedRevision =
        AfterNestedMemberSetterAdd.Revision;
    RemoveReferencedNestedMemberRequest.bConfirmDestructive = true;
    RemoveReferencedNestedMemberRequest.Operations.Add(
        RemoveReferencedNestedMemberVariable);
    Test.TestEqual(
        TEXT("Control Rig nested member references protect descriptor"),
        UThomasDomainsToolset::PlanAnimationPatch(
            RemoveReferencedNestedMemberRequest).Code,
        FString(TEXT("control_rig_member_variable_conflict")));

    FThomasAnimationOperation RemoveNestedMemberGetter;
    RemoveNestedMemberGetter.Action =
        TEXT("remove_control_rig_variable_node");
    RemoveNestedMemberGetter.RigVMGraphName = ContainedGraphName;
    RemoveNestedMemberGetter.Name = NestedMemberGetterName;
    RemoveNestedMemberGetter.ExpectedValue = NodeStateInGraph(
        AfterNestedMemberSetterAdd,
        ContainedGraphName,
        NestedMemberGetterName);
    FThomasSpecializedAssetInspectionResult AfterNestedMemberGetterRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove collapse member getter"),
            RemoveNestedMemberGetter,
            true);
    Test.TestTrue(TEXT("Control Rig collapse member getter cleanup reloads"),
        AfterNestedMemberGetterRemove.bOk
            && FindNodeItemInGraph(
                AfterNestedMemberGetterRemove,
                ContainedGraphName,
                NestedMemberGetterName).IsEmpty());

    FThomasAnimationOperation RemoveNestedMemberSetter;
    RemoveNestedMemberSetter.Action =
        TEXT("remove_control_rig_variable_node");
    RemoveNestedMemberSetter.RigVMGraphName = ContainedGraphName;
    RemoveNestedMemberSetter.Name = NestedMemberSetterName;
    RemoveNestedMemberSetter.ExpectedValue = NodeStateInGraph(
        AfterNestedMemberGetterRemove,
        ContainedGraphName,
        NestedMemberSetterName);
    FThomasSpecializedAssetInspectionResult AfterNestedMemberSetterRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove collapse member setter"),
            RemoveNestedMemberSetter,
            true);
    Test.TestTrue(TEXT("Control Rig collapse member setter cleanup reloads"),
        AfterNestedMemberSetterRemove.bOk
            && FindNodeItemInGraph(
                AfterNestedMemberSetterRemove,
                ContainedGraphName,
                NestedMemberSetterName).IsEmpty());

    FThomasAnimationOperation RemoveNestedMemberVariable =
        RemoveReferencedNestedMemberVariable;
    RemoveNestedMemberVariable.ExpectedValue = GetDelimitedValue(
        FindMemberVariableItem(
            AfterNestedMemberSetterRemove,
            NestedMemberVariableName),
        TEXT(" State="),
        FString());
    FThomasSpecializedAssetInspectionResult AfterNestedMemberCleanup =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove collapse member variable"),
            RemoveNestedMemberVariable,
            true);
    Test.TestTrue(
        TEXT("Control Rig collapse member lifecycle cleans exact graph"),
        AfterNestedMemberCleanup.bOk
            && AfterNestedMemberCleanup.Metrics.Contains(
                TEXT("RigVMMemberVariableCount=0"))
            && NodeState(
                AfterNestedMemberCleanup, CollapseNodeName) == CollapseState
            && !FindNodeItemInGraph(
                AfterNestedMemberCleanup,
                ContainedGraphName,
                AddNodeName).IsEmpty()
            && !FindNodeItemInGraph(
                AfterNestedMemberCleanup,
                ContainedGraphName,
                MultiplyNodeName).IsEmpty());

    FThomasAnimationOperation ExpandCollapse;
    ExpandCollapse.Action = TEXT("expand_control_rig_library_node");
    ExpandCollapse.Name = CollapseNodeName;
    ExpandCollapse.ExpectedValue = NodeState(
        AfterNestedMemberCleanup, CollapseNodeName);
    ExpandCollapse.RigVMNodeNames = {AddNodeName, MultiplyNodeName};
    ExpandCollapse.RigVMExpectedNodeStates = {
        NodeStateInGraph(
            AfterNestedMemberCleanup, ContainedGraphName, AddNodeName),
        NodeStateInGraph(
            AfterNestedMemberCleanup, ContainedGraphName, MultiplyNodeName)};
    FThomasAnimationPatchRequest UnconfirmedExpandRequest;
    UnconfirmedExpandRequest.AssetPath = ControlRigPath;
    UnconfirmedExpandRequest.ExpectedRevision =
        AfterNestedMemberCleanup.Revision;
    UnconfirmedExpandRequest.Operations.Add(ExpandCollapse);
    Test.TestEqual(TEXT("Control Rig RigVM expansion requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedExpandRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterExpand =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 expand collapse node"),
            ExpandCollapse,
            true);
    Test.TestTrue(TEXT("Control Rig RigVM expansion restores exact graph"),
        AfterExpand.bOk
            && AfterExpand.Metrics.Contains(
                TEXT("RigVMCollapseNodeCount=0"))
            && FindNodeItem(
                AfterExpand, CollapseNodeName).IsEmpty()
            && NodeState(AfterExpand, AddNodeName)
                == NodeState(AfterRestoredLink, AddNodeName)
            && NodeState(AfterExpand, MultiplyNodeName)
                == NodeState(AfterRestoredLink, MultiplyNodeName)
            && PinDefault(AfterExpand, AddInputPin)
                == TEXT("1.250000")
            && HasLink(AfterExpand, AddResultPin, MultiplyInputPin));

    FThomasAnimationOperation RemoveLink = AddLink;
    RemoveLink.Action = TEXT("remove_control_rig_link");
    RemoveLink.ExpectedValue =
        AddResultPin + TEXT(" -> ") + MultiplyInputPin;
    FThomasAnimationPatchRequest UnconfirmedRemoveLinkRequest;
    UnconfirmedRemoveLinkRequest.AssetPath = ControlRigPath;
    UnconfirmedRemoveLinkRequest.ExpectedRevision = AfterExpand.Revision;
    UnconfirmedRemoveLinkRequest.Operations.Add(RemoveLink);
    Test.TestEqual(TEXT("Control Rig RigVM link removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedRemoveLinkRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterLinkRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove link"),
            RemoveLink,
            true);
    Test.TestTrue(TEXT("Control Rig RigVM link removal survives reload"),
        AfterLinkRemove.Metrics.Contains(TEXT("RigVMLinkCount=0"))
            && !HasLink(
                AfterLinkRemove, AddResultPin, MultiplyInputPin));

    FThomasAnimationOperation RemoveMultiply;
    RemoveMultiply.Action = TEXT("remove_control_rig_unit_node");
    RemoveMultiply.Name = MultiplyNodeName;
    RemoveMultiply.ExpectedValue =
        NodeState(AfterLinkRemove, MultiplyNodeName);
    FThomasSpecializedAssetInspectionResult AfterMultiplyRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove second unit"),
            RemoveMultiply,
            true);
    Test.TestTrue(TEXT("Control Rig RigVM second unit removal survives reload"),
        AfterMultiplyRemove.Metrics.Contains(TEXT("RigVMNodeCount=1"))
            && FindNodeItem(
                AfterMultiplyRemove, MultiplyNodeName).IsEmpty());

    FThomasAnimationOperation RemoveAdd;
    RemoveAdd.Action = TEXT("remove_control_rig_unit_node");
    RemoveAdd.Name = AddNodeName;
    RemoveAdd.ExpectedValue = NodeState(AfterMultiplyRemove, AddNodeName);
    FThomasSpecializedAssetInspectionResult AfterCleanup =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove first unit"),
            RemoveAdd,
            true);
    Test.TestTrue(TEXT("Control Rig RigVM graph cleanup survives reload"),
        AfterCleanup.bOk
            && AfterCleanup.Metrics.Contains(TEXT("RigVMNodeCount=0"))
            && AfterCleanup.Metrics.Contains(TEXT("RigVMLinkCount=0")));

    Test.TestTrue(TEXT("Control Rig inspection exposes registered RigVM units"),
        AfterCleanup.Metrics.ContainsByPredicate([](const FString& Metric)
        {
            return Metric.StartsWith(TEXT("RigVMRegisteredUnitCount="));
        })
        && AfterCleanup.Metrics.ContainsByPredicate([](const FString& Metric)
        {
            return Metric.StartsWith(
                TEXT("RigVMRegisteredUnitStructCount="));
        })
        && AfterCleanup.Metrics.ContainsByPredicate([](const FString& Metric)
        {
            return Metric.StartsWith(
                TEXT("RigVMRegisteredUnitFunctionCount="));
        })
        && AfterCleanup.Items.ContainsByPredicate([](const FString& Item)
        {
            return Item.StartsWith(TEXT("ControlRigAvailableUnit "))
                && Item.Contains(TEXT(" Method="))
                && Item.Contains(TEXT(" Function="));
        }));

    FString DiscoveredUnitStructPath;
    FString DiscoveredUnitMethod;
    FString DiscoveredUnitFunction;
    FString DiscoveredUnitArrayPinName;
    FString FallbackUnitStructPath;
    FString FallbackUnitMethod;
    FString FallbackUnitFunction;
    FString FallbackUnitArrayPinName;
    for (const FString& Item : AfterCleanup.Items)
    {
        if (!Item.StartsWith(TEXT("ControlRigAvailableUnit "))
            || !Item.Contains(TEXT(" IsEvent=false")))
        {
            continue;
        }
        const FString StructPath = GetDelimitedValue(
            Item, TEXT(" Struct="), TEXT(" Method="));
        const FString MethodName = GetDelimitedValue(
            Item, TEXT(" Method="), TEXT(" Function="));
        const FString FunctionName = GetDelimitedValue(
            Item, TEXT(" Function="), TEXT(" Template="));
        FString EditableArrayPins = GetDelimitedValue(
            Item,
            TEXT(" EditableArrayPins="),
            TEXT(" AggregateInputs="));
        FString EditableArrayPinName;
        EditableArrayPins.Split(
            TEXT(","), &EditableArrayPinName, nullptr);
        if (EditableArrayPinName.IsEmpty()
            && EditableArrayPins != TEXT("None"))
        {
            EditableArrayPinName = EditableArrayPins;
        }
        if (StructPath.IsEmpty()
            || MethodName.IsEmpty()
            || FunctionName.IsEmpty()
            || EditableArrayPinName.IsEmpty())
        {
            continue;
        }
        if (FallbackUnitStructPath.IsEmpty())
        {
            FallbackUnitStructPath = StructPath;
            FallbackUnitMethod = MethodName;
            FallbackUnitFunction = FunctionName;
            FallbackUnitArrayPinName = EditableArrayPinName;
        }
        if (!StructPath.StartsWith(TEXT("/Script/ControlRig."))
            && !StructPath.StartsWith(TEXT("/Script/RigVM.")))
        {
            DiscoveredUnitStructPath = StructPath;
            DiscoveredUnitMethod = MethodName;
            DiscoveredUnitFunction = FunctionName;
            DiscoveredUnitArrayPinName = EditableArrayPinName;
            break;
        }
    }
    if (DiscoveredUnitStructPath.IsEmpty())
    {
        DiscoveredUnitStructPath = FallbackUnitStructPath;
        DiscoveredUnitMethod = FallbackUnitMethod;
        DiscoveredUnitFunction = FallbackUnitFunction;
        DiscoveredUnitArrayPinName = FallbackUnitArrayPinName;
    }
    Test.AddInfo(TEXT("Control Rig selected registered Unit struct=")
        + DiscoveredUnitStructPath + TEXT(" method=")
        + DiscoveredUnitMethod + TEXT(" function=")
        + DiscoveredUnitFunction + TEXT(" editable_array=")
        + DiscoveredUnitArrayPinName);
    Test.TestTrue(
        TEXT("Control Rig discovery provides an exact registered Unit function"),
        !DiscoveredUnitStructPath.IsEmpty()
            && !DiscoveredUnitMethod.IsEmpty()
            && !DiscoveredUnitFunction.IsEmpty()
            && !DiscoveredUnitArrayPinName.IsEmpty());

    const FString DiscoveredUnitNodeName =
        TEXT("TE_RigVMDiscoveredUnit");
    FThomasAnimationOperation AddDiscoveredUnit;
    AddDiscoveredUnit.Action = TEXT("add_control_rig_unit_node");
    AddDiscoveredUnit.Name = DiscoveredUnitNodeName;
    AddDiscoveredUnit.ClassPath = DiscoveredUnitStructPath;
    AddDiscoveredUnit.RigVMMethodName = DiscoveredUnitMethod;
    AddDiscoveredUnit.PositionX = 650;
    AddDiscoveredUnit.PositionY = 200;

    FThomasAnimationOperation AddUnknownUnitMethod = AddDiscoveredUnit;
    AddUnknownUnitMethod.Name = TEXT("TE_RigVMUnknownMethod");
    AddUnknownUnitMethod.RigVMMethodName = TEXT("TE_NotRegistered");
    FThomasAnimationPatchRequest AddUnknownUnitMethodRequest;
    AddUnknownUnitMethodRequest.AssetPath = ControlRigPath;
    AddUnknownUnitMethodRequest.ExpectedRevision = AfterCleanup.Revision;
    AddUnknownUnitMethodRequest.Operations.Add(AddUnknownUnitMethod);
    Test.TestEqual(
        TEXT("Control Rig rejects an unregistered Unit method before mutation"),
        UThomasDomainsToolset::PlanAnimationPatch(
            AddUnknownUnitMethodRequest).Code,
        FString(TEXT("invalid_control_rig_unit_node")));

    FThomasSpecializedAssetInspectionResult AfterDiscoveredUnitAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add discovered registered Unit"),
            AddDiscoveredUnit,
            false);
    const FString DiscoveredUnitState = NodeState(
        AfterDiscoveredUnitAdd, DiscoveredUnitNodeName);
    Test.TestTrue(
        TEXT("Dynamically discovered Control Rig Unit survives reload"),
        AfterDiscoveredUnitAdd.bOk
            && AfterDiscoveredUnitAdd.Metrics.Contains(
                TEXT("RigVMNodeCount=1"))
            && DiscoveredUnitState.Contains(
                TEXT("|Struct=") + DiscoveredUnitStructPath)
            && DiscoveredUnitState.Contains(
                TEXT("|Method=") + DiscoveredUnitMethod)
            && DiscoveredUnitState.Contains(TEXT("|Event=false")));

    const FString DiscoveredUnitArrayPinPath =
        DiscoveredUnitNodeName + TEXT(".")
        + DiscoveredUnitArrayPinName;
    FString DiscoveredUnitArrayPinItem = FindPinItem(
        AfterDiscoveredUnitAdd, DiscoveredUnitArrayPinPath);
    FString DiscoveredUnitArrayState = GetDelimitedValue(
        DiscoveredUnitArrayPinItem,
        TEXT(" ArrayState="),
        FString());
    const int32 DiscoveredUnitInitialArraySize =
        ArrayStateSize(DiscoveredUnitArrayState);
    Test.TestTrue(
        TEXT("Dynamically discovered Unit exposes its editable array pin"),
        DiscoveredUnitArrayPinItem.Contains(
                TEXT(" IsDynamicArray=true "))
            && DiscoveredUnitInitialArraySize >= 0
            && DiscoveredUnitInitialArraySize < 60);

    FThomasAnimationOperation ResizeDiscoveredUnitArray;
    ResizeDiscoveredUnitArray.Action =
        TEXT("set_control_rig_array_pin_size");
    ResizeDiscoveredUnitArray.RigVMPinPath =
        DiscoveredUnitArrayPinPath;
    ResizeDiscoveredUnitArray.ExpectedValue =
        DiscoveredUnitArrayState;
    ResizeDiscoveredUnitArray.RigVMArraySize =
        DiscoveredUnitInitialArraySize + 1;
    FThomasSpecializedAssetInspectionResult
        AfterDiscoveredUnitArrayResize =
            ApplyControlRigOperationAndReload(
                TEXT("Control Rig RigVM R2 resize discovered Unit array"),
                ResizeDiscoveredUnitArray,
                true);
    DiscoveredUnitArrayPinItem = FindPinItem(
        AfterDiscoveredUnitArrayResize,
        DiscoveredUnitArrayPinPath);
    DiscoveredUnitArrayState = GetDelimitedValue(
        DiscoveredUnitArrayPinItem,
        TEXT(" ArrayState="),
        FString());
    Test.TestEqual(
        TEXT("Discovered Unit array resize survives reload"),
        ArrayStateSize(DiscoveredUnitArrayState),
        DiscoveredUnitInitialArraySize + 1);

    FThomasAnimationOperation RestoreDiscoveredUnitArray =
        ResizeDiscoveredUnitArray;
    RestoreDiscoveredUnitArray.ExpectedValue =
        DiscoveredUnitArrayState;
    RestoreDiscoveredUnitArray.RigVMArraySize =
        DiscoveredUnitInitialArraySize;
    FThomasSpecializedAssetInspectionResult
        AfterDiscoveredUnitArrayRestore =
            ApplyControlRigOperationAndReload(
                TEXT("Control Rig RigVM R2 restore discovered Unit array"),
                RestoreDiscoveredUnitArray,
                true);
    DiscoveredUnitArrayPinItem = FindPinItem(
        AfterDiscoveredUnitArrayRestore,
        DiscoveredUnitArrayPinPath);
    DiscoveredUnitArrayState = GetDelimitedValue(
        DiscoveredUnitArrayPinItem,
        TEXT(" ArrayState="),
        FString());
    Test.TestEqual(
        TEXT("Discovered Unit array cleanup survives reload"),
        ArrayStateSize(DiscoveredUnitArrayState),
        DiscoveredUnitInitialArraySize);

    FThomasAnimationOperation RemoveDiscoveredUnit;
    RemoveDiscoveredUnit.Action = TEXT("remove_control_rig_unit_node");
    RemoveDiscoveredUnit.Name = DiscoveredUnitNodeName;
    RemoveDiscoveredUnit.ExpectedValue = NodeState(
        AfterDiscoveredUnitArrayRestore,
        DiscoveredUnitNodeName);
    FThomasSpecializedAssetInspectionResult AfterDiscoveredUnitRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove discovered registered Unit"),
            RemoveDiscoveredUnit,
            true);
    Test.TestTrue(
        TEXT("Dynamically discovered Control Rig Unit cleanup survives reload"),
        AfterDiscoveredUnitRemove.bOk
            && AfterDiscoveredUnitRemove.Metrics.Contains(
                TEXT("RigVMNodeCount=0"))
            && FindNodeItem(
                AfterDiscoveredUnitRemove,
                DiscoveredUnitNodeName).IsEmpty());

    Test.TestTrue(
        TEXT("Control Rig inspection exposes registered template notations"),
        AfterCleanup.Metrics.ContainsByPredicate([](const FString& Metric)
        {
            return Metric.StartsWith(TEXT("RigVMRegisteredTemplateCount="));
        })
        && AfterCleanup.Items.ContainsByPredicate([](const FString& Item)
        {
            return Item.StartsWith(TEXT("ControlRigAvailableTemplate "));
        }));

    FString AggregateUnitStruct;
    FString AggregateUnitMethod;
    for (const FString& Item : AfterCleanup.Items)
    {
        if (!Item.StartsWith(TEXT("ControlRigAvailableUnit "))
            || !Item.Contains(TEXT(" IsAggregate=true "))
            || !Item.Contains(TEXT(" IsEvent=false ")))
        {
            continue;
        }
        AggregateUnitStruct = GetDelimitedValue(
            Item, TEXT(" Struct="), TEXT(" Method="));
        AggregateUnitMethod = GetDelimitedValue(
            Item, TEXT(" Method="), TEXT(" Function="));
        if (!AggregateUnitStruct.IsEmpty()
            && !AggregateUnitMethod.IsEmpty())
        {
            break;
        }
    }
    Test.TestTrue(
        TEXT("Control Rig discovery finds a registered aggregate Unit"),
        !AggregateUnitStruct.IsEmpty()
            && !AggregateUnitMethod.IsEmpty());

    const FString AggregateNodeName = TEXT("TE_RigVMAggregate");
    FThomasAnimationOperation AddAggregateUnit;
    AddAggregateUnit.Action = TEXT("add_control_rig_unit_node");
    AddAggregateUnit.Name = AggregateNodeName;
    AddAggregateUnit.ClassPath = AggregateUnitStruct;
    AddAggregateUnit.RigVMMethodName = AggregateUnitMethod;
    AddAggregateUnit.PositionX = 650;
    AddAggregateUnit.PositionY = 80;
    FThomasSpecializedAssetInspectionResult AfterAggregateUnitAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add discovered aggregate Unit"),
            AddAggregateUnit,
            false);
    const FString InitialAggregateState = NodeState(
        AfterAggregateUnitAdd, AggregateNodeName);
    const TArray<FString> InitialAggregatePins =
        AggregatePinNames(InitialAggregateState);
    const FString NextAggregatePinName = GetDelimitedValue(
        InitialAggregateState,
        TEXT("|AggregateNext="),
        TEXT("|Struct="));
    Test.TestTrue(
        TEXT("Discovered Control Rig Unit exposes aggregate capability"),
        AfterAggregateUnitAdd.bOk
            && InitialAggregateState.Contains(TEXT("|Aggregate=true"))
            && InitialAggregatePins.Num() >= 2
            && !NextAggregatePinName.IsEmpty());

    FThomasAnimationOperation AddAggregatePin;
    AddAggregatePin.Action = TEXT("add_control_rig_aggregate_pin");
    AddAggregatePin.Name = AggregateNodeName;
    AddAggregatePin.NewName = NextAggregatePinName;
    AddAggregatePin.ExpectedValue = InitialAggregateState;
    FThomasSpecializedAssetInspectionResult AfterAggregateAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add aggregate pin"),
            AddAggregatePin,
            false);
    const FString AddedAggregateState = NodeState(
        AfterAggregateAdd, AggregateNodeName);
    const TArray<FString> AddedAggregatePins =
        AggregatePinNames(AddedAggregateState);
    const FString AddedAggregatePinPath =
        AggregateNodeName + TEXT(".") + NextAggregatePinName;
    Test.TestTrue(
        TEXT("Control Rig aggregate pin addition survives reload"),
        AfterAggregateAdd.bOk
            && AddedAggregatePins.Num() == InitialAggregatePins.Num() + 1
            && AddedAggregatePins.Contains(NextAggregatePinName)
            && !FindPinItem(
                AfterAggregateAdd,
                AddedAggregatePinPath).IsEmpty());

    FThomasAnimationPatchRequest StaleAggregateAddRequest;
    StaleAggregateAddRequest.AssetPath = ControlRigPath;
    StaleAggregateAddRequest.ExpectedRevision =
        AfterAggregateAdd.Revision;
    StaleAggregateAddRequest.Operations.Add(AddAggregatePin);
    Test.TestEqual(
        TEXT("Control Rig aggregate edit rejects stale node state"),
        UThomasDomainsToolset::PlanAnimationPatch(
            StaleAggregateAddRequest).Code,
        FString(TEXT("control_rig_aggregate_node_conflict")));

    FThomasAnimationOperation RemoveAggregatePin;
    RemoveAggregatePin.Action = TEXT("remove_control_rig_aggregate_pin");
    RemoveAggregatePin.Name = AggregateNodeName;
    RemoveAggregatePin.RigVMPinPath = AddedAggregatePinPath;
    RemoveAggregatePin.ExpectedValue = AddedAggregateState;
    FThomasAnimationPatchRequest UnconfirmedAggregateRemoveRequest;
    UnconfirmedAggregateRemoveRequest.AssetPath = ControlRigPath;
    UnconfirmedAggregateRemoveRequest.ExpectedRevision =
        AfterAggregateAdd.Revision;
    UnconfirmedAggregateRemoveRequest.Operations.Add(RemoveAggregatePin);
    Test.TestEqual(
        TEXT("Control Rig aggregate pin removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedAggregateRemoveRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterAggregateRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove aggregate pin"),
            RemoveAggregatePin,
            true);
    Test.TestTrue(
        TEXT("Control Rig aggregate pin cleanup survives reload"),
        AfterAggregateRemove.bOk
            && NodeState(AfterAggregateRemove, AggregateNodeName)
                == InitialAggregateState
            && FindPinItem(
                AfterAggregateRemove,
                AddedAggregatePinPath).IsEmpty());

    FThomasAnimationOperation RemoveAggregateUnit;
    RemoveAggregateUnit.Action =
        TEXT("remove_control_rig_unit_node");
    RemoveAggregateUnit.Name = AggregateNodeName;
    RemoveAggregateUnit.ExpectedValue = NodeState(
        AfterAggregateRemove, AggregateNodeName);
    FThomasSpecializedAssetInspectionResult AfterAggregateUnitRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove aggregate Unit"),
            RemoveAggregateUnit,
            true);
    Test.TestTrue(
        TEXT("Control Rig aggregate Unit cleanup survives reload"),
        AfterAggregateUnitRemove.bOk
            && FindNodeItem(
                AfterAggregateUnitRemove,
                AggregateNodeName).IsEmpty());

    FString TemplateNotation;
    for (const FString& Item : AfterCleanup.Items)
    {
        if (Item.StartsWith(TEXT("ControlRigAvailableTemplate "))
            && !Item.Contains(TEXT(" EditableArrayPins=None")))
        {
            TemplateNotation = GetDelimitedValue(
                Item, TEXT(" Notation="), TEXT(" Kind="));
            if (!TemplateNotation.IsEmpty())
            {
                break;
            }
        }
    }
    Test.TestTrue(
        TEXT("Control Rig discovery finds an editable array template"),
        !TemplateNotation.IsEmpty());

    const FString TemplateNodeName = TEXT("TE_RigVMArrayTemplate");
    FThomasAnimationOperation AddTemplate;
    AddTemplate.Action = TEXT("add_control_rig_template_node");
    AddTemplate.Name = TemplateNodeName;
    AddTemplate.RigVMTemplateNotation = TemplateNotation;
    AddTemplate.PositionX = 700;
    AddTemplate.PositionY = 200;
    FThomasSpecializedAssetInspectionResult AfterTemplateAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add registered template node"),
            AddTemplate,
            false);
    Test.TestTrue(TEXT("Control Rig template node survives reload"),
        AfterTemplateAdd.bOk
            && AfterTemplateAdd.Metrics.Contains(TEXT("RigVMNodeCount=1"))
            && NodeState(AfterTemplateAdd, TemplateNodeName).Contains(
                TEXT("TemplateNotation=") + TemplateNotation));

    const FString ArrayPinItem = FindEditableArrayPinItem(
        AfterTemplateAdd, TemplateNodeName);
    const FString ArrayPinPath = GetDelimitedValue(
        ArrayPinItem, TEXT(" Path="), TEXT(" Direction="));
    const FString InitialArrayState = GetDelimitedValue(
        ArrayPinItem, TEXT(" ArrayState="), FString());
    const FString InitialArrayTypeState = GetDelimitedValue(
        ArrayPinItem, TEXT(" TypeState="), TEXT(" ArrayState="));
    const int32 InitialArraySize = ArrayStateSize(InitialArrayState);
    Test.TestTrue(TEXT("Control Rig template exposes exact dynamic-array state"),
        !ArrayPinPath.IsEmpty()
            && InitialArrayTypeState.Contains(
                TEXT("IsWildCard=true"))
            && InitialArraySize >= 0
            && InitialArraySize < 60);

    FThomasAnimationOperation InvalidArrayResolve;
    InvalidArrayResolve.Action =
        TEXT("resolve_control_rig_wildcard_pin");
    InvalidArrayResolve.RigVMPinPath = ArrayPinPath;
    InvalidArrayResolve.RigVMType = TEXT("arbitrary_cpp_type");
    InvalidArrayResolve.ExpectedValue = InitialArrayTypeState;
    FThomasAnimationPatchRequest InvalidArrayResolveRequest;
    InvalidArrayResolveRequest.AssetPath = ControlRigPath;
    InvalidArrayResolveRequest.ExpectedRevision =
        AfterTemplateAdd.Revision;
    InvalidArrayResolveRequest.Operations.Add(InvalidArrayResolve);
    Test.TestEqual(TEXT("Control Rig wildcard resolution rejects unknown type"),
        UThomasDomainsToolset::PlanAnimationPatch(
            InvalidArrayResolveRequest).Code,
        FString(TEXT("control_rig_wildcard_pin_conflict")));

    FThomasAnimationOperation ResolveArray = InvalidArrayResolve;
    ResolveArray.RigVMType = TEXT("float");
    FThomasSpecializedAssetInspectionResult AfterArrayResolve =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM resolve array wildcard to float"),
            ResolveArray,
            false);
    FString ResolvedArrayPinItem = FindPinItem(
        AfterArrayResolve, ArrayPinPath);
    const FString ResolvedArrayTypeState = GetDelimitedValue(
        ResolvedArrayPinItem,
        TEXT(" TypeState="),
        TEXT(" ArrayState="));
    Test.TestTrue(TEXT("Control Rig array wildcard resolution survives reload"),
        AfterArrayResolve.bOk
            && ResolvedArrayPinItem.Contains(
                TEXT(" CPPType=TArray<float> "))
            && ResolvedArrayTypeState.Contains(
                TEXT("IsWildCard=false"))
            && NodeState(AfterArrayResolve, TemplateNodeName).Contains(
                TEXT("TemplateResolved=true")));

    FThomasAnimationPatchRequest StaleArrayResolveRequest;
    StaleArrayResolveRequest.AssetPath = ControlRigPath;
    StaleArrayResolveRequest.ExpectedRevision =
        AfterArrayResolve.Revision;
    StaleArrayResolveRequest.Operations.Add(ResolveArray);
    Test.TestEqual(TEXT("Control Rig wildcard resolution rejects stale state"),
        UThomasDomainsToolset::PlanAnimationPatch(
            StaleArrayResolveRequest).Code,
        FString(TEXT("control_rig_wildcard_pin_conflict")));

    ResolvedArrayPinItem = FindPinItem(
        AfterArrayResolve, ArrayPinPath);
    const FString ResolvedInitialArrayState = GetDelimitedValue(
        ResolvedArrayPinItem, TEXT(" ArrayState="), FString());

    FThomasAnimationOperation ResizeArray;
    ResizeArray.Action = TEXT("set_control_rig_array_pin_size");
    ResizeArray.RigVMPinPath = ArrayPinPath;
    ResizeArray.ExpectedValue = ResolvedInitialArrayState;
    ResizeArray.RigVMArraySize = InitialArraySize + 1;
    FThomasAnimationPatchRequest UnconfirmedResizeRequest;
    UnconfirmedResizeRequest.AssetPath = ControlRigPath;
    UnconfirmedResizeRequest.ExpectedRevision = AfterArrayResolve.Revision;
    UnconfirmedResizeRequest.Operations.Add(ResizeArray);
    Test.TestEqual(TEXT("Control Rig array resize requires R2 confirmation"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedResizeRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterArrayResize =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 resize array pin"),
            ResizeArray,
            true);
    FString CurrentArrayItem = FindPinItem(
        AfterArrayResize, ArrayPinPath);
    FString CurrentArrayState = GetDelimitedValue(
        CurrentArrayItem, TEXT(" ArrayState="), FString());
    Test.TestEqual(TEXT("Control Rig array resize survives reload"),
        ArrayStateSize(CurrentArrayState), InitialArraySize + 1);

    FThomasAnimationOperation StaleArrayAdd;
    StaleArrayAdd.Action = TEXT("add_control_rig_array_pin");
    StaleArrayAdd.RigVMPinPath = ArrayPinPath;
    StaleArrayAdd.ExpectedValue = ResolvedInitialArrayState;
    FThomasAnimationPatchRequest StaleArrayRequest;
    StaleArrayRequest.AssetPath = ControlRigPath;
    StaleArrayRequest.ExpectedRevision = AfterArrayResize.Revision;
    StaleArrayRequest.Operations.Add(StaleArrayAdd);
    Test.TestEqual(TEXT("Control Rig array edit rejects stale state"),
        UThomasDomainsToolset::PlanAnimationPatch(StaleArrayRequest).Code,
        FString(TEXT("control_rig_array_pin_conflict")));

    FThomasAnimationOperation AddArrayPin = StaleArrayAdd;
    AddArrayPin.ExpectedValue = CurrentArrayState;
    FThomasSpecializedAssetInspectionResult AfterArrayAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add array element pin"),
            AddArrayPin,
            false);
    CurrentArrayItem = FindPinItem(AfterArrayAdd, ArrayPinPath);
    CurrentArrayState = GetDelimitedValue(
        CurrentArrayItem, TEXT(" ArrayState="), FString());
    Test.TestEqual(TEXT("Control Rig array element addition survives reload"),
        ArrayStateSize(CurrentArrayState), InitialArraySize + 2);

    FThomasAnimationOperation DuplicateArrayPin;
    DuplicateArrayPin.Action = TEXT("duplicate_control_rig_array_pin");
    DuplicateArrayPin.RigVMPinPath = ArrayPinPath + TEXT(".0");
    DuplicateArrayPin.ExpectedValue = CurrentArrayState;
    FThomasSpecializedAssetInspectionResult AfterArrayDuplicate =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM duplicate array element pin"),
            DuplicateArrayPin,
            false);
    CurrentArrayItem = FindPinItem(
        AfterArrayDuplicate, ArrayPinPath);
    CurrentArrayState = GetDelimitedValue(
        CurrentArrayItem, TEXT(" ArrayState="), FString());
    const int32 DuplicatedArraySize = ArrayStateSize(CurrentArrayState);
    Test.TestEqual(
        TEXT("Control Rig array element duplication survives reload"),
        DuplicatedArraySize, InitialArraySize + 3);

    FThomasAnimationOperation RemoveArrayPin;
    RemoveArrayPin.Action = TEXT("remove_control_rig_array_pin");
    RemoveArrayPin.RigVMPinPath = ArrayPinPath + TEXT(".")
        + FString::FromInt(DuplicatedArraySize - 1);
    RemoveArrayPin.ExpectedValue = CurrentArrayState;
    FThomasSpecializedAssetInspectionResult AfterArrayRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove array element pin"),
            RemoveArrayPin,
            true);
    CurrentArrayItem = FindPinItem(AfterArrayRemove, ArrayPinPath);
    CurrentArrayState = GetDelimitedValue(
        CurrentArrayItem, TEXT(" ArrayState="), FString());
    Test.TestEqual(TEXT("Control Rig array element removal survives reload"),
        ArrayStateSize(CurrentArrayState), InitialArraySize + 2);

    FThomasAnimationOperation RestoreArray = ResizeArray;
    RestoreArray.ExpectedValue = CurrentArrayState;
    RestoreArray.RigVMArraySize = InitialArraySize;
    FThomasSpecializedAssetInspectionResult AfterArrayRestore =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 restore array size"),
            RestoreArray,
            true);
    CurrentArrayItem = FindPinItem(AfterArrayRestore, ArrayPinPath);
    CurrentArrayState = GetDelimitedValue(
        CurrentArrayItem, TEXT(" ArrayState="), FString());
    Test.TestEqual(TEXT("Control Rig array cleanup survives reload"),
        ArrayStateSize(CurrentArrayState), InitialArraySize);

    FThomasAnimationOperation UnresolveTemplate;
    UnresolveTemplate.Action =
        TEXT("unresolve_control_rig_template_node");
    UnresolveTemplate.Name = TemplateNodeName;
    UnresolveTemplate.ExpectedValue =
        NodeState(AfterArrayRestore, TemplateNodeName);
    FThomasAnimationPatchRequest UnconfirmedUnresolveRequest;
    UnconfirmedUnresolveRequest.AssetPath = ControlRigPath;
    UnconfirmedUnresolveRequest.ExpectedRevision =
        AfterArrayRestore.Revision;
    UnconfirmedUnresolveRequest.Operations.Add(UnresolveTemplate);
    Test.TestEqual(TEXT("Control Rig template unresolve requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedUnresolveRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterTemplateUnresolve =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 unresolve array template"),
            UnresolveTemplate,
            true);
    Test.TestTrue(TEXT("Control Rig array template unresolve survives reload"),
        AfterTemplateUnresolve.bOk
            && NodeState(
                AfterTemplateUnresolve, TemplateNodeName).Contains(
                    TEXT("TemplateFullyUnresolved=true"))
            && FindPinItem(
                AfterTemplateUnresolve, ArrayPinPath).Contains(
                    TEXT(" IsWildCard=true ")));

    FThomasAnimationOperation RemoveTemplate;
    RemoveTemplate.Action = TEXT("remove_control_rig_template_node");
    RemoveTemplate.Name = TemplateNodeName;
    RemoveTemplate.ExpectedValue =
        NodeState(AfterTemplateUnresolve, TemplateNodeName);
    FThomasSpecializedAssetInspectionResult AfterTemplateRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove template node"),
            RemoveTemplate,
            true);
    Test.TestTrue(TEXT("Control Rig template cleanup survives reload"),
        AfterTemplateRemove.bOk
            && AfterTemplateRemove.Metrics.Contains(
                TEXT("RigVMNodeCount=0"))
            && FindNodeItem(
                AfterTemplateRemove, TemplateNodeName).IsEmpty());

    Test.TestTrue(TEXT("Control Rig discovery separates dispatch templates"),
        AfterTemplateRemove.Metrics.ContainsByPredicate(
            [](const FString& Metric)
            {
                return Metric.StartsWith(
                    TEXT("RigVMRegisteredDispatchTemplateCount="))
                    && !Metric.EndsWith(TEXT("=0"));
            })
        && AfterTemplateRemove.Items.ContainsByPredicate(
            [](const FString& Item)
            {
                return Item.StartsWith(
                    TEXT("ControlRigAvailableDispatch "));
            }));

    FString DispatchNotation;
    FString FallbackDispatchNotation;
    for (const FString& Item : AfterTemplateRemove.Items)
    {
        if (!Item.StartsWith(TEXT("ControlRigAvailableDispatch ")))
        {
            continue;
        }
        const FString Notation = GetDelimitedValue(
            Item, TEXT(" Notation="), TEXT(" Factory="));
        if (FallbackDispatchNotation.IsEmpty())
        {
            FallbackDispatchNotation = Notation;
        }
        if (Item.Contains(TEXT("CoreEquals"))
            || Notation.StartsWith(TEXT("Equals::")))
        {
            DispatchNotation = Notation;
            break;
        }
    }
    if (DispatchNotation.IsEmpty())
    {
        DispatchNotation = FallbackDispatchNotation;
    }
    Test.AddInfo(TEXT("Control Rig selected dispatch notation=")
        + DispatchNotation);
    Test.TestFalse(TEXT("Control Rig discovery provides a dispatch notation"),
        DispatchNotation.IsEmpty());

    const FString DispatchNodeName = TEXT("TE_RigVMDispatch");
    FThomasAnimationOperation AddDispatch;
    AddDispatch.Action = TEXT("add_control_rig_template_node");
    AddDispatch.Name = DispatchNodeName;
    AddDispatch.RigVMTemplateNotation = DispatchNotation;
    AddDispatch.PositionX = 900;
    AddDispatch.PositionY = 200;
    FThomasSpecializedAssetInspectionResult AfterDispatchAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add registered dispatch node"),
            AddDispatch,
            false);
    Test.TestTrue(TEXT("Control Rig dispatch node survives reload"),
        AfterDispatchAdd.bOk
            && NodeState(AfterDispatchAdd, DispatchNodeName).Contains(
                TEXT("Class=/Script/RigVMDeveloper.RigVMDispatchNode"))
            && NodeState(AfterDispatchAdd, DispatchNodeName).Contains(
                TEXT("|Dispatch=true|DispatchFactory="))
            && !NodeState(AfterDispatchAdd, DispatchNodeName).Contains(
                TEXT("|DispatchFactory=None")));

    const FString DispatchWildcardPinItem = FindWildcardPinItem(
        AfterDispatchAdd, DispatchNodeName);
    const FString DispatchWildcardPinPath = GetDelimitedValue(
        DispatchWildcardPinItem, TEXT(" Path="), TEXT(" Direction="));
    const FString DispatchWildcardTypeState = GetDelimitedValue(
        DispatchWildcardPinItem,
        TEXT(" TypeState="),
        TEXT(" ArrayState="));
    Test.TestTrue(TEXT("Control Rig dispatch exposes a wildcard input"),
        !DispatchWildcardPinPath.IsEmpty()
            && DispatchWildcardTypeState.Contains(
                TEXT("IsWildCard=true")));

    FThomasAnimationOperation ResolveDispatch;
    ResolveDispatch.Action = TEXT("resolve_control_rig_wildcard_pin");
    ResolveDispatch.RigVMPinPath = DispatchWildcardPinPath;
    ResolveDispatch.RigVMType = TEXT("float");
    ResolveDispatch.ExpectedValue = DispatchWildcardTypeState;
    FThomasSpecializedAssetInspectionResult AfterDispatchResolve =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM resolve dispatch wildcard to float"),
            ResolveDispatch,
            false);
    const FString ResolvedDispatchPinItem = FindPinItem(
        AfterDispatchResolve, DispatchWildcardPinPath);
    Test.TestTrue(TEXT("Control Rig dispatch resolution survives reload"),
        AfterDispatchResolve.bOk
            && ResolvedDispatchPinItem.Contains(TEXT(" CPPType=float "))
            && ResolvedDispatchPinItem.Contains(
                TEXT(" IsWildCard=false "))
            && NodeState(
                AfterDispatchResolve, DispatchNodeName).Contains(
                    TEXT("TemplateFullyUnresolved=false"))
            && NodeState(
                AfterDispatchResolve, DispatchNodeName).Contains(
                    TEXT("TemplatePermutations=1")));

    const FString ResolvedDispatchTypeState = GetDelimitedValue(
        ResolvedDispatchPinItem,
        TEXT(" TypeState="), TEXT(" ArrayState="));
    FThomasAnimationOperation RetypeDispatch;
    RetypeDispatch.Action =
        TEXT("set_control_rig_template_pin_type");
    RetypeDispatch.RigVMPinPath = DispatchWildcardPinPath;
    RetypeDispatch.RigVMType = TEXT("double");
    RetypeDispatch.ExpectedValue = ResolvedDispatchTypeState;
    FThomasAnimationPatchRequest UnconfirmedRetypeDispatchRequest;
    UnconfirmedRetypeDispatchRequest.AssetPath = ControlRigPath;
    UnconfirmedRetypeDispatchRequest.ExpectedRevision =
        AfterDispatchResolve.Revision;
    UnconfirmedRetypeDispatchRequest.Operations.Add(RetypeDispatch);
    Test.TestEqual(TEXT("Control Rig resolved template retype requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedRetypeDispatchRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterDispatchRetype =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 retype resolved dispatch"),
            RetypeDispatch,
            true);
    const FString RetypedDispatchPinItem = FindPinItem(
        AfterDispatchRetype, DispatchWildcardPinPath);
    int32 RetypedDispatchDoublePinCount = 0;
    for (const FString& Item : AfterDispatchRetype.Items)
    {
        if (Item.StartsWith(TEXT("ControlRigPin Graph="))
            && Item.Contains(TEXT(" Path=")
                + DispatchNodeName + TEXT("."))
            && Item.Contains(TEXT(" CPPType=double ")))
        {
            ++RetypedDispatchDoublePinCount;
        }
    }
    Test.TestTrue(TEXT("Control Rig resolved template retype survives reload"),
        AfterDispatchRetype.bOk
            && RetypedDispatchPinItem.Contains(
                TEXT(" CPPType=double "))
            && RetypedDispatchPinItem.Contains(
                TEXT(" IsWildCard=false "))
            && RetypedDispatchDoublePinCount >= 2
            && NodeState(
                AfterDispatchRetype, DispatchNodeName).Contains(
                    TEXT("TemplateFullyUnresolved=false"))
            && NodeState(
                AfterDispatchRetype, DispatchNodeName).Contains(
                    TEXT("TemplatePermutations=1")));

    FThomasAnimationOperation UnresolveDispatch;
    UnresolveDispatch.Action =
        TEXT("unresolve_control_rig_template_node");
    UnresolveDispatch.Name = DispatchNodeName;
    UnresolveDispatch.ExpectedValue =
        NodeState(AfterDispatchRetype, DispatchNodeName);
    FThomasSpecializedAssetInspectionResult AfterDispatchUnresolve =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 unresolve dispatch"),
            UnresolveDispatch,
            true);
    Test.TestTrue(TEXT("Control Rig dispatch unresolve survives reload"),
        AfterDispatchUnresolve.bOk
            && NodeState(
                AfterDispatchUnresolve, DispatchNodeName).Contains(
                    TEXT("TemplateFullyUnresolved=true"))
            && FindWildcardPinItem(
                AfterDispatchUnresolve, DispatchNodeName).Contains(
                    TEXT(" IsWildCard=true ")));

    FThomasAnimationOperation RemoveDispatch;
    RemoveDispatch.Action = TEXT("remove_control_rig_template_node");
    RemoveDispatch.Name = DispatchNodeName;
    RemoveDispatch.ExpectedValue =
        NodeState(AfterDispatchUnresolve, DispatchNodeName);
    FThomasSpecializedAssetInspectionResult AfterDispatchRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove dispatch node"),
            RemoveDispatch,
            true);
    Test.TestTrue(TEXT("Control Rig dispatch cleanup survives reload"),
        AfterDispatchRemove.bOk
            && AfterDispatchRemove.Metrics.Contains(
                TEXT("RigVMNodeCount=0"))
            && AfterDispatchRemove.Metrics.Contains(
                TEXT("RigVMLinkCount=0"))
            && FindNodeItem(
                AfterDispatchRemove, DispatchNodeName).IsEmpty());

    Test.TestTrue(TEXT("Control Rig discovery exposes registered events"),
        AfterDispatchRemove.Metrics.ContainsByPredicate(
            [](const FString& Metric)
            {
                return Metric.StartsWith(
                    TEXT("RigVMRegisteredEventCount="))
                    && !Metric.EndsWith(TEXT("=0"));
            })
        && AfterDispatchRemove.Items.ContainsByPredicate(
            [](const FString& Item)
            {
                return Item.StartsWith(
                    TEXT("ControlRigAvailableEvent "));
            }));
    FString EventStructPath;
    FString EventMethod;
    FString EventName;
    FString FallbackEventStructPath;
    FString FallbackEventMethod;
    FString FallbackEventName;
    for (const FString& Item : AfterDispatchRemove.Items)
    {
        if (!Item.StartsWith(TEXT("ControlRigAvailableEvent ")))
        {
            continue;
        }
        const FString StructPath = GetDelimitedValue(
            Item, TEXT(" Struct="), TEXT(" Method="));
        const FString InspectedMethod = GetDelimitedValue(
            Item, TEXT(" Method="), TEXT(" Function="));
        const FString InspectedEventName = GetDelimitedValue(
            Item, TEXT(" EventName="), TEXT(" CanOnlyExistOnce="));
        if (FallbackEventStructPath.IsEmpty())
        {
            FallbackEventStructPath = StructPath;
            FallbackEventMethod = InspectedMethod;
            FallbackEventName = InspectedEventName;
        }
        if (StructPath.Contains(TEXT("RigUnit_BeginExecution")))
        {
            EventStructPath = StructPath;
            EventMethod = InspectedMethod;
            EventName = InspectedEventName;
            break;
        }
    }
    if (EventStructPath.IsEmpty())
    {
        EventStructPath = FallbackEventStructPath;
        EventMethod = FallbackEventMethod;
        EventName = FallbackEventName;
    }
    Test.AddInfo(TEXT("Control Rig selected event struct=")
        + EventStructPath + TEXT(" method=") + EventMethod
        + TEXT(" event=") + EventName);
    Test.TestTrue(TEXT("Control Rig discovery provides a registered event"),
        !EventStructPath.IsEmpty()
            && !EventMethod.IsEmpty()
            && !EventName.IsEmpty());

    const FString EventNodeName = TEXT("TE_RigVMEvent");
    FThomasAnimationOperation AddEvent;
    AddEvent.Action = TEXT("add_control_rig_event_node");
    AddEvent.Name = EventNodeName;
    AddEvent.ClassPath = EventStructPath;
    AddEvent.RigVMMethodName = EventMethod;
    AddEvent.PositionX = 1100;
    AddEvent.PositionY = 200;
    FThomasSpecializedAssetInspectionResult AfterEventAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add registered event node"),
            AddEvent,
            false);
    Test.TestTrue(TEXT("Control Rig event node survives reload"),
        AfterEventAdd.bOk
            && AfterEventAdd.Metrics.Contains(TEXT("RigVMNodeCount=1"))
            && NodeState(AfterEventAdd, EventNodeName).Contains(
                TEXT("|Event=true|EventName=") + EventName)
            && NodeState(AfterEventAdd, EventNodeName).Contains(
                TEXT("Struct=") + EventStructPath)
            && NodeState(AfterEventAdd, EventNodeName).Contains(
                TEXT("|Method=") + EventMethod));

    FThomasAnimationOperation DuplicateEvent = AddEvent;
    DuplicateEvent.Name = TEXT("TE_RigVMEventDuplicate");
    FThomasAnimationPatchRequest DuplicateEventRequest;
    DuplicateEventRequest.AssetPath = ControlRigPath;
    DuplicateEventRequest.ExpectedRevision = AfterEventAdd.Revision;
    DuplicateEventRequest.Operations.Add(DuplicateEvent);
    Test.TestEqual(TEXT("Control Rig duplicate event is rejected"),
        UThomasDomainsToolset::PlanAnimationPatch(
            DuplicateEventRequest).Code,
        FString(TEXT("invalid_control_rig_event_node")));

    FThomasAnimationOperation RemoveEvent;
    RemoveEvent.Action = TEXT("remove_control_rig_event_node");
    RemoveEvent.Name = EventNodeName;
    RemoveEvent.ExpectedValue = NodeState(AfterEventAdd, EventNodeName);
    FThomasAnimationPatchRequest UnconfirmedEventRemoveRequest;
    UnconfirmedEventRemoveRequest.AssetPath = ControlRigPath;
    UnconfirmedEventRemoveRequest.ExpectedRevision = AfterEventAdd.Revision;
    UnconfirmedEventRemoveRequest.Operations.Add(RemoveEvent);
    Test.TestEqual(TEXT("Control Rig event removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedEventRemoveRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterEventRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove event node"),
            RemoveEvent,
            true);
    Test.TestTrue(TEXT("Control Rig event cleanup survives reload"),
        AfterEventRemove.bOk
            && AfterEventRemove.Metrics.Contains(TEXT("RigVMNodeCount=0"))
            && FindNodeItem(
                AfterEventRemove, EventNodeName).IsEmpty());

    const FString VariableName = TEXT("TE_Speed");
    FThomasAnimationOperation AddMemberVariable;
    AddMemberVariable.Action = TEXT("add_control_rig_member_variable");
    AddMemberVariable.RigVMVariableName = VariableName;
    AddMemberVariable.RigVMType = TEXT("float");
    AddMemberVariable.RigVMPinDefaultValue = TEXT("2.500000");
    FThomasSpecializedAssetInspectionResult AfterMemberVariableAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add float member variable"),
            AddMemberVariable,
            false);
    const FString MemberVariableItem = FindMemberVariableItem(
        AfterMemberVariableAdd, VariableName);
    const FString MemberVariableState = GetDelimitedValue(
        MemberVariableItem, TEXT(" State="), FString());
    Test.TestTrue(TEXT("Control Rig member variable survives reload"),
        AfterMemberVariableAdd.bOk
            && AfterMemberVariableAdd.Metrics.Contains(
                TEXT("RigVMMemberVariableCount=1"))
            && MemberVariableState.Contains(TEXT("CPPType=float"))
            && MemberVariableState.Contains(TEXT("Default=2.500000"))
            && MemberVariableState.Contains(TEXT("Guid=")));

    const FString VariableGetterNodeName = TEXT("TE_RigVMVariableGet");
    FThomasAnimationOperation AddVariableGetter;
    AddVariableGetter.Action = TEXT("add_control_rig_variable_node");
    AddVariableGetter.Name = VariableGetterNodeName;
    AddVariableGetter.RigVMVariableName = VariableName;
    AddVariableGetter.RigVMType = TEXT("float");
    AddVariableGetter.RigVMPinDefaultValue = TEXT("2.500000");
    AddVariableGetter.bRigVMVariableGetter = true;
    AddVariableGetter.PositionX = 1300;
    AddVariableGetter.PositionY = 200;
    FThomasSpecializedAssetInspectionResult AfterVariableGetterAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add float variable getter"),
            AddVariableGetter,
            false);
    Test.TestTrue(TEXT("Control Rig variable getter survives reload"),
        AfterVariableGetterAdd.bOk
            && AfterVariableGetterAdd.Metrics.Contains(
                TEXT("RigVMNodeCount=1"))
            && NodeState(
                AfterVariableGetterAdd,
                VariableGetterNodeName).Contains(
                    TEXT("|Variable=true|VariableName=TE_Speed|CPPType=float"))
            && NodeState(
                AfterVariableGetterAdd,
                VariableGetterNodeName).Contains(TEXT("|Getter=true"))
            && NodeState(
                AfterVariableGetterAdd,
                VariableGetterNodeName).Contains(
                    TEXT("|Default=2.500000")));

    FThomasAnimationOperation IncompatibleVariable = AddVariableGetter;
    IncompatibleVariable.Name = TEXT("TE_RigVMVariableIncompatible");
    IncompatibleVariable.RigVMType = TEXT("int32");
    FThomasAnimationPatchRequest IncompatibleVariableRequest;
    IncompatibleVariableRequest.AssetPath = ControlRigPath;
    IncompatibleVariableRequest.ExpectedRevision =
        AfterVariableGetterAdd.Revision;
    IncompatibleVariableRequest.Operations.Add(IncompatibleVariable);
    Test.TestEqual(TEXT("Control Rig variable rejects incompatible reuse"),
        UThomasDomainsToolset::PlanAnimationPatch(
            IncompatibleVariableRequest).Code,
        FString(TEXT("invalid_control_rig_variable_node")));

    const FString VariableSetterNodeName = TEXT("TE_RigVMVariableSet");
    FThomasAnimationOperation AddVariableSetter = AddVariableGetter;
    AddVariableSetter.Name = VariableSetterNodeName;
    AddVariableSetter.bRigVMVariableGetter = false;
    AddVariableSetter.PositionX = 1500;
    FThomasSpecializedAssetInspectionResult AfterVariableSetterAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add float variable setter"),
            AddVariableSetter,
            false);
    Test.TestTrue(TEXT("Control Rig variable setter survives reload"),
        AfterVariableSetterAdd.bOk
            && AfterVariableSetterAdd.Metrics.Contains(
                TEXT("RigVMNodeCount=2"))
            && NodeState(
                AfterVariableSetterAdd,
                VariableSetterNodeName).Contains(
                    TEXT("|Variable=true|VariableName=TE_Speed|CPPType=float"))
            && NodeState(
                AfterVariableSetterAdd,
                VariableSetterNodeName).Contains(TEXT("|Getter=false")));

    FThomasAnimationOperation RemoveVariableGetter;
    RemoveVariableGetter.Action = TEXT("remove_control_rig_variable_node");
    RemoveVariableGetter.Name = VariableGetterNodeName;
    RemoveVariableGetter.ExpectedValue = NodeState(
        AfterVariableSetterAdd, VariableGetterNodeName);
    FThomasAnimationPatchRequest UnconfirmedVariableRemoveRequest;
    UnconfirmedVariableRemoveRequest.AssetPath = ControlRigPath;
    UnconfirmedVariableRemoveRequest.ExpectedRevision =
        AfterVariableSetterAdd.Revision;
    UnconfirmedVariableRemoveRequest.Operations.Add(RemoveVariableGetter);
    Test.TestEqual(TEXT("Control Rig variable removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedVariableRemoveRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterVariableGetterRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove variable getter"),
            RemoveVariableGetter,
            true);
    Test.TestTrue(TEXT("Control Rig variable getter cleanup survives reload"),
        AfterVariableGetterRemove.bOk
            && AfterVariableGetterRemove.Metrics.Contains(
                TEXT("RigVMNodeCount=1"))
            && FindNodeItem(
                AfterVariableGetterRemove,
                VariableGetterNodeName).IsEmpty()
            && !FindNodeItem(
                AfterVariableGetterRemove,
                VariableSetterNodeName).IsEmpty());

    FThomasAnimationOperation RemoveVariableSetter;
    RemoveVariableSetter.Action = TEXT("remove_control_rig_variable_node");
    RemoveVariableSetter.Name = VariableSetterNodeName;
    RemoveVariableSetter.ExpectedValue = NodeState(
        AfterVariableGetterRemove, VariableSetterNodeName);
    FThomasSpecializedAssetInspectionResult AfterVariableSetterRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove variable setter"),
            RemoveVariableSetter,
            true);
    Test.TestTrue(TEXT("Control Rig variable cleanup survives reload"),
        AfterVariableSetterRemove.bOk
            && AfterVariableSetterRemove.Metrics.Contains(
                TEXT("RigVMNodeCount=0"))
            && AfterVariableSetterRemove.Metrics.Contains(
                TEXT("RigVMLinkCount=0"))
            && FindNodeItem(
                AfterVariableSetterRemove,
                VariableSetterNodeName).IsEmpty());

    FThomasAnimationOperation RemoveMemberVariable;
    RemoveMemberVariable.Action = TEXT("remove_control_rig_member_variable");
    RemoveMemberVariable.RigVMVariableName = VariableName;
    RemoveMemberVariable.ExpectedValue = GetDelimitedValue(
        FindMemberVariableItem(AfterVariableSetterRemove, VariableName),
        TEXT(" State="),
        FString());
    FThomasAnimationPatchRequest UnconfirmedMemberVariableRemoveRequest;
    UnconfirmedMemberVariableRemoveRequest.AssetPath = ControlRigPath;
    UnconfirmedMemberVariableRemoveRequest.ExpectedRevision =
        AfterVariableSetterRemove.Revision;
    UnconfirmedMemberVariableRemoveRequest.Operations.Add(
        RemoveMemberVariable);
    Test.TestEqual(TEXT("Control Rig member variable removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedMemberVariableRemoveRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterMemberVariableRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove member variable"),
            RemoveMemberVariable,
            true);
    Test.TestTrue(TEXT("Control Rig member variable cleanup survives reload"),
        AfterMemberVariableRemove.bOk
            && AfterMemberVariableRemove.Metrics.Contains(
                TEXT("RigVMMemberVariableCount=0"))
            && AfterMemberVariableRemove.Metrics.Contains(
                TEXT("RigVMNodeCount=0"))
            && FindMemberVariableItem(
                AfterMemberVariableRemove, VariableName).IsEmpty());

    const FString ExternalFunctionName = TEXT("TE_ExternalFunction");
    const FString ExternalFunctionCallName = TEXT("TE_ExternalFunctionCall");
    FThomasAnimationOperation AddExternalFunction;
    AddExternalFunction.Action = TEXT("add_control_rig_function");
    AddExternalFunction.RigVMFunctionName = ExternalFunctionName;
    AddExternalFunction.bRigVMFunctionMutable = true;
    AddExternalFunction.PositionX = 120;
    AddExternalFunction.PositionY = 80;
    const FThomasSpecializedAssetInspectionResult AfterExternalFunctionAdd =
        ApplyExternalControlRigOperationAndReload(
            TEXT("Control Rig RigVM add external-library function"),
            AddExternalFunction,
            false);
    const FString ExternalFunctionState = GetDelimitedValue(
        FindFunctionItem(
            AfterExternalFunctionAdd, ExternalFunctionName),
        TEXT(" State="),
        FString());
    const FString ExternalFunctionGraphName = GetDelimitedValue(
        ExternalFunctionState,
        TEXT("|ContainedGraph="),
        TEXT("|NodeCount="));
    Test.TestTrue(TEXT("Control Rig external-library function survives reload"),
        AfterExternalFunctionAdd.bOk
            && AfterExternalFunctionAdd.Metrics.Contains(
                TEXT("RigVMLocalFunctionCount=1"))
            && ExternalFunctionState.Contains(TEXT("|Public=false"))
            && !ExternalFunctionGraphName.IsEmpty());

    FThomasAnimationOperation AddExternalFunctionReference;
    AddExternalFunctionReference.Action =
        TEXT("add_control_rig_function_reference_node");
    AddExternalFunctionReference.ReferencedAssetPath =
        ExternalControlRigPath;
    AddExternalFunctionReference.RigVMFunctionName =
        ExternalFunctionName;
    AddExternalFunctionReference.Name = ExternalFunctionCallName;
    AddExternalFunctionReference.PositionX = 1750;
    AddExternalFunctionReference.PositionY = 300;
    const FThomasSpecializedAssetInspectionResult BeforeExternalReference =
        UThomasDomainsToolset::InspectAnimationAsset(
            ControlRigPath, 1000);
    FThomasAnimationPatchRequest PrivateExternalFunctionRequest;
    PrivateExternalFunctionRequest.AssetPath = ControlRigPath;
    PrivateExternalFunctionRequest.ExpectedRevision =
        BeforeExternalReference.Revision;
    PrivateExternalFunctionRequest.Operations.Add(
        AddExternalFunctionReference);
    Test.TestEqual(TEXT("Control Rig rejects a private external function"),
        UThomasDomainsToolset::PlanAnimationPatch(
            PrivateExternalFunctionRequest).Code,
        FString(TEXT("control_rig_external_function_not_public")));

    FThomasAnimationOperation PublishExternalFunction;
    PublishExternalFunction.Action =
        TEXT("set_public_control_rig_function");
    PublishExternalFunction.RigVMGraphName =
        ExternalFunctionGraphName;
    PublishExternalFunction.RigVMFunctionName = ExternalFunctionName;
    PublishExternalFunction.bRigVMFunctionPublic = true;
    PublishExternalFunction.ExpectedValue = ExternalFunctionState;
    const FThomasSpecializedAssetInspectionResult
        AfterExternalFunctionPublish =
            ApplyExternalControlRigOperationAndReload(
                TEXT("Control Rig RigVM publish external-library function"),
                PublishExternalFunction,
                false);
    Test.TestTrue(TEXT("Control Rig external function publication survives reload"),
        AfterExternalFunctionPublish.bOk
            && GetDelimitedValue(
                FindFunctionItem(
                    AfterExternalFunctionPublish,
                    ExternalFunctionName),
                TEXT(" State="),
                FString()).Contains(TEXT("|Public=true")));

    const FThomasSpecializedAssetInspectionResult AfterExternalReferenceAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add external function reference"),
            AddExternalFunctionReference,
            false);
    const FString ExternalReferenceState = NodeState(
        AfterExternalReferenceAdd, ExternalFunctionCallName);
    Test.TestTrue(TEXT("Control Rig external function reference survives reload"),
        AfterExternalReferenceAdd.bOk
            && AfterExternalReferenceAdd.Metrics.Contains(
                TEXT("RigVMFunctionReferenceCount=1"))
            && AfterExternalReferenceAdd.Metrics.Contains(
                TEXT("RigVMExternalFunctionReferenceCount=1"))
            && ExternalReferenceState.Contains(
                TEXT("|FunctionReference=true|FunctionName=")
                    + ExternalFunctionName)
            && ExternalReferenceState.Contains(
                TEXT("|FunctionHost=") + ExternalControlRigPath));

    const FThomasSpecializedAssetInspectionResult
        ExternalFunctionWithReference =
            UThomasDomainsToolset::InspectAnimationAsset(
                ExternalControlRigPath, 1000);
    const FString ExternalFunctionWithReferenceState = GetDelimitedValue(
        FindFunctionItem(
            ExternalFunctionWithReference,
            ExternalFunctionName),
        TEXT(" State="),
        FString());
    Test.TestTrue(TEXT("Control Rig external library sees its consumer reference"),
        ExternalFunctionWithReference.bOk
            && ExternalFunctionWithReferenceState.Contains(
                TEXT("|ReferenceCount=1")));
    FThomasAnimationOperation RemoveReferencedExternalFunction;
    RemoveReferencedExternalFunction.Action =
        TEXT("remove_control_rig_function");
    RemoveReferencedExternalFunction.RigVMFunctionName =
        ExternalFunctionName;
    RemoveReferencedExternalFunction.ExpectedValue =
        ExternalFunctionWithReferenceState;
    FThomasAnimationPatchRequest RemoveReferencedExternalFunctionRequest;
    RemoveReferencedExternalFunctionRequest.AssetPath =
        ExternalControlRigPath;
    RemoveReferencedExternalFunctionRequest.ExpectedRevision =
        ExternalFunctionWithReference.Revision;
    RemoveReferencedExternalFunctionRequest.bConfirmDestructive = true;
    RemoveReferencedExternalFunctionRequest.Operations.Add(
        RemoveReferencedExternalFunction);
    Test.TestEqual(TEXT("Control Rig protects an externally referenced function"),
        UThomasDomainsToolset::PlanAnimationPatch(
            RemoveReferencedExternalFunctionRequest).Code,
        FString(TEXT("control_rig_function_conflict")));

    FThomasAnimationOperation RemoveExternalFunctionReference;
    RemoveExternalFunctionReference.Action =
        TEXT("remove_control_rig_function_reference_node");
    RemoveExternalFunctionReference.ReferencedAssetPath =
        ExternalControlRigPath;
    RemoveExternalFunctionReference.RigVMFunctionName =
        ExternalFunctionName;
    RemoveExternalFunctionReference.Name = ExternalFunctionCallName;
    RemoveExternalFunctionReference.ExpectedValue =
        ExternalReferenceState;
    FThomasAnimationPatchRequest UnconfirmedExternalReferenceRemoveRequest;
    UnconfirmedExternalReferenceRemoveRequest.AssetPath = ControlRigPath;
    UnconfirmedExternalReferenceRemoveRequest.ExpectedRevision =
        AfterExternalReferenceAdd.Revision;
    UnconfirmedExternalReferenceRemoveRequest.Operations.Add(
        RemoveExternalFunctionReference);
    Test.TestEqual(TEXT("Control Rig external reference removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedExternalReferenceRemoveRequest).Code,
        FString(TEXT("confirmation_required")));
    const FThomasSpecializedAssetInspectionResult
        AfterExternalReferenceRemove =
            ApplyControlRigOperationAndReload(
                TEXT("Control Rig RigVM R2 remove external function reference"),
                RemoveExternalFunctionReference,
                true);
    Test.TestTrue(TEXT("Control Rig external function reference cleanup survives reload"),
        AfterExternalReferenceRemove.bOk
            && AfterExternalReferenceRemove.Metrics.Contains(
                TEXT("RigVMFunctionReferenceCount=0"))
            && AfterExternalReferenceRemove.Metrics.Contains(
                TEXT("RigVMExternalFunctionReferenceCount=0"))
            && NodeState(
                AfterExternalReferenceRemove,
                ExternalFunctionCallName).IsEmpty());

    const FThomasSpecializedAssetInspectionResult
        ExternalFunctionAfterReferenceRemove =
            UThomasDomainsToolset::InspectAnimationAsset(
                ExternalControlRigPath, 1000);
    FThomasAnimationOperation RemoveExternalFunction;
    RemoveExternalFunction.Action = TEXT("remove_control_rig_function");
    RemoveExternalFunction.RigVMFunctionName = ExternalFunctionName;
    RemoveExternalFunction.ExpectedValue = GetDelimitedValue(
        FindFunctionItem(
            ExternalFunctionAfterReferenceRemove,
            ExternalFunctionName),
        TEXT(" State="),
        FString());
    const FThomasSpecializedAssetInspectionResult
        AfterExternalFunctionRemove =
            ApplyExternalControlRigOperationAndReload(
                TEXT("Control Rig RigVM R2 remove external-library function"),
                RemoveExternalFunction,
                true);
    Test.TestTrue(TEXT("Control Rig external function cleanup survives reload"),
        AfterExternalFunctionRemove.bOk
            && AfterExternalFunctionRemove.Metrics.Contains(
                TEXT("RigVMLocalFunctionCount=0"))
            && FindFunctionItem(
                AfterExternalFunctionRemove,
                ExternalFunctionName).IsEmpty());

    const FString FunctionName = TEXT("TE_LocalFunction");
    FThomasAnimationOperation AddFunction;
    AddFunction.Action = TEXT("add_control_rig_function");
    AddFunction.RigVMFunctionName = FunctionName;
    AddFunction.bRigVMFunctionMutable = true;
    AddFunction.PositionX = 200;
    AddFunction.PositionY = 100;
    FThomasSpecializedAssetInspectionResult AfterFunctionAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add mutable local function"),
            AddFunction,
            false);
    const FString FunctionItem = FindFunctionItem(
        AfterFunctionAdd, FunctionName);
    const FString FunctionState = GetDelimitedValue(
        FunctionItem, TEXT(" State="), FString());
    const FString FunctionGraphName = GetDelimitedValue(
        FunctionState, TEXT("|ContainedGraph="), TEXT("|NodeCount="));
    Test.TestTrue(TEXT("Control Rig local function survives reload"),
        AfterFunctionAdd.bOk
            && AfterFunctionAdd.Metrics.Contains(
                TEXT("RigVMLocalFunctionCount=1"))
            && FunctionState.Contains(TEXT("|Mutable=true"))
            && !FunctionGraphName.IsEmpty()
            && FunctionState.Contains(TEXT("|ReferenceCount=0")));

    FThomasAnimationPatchRequest DuplicateFunctionRequest;
    DuplicateFunctionRequest.AssetPath = ControlRigPath;
    DuplicateFunctionRequest.ExpectedRevision = AfterFunctionAdd.Revision;
    DuplicateFunctionRequest.Operations.Add(AddFunction);
    Test.TestEqual(TEXT("Control Rig duplicate local function is rejected"),
        UThomasDomainsToolset::PlanAnimationPatch(
            DuplicateFunctionRequest).Code,
        FString(TEXT("invalid_control_rig_function")));

    FThomasAnimationOperation SetFunctionCategory;
    SetFunctionCategory.Action =
        TEXT("set_category_control_rig_function");
    SetFunctionCategory.RigVMGraphName = FunctionGraphName;
    SetFunctionCategory.RigVMFunctionName = FunctionName;
    SetFunctionCategory.RigVMFunctionCategory =
        TEXT("ThomasEditor.Tests");
    SetFunctionCategory.ExpectedValue = FunctionState;
    FThomasSpecializedAssetInspectionResult AfterFunctionCategory =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM set local function category"),
            SetFunctionCategory,
            false);
    Test.TestTrue(TEXT("Control Rig function category survives reload"),
        AfterFunctionCategory.bOk
            && GetDelimitedValue(
                FindFunctionItem(AfterFunctionCategory, FunctionName),
                TEXT(" State="),
                FString()).Contains(
                    TEXT("|Category=ThomasEditor.Tests")));

    FThomasAnimationOperation SetFunctionKeywords;
    SetFunctionKeywords.Action =
        TEXT("set_keywords_control_rig_function");
    SetFunctionKeywords.RigVMGraphName = FunctionGraphName;
    SetFunctionKeywords.RigVMFunctionName = FunctionName;
    SetFunctionKeywords.RigVMFunctionKeywords =
        TEXT("ThomasEditor Control Rig Test");
    SetFunctionKeywords.ExpectedValue = GetDelimitedValue(
        FindFunctionItem(AfterFunctionCategory, FunctionName),
        TEXT(" State="),
        FString());
    FThomasSpecializedAssetInspectionResult AfterFunctionKeywords =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM set local function keywords"),
            SetFunctionKeywords,
            false);
    Test.TestTrue(TEXT("Control Rig function keywords survive reload"),
        AfterFunctionKeywords.bOk
            && GetDelimitedValue(
                FindFunctionItem(AfterFunctionKeywords, FunctionName),
                TEXT(" State="),
                FString()).Contains(
                    TEXT("|Keywords=ThomasEditor Control Rig Test")));

    FThomasAnimationOperation SetFunctionDescription;
    SetFunctionDescription.Action =
        TEXT("set_description_control_rig_function");
    SetFunctionDescription.RigVMGraphName = FunctionGraphName;
    SetFunctionDescription.RigVMFunctionName = FunctionName;
    SetFunctionDescription.RigVMFunctionDescription =
        TEXT("Validated local Control Rig function");
    SetFunctionDescription.ExpectedValue = GetDelimitedValue(
        FindFunctionItem(AfterFunctionKeywords, FunctionName),
        TEXT(" State="),
        FString());
    FThomasSpecializedAssetInspectionResult AfterFunctionDescription =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM set local function description"),
            SetFunctionDescription,
            false);
    Test.TestTrue(TEXT("Control Rig function description survives reload"),
        AfterFunctionDescription.bOk
            && GetDelimitedValue(
                FindFunctionItem(AfterFunctionDescription, FunctionName),
                TEXT(" State="),
                FString()).Contains(
                    TEXT("|Description=Validated local Control Rig function")));

    FThomasAnimationOperation SetFunctionPublic;
    SetFunctionPublic.Action = TEXT("set_public_control_rig_function");
    SetFunctionPublic.RigVMGraphName = FunctionGraphName;
    SetFunctionPublic.RigVMFunctionName = FunctionName;
    SetFunctionPublic.bRigVMFunctionPublic = true;
    SetFunctionPublic.ExpectedValue = GetDelimitedValue(
        FindFunctionItem(AfterFunctionDescription, FunctionName),
        TEXT(" State="),
        FString());
    FThomasSpecializedAssetInspectionResult AfterFunctionPublic =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM publish local function"),
            SetFunctionPublic,
            false);
    Test.TestTrue(TEXT("Control Rig public function survives reload"),
        AfterFunctionPublic.bOk
            && GetDelimitedValue(
                FindFunctionItem(AfterFunctionPublic, FunctionName),
                TEXT(" State="),
                FString()).Contains(TEXT("|Public=true")));

    const FString LocalVariableName = TEXT("TE_LocalSpeed");
    FThomasAnimationOperation AddLocalVariable;
    AddLocalVariable.Action = TEXT("add_control_rig_local_variable");
    AddLocalVariable.RigVMGraphName = FunctionGraphName;
    AddLocalVariable.RigVMVariableName = LocalVariableName;
    AddLocalVariable.RigVMType = TEXT("float");
    AddLocalVariable.RigVMPinDefaultValue = TEXT("3.500000");
    FThomasSpecializedAssetInspectionResult AfterLocalVariableAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add function local variable"),
            AddLocalVariable,
            false);
    const FString LocalVariableItem = FindLocalVariableItem(
        AfterLocalVariableAdd, FunctionGraphName, LocalVariableName);
    const FString LocalVariableState = GetDelimitedValue(
        LocalVariableItem, TEXT(" State="), FString());
    Test.TestTrue(TEXT("Control Rig function local variable survives reload"),
        AfterLocalVariableAdd.bOk
            && AfterLocalVariableAdd.Metrics.Contains(
                TEXT("RigVMLocalVariableCount=1"))
            && LocalVariableState.Contains(TEXT("CPPType=float"))
            && LocalVariableState.Contains(TEXT("Default=3.500000"))
            && LocalVariableState.Contains(TEXT("Guid=")));

    FThomasAnimationOperation InvalidRootLocalVariable = AddLocalVariable;
    InvalidRootLocalVariable.RigVMGraphName.Reset();
    InvalidRootLocalVariable.RigVMVariableName = TEXT("TE_InvalidRootLocal");
    FThomasAnimationPatchRequest InvalidRootLocalVariableRequest;
    InvalidRootLocalVariableRequest.AssetPath = ControlRigPath;
    InvalidRootLocalVariableRequest.ExpectedRevision =
        AfterLocalVariableAdd.Revision;
    InvalidRootLocalVariableRequest.Operations.Add(
        InvalidRootLocalVariable);
    Test.TestEqual(TEXT("Control Rig root graph rejects local variable"),
        UThomasDomainsToolset::PlanAnimationPatch(
            InvalidRootLocalVariableRequest).Code,
        FString(TEXT("invalid_control_rig_local_variable")));

    const FString LocalGetterName = TEXT("TE_LocalVariableGet");
    FThomasAnimationOperation AddLocalGetter;
    AddLocalGetter.Action = TEXT("add_control_rig_variable_node");
    AddLocalGetter.RigVMGraphName = FunctionGraphName;
    AddLocalGetter.Name = LocalGetterName;
    AddLocalGetter.RigVMVariableName = LocalVariableName;
    AddLocalGetter.RigVMType = TEXT("float");
    AddLocalGetter.RigVMPinDefaultValue = TEXT("3.500000");
    AddLocalGetter.bRigVMVariableGetter = true;
    AddLocalGetter.PositionX = 250;
    AddLocalGetter.PositionY = 200;
    FThomasSpecializedAssetInspectionResult AfterLocalGetterAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add local variable getter"),
            AddLocalGetter,
            false);
    Test.TestTrue(TEXT("Control Rig local variable getter survives reload"),
        AfterLocalGetterAdd.bOk
            && NodeState(AfterLocalGetterAdd, LocalGetterName).Contains(
                TEXT("|Variable=true|VariableName=TE_LocalSpeed|CPPType=float"))
            && NodeState(AfterLocalGetterAdd, LocalGetterName).Contains(
                TEXT("|Getter=true|External=false|Local=true")));

    const FString LocalSetterName = TEXT("TE_LocalVariableSet");
    FThomasAnimationOperation AddLocalSetter = AddLocalGetter;
    AddLocalSetter.Name = LocalSetterName;
    AddLocalSetter.bRigVMVariableGetter = false;
    AddLocalSetter.PositionX = 450;
    FThomasSpecializedAssetInspectionResult AfterLocalSetterAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add local variable setter"),
            AddLocalSetter,
            false);
    Test.TestTrue(TEXT("Control Rig local variable setter survives reload"),
        AfterLocalSetterAdd.bOk
            && NodeState(AfterLocalSetterAdd, LocalSetterName).Contains(
                TEXT("|Variable=true|VariableName=TE_LocalSpeed|CPPType=float"))
            && NodeState(AfterLocalSetterAdd, LocalSetterName).Contains(
                TEXT("|Getter=false|External=false|Local=true")));

    const FString RenamedLocalVariableName = TEXT("TE_LocalRate");
    FThomasAnimationOperation RenameLocalVariable;
    RenameLocalVariable.Action =
        TEXT("rename_control_rig_local_variable");
    RenameLocalVariable.RigVMGraphName = FunctionGraphName;
    RenameLocalVariable.RigVMVariableName = LocalVariableName;
    RenameLocalVariable.NewName = RenamedLocalVariableName;
    RenameLocalVariable.ExpectedValue = GetDelimitedValue(
        FindLocalVariableItem(
            AfterLocalSetterAdd, FunctionGraphName, LocalVariableName),
        TEXT(" State="),
        FString());
    FThomasAnimationPatchRequest UnconfirmedLocalVariableRenameRequest;
    UnconfirmedLocalVariableRenameRequest.AssetPath = ControlRigPath;
    UnconfirmedLocalVariableRenameRequest.ExpectedRevision =
        AfterLocalSetterAdd.Revision;
    UnconfirmedLocalVariableRenameRequest.Operations.Add(
        RenameLocalVariable);
    Test.TestEqual(TEXT("Control Rig local variable rename requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedLocalVariableRenameRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterLocalVariableRename =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 rename local variable"),
            RenameLocalVariable,
            true);
    const FString RenamedLocalState = GetDelimitedValue(
        FindLocalVariableItem(
            AfterLocalVariableRename,
            FunctionGraphName,
            RenamedLocalVariableName),
        TEXT(" State="),
        FString());
    Test.TestTrue(TEXT("Control Rig local variable rename propagates"),
        AfterLocalVariableRename.bOk
            && FindLocalVariableItem(
                AfterLocalVariableRename,
                FunctionGraphName,
                LocalVariableName).IsEmpty()
            && RenamedLocalState.Contains(TEXT("CPPType=float"))
            && RenamedLocalState.Contains(TEXT("Default=3.500000"))
            && RenamedLocalState.Contains(TEXT("|Index=0"))
            && NodeState(
                AfterLocalVariableRename, LocalGetterName).Contains(
                    TEXT("|VariableName=TE_LocalRate|CPPType=float"))
            && NodeState(
                AfterLocalVariableRename, LocalSetterName).Contains(
                    TEXT("|VariableName=TE_LocalRate|CPPType=float")));

    FThomasAnimationOperation SetLocalVariableDefault;
    SetLocalVariableDefault.Action =
        TEXT("set_default_control_rig_local_variable");
    SetLocalVariableDefault.RigVMGraphName = FunctionGraphName;
    SetLocalVariableDefault.RigVMVariableName =
        RenamedLocalVariableName;
    SetLocalVariableDefault.RigVMPinDefaultValue = TEXT("6.250000");
    SetLocalVariableDefault.ExpectedValue = RenamedLocalState;
    FThomasSpecializedAssetInspectionResult AfterLocalVariableDefault =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM set local variable default"),
            SetLocalVariableDefault,
            false);
    Test.TestTrue(TEXT("Control Rig local variable default propagates"),
        AfterLocalVariableDefault.bOk
            && GetDelimitedValue(
                FindLocalVariableItem(
                    AfterLocalVariableDefault,
                    FunctionGraphName,
                    RenamedLocalVariableName),
                TEXT(" State="),
                FString()).Contains(TEXT("Default=6.250000")));

    FThomasAnimationOperation SetLocalVariableType;
    SetLocalVariableType.Action =
        TEXT("set_type_control_rig_local_variable");
    SetLocalVariableType.RigVMGraphName = FunctionGraphName;
    SetLocalVariableType.RigVMVariableName =
        RenamedLocalVariableName;
    SetLocalVariableType.RigVMType = TEXT("double");
    SetLocalVariableType.RigVMPinDefaultValue = TEXT("6.250000");
    SetLocalVariableType.ExpectedValue = GetDelimitedValue(
        FindLocalVariableItem(
            AfterLocalVariableDefault,
            FunctionGraphName,
            RenamedLocalVariableName),
        TEXT(" State="),
        FString());
    FThomasAnimationPatchRequest UnconfirmedLocalVariableTypeRequest;
    UnconfirmedLocalVariableTypeRequest.AssetPath = ControlRigPath;
    UnconfirmedLocalVariableTypeRequest.ExpectedRevision =
        AfterLocalVariableDefault.Revision;
    UnconfirmedLocalVariableTypeRequest.Operations.Add(
        SetLocalVariableType);
    Test.TestEqual(TEXT("Control Rig local variable type change requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedLocalVariableTypeRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterLocalVariableType =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 set local variable type"),
            SetLocalVariableType,
            true);
    Test.TestTrue(TEXT("Control Rig local variable type propagates"),
        AfterLocalVariableType.bOk
            && GetDelimitedValue(
                FindLocalVariableItem(
                    AfterLocalVariableType,
                    FunctionGraphName,
                    RenamedLocalVariableName),
                TEXT(" State="),
                FString()).Contains(
                    TEXT("CPPType=double|CPPTypeObject=None|Default=6.250000"))
            && NodeState(
                AfterLocalVariableType, LocalGetterName).Contains(
                    TEXT("|VariableName=TE_LocalRate|CPPType=double"))
            && NodeState(
                AfterLocalVariableType, LocalSetterName).Contains(
                    TEXT("|VariableName=TE_LocalRate|CPPType=double")));

    const FString SecondaryLocalVariableName = TEXT("TE_LocalOther");
    FThomasAnimationOperation AddSecondaryLocalVariable = AddLocalVariable;
    AddSecondaryLocalVariable.RigVMVariableName =
        SecondaryLocalVariableName;
    AddSecondaryLocalVariable.RigVMPinDefaultValue = TEXT("9.000000");
    FThomasSpecializedAssetInspectionResult AfterSecondaryLocalVariableAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add secondary local variable"),
            AddSecondaryLocalVariable,
            false);
    Test.TestTrue(TEXT("Control Rig secondary local variable survives reload"),
        AfterSecondaryLocalVariableAdd.bOk
            && AfterSecondaryLocalVariableAdd.Metrics.Contains(
                TEXT("RigVMLocalVariableCount=2"))
            && GetDelimitedValue(
                FindLocalVariableItem(
                    AfterSecondaryLocalVariableAdd,
                    FunctionGraphName,
                    SecondaryLocalVariableName),
                TEXT(" State="),
                FString()).Contains(TEXT("|Index=1")));

    FThomasAnimationOperation MoveLocalVariable;
    MoveLocalVariable.Action = TEXT("set_index_control_rig_local_variable");
    MoveLocalVariable.RigVMGraphName = FunctionGraphName;
    MoveLocalVariable.RigVMVariableName = RenamedLocalVariableName;
    MoveLocalVariable.Index = 1;
    MoveLocalVariable.ExpectedValue = GetDelimitedValue(
        FindLocalVariableItem(
            AfterSecondaryLocalVariableAdd,
            FunctionGraphName,
            RenamedLocalVariableName),
        TEXT(" State="),
        FString());
    FThomasSpecializedAssetInspectionResult AfterLocalVariableMove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM move local variable"),
            MoveLocalVariable,
            false);
    Test.TestTrue(TEXT("Control Rig local variable order survives reload"),
        AfterLocalVariableMove.bOk
            && GetDelimitedValue(
                FindLocalVariableItem(
                    AfterLocalVariableMove,
                    FunctionGraphName,
                    RenamedLocalVariableName),
                TEXT(" State="),
                FString()).Contains(TEXT("|Index=1"))
            && GetDelimitedValue(
                FindLocalVariableItem(
                    AfterLocalVariableMove,
                    FunctionGraphName,
                    SecondaryLocalVariableName),
                TEXT(" State="),
                FString()).Contains(TEXT("|Index=0")));

    FThomasAnimationOperation RestoreLocalVariableIndex = MoveLocalVariable;
    RestoreLocalVariableIndex.Index = 0;
    RestoreLocalVariableIndex.ExpectedValue = GetDelimitedValue(
        FindLocalVariableItem(
            AfterLocalVariableMove,
            FunctionGraphName,
            RenamedLocalVariableName),
        TEXT(" State="),
        FString());
    FThomasSpecializedAssetInspectionResult AfterLocalVariableIndexRestore =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM restore local variable index"),
            RestoreLocalVariableIndex,
            false);
    Test.TestTrue(TEXT("Control Rig local variable order restores"),
        AfterLocalVariableIndexRestore.bOk
            && GetDelimitedValue(
                FindLocalVariableItem(
                    AfterLocalVariableIndexRestore,
                    FunctionGraphName,
                    RenamedLocalVariableName),
                TEXT(" State="),
                FString()).Contains(TEXT("|Index=0")));

    FThomasAnimationOperation RemoveSecondaryLocalVariable;
    RemoveSecondaryLocalVariable.Action =
        TEXT("remove_control_rig_local_variable");
    RemoveSecondaryLocalVariable.RigVMGraphName = FunctionGraphName;
    RemoveSecondaryLocalVariable.RigVMVariableName =
        SecondaryLocalVariableName;
    RemoveSecondaryLocalVariable.ExpectedValue = GetDelimitedValue(
        FindLocalVariableItem(
            AfterLocalVariableIndexRestore,
            FunctionGraphName,
            SecondaryLocalVariableName),
        TEXT(" State="),
        FString());
    FThomasSpecializedAssetInspectionResult AfterSecondaryLocalVariableRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove secondary local variable"),
            RemoveSecondaryLocalVariable,
            true);
    Test.TestTrue(TEXT("Control Rig secondary local variable cleanup survives reload"),
        AfterSecondaryLocalVariableRemove.bOk
            && AfterSecondaryLocalVariableRemove.Metrics.Contains(
                TEXT("RigVMLocalVariableCount=1"))
            && FindLocalVariableItem(
                AfterSecondaryLocalVariableRemove,
                FunctionGraphName,
                SecondaryLocalVariableName).IsEmpty());

    FThomasAnimationOperation RemoveReferencedLocalVariable;
    RemoveReferencedLocalVariable.Action =
        TEXT("remove_control_rig_local_variable");
    RemoveReferencedLocalVariable.RigVMGraphName = FunctionGraphName;
    RemoveReferencedLocalVariable.RigVMVariableName =
        RenamedLocalVariableName;
    RemoveReferencedLocalVariable.ExpectedValue = GetDelimitedValue(
        FindLocalVariableItem(
            AfterSecondaryLocalVariableRemove,
            FunctionGraphName,
            RenamedLocalVariableName),
        TEXT(" State="),
        FString());
    FThomasAnimationPatchRequest RemoveReferencedLocalVariableRequest;
    RemoveReferencedLocalVariableRequest.AssetPath = ControlRigPath;
    RemoveReferencedLocalVariableRequest.ExpectedRevision =
        AfterSecondaryLocalVariableRemove.Revision;
    RemoveReferencedLocalVariableRequest.bConfirmDestructive = true;
    RemoveReferencedLocalVariableRequest.Operations.Add(
        RemoveReferencedLocalVariable);
    Test.TestEqual(TEXT("Control Rig referenced local variable is protected"),
        UThomasDomainsToolset::PlanAnimationPatch(
            RemoveReferencedLocalVariableRequest).Code,
        FString(TEXT("control_rig_local_variable_conflict")));

    FThomasAnimationOperation RemoveLocalGetter;
    RemoveLocalGetter.Action = TEXT("remove_control_rig_variable_node");
    RemoveLocalGetter.RigVMGraphName = FunctionGraphName;
    RemoveLocalGetter.Name = LocalGetterName;
    RemoveLocalGetter.ExpectedValue = NodeState(
        AfterSecondaryLocalVariableRemove, LocalGetterName);
    FThomasAnimationPatchRequest UnconfirmedLocalGetterRemoveRequest;
    UnconfirmedLocalGetterRemoveRequest.AssetPath = ControlRigPath;
    UnconfirmedLocalGetterRemoveRequest.ExpectedRevision =
        AfterSecondaryLocalVariableRemove.Revision;
    UnconfirmedLocalGetterRemoveRequest.Operations.Add(RemoveLocalGetter);
    Test.TestEqual(TEXT("Control Rig local getter removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedLocalGetterRemoveRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterLocalGetterRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove local variable getter"),
            RemoveLocalGetter,
            true);
    Test.TestTrue(TEXT("Control Rig local getter cleanup survives reload"),
        AfterLocalGetterRemove.bOk
            && FindNodeItem(
                AfterLocalGetterRemove, LocalGetterName).IsEmpty()
            && !FindNodeItem(
                AfterLocalGetterRemove, LocalSetterName).IsEmpty());

    FThomasAnimationOperation RemoveLocalSetter;
    RemoveLocalSetter.Action = TEXT("remove_control_rig_variable_node");
    RemoveLocalSetter.RigVMGraphName = FunctionGraphName;
    RemoveLocalSetter.Name = LocalSetterName;
    RemoveLocalSetter.ExpectedValue = NodeState(
        AfterLocalGetterRemove, LocalSetterName);
    FThomasSpecializedAssetInspectionResult AfterLocalSetterRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove local variable setter"),
            RemoveLocalSetter,
            true);
    Test.TestTrue(TEXT("Control Rig local setter cleanup survives reload"),
        AfterLocalSetterRemove.bOk
            && FindNodeItem(
                AfterLocalSetterRemove, LocalSetterName).IsEmpty());

    FThomasAnimationOperation RemoveLocalVariable =
        RemoveReferencedLocalVariable;
    RemoveLocalVariable.ExpectedValue = GetDelimitedValue(
        FindLocalVariableItem(
            AfterLocalSetterRemove,
            FunctionGraphName,
            RenamedLocalVariableName),
        TEXT(" State="),
        FString());
    FThomasAnimationPatchRequest UnconfirmedLocalVariableRemoveRequest;
    UnconfirmedLocalVariableRemoveRequest.AssetPath = ControlRigPath;
    UnconfirmedLocalVariableRemoveRequest.ExpectedRevision =
        AfterLocalSetterRemove.Revision;
    UnconfirmedLocalVariableRemoveRequest.Operations.Add(
        RemoveLocalVariable);
    Test.TestEqual(TEXT("Control Rig local variable removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedLocalVariableRemoveRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterLocalVariableRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove local variable"),
            RemoveLocalVariable,
            true);
    Test.TestTrue(TEXT("Control Rig local variable cleanup survives reload"),
        AfterLocalVariableRemove.bOk
            && AfterLocalVariableRemove.Metrics.Contains(
                TEXT("RigVMLocalVariableCount=0"))
            && FindLocalVariableItem(
                AfterLocalVariableRemove,
                FunctionGraphName,
                RenamedLocalVariableName).IsEmpty());

    const FString FunctionInputPinName = TEXT("InputScale");
    FThomasAnimationOperation AddFunctionInputPin;
    AddFunctionInputPin.Action = TEXT("add_control_rig_function_pin");
    AddFunctionInputPin.RigVMGraphName = FunctionGraphName;
    AddFunctionInputPin.RigVMFunctionName = FunctionName;
    AddFunctionInputPin.Name = FunctionInputPinName;
    AddFunctionInputPin.RigVMPinDirection = TEXT("input");
    AddFunctionInputPin.RigVMType = TEXT("float");
    AddFunctionInputPin.RigVMPinDefaultValue = TEXT("1.500000");
    FThomasSpecializedAssetInspectionResult AfterFunctionInputPinAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add function input pin"),
            AddFunctionInputPin,
            false);
    Test.TestTrue(TEXT("Control Rig function input pin survives reload"),
        AfterFunctionInputPinAdd.bOk
            && AfterFunctionInputPinAdd.Metrics.Contains(
                TEXT("RigVMFunctionPinCount=1"))
            && FunctionPinState(
                AfterFunctionInputPinAdd,
                FunctionName,
                FunctionInputPinName).Contains(
                    TEXT("Direction=Input|CPPType=float"))
            && FunctionPinState(
                AfterFunctionInputPinAdd,
                FunctionName,
                FunctionInputPinName).Contains(
                    TEXT("|Default=1.500000"))
            && FindPinItem(
                AfterFunctionInputPinAdd,
                TEXT("Entry.") + FunctionInputPinName).Contains(
                    TEXT(" Direction=Output ")));

    FThomasAnimationOperation InvalidRootFunctionPin =
        AddFunctionInputPin;
    InvalidRootFunctionPin.RigVMGraphName.Reset();
    InvalidRootFunctionPin.Name = TEXT("InvalidRootPin");
    FThomasAnimationPatchRequest InvalidRootFunctionPinRequest;
    InvalidRootFunctionPinRequest.AssetPath = ControlRigPath;
    InvalidRootFunctionPinRequest.ExpectedRevision =
        AfterFunctionInputPinAdd.Revision;
    InvalidRootFunctionPinRequest.Operations.Add(
        InvalidRootFunctionPin);
    Test.TestEqual(TEXT("Control Rig root graph rejects function pin"),
        UThomasDomainsToolset::PlanAnimationPatch(
            InvalidRootFunctionPinRequest).Code,
        FString(TEXT("invalid_control_rig_function_pin")));

    const FString FunctionOutputPinName = TEXT("ResultValue");
    FThomasAnimationOperation AddFunctionOutputPin =
        AddFunctionInputPin;
    AddFunctionOutputPin.Name = FunctionOutputPinName;
    AddFunctionOutputPin.RigVMPinDirection = TEXT("output");
    AddFunctionOutputPin.RigVMPinDefaultValue = TEXT("0.000000");
    FThomasSpecializedAssetInspectionResult AfterFunctionOutputPinAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add function output pin"),
            AddFunctionOutputPin,
            false);
    Test.TestTrue(TEXT("Control Rig function output pin survives reload"),
        AfterFunctionOutputPinAdd.bOk
            && AfterFunctionOutputPinAdd.Metrics.Contains(
                TEXT("RigVMFunctionPinCount=2"))
            && FunctionPinState(
                AfterFunctionOutputPinAdd,
                FunctionName,
                FunctionOutputPinName).Contains(
                    TEXT("Direction=Output|CPPType=float"))
            && FindPinItem(
                AfterFunctionOutputPinAdd,
                TEXT("Return.") + FunctionOutputPinName).Contains(
                    TEXT(" Direction=Input ")));

    FThomasAnimationOperation MoveFunctionOutputPin;
    MoveFunctionOutputPin.Action =
        TEXT("set_index_control_rig_function_pin");
    MoveFunctionOutputPin.RigVMGraphName = FunctionGraphName;
    MoveFunctionOutputPin.RigVMFunctionName = FunctionName;
    MoveFunctionOutputPin.Name = FunctionOutputPinName;
    MoveFunctionOutputPin.Index = 1;
    MoveFunctionOutputPin.ExpectedValue = FunctionPinState(
        AfterFunctionOutputPinAdd,
        FunctionName,
        FunctionOutputPinName);
    FThomasSpecializedAssetInspectionResult AfterFunctionOutputPinMove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM move function output pin"),
            MoveFunctionOutputPin,
            false);
    Test.TestTrue(TEXT("Control Rig function pin index survives reload"),
        AfterFunctionOutputPinMove.bOk
            && FunctionPinState(
                AfterFunctionOutputPinMove,
                FunctionName,
                FunctionOutputPinName).Contains(TEXT("|Index=1"))
            && FunctionPinState(
                AfterFunctionOutputPinMove,
                FunctionName,
                FunctionInputPinName).Contains(TEXT("|Index=2")));

    FThomasAnimationOperation RestoreFunctionOutputPin =
        MoveFunctionOutputPin;
    RestoreFunctionOutputPin.Index = 2;
    RestoreFunctionOutputPin.ExpectedValue = FunctionPinState(
        AfterFunctionOutputPinMove,
        FunctionName,
        FunctionOutputPinName);
    FThomasSpecializedAssetInspectionResult AfterFunctionOutputPinRestore =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM restore function output pin index"),
            RestoreFunctionOutputPin,
            false);
    Test.TestTrue(TEXT("Control Rig function pin index restores after reload"),
        AfterFunctionOutputPinRestore.bOk
            && FunctionPinState(
                AfterFunctionOutputPinRestore,
                FunctionName,
                FunctionInputPinName).Contains(TEXT("|Index=1"))
            && FunctionPinState(
                AfterFunctionOutputPinRestore,
                FunctionName,
                FunctionOutputPinName).Contains(TEXT("|Index=2")));

    FThomasAnimationPatchRequest DuplicateFunctionPinRequest;
    DuplicateFunctionPinRequest.AssetPath = ControlRigPath;
    DuplicateFunctionPinRequest.ExpectedRevision =
        AfterFunctionOutputPinRestore.Revision;
    DuplicateFunctionPinRequest.Operations.Add(AddFunctionInputPin);
    Test.TestEqual(TEXT("Control Rig duplicate function pin is rejected"),
        UThomasDomainsToolset::PlanAnimationPatch(
            DuplicateFunctionPinRequest).Code,
        FString(TEXT("invalid_control_rig_function_pin")));

    const FString FunctionCallName = TEXT("TE_LocalFunctionCall");
    FThomasAnimationOperation AddFunctionReference;
    AddFunctionReference.Action =
        TEXT("add_control_rig_function_reference_node");
    AddFunctionReference.RigVMFunctionName = FunctionName;
    AddFunctionReference.Name = FunctionCallName;
    AddFunctionReference.PositionX = 1700;
    AddFunctionReference.PositionY = 200;
    FThomasSpecializedAssetInspectionResult AfterFunctionReferenceAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM add local function reference"),
            AddFunctionReference,
            false);
    Test.TestTrue(TEXT("Control Rig function reference survives reload"),
        AfterFunctionReferenceAdd.bOk
            && NodeState(
                AfterFunctionReferenceAdd, FunctionCallName).Contains(
                    TEXT("|FunctionReference=true|FunctionName=")
                        + FunctionName)
            && NodeState(
                AfterFunctionReferenceAdd, FunctionCallName).Contains(
                    TEXT("|FunctionMutable=true"))
            && FindPinItem(
                AfterFunctionReferenceAdd,
                FunctionCallName + TEXT(".")
                    + FunctionInputPinName).Contains(
                        TEXT(" Direction=Input "))
            && FindPinItem(
                AfterFunctionReferenceAdd,
                FunctionCallName + TEXT(".")
                    + FunctionOutputPinName).Contains(
                        TEXT(" Direction=Output "))
            && GetDelimitedValue(
                FindFunctionItem(
                    AfterFunctionReferenceAdd, FunctionName),
                TEXT(" State="),
                FString()).Contains(TEXT("|ReferenceCount=1")));

    FThomasAnimationOperation SetFunctionPure;
    SetFunctionPure.Action = TEXT("set_mutable_control_rig_function");
    SetFunctionPure.RigVMGraphName = FunctionGraphName;
    SetFunctionPure.RigVMFunctionName = FunctionName;
    SetFunctionPure.bRigVMFunctionMutable = false;
    SetFunctionPure.ExpectedValue = GetDelimitedValue(
        FindFunctionItem(AfterFunctionReferenceAdd, FunctionName),
        TEXT(" State="),
        FString());
    FThomasAnimationPatchRequest UnconfirmedFunctionPureRequest;
    UnconfirmedFunctionPureRequest.AssetPath = ControlRigPath;
    UnconfirmedFunctionPureRequest.ExpectedRevision =
        AfterFunctionReferenceAdd.Revision;
    UnconfirmedFunctionPureRequest.Operations.Add(SetFunctionPure);
    Test.TestEqual(TEXT("Control Rig function purity change requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedFunctionPureRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterFunctionPure =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 make local function pure"),
            SetFunctionPure,
            true);
    Test.TestTrue(TEXT("Control Rig pure function propagates to call"),
        AfterFunctionPure.bOk
            && GetDelimitedValue(
                FindFunctionItem(AfterFunctionPure, FunctionName),
                TEXT(" State="),
                FString()).Contains(TEXT("|Mutable=false"))
            && NodeState(AfterFunctionPure, FunctionCallName).Contains(
                TEXT("|FunctionMutable=false"))
            && FindPinItem(
                AfterFunctionPure,
                FunctionCallName + TEXT(".ExecuteContext")).IsEmpty());

    FThomasAnimationOperation RestoreFunctionMutable = SetFunctionPure;
    RestoreFunctionMutable.bRigVMFunctionMutable = true;
    RestoreFunctionMutable.ExpectedValue = GetDelimitedValue(
        FindFunctionItem(AfterFunctionPure, FunctionName),
        TEXT(" State="),
        FString());
    FThomasSpecializedAssetInspectionResult AfterFunctionMutableRestore =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 restore mutable local function"),
            RestoreFunctionMutable,
            true);
    Test.TestTrue(TEXT("Control Rig mutable function restores on call"),
        AfterFunctionMutableRestore.bOk
            && GetDelimitedValue(
                FindFunctionItem(
                    AfterFunctionMutableRestore, FunctionName),
                TEXT(" State="),
                FString()).Contains(TEXT("|Mutable=true"))
            && NodeState(
                AfterFunctionMutableRestore, FunctionCallName).Contains(
                    TEXT("|FunctionMutable=true"))
            && FindPinItem(
                AfterFunctionMutableRestore,
                FunctionCallName + TEXT(".ExecuteContext")).Contains(
                    TEXT(" Direction=IO ")));

    const FString RenamedFunctionInputPinName = TEXT("Gain");
    FThomasAnimationOperation RenameFunctionInputPin;
    RenameFunctionInputPin.Action =
        TEXT("rename_control_rig_function_pin");
    RenameFunctionInputPin.RigVMGraphName = FunctionGraphName;
    RenameFunctionInputPin.RigVMFunctionName = FunctionName;
    RenameFunctionInputPin.Name = FunctionInputPinName;
    RenameFunctionInputPin.NewName = RenamedFunctionInputPinName;
    RenameFunctionInputPin.ExpectedValue = FunctionPinState(
        AfterFunctionMutableRestore,
        FunctionName,
        FunctionInputPinName);
    FThomasAnimationPatchRequest UnconfirmedFunctionPinRenameRequest;
    UnconfirmedFunctionPinRenameRequest.AssetPath = ControlRigPath;
    UnconfirmedFunctionPinRenameRequest.ExpectedRevision =
        AfterFunctionMutableRestore.Revision;
    UnconfirmedFunctionPinRenameRequest.Operations.Add(
        RenameFunctionInputPin);
    Test.TestEqual(TEXT("Control Rig function pin rename requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedFunctionPinRenameRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterFunctionInputPinRename =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 rename function input pin"),
            RenameFunctionInputPin,
            true);
    Test.TestTrue(TEXT("Control Rig function pin rename propagates to call"),
        AfterFunctionInputPinRename.bOk
            && FindFunctionPinItem(
                AfterFunctionInputPinRename,
                FunctionName,
                FunctionInputPinName).IsEmpty()
            && FunctionPinState(
                AfterFunctionInputPinRename,
                FunctionName,
                RenamedFunctionInputPinName).Contains(
                    TEXT("Direction=Input|CPPType=float"))
            && FindPinItem(
                AfterFunctionInputPinRename,
                FunctionCallName + TEXT(".")
                    + FunctionInputPinName).IsEmpty()
            && FindPinItem(
                AfterFunctionInputPinRename,
                FunctionCallName + TEXT(".")
                    + RenamedFunctionInputPinName).Contains(
                        TEXT(" Direction=Input ")));

    FThomasAnimationOperation SetFunctionOutputPinType;
    SetFunctionOutputPinType.Action =
        TEXT("set_type_control_rig_function_pin");
    SetFunctionOutputPinType.RigVMGraphName = FunctionGraphName;
    SetFunctionOutputPinType.RigVMFunctionName = FunctionName;
    SetFunctionOutputPinType.Name = FunctionOutputPinName;
    SetFunctionOutputPinType.RigVMType = TEXT("double");
    SetFunctionOutputPinType.ExpectedValue = FunctionPinState(
        AfterFunctionInputPinRename,
        FunctionName,
        FunctionOutputPinName);
    FThomasAnimationPatchRequest UnconfirmedFunctionPinTypeRequest;
    UnconfirmedFunctionPinTypeRequest.AssetPath = ControlRigPath;
    UnconfirmedFunctionPinTypeRequest.ExpectedRevision =
        AfterFunctionInputPinRename.Revision;
    UnconfirmedFunctionPinTypeRequest.Operations.Add(
        SetFunctionOutputPinType);
    Test.TestEqual(TEXT("Control Rig function pin type change requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedFunctionPinTypeRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterFunctionOutputPinType =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 set function output pin type"),
            SetFunctionOutputPinType,
            true);
    Test.TestTrue(TEXT("Control Rig function pin type propagates to call"),
        AfterFunctionOutputPinType.bOk
            && FunctionPinState(
                AfterFunctionOutputPinType,
                FunctionName,
                FunctionOutputPinName).Contains(
                    TEXT("Direction=Output|CPPType=double"))
            && FindPinItem(
                AfterFunctionOutputPinType,
                FunctionCallName + TEXT(".")
                    + FunctionOutputPinName).Contains(
                        TEXT(" Direction=Output CPPType=double ")));

    FThomasAnimationOperation RemoveFunctionInputPin;
    RemoveFunctionInputPin.Action =
        TEXT("remove_control_rig_function_pin");
    RemoveFunctionInputPin.RigVMGraphName = FunctionGraphName;
    RemoveFunctionInputPin.RigVMFunctionName = FunctionName;
    RemoveFunctionInputPin.Name = RenamedFunctionInputPinName;
    RemoveFunctionInputPin.ExpectedValue = FunctionPinState(
        AfterFunctionOutputPinType,
        FunctionName,
        RenamedFunctionInputPinName);
    FThomasSpecializedAssetInspectionResult AfterFunctionInputPinRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove function input pin"),
            RemoveFunctionInputPin,
            true);
    Test.TestTrue(TEXT("Control Rig function pin removal propagates to call"),
        AfterFunctionInputPinRemove.bOk
            && AfterFunctionInputPinRemove.Metrics.Contains(
                TEXT("RigVMFunctionPinCount=1"))
            && FindFunctionPinItem(
                AfterFunctionInputPinRemove,
                FunctionName,
                RenamedFunctionInputPinName).IsEmpty()
            && FindPinItem(
                AfterFunctionInputPinRemove,
                FunctionCallName + TEXT(".")
                    + RenamedFunctionInputPinName).IsEmpty());

    FThomasAnimationOperation RemoveReferencedFunction;
    RemoveReferencedFunction.Action = TEXT("remove_control_rig_function");
    RemoveReferencedFunction.RigVMFunctionName = FunctionName;
    RemoveReferencedFunction.ExpectedValue = GetDelimitedValue(
        FindFunctionItem(AfterFunctionInputPinRemove, FunctionName),
        TEXT(" State="),
        FString());
    FThomasAnimationPatchRequest RemoveReferencedFunctionRequest;
    RemoveReferencedFunctionRequest.AssetPath = ControlRigPath;
    RemoveReferencedFunctionRequest.ExpectedRevision =
        AfterFunctionInputPinRemove.Revision;
    RemoveReferencedFunctionRequest.bConfirmDestructive = true;
    RemoveReferencedFunctionRequest.Operations.Add(
        RemoveReferencedFunction);
    Test.TestEqual(TEXT("Control Rig referenced function is protected"),
        UThomasDomainsToolset::PlanAnimationPatch(
            RemoveReferencedFunctionRequest).Code,
        FString(TEXT("control_rig_function_conflict")));

    FThomasAnimationOperation RemoveFunctionReference;
    RemoveFunctionReference.Action =
        TEXT("remove_control_rig_function_reference_node");
    RemoveFunctionReference.RigVMFunctionName = FunctionName;
    RemoveFunctionReference.Name = FunctionCallName;
    RemoveFunctionReference.ExpectedValue = NodeState(
        AfterFunctionInputPinRemove, FunctionCallName);
    FThomasAnimationPatchRequest UnconfirmedFunctionReferenceRemoveRequest;
    UnconfirmedFunctionReferenceRemoveRequest.AssetPath = ControlRigPath;
    UnconfirmedFunctionReferenceRemoveRequest.ExpectedRevision =
        AfterFunctionInputPinRemove.Revision;
    UnconfirmedFunctionReferenceRemoveRequest.Operations.Add(
        RemoveFunctionReference);
    Test.TestEqual(TEXT("Control Rig function reference removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedFunctionReferenceRemoveRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterFunctionReferenceRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove function reference"),
            RemoveFunctionReference,
            true);
    Test.TestTrue(TEXT("Control Rig function reference cleanup survives reload"),
        AfterFunctionReferenceRemove.bOk
            && FindNodeItem(
                AfterFunctionReferenceRemove, FunctionCallName).IsEmpty()
            && GetDelimitedValue(
                FindFunctionItem(
                    AfterFunctionReferenceRemove, FunctionName),
                TEXT(" State="),
                FString()).Contains(TEXT("|ReferenceCount=0")));

    FThomasAnimationOperation RemoveFunctionOutputPin;
    RemoveFunctionOutputPin.Action =
        TEXT("remove_control_rig_function_pin");
    RemoveFunctionOutputPin.RigVMGraphName = FunctionGraphName;
    RemoveFunctionOutputPin.RigVMFunctionName = FunctionName;
    RemoveFunctionOutputPin.Name = FunctionOutputPinName;
    RemoveFunctionOutputPin.ExpectedValue = FunctionPinState(
        AfterFunctionReferenceRemove,
        FunctionName,
        FunctionOutputPinName);
    FThomasAnimationPatchRequest UnconfirmedFunctionPinRemoveRequest;
    UnconfirmedFunctionPinRemoveRequest.AssetPath = ControlRigPath;
    UnconfirmedFunctionPinRemoveRequest.ExpectedRevision =
        AfterFunctionReferenceRemove.Revision;
    UnconfirmedFunctionPinRemoveRequest.Operations.Add(
        RemoveFunctionOutputPin);
    Test.TestEqual(TEXT("Control Rig function pin removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedFunctionPinRemoveRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterFunctionOutputPinRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove function output pin"),
            RemoveFunctionOutputPin,
            true);
    Test.TestTrue(TEXT("Control Rig function pins clean up after reload"),
        AfterFunctionOutputPinRemove.bOk
            && AfterFunctionOutputPinRemove.Metrics.Contains(
                TEXT("RigVMFunctionPinCount=0"))
            && FindFunctionPinItem(
                AfterFunctionOutputPinRemove,
                FunctionName,
                FunctionOutputPinName).IsEmpty());

    FThomasAnimationOperation RemoveFunction;
    RemoveFunction.Action = TEXT("remove_control_rig_function");
    RemoveFunction.RigVMFunctionName = FunctionName;
    RemoveFunction.ExpectedValue = GetDelimitedValue(
        FindFunctionItem(AfterFunctionOutputPinRemove, FunctionName),
        TEXT(" State="),
        FString());
    FThomasAnimationPatchRequest UnconfirmedFunctionRemoveRequest;
    UnconfirmedFunctionRemoveRequest.AssetPath = ControlRigPath;
    UnconfirmedFunctionRemoveRequest.ExpectedRevision =
        AfterFunctionOutputPinRemove.Revision;
    UnconfirmedFunctionRemoveRequest.Operations.Add(RemoveFunction);
    Test.TestEqual(TEXT("Control Rig local function removal requires R2"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedFunctionRemoveRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterFunctionRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig RigVM R2 remove local function"),
            RemoveFunction,
            true);
    Test.TestTrue(TEXT("Control Rig local function cleanup survives reload"),
        AfterFunctionRemove.bOk
            && AfterFunctionRemove.Metrics.Contains(
                TEXT("RigVMLocalFunctionCount=0"))
            && AfterFunctionRemove.Metrics.Contains(
                TEXT("RigVMLocalVariableCount=0"))
            && AfterFunctionRemove.Metrics.Contains(
                TEXT("RigVMFunctionPinCount=0"))
            && AfterFunctionRemove.Metrics.Contains(
                TEXT("RigVMNodeCount=0"))
            && AfterFunctionRemove.Metrics.Contains(
                TEXT("RigVMLinkCount=0"))
            && FindFunctionItem(
                AfterFunctionRemove, FunctionName).IsEmpty());
}

static void RunControlRigHierarchyMutationTests(
    FThomasEditorNativeCoreTest& Test,
    const FString& ControlRigPath,
    const FString& ExternalControlRigPath,
    const USkeletalMesh* ControlRigMesh,
    int32 ExpectedControlRigBoneCount);

static FThomasAnimationTransitionToken MakeTransitionExpressionToken(
    const FString& Kind,
    const FString& Name = FString(),
    const FString& Operator = FString(),
    const double Value = 0.0,
    const FString& ContextName = FString())
{
    FThomasAnimationTransitionToken Token;
    Token.Kind = Kind;
    Token.Name = Name;
    Token.Operator = Operator;
    Token.Value = Value;
    Token.ContextName = ContextName;
    return Token;
}

static TArray<FThomasAnimationTransitionToken>
MakeR38TransitionExpressionTokens()
{
    return {
        MakeTransitionExpressionToken(
            TEXT("bool_variable"), TEXT("CanMove")),
        MakeTransitionExpressionToken(
            TEXT("transition_getter"), TEXT("current_time")),
        MakeTransitionExpressionToken(
            TEXT("transition_getter"), TEXT("time_remaining")),
        MakeTransitionExpressionToken(TEXT("add")),
        MakeTransitionExpressionToken(
            TEXT("transition_getter"), TEXT("length")),
        MakeTransitionExpressionToken(TEXT("subtract")),
        MakeTransitionExpressionToken(TEXT("abs")),
        MakeTransitionExpressionToken(
            TEXT("number"), FString(), FString(), 0.001),
        MakeTransitionExpressionToken(
            TEXT("compare"), FString(), TEXT("less_equal")),
        MakeTransitionExpressionToken(
            TEXT("transition_getter"), TEXT("current_time_fraction")),
        MakeTransitionExpressionToken(
            TEXT("transition_getter"), TEXT("time_remaining_fraction")),
        MakeTransitionExpressionToken(TEXT("max")),
        MakeTransitionExpressionToken(
            TEXT("transition_getter"), TEXT("state_blend_weight")),
        MakeTransitionExpressionToken(
            TEXT("number"), FString(), FString(), 1.0),
        MakeTransitionExpressionToken(TEXT("min")),
        MakeTransitionExpressionToken(
            TEXT("compare"), FString(), TEXT("greater_equal")),
        MakeTransitionExpressionToken(
            TEXT("transition_getter"), TEXT("state_elapsed_time")),
        MakeTransitionExpressionToken(
            TEXT("transition_getter"), TEXT("transition_duration")),
        MakeTransitionExpressionToken(TEXT("subtract")),
        MakeTransitionExpressionToken(
            TEXT("number"), FString(), FString(), 1.0),
        MakeTransitionExpressionToken(TEXT("multiply")),
        MakeTransitionExpressionToken(
            TEXT("number"), FString(), FString(), 1.0),
        MakeTransitionExpressionToken(TEXT("divide")),
        MakeTransitionExpressionToken(
            TEXT("number"), FString(), FString(), 999999.0),
        MakeTransitionExpressionToken(
            TEXT("compare"), FString(), TEXT("not_equal")),
        MakeTransitionExpressionToken(
            TEXT("transition_getter"), TEXT("state_elapsed_time")),
        MakeTransitionExpressionToken(
            TEXT("transition_getter"), TEXT("state_elapsed_time")),
        MakeTransitionExpressionToken(
            TEXT("compare"), FString(), TEXT("equal")),
        MakeTransitionExpressionToken(TEXT("and")),
        MakeTransitionExpressionToken(TEXT("and")),
        MakeTransitionExpressionToken(TEXT("or")),
        MakeTransitionExpressionToken(TEXT("and")),
        MakeTransitionExpressionToken(
            TEXT("transition_getter"),
            TEXT("arbitrary_state_blend_weight"),
            FString(),
            0.0,
            TEXT("Move")),
        MakeTransitionExpressionToken(
            TEXT("number"), FString(), FString(), 0.5),
        MakeTransitionExpressionToken(
            TEXT("compare"), FString(), TEXT("greater_equal")),
        MakeTransitionExpressionToken(TEXT("and"))};
}

static FString R38TransitionExpressionCanonical()
{
    return TEXT("bool_variable:CanMove|transition_getter:current_time|transition_getter:time_remaining|add|transition_getter:length|subtract|abs|number:0.001|compare:less_equal|transition_getter:current_time_fraction|transition_getter:time_remaining_fraction|max|transition_getter:state_blend_weight|number:1|min|compare:greater_equal|transition_getter:state_elapsed_time|transition_getter:transition_duration|subtract|number:1|multiply|number:1|divide|number:999999|compare:not_equal|transition_getter:state_elapsed_time|transition_getter:state_elapsed_time|compare:equal|and|and|or|and|transition_getter:arbitrary_state_blend_weight:Move|number:0.5|compare:greater_equal|and");
}

static void RunR38TransitionExpressionRefusalTests(
    FThomasEditorNativeCoreTest& Test,
    const FString& AnimBlueprintPath,
    const FThomasSpecializedAssetInspectionResult& BaselineInspection)
{
    auto CheckRefusal = [&](
        const FString& Label,
        TArray<FThomasAnimationTransitionToken> Tokens)
    {
        FThomasAnimationPatchRequest Request;
        Request.AssetPath = AnimBlueprintPath;
        Request.ExpectedRevision = BaselineInspection.Revision;
        FThomasAnimationOperation Operation;
        Operation.Action = TEXT("set_transition_rule_expression");
        Operation.MachineName = TEXT("Locomotion");
        Operation.FromStateName = TEXT("Idle");
        Operation.ToStateName = TEXT("Move");
        Operation.TransitionExpressionTokens = MoveTemp(Tokens);
        Request.Operations.Add(MoveTemp(Operation));
        Test.TestEqual(
            Label,
            UThomasDomainsToolset::PlanAnimationPatch(Request).Code,
            FString(TEXT("invalid_transition_expression")));
    };
    CheckRefusal(
        TEXT("Transition expression refuses getter without explicit arbitrary-state context"),
        {
            MakeTransitionExpressionToken(
                TEXT("transition_getter"),
                TEXT("arbitrary_state_blend_weight")),
            MakeTransitionExpressionToken(
                TEXT("number"), FString(), FString(), 0.5),
            MakeTransitionExpressionToken(
                TEXT("compare"), FString(), TEXT("greater_equal"))});
    CheckRefusal(
        TEXT("Transition expression refuses unknown arbitrary-state context"),
        {
            MakeTransitionExpressionToken(
                TEXT("transition_getter"),
                TEXT("arbitrary_state_blend_weight"),
                FString(),
                0.0,
                TEXT("MissingState")),
            MakeTransitionExpressionToken(
                TEXT("number"), FString(), FString(), 0.5),
            MakeTransitionExpressionToken(
                TEXT("compare"), FString(), TEXT("greater_equal"))});
    CheckRefusal(
        TEXT("Transition expression refuses context on non-arbitrary getter"),
        {
            MakeTransitionExpressionToken(
                TEXT("transition_getter"),
                TEXT("current_time"),
                FString(),
                0.0,
                TEXT("Move")),
            MakeTransitionExpressionToken(
                TEXT("number"), FString(), FString(), 0.5),
            MakeTransitionExpressionToken(
                TEXT("compare"), FString(), TEXT("greater_equal"))});
    CheckRefusal(
        TEXT("Transition expression rejects numeric Kismet call on bool"),
        {
            MakeTransitionExpressionToken(
                TEXT("bool_variable"), TEXT("CanMove")),
            MakeTransitionExpressionToken(TEXT("abs"))});
    CheckRefusal(
        TEXT("Transition expression refuses non-allowlisted Kismet call"),
        {
            MakeTransitionExpressionToken(
                TEXT("number"), FString(), FString(), 4.0),
            MakeTransitionExpressionToken(TEXT("sqrt")),
            MakeTransitionExpressionToken(
                TEXT("number"), FString(), FString(), 2.0),
            MakeTransitionExpressionToken(
                TEXT("compare"), FString(), TEXT("equal"))});
}

static FThomasSpecializedAssetInspectionResult
RunR38ArbitraryStateContextRenameTests(
    FThomasEditorNativeCoreTest& Test,
    const FString& AnimBlueprintPath,
    const FThomasSpecializedAssetInspectionResult& BaselineInspection)
{
    auto RenameAndReload = [&Test, &AnimBlueprintPath](
        const FString& Label,
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& FromName,
        const FString& ToName)
    {
        FThomasAnimationPatchRequest Request;
        Request.AssetPath = AnimBlueprintPath;
        Request.ExpectedRevision = Before.Revision;
        Request.bConfirmDestructive = true;
        FThomasAnimationOperation Operation;
        Operation.Action = TEXT("rename_state");
        Operation.MachineName = TEXT("Locomotion");
        Operation.StateName = FromName;
        Operation.NewName = ToName;
        Request.Operations.Add(Operation);
        const FThomasAnimationPlanResult Plan =
            UThomasDomainsToolset::PlanAnimationPatch(Request);
        Test.TestTrue(Label + TEXT(" plan succeeds"), Plan.bOk);
        if (!Plan.bOk)
        {
            return FThomasSpecializedAssetInspectionResult();
        }
        const FThomasAnimationApplyResult Apply =
            UThomasDomainsToolset::ApplyAnimationPlan(Plan.PlanId, true);
        Test.TestTrue(Label + TEXT(" compiles and saves"),
            Apply.bOk && Apply.bSaved);
        if (!Apply.bOk || !Apply.bSaved)
        {
            return FThomasSpecializedAssetInspectionResult();
        }
        UObject* SavedBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    AnimBlueprintPath)));
        FText ReloadError;
        Test.TestTrue(Label + TEXT(" reloads from saved file"),
            SavedBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedBlueprint->GetOutermost()},
                    ReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        return UThomasDomainsToolset::InspectAnimationAsset(
            AnimBlueprintPath, 800);
    };

    const FThomasSpecializedAssetInspectionResult Renamed = RenameAndReload(
        TEXT("Arbitrary-state context rename Move to MotionR38"),
        BaselineInspection,
        TEXT("Move"),
        TEXT("MotionR38"));
    Test.TestTrue(TEXT("Arbitrary-state getter follows saved state rename"),
        Renamed.bOk
            && Renamed.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.Contains(
                            TEXT("Transition Machine=Locomotion From=Idle To=MotionR38"))
                        && Item.Contains(
                            TEXT("transition_getter:arbitrary_state_blend_weight:MotionR38"))
                        && Item.Contains(TEXT("RuleExpressionTokens=36"))
                        && Item.Contains(TEXT("RuleNodes=31"));
                }));

    const FThomasSpecializedAssetInspectionResult Restored = RenameAndReload(
        TEXT("Arbitrary-state context restore MotionR38 to Move"),
        Renamed,
        TEXT("MotionR38"),
        TEXT("Move"));
    Test.TestTrue(TEXT("Arbitrary-state getter returns to exact canonical context"),
        Restored.bOk
            && Restored.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.Contains(
                            TEXT("Transition Machine=Locomotion From=Idle To=Move"))
                        && Item.Contains(
                            TEXT("RuleExpression=")
                                + R38TransitionExpressionCanonical())
                        && Item.Contains(TEXT("RuleExpressionTokens=36"))
                        && Item.Contains(TEXT("RuleNodes=31"));
                }));
    return Restored;
}

static FString R39DelimitedField(
    const FString& Item,
    const FString& Marker,
    const FString& NextMarker = FString())
{
    const int32 Start = Item.Find(Marker);
    if (Start == INDEX_NONE)
    {
        return FString();
    }
    const int32 ValueStart = Start + Marker.Len();
    const int32 End = NextMarker.IsEmpty()
        ? INDEX_NONE
        : Item.Find(
            NextMarker,
            ESearchCase::CaseSensitive,
            ESearchDir::FromStart,
            ValueStart);
    return End == INDEX_NONE
        ? Item.Mid(ValueStart)
        : Item.Mid(ValueStart, End - ValueStart);
}

static FString R39MoveStateGraphState(
    const FThomasSpecializedAssetInspectionResult& Inspection)
{
    const FString Prefix =
        TEXT("StateGraph Machine=Locomotion State=Move State=");
    for (const FString& Item : Inspection.Items)
    {
        if (Item.StartsWith(Prefix))
        {
            return Item.Mid(Prefix.Len());
        }
    }
    return FString();
}

static FString R39FindStateGraphPropertyItem(
    const FThomasSpecializedAssetInspectionResult& Inspection,
    const FString& ClassPath,
    const FString& PropertyPath,
    FString& OutNodeGuid)
{
    OutNodeGuid.Reset();
    const FString Prefix =
        TEXT("StateGraphNodeProperty Machine=Locomotion State=Move ");
    const FString ClassMarker = TEXT(" Class=") + ClassPath + TEXT(" ");
    const FString PropertyMarker =
        TEXT("State=Path=") + PropertyPath + TEXT("|");
    for (const FString& Item : Inspection.Items)
    {
        if (Item.StartsWith(Prefix)
            && Item.Contains(ClassMarker)
            && Item.Contains(PropertyMarker))
        {
            OutNodeGuid = R39DelimitedField(
                Item, TEXT("NodeGuid="), TEXT(" Class="));
            return Item;
        }
    }
    return FString();
}

static FThomasSpecializedAssetInspectionResult R39ApplyStateGraphProperty(
    FThomasEditorNativeCoreTest& Test,
    const FString& Label,
    const FString& AnimBlueprintPath,
    const FThomasSpecializedAssetInspectionResult& Before,
    const FString& NodeGuid,
    const FString& PropertyPath,
    const FString& Value)
{
    FThomasAnimationPatchRequest Request;
    Request.AssetPath = AnimBlueprintPath;
    Request.ExpectedRevision = Before.Revision;
    FThomasAnimationOperation Operation;
    Operation.Action = TEXT("set_state_graph_node_property");
    Operation.MachineName = TEXT("Locomotion");
    Operation.StateName = TEXT("Move");
    Operation.StateGraphNodeGuid = NodeGuid;
    Operation.PropertyName = PropertyPath;
    Operation.SettingValue = Value;
    Operation.ExpectedValue = R39MoveStateGraphState(Before);
    Request.Operations.Add(Operation);
    const FThomasAnimationPlanResult Plan =
        UThomasDomainsToolset::PlanAnimationPatch(Request);
    Test.TestTrue(Label + TEXT(" plan succeeds"), Plan.bOk);
    if (!Plan.bOk)
    {
        return FThomasSpecializedAssetInspectionResult();
    }
    const FThomasAnimationApplyResult Apply =
        UThomasDomainsToolset::ApplyAnimationPlan(Plan.PlanId, true);
    Test.AddInfo(FString::Printf(
        TEXT("%s apply: ok=%d saved=%d code=%s message=%s diagnostics=%s"),
        *Label,
        Apply.bOk ? 1 : 0,
        Apply.bSaved ? 1 : 0,
        *Apply.Code,
        *Apply.Message,
        *FString::Join(Apply.Diagnostics, TEXT(" | "))));
    Test.TestTrue(Label + TEXT(" compiles and saves"),
        Apply.bOk && Apply.bSaved);
    if (!Apply.bOk || !Apply.bSaved)
    {
        return UThomasDomainsToolset::InspectAnimationAsset(
            AnimBlueprintPath, 1400);
    }
    UObject* SavedBlueprint = LoadObject<UObject>(
        nullptr,
        *(AnimBlueprintPath + TEXT(".")
            + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
    FText ReloadError;
    Test.TestTrue(Label + TEXT(" reloads from saved file"),
        SavedBlueprint
            && UPackageTools::ReloadPackages(
                {SavedBlueprint->GetOutermost()},
                ReloadError,
                EReloadPackagesInteractionMode::AssumePositive));
    return UThomasDomainsToolset::InspectAnimationAsset(
        AnimBlueprintPath, 1400);
}

static FThomasSpecializedAssetInspectionResult
RunR39ComplexStateGraphPropertyTests(
    FThomasEditorNativeCoreTest& Test,
    const FString& AnimBlueprintPath,
    const FString& SkeletonContextPath,
    const FThomasSpecializedAssetInspectionResult& BaselineInspection,
    FString& OutTemporarySequencePath)
{
    const FString SequencePlayerClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer");
    const FString LayeredBlendClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_LayeredBoneBlend");
    const FString SequencePropertyPath = TEXT("Node.Sequence");
    const FString BlendDepthPropertyPath =
        TEXT("Node.LayerSetup[0].BranchFilters[0].BlendDepth");
    auto CheckRefusal = [&Test, &AnimBlueprintPath](
        const FString& Label,
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& NodeGuid,
        const FString& PropertyPath,
        const FString& Value,
        const FString& ExpectedCode)
    {
        FThomasAnimationPatchRequest Request;
        Request.AssetPath = AnimBlueprintPath;
        Request.ExpectedRevision = Before.Revision;
        FThomasAnimationOperation Operation;
        Operation.Action = TEXT("set_state_graph_node_property");
        Operation.MachineName = TEXT("Locomotion");
        Operation.StateName = TEXT("Move");
        Operation.StateGraphNodeGuid = NodeGuid;
        Operation.PropertyName = PropertyPath;
        Operation.SettingValue = Value;
        Operation.ExpectedValue = R39MoveStateGraphState(Before);
        Request.Operations.Add(Operation);
        Test.TestEqual(
            Label,
            UThomasDomainsToolset::PlanAnimationPatch(Request).Code,
            ExpectedCode);
    };

    FString SequenceNodeGuid;
    const FString SequencePropertyItem = R39FindStateGraphPropertyItem(
        BaselineInspection,
        SequencePlayerClass,
        SequencePropertyPath,
        SequenceNodeGuid);
    const FString InitialSequenceValue = R39DelimitedField(
        SequencePropertyItem, TEXT("|Value="));
    Test.TestTrue(
        TEXT("R39 exposes a guarded Animation object reference"),
        !SequenceNodeGuid.IsEmpty()
            && !InitialSequenceValue.IsEmpty()
            && InitialSequenceValue != TEXT("None"));

    OutTemporarySequencePath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/AS_TE_R39_Reference_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FThomasDomainAssetCreateRequest CreateRequest;
    CreateRequest.AssetPath = OutTemporarySequencePath;
    CreateRequest.ExpectedRevision = TEXT("missing");
    CreateRequest.AssetKind = TEXT("anim_sequence");
    CreateRequest.ContextAssetPath = SkeletonContextPath;
    const FThomasDomainAssetCreatePlanResult CreatePlan =
        UThomasDomainsToolset::PlanAnimationAssetCreate(CreateRequest);
    Test.TestTrue(TEXT("R39 reference Anim Sequence create plan succeeds"),
        CreatePlan.bOk);
    const FThomasDomainAssetCreateApplyResult CreateApply =
        CreatePlan.bOk
            ? UThomasDomainsToolset::ApplyAnimationAssetCreate(
                CreatePlan.PlanId, true)
            : FThomasDomainAssetCreateApplyResult();
    Test.TestTrue(TEXT("R39 reference Anim Sequence creates and saves"),
        CreateApply.bOk && CreateApply.bSaved);

    CheckRefusal(
        TEXT("R39 object reference refuses path outside PropHunt"),
        BaselineInspection,
        SequenceNodeGuid,
        SequencePropertyPath,
        TEXT("/Engine/EngineAnimations/DefaultAnim.DefaultAnim"),
        TEXT("invalid_state_graph_node_property_value"));
    CheckRefusal(
        TEXT("R39 object reference refuses incompatible asset class"),
        BaselineInspection,
        SequenceNodeGuid,
        SequencePropertyPath,
        SkeletonContextPath,
        TEXT("invalid_state_graph_node_property_value"));

    FThomasSpecializedAssetInspectionResult AfterObjectReference =
        R39ApplyStateGraphProperty(
            Test,
            TEXT("R39 Animation object reference"),
            AnimBlueprintPath,
            BaselineInspection,
            SequenceNodeGuid,
            SequencePropertyPath,
            OutTemporarySequencePath);
    FString ChangedSequenceNodeGuid;
    const FString ChangedSequenceItem = R39FindStateGraphPropertyItem(
        AfterObjectReference,
        SequencePlayerClass,
        SequencePropertyPath,
        ChangedSequenceNodeGuid);
    const FString ExpectedObjectPath = OutTemporarySequencePath + TEXT(".")
        + FPackageName::GetLongPackageAssetName(OutTemporarySequencePath);
    Test.TestTrue(
        TEXT("R39 Animation object reference survives save and reload"),
        ChangedSequenceNodeGuid == SequenceNodeGuid
            && R39DelimitedField(ChangedSequenceItem, TEXT("|Value="))
                == ExpectedObjectPath);

    FThomasSpecializedAssetInspectionResult AfterObjectRestore =
        R39ApplyStateGraphProperty(
            Test,
            TEXT("R39 Animation object reference restore"),
            AnimBlueprintPath,
            AfterObjectReference,
            SequenceNodeGuid,
            SequencePropertyPath,
            InitialSequenceValue);
    Test.TestEqual(
        TEXT("R39 object reference restore returns exact graph hash"),
        R39MoveStateGraphState(AfterObjectRestore),
        R39MoveStateGraphState(BaselineInspection));

    FString LayeredBlendNodeGuid;
    const FString BlendDepthPropertyItem = R39FindStateGraphPropertyItem(
        AfterObjectRestore,
        LayeredBlendClass,
        BlendDepthPropertyPath,
        LayeredBlendNodeGuid);
    const FString InitialBlendDepth = R39DelimitedField(
        BlendDepthPropertyItem, TEXT("|Value="));
    Test.TestTrue(
        TEXT("R39 exposes nested struct leaves inside bounded arrays"),
        !LayeredBlendNodeGuid.IsEmpty()
            && InitialBlendDepth == TEXT("1"));
    CheckRefusal(
        TEXT("R39 refuses an out-of-bounds state-graph array element"),
        AfterObjectRestore,
        LayeredBlendNodeGuid,
        TEXT("Node.LayerSetup[16].BranchFilters[0].BlendDepth"),
        TEXT("3"),
        TEXT("state_graph_node_property_not_found"));
    CheckRefusal(
        TEXT("R39 refuses an invalid nested array value"),
        AfterObjectRestore,
        LayeredBlendNodeGuid,
        BlendDepthPropertyPath,
        TEXT("not_an_integer"),
        TEXT("invalid_state_graph_node_property_value"));

    FThomasSpecializedAssetInspectionResult AfterBlendDepth =
        R39ApplyStateGraphProperty(
            Test,
            TEXT("R39 nested array blend depth"),
            AnimBlueprintPath,
            AfterObjectRestore,
            LayeredBlendNodeGuid,
            BlendDepthPropertyPath,
            TEXT("3"));
    FString ChangedBlendNodeGuid;
    const FString ChangedBlendDepthItem = R39FindStateGraphPropertyItem(
        AfterBlendDepth,
        LayeredBlendClass,
        BlendDepthPropertyPath,
        ChangedBlendNodeGuid);
    Test.TestTrue(
        TEXT("R39 nested array value survives save and specialized readback"),
        ChangedBlendNodeGuid == LayeredBlendNodeGuid
            && R39DelimitedField(
                ChangedBlendDepthItem, TEXT("|Value=")) == TEXT("3")
            && AfterBlendDepth.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.Contains(
                            TEXT("StateLayeredBlendLayer Machine=Locomotion State=Move Index=0"))
                        && Item.Contains(TEXT("BlendDepth=3"));
                }));

    FThomasSpecializedAssetInspectionResult Restored =
        R39ApplyStateGraphProperty(
            Test,
            TEXT("R39 nested array blend depth restore"),
            AnimBlueprintPath,
            AfterBlendDepth,
            LayeredBlendNodeGuid,
            BlendDepthPropertyPath,
            InitialBlendDepth);
    Test.TestEqual(
        TEXT("R39 nested array restore returns exact graph hash"),
        R39MoveStateGraphState(Restored),
        R39MoveStateGraphState(BaselineInspection));
    return Restored;
}

static FString R40FindStateGraphArrayItem(
    const FThomasSpecializedAssetInspectionResult& Inspection,
    const FString& ClassPath,
    const FString& ArrayPath,
    FString& OutNodeGuid)
{
    OutNodeGuid.Reset();
    const FString Prefix =
        TEXT("StateGraphNodeArray Machine=Locomotion State=Move ");
    const FString ClassMarker = TEXT(" Class=") + ClassPath + TEXT(" ");
    const FString PathMarker = TEXT("State=Path=")
        + ArrayPath + TEXT("|");
    for (const FString& Item : Inspection.Items)
    {
        if (Item.StartsWith(Prefix)
            && Item.Contains(ClassMarker)
            && Item.Contains(PathMarker))
        {
            OutNodeGuid = R39DelimitedField(
                Item, TEXT("NodeGuid="), TEXT(" Class="));
            return Item;
        }
    }
    return FString();
}

static FThomasSpecializedAssetInspectionResult
R40ApplyStateGraphArrayResize(
    FThomasEditorNativeCoreTest& Test,
    const FString& Label,
    const FString& AnimBlueprintPath,
    const FThomasSpecializedAssetInspectionResult& Before,
    const FString& NodeGuid,
    const FString& ArrayPath,
    const int32 TargetSize)
{
    FThomasAnimationPatchRequest Request;
    Request.AssetPath = AnimBlueprintPath;
    Request.ExpectedRevision = Before.Revision;
    Request.bConfirmDestructive = true;
    FThomasAnimationOperation Operation;
    Operation.Action = TEXT("resize_state_graph_node_array");
    Operation.MachineName = TEXT("Locomotion");
    Operation.StateName = TEXT("Move");
    Operation.StateGraphNodeGuid = NodeGuid;
    Operation.PropertyName = ArrayPath;
    Operation.StateGraphContainerSize = TargetSize;
    Operation.ExpectedValue = R39MoveStateGraphState(Before);
    Request.Operations.Add(Operation);
    const FThomasAnimationPlanResult Plan =
        UThomasDomainsToolset::PlanAnimationPatch(Request);
    Test.TestTrue(Label + TEXT(" plan succeeds"), Plan.bOk);
    if (!Plan.bOk)
    {
        return FThomasSpecializedAssetInspectionResult();
    }
    const FThomasAnimationApplyResult Apply =
        UThomasDomainsToolset::ApplyAnimationPlan(Plan.PlanId, true);
    Test.AddInfo(FString::Printf(
        TEXT("%s apply: ok=%d saved=%d code=%s message=%s diagnostics=%s"),
        *Label,
        Apply.bOk ? 1 : 0,
        Apply.bSaved ? 1 : 0,
        *Apply.Code,
        *Apply.Message,
        *FString::Join(Apply.Diagnostics, TEXT(" | "))));
    Test.TestTrue(Label + TEXT(" compiles and saves"),
        Apply.bOk && Apply.bSaved);
    if (!Apply.bOk || !Apply.bSaved)
    {
        return UThomasDomainsToolset::InspectAnimationAsset(
            AnimBlueprintPath, 1600);
    }
    UObject* SavedBlueprint = LoadObject<UObject>(
        nullptr,
        *(AnimBlueprintPath + TEXT(".")
            + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
    FText ReloadError;
    Test.TestTrue(Label + TEXT(" reloads from saved file"),
        SavedBlueprint
            && UPackageTools::ReloadPackages(
                {SavedBlueprint->GetOutermost()},
                ReloadError,
                EReloadPackagesInteractionMode::AssumePositive));
    return UThomasDomainsToolset::InspectAnimationAsset(
        AnimBlueprintPath, 1600);
}

static FThomasSpecializedAssetInspectionResult
RunR40StateGraphArrayLifecycleTests(
    FThomasEditorNativeCoreTest& Test,
    const FString& AnimBlueprintPath,
    const FThomasSpecializedAssetInspectionResult& BaselineInspection)
{
    const FString LayeredBlendClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_LayeredBoneBlend");
    const FString StructuralArrayPath = TEXT("Node.LayerSetup");
    const FString BranchFiltersPath =
        TEXT("Node.LayerSetup[0].BranchFilters");
    const FString AddedBlendDepthPath = BranchFiltersPath
        + TEXT("[1].BlendDepth");

    FString LayeredBlendNodeGuid;
    const FString BaselineArrayItem = R40FindStateGraphArrayItem(
        BaselineInspection,
        LayeredBlendClass,
        BranchFiltersPath,
        LayeredBlendNodeGuid);
    FString StructuralArrayNodeGuid;
    const FString StructuralArrayItem = R40FindStateGraphArrayItem(
        BaselineInspection,
        LayeredBlendClass,
        StructuralArrayPath,
        StructuralArrayNodeGuid);
    Test.TestTrue(
        TEXT("R40 exposes bounded nested arrays and structural resize policy"),
        !LayeredBlendNodeGuid.IsEmpty()
            && StructuralArrayNodeGuid == LayeredBlendNodeGuid
            && BaselineArrayItem.Contains(TEXT("|Num=1|Max=16|Resizable=true"))
            && StructuralArrayItem.Contains(
                TEXT("|Num=2|Max=16|Resizable=false"))
            && BaselineInspection.Metrics.ContainsByPredicate(
                [](const FString& Metric)
                {
                    return Metric.StartsWith(
                            TEXT("StateGraphNodeArrayCount="))
                        && !Metric.EndsWith(TEXT("=0"));
                }));

    auto PlanResize = [&AnimBlueprintPath, &LayeredBlendNodeGuid](
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& ArrayPath,
        const int32 TargetSize,
        const bool bConfirm)
    {
        FThomasAnimationPatchRequest Request;
        Request.AssetPath = AnimBlueprintPath;
        Request.ExpectedRevision = Before.Revision;
        Request.bConfirmDestructive = bConfirm;
        FThomasAnimationOperation Operation;
        Operation.Action = TEXT("resize_state_graph_node_array");
        Operation.MachineName = TEXT("Locomotion");
        Operation.StateName = TEXT("Move");
        Operation.StateGraphNodeGuid = LayeredBlendNodeGuid;
        Operation.PropertyName = ArrayPath;
        Operation.StateGraphContainerSize = TargetSize;
        Operation.ExpectedValue = R39MoveStateGraphState(Before);
        Request.Operations.Add(Operation);
        return UThomasDomainsToolset::PlanAnimationPatch(Request);
    };
    Test.TestEqual(
        TEXT("R40 array resize always requires R2 confirmation"),
        PlanResize(BaselineInspection, BranchFiltersPath, 2, false).Code,
        FString(TEXT("confirmation_required")));
    Test.TestEqual(
        TEXT("R40 refuses resize of a pin-coupled structural array"),
        PlanResize(BaselineInspection, StructuralArrayPath, 3, true).Code,
        FString(TEXT("state_graph_node_array_not_resizable")));
    Test.TestEqual(
        TEXT("R40 refuses array target size above the hard cap"),
        PlanResize(BaselineInspection, BranchFiltersPath, 17, true).Code,
        FString(TEXT("invalid_state_graph_node_array_size")));
    Test.TestEqual(
        TEXT("R40 refuses a no-op array resize"),
        PlanResize(BaselineInspection, BranchFiltersPath, 1, true).Code,
        FString(TEXT("state_graph_node_array_unchanged")));

    const FThomasSpecializedAssetInspectionResult AfterGrow =
        R40ApplyStateGraphArrayResize(
            Test,
            TEXT("R40 nested BranchFilters grow"),
            AnimBlueprintPath,
            BaselineInspection,
            LayeredBlendNodeGuid,
            BranchFiltersPath,
            2);
    FString GrownNodeGuid;
    const FString GrownArrayItem = R40FindStateGraphArrayItem(
        AfterGrow,
        LayeredBlendClass,
        BranchFiltersPath,
        GrownNodeGuid);
    FString AddedPropertyNodeGuid;
    const FString AddedPropertyItem = R39FindStateGraphPropertyItem(
        AfterGrow,
        LayeredBlendClass,
        AddedBlendDepthPath,
        AddedPropertyNodeGuid);
    Test.TestTrue(
        TEXT("R40 added array element survives compile save and reload"),
        GrownNodeGuid == LayeredBlendNodeGuid
            && AddedPropertyNodeGuid == LayeredBlendNodeGuid
            && GrownArrayItem.Contains(TEXT("|Num=2|Max=16|Resizable=true"))
            && AddedPropertyItem.Contains(TEXT("|Type=int32|"))
            && R39MoveStateGraphState(AfterGrow)
                != R39MoveStateGraphState(BaselineInspection));

    const FThomasSpecializedAssetInspectionResult AfterAddedValue =
        R39ApplyStateGraphProperty(
            Test,
            TEXT("R40 added BranchFilter value"),
            AnimBlueprintPath,
            AfterGrow,
            LayeredBlendNodeGuid,
            AddedBlendDepthPath,
            TEXT("4"));
    FString ChangedPropertyNodeGuid;
    Test.TestTrue(
        TEXT("R40 added array element value survives compile save and reload"),
        R39DelimitedField(
            R39FindStateGraphPropertyItem(
                AfterAddedValue,
                LayeredBlendClass,
                AddedBlendDepthPath,
                ChangedPropertyNodeGuid),
            TEXT("|Value=")) == TEXT("4")
            && ChangedPropertyNodeGuid == LayeredBlendNodeGuid);

    const FThomasSpecializedAssetInspectionResult Restored =
        R40ApplyStateGraphArrayResize(
            Test,
            TEXT("R40 nested BranchFilters shrink restore"),
            AnimBlueprintPath,
            AfterAddedValue,
            LayeredBlendNodeGuid,
            BranchFiltersPath,
            1);
    FString RestoredNodeGuid;
    FString RemovedPropertyNodeGuid;
    Test.TestTrue(
        TEXT("R40 shrink removes the element and restores the exact graph hash"),
        R40FindStateGraphArrayItem(
            Restored,
            LayeredBlendClass,
            BranchFiltersPath,
            RestoredNodeGuid).Contains(
                TEXT("|Num=1|Max=16|Resizable=true"))
            && RestoredNodeGuid == LayeredBlendNodeGuid
            && R39FindStateGraphPropertyItem(
                Restored,
                LayeredBlendClass,
                AddedBlendDepthPath,
                RemovedPropertyNodeGuid).IsEmpty()
            && R39MoveStateGraphState(Restored)
                == R39MoveStateGraphState(BaselineInspection));
    return Restored;
}

static FThomasSpecializedAssetInspectionResult RunGenericStateGraphTests(
    FThomasEditorNativeCoreTest& Test,
    const FString& AnimBlueprintPath,
    const FString& SkeletonContextPath,
    const FString& SequenceAssetPath,
    const FString& IKRigPath,
    const FString& IKRetargeterPath,
    const FThomasSpecializedAssetInspectionResult& BaselineInspection,
    FString& OutLinkedTargetPath,
    FString& OutCurveFloatPath,
    FString& OutPhysicsAssetPath,
    FString& OutMirrorDataTablePath,
    FString& OutPhysicsControlAssetPath)
{
    const FString MachineName = TEXT("Locomotion");
    const FString StateName = TEXT("Move");
    const FString LocalRefPoseClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_LocalRefPose");
    const FString RotateRootBoneClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_RotateRootBone");
    const FString ModifyCurveClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_ModifyCurve");
    const FString RemoveCurveClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_RemoveCurve");
    const FString LinkedAnimGraphClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_LinkedAnimGraph");
    const FString LinkedAnimLayerClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_LinkedAnimLayer");
    const FString BlendListByBoolClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByBool");
    const FString BlendListByEnumClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByEnum");
    const FString MultiWayBlendClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_MultiWayBlend");
    const FString ConstraintClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_Constraint");
    const FString RigidBodyClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_RigidBody");
    const FString AnimDynamicsClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_AnimDynamics");
    const FString PoseDriverClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_PoseDriver");
    const FString SplineIKClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_SplineIK");
    const FString TrailClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_Trail");
    const FString InertializationClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_Inertialization");
    const FString MirrorClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_Mirror");
    const FString IKRigNodeClass =
        TEXT("/Script/IKRigDeveloper.AnimGraphNode_IKRig");
    const FString IKRetargetNodeClass =
        TEXT("/Script/IKRigDeveloper.AnimGraphNode_RetargetPoseFromMesh");
    const FString RigidBodyWithControlClass =
        TEXT("/Script/PhysicsControlUncookedOnly.AnimGraphNode_RigidBodyWithControl");
    const FString SequencePlayerClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer");
    const FString LookAtClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_LookAt");
    const FString TwistCorrectiveClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_TwistCorrectiveNode");
    const FString StateResultClass =
        TEXT("/Script/AnimGraph.AnimGraphNode_StateResult");

    OutLinkedTargetPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/ABP_TE_R42_LinkedTarget_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FThomasDomainAssetCreateRequest LinkedTargetRequest;
    LinkedTargetRequest.AssetPath = OutLinkedTargetPath;
    LinkedTargetRequest.ExpectedRevision = TEXT("missing");
    LinkedTargetRequest.AssetKind = TEXT("anim_blueprint");
    LinkedTargetRequest.ContextAssetPath = SkeletonContextPath;
    const FThomasDomainAssetCreatePlanResult LinkedTargetPlan =
        UThomasDomainsToolset::PlanAnimationAssetCreate(
            LinkedTargetRequest);
    Test.TestTrue(
        TEXT("R42 linked Anim Blueprint target create plan succeeds"),
        LinkedTargetPlan.bOk);
    const FThomasDomainAssetCreateApplyResult LinkedTargetApply =
        LinkedTargetPlan.bOk
            ? UThomasDomainsToolset::ApplyAnimationAssetCreate(
                LinkedTargetPlan.PlanId, true)
            : FThomasDomainAssetCreateApplyResult();
    Test.TestTrue(
        TEXT("R42 linked Anim Blueprint target creates and saves"),
        LinkedTargetApply.bOk && LinkedTargetApply.bSaved);
    UAnimBlueprint* LinkedTargetBlueprint = LoadObject<UAnimBlueprint>(
        nullptr,
        *(OutLinkedTargetPath + TEXT(".")
            + FPackageName::GetLongPackageAssetName(
                OutLinkedTargetPath)));
    const FString LinkedTargetClassPath =
        LinkedTargetBlueprint && LinkedTargetBlueprint->GeneratedClass
            ? LinkedTargetBlueprint->GeneratedClass->GetPathName()
            : FString();
    Test.TestTrue(
        TEXT("R42 target exposes an exact generated AnimInstance class"),
        !LinkedTargetClassPath.IsEmpty()
            && LinkedTargetClassPath.StartsWith(
                OutLinkedTargetPath + TEXT("."))
            && LinkedTargetClassPath.EndsWith(TEXT("_C")));

    OutCurveFloatPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/CF_TE_R43_CustomBlend_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* CurvePackage = CreatePackage(*OutCurveFloatPath);
    const FString CurveAssetName =
        FPackageName::GetLongPackageAssetName(OutCurveFloatPath);
    UCurveFloat* CurveFloat = CurvePackage
        ? NewObject<UCurveFloat>(
            CurvePackage,
            *CurveAssetName,
            RF_Public | RF_Standalone | RF_Transactional)
        : nullptr;
    if (CurveFloat)
    {
        CurveFloat->FloatCurve.AddKey(0.0f, 0.0f);
        CurveFloat->FloatCurve.AddKey(1.0f, 1.0f);
        FAssetRegistryModule::AssetCreated(CurveFloat);
        CurvePackage->MarkPackageDirty();
    }
    FSavePackageArgs CurveSaveArgs;
    CurveSaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    CurveSaveArgs.SaveFlags = SAVE_NoError;
    CurveSaveArgs.Error = GError;
    const FString CurveFilename =
        FPackageName::LongPackageNameToFilename(
            OutCurveFloatPath,
            FPackageName::GetAssetPackageExtension());
    Test.TestTrue(
        TEXT("R43 CurveFloat fixture creates and saves through Unreal"),
        CurveFloat
            && UPackage::SavePackage(
                CurvePackage,
                CurveFloat,
                *CurveFilename,
                CurveSaveArgs));

    auto CreateAndSaveFixture = [&Test](
        const FString& Label,
        const FString& AssetPath,
        UClass* AssetClass)
    {
        UPackage* Package = CreatePackage(*AssetPath);
        const FString AssetName =
            FPackageName::GetLongPackageAssetName(AssetPath);
        UObject* Asset = Package && AssetClass
            ? NewObject<UObject>(
                Package,
                AssetClass,
                *AssetName,
                RF_Public | RF_Standalone | RF_Transactional)
            : nullptr;
        if (UMirrorDataTable* MirrorFixture =
            Cast<UMirrorDataTable>(Asset))
        {
            MirrorFixture->RowStruct = FMirrorTableRow::StaticStruct();
        }
        if (Asset)
        {
            FAssetRegistryModule::AssetCreated(Asset);
            Package->MarkPackageDirty();
        }
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        SaveArgs.Error = GError;
        const FString Filename =
            FPackageName::LongPackageNameToFilename(
                AssetPath,
                FPackageName::GetAssetPackageExtension());
        const bool bSaved = Asset
            && UPackage::SavePackage(
                Package, Asset, *Filename, SaveArgs);
        Test.TestTrue(Label, bSaved);
        return Asset;
    };
    OutPhysicsAssetPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/PA_TE_R44_Override_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPhysicsAsset* PhysicsAsset = Cast<UPhysicsAsset>(
        CreateAndSaveFixture(
            TEXT("R44 PhysicsAsset fixture creates and saves through Unreal"),
            OutPhysicsAssetPath,
            UPhysicsAsset::StaticClass()));
    Test.TestNotNull(TEXT("R44 PhysicsAsset fixture has exact class"),
        PhysicsAsset);
    OutMirrorDataTablePath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/MDT_TE_R44_Mirror_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UMirrorDataTable* MirrorDataTable = Cast<UMirrorDataTable>(
        CreateAndSaveFixture(
            TEXT("R44 MirrorDataTable fixture creates and saves through Unreal"),
            OutMirrorDataTablePath,
            UMirrorDataTable::StaticClass()));
    Test.TestNotNull(TEXT("R44 MirrorDataTable fixture has exact class"),
        MirrorDataTable);

    USkeleton* BlendProfileSkeleton = LoadObject<USkeleton>(
        nullptr,
        *(SkeletonContextPath + TEXT(".")
            + FPackageName::GetLongPackageAssetName(
                SkeletonContextPath)));
    const FName BlendProfileName(*(
        TEXT("TE_R45_Profile_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    UBlendProfile* BlendProfile = BlendProfileSkeleton
        ? BlendProfileSkeleton->CreateNewBlendProfile(
            BlendProfileName)
        : nullptr;
    if (BlendProfileSkeleton && BlendProfile)
    {
        BlendProfileSkeleton->MarkPackageDirty();
    }
    FSavePackageArgs BlendProfileSaveArgs;
    BlendProfileSaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    BlendProfileSaveArgs.SaveFlags = SAVE_NoError;
    BlendProfileSaveArgs.Error = GError;
    const FString BlendProfileSkeletonFilename =
        FPackageName::LongPackageNameToFilename(
            SkeletonContextPath,
            FPackageName::GetAssetPackageExtension());
    Test.TestTrue(
        TEXT("R45 BlendProfile fixture saves on its temporary Skeleton"),
        BlendProfileSkeleton && BlendProfile
            && BlendProfile->GetSkeleton() == BlendProfileSkeleton
            && UPackage::SavePackage(
                BlendProfileSkeleton->GetOutermost(),
                BlendProfileSkeleton,
                *BlendProfileSkeletonFilename,
                BlendProfileSaveArgs));
    const FString BlendProfileObjectPath = BlendProfile
        ? BlendProfile->GetPathName() : FString();

    UClass* PhysicsControlAssetClass = LoadObject<UClass>(
        nullptr,
        TEXT("/Script/PhysicsControl.PhysicsControlAsset"));
    OutPhysicsControlAssetPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/PCA_TE_R45_Control_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UObject* PhysicsControlAsset = CreateAndSaveFixture(
        TEXT("R45 PhysicsControlAsset fixture creates and saves through Unreal"),
        OutPhysicsControlAssetPath,
        PhysicsControlAssetClass);
    Test.TestTrue(
        TEXT("R45 PhysicsControlAsset fixture has exact native class"),
        PhysicsControlAsset
            && PhysicsControlAsset->GetClass()->GetPathName()
                == TEXT("/Script/PhysicsControl.PhysicsControlAsset"));

    auto GetDelimitedField = [](
        const FString& Item,
        const FString& Marker,
        const FString& NextMarker)
    {
        const int32 Start = Item.Find(Marker);
        if (Start == INDEX_NONE)
        {
            return FString();
        }
        const int32 ValueStart = Start + Marker.Len();
        const int32 End = Item.Find(
            NextMarker,
            ESearchCase::CaseSensitive,
            ESearchDir::FromStart,
            ValueStart);
        return End == INDEX_NONE
            ? Item.Mid(ValueStart)
            : Item.Mid(ValueStart, End - ValueStart);
    };
    auto GetGraphState = [MachineName, StateName](
        const FThomasSpecializedAssetInspectionResult& Inspection)
    {
        const FString Prefix = TEXT("StateGraph Machine=") + MachineName
            + TEXT(" State=") + StateName + TEXT(" State=");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(Prefix))
            {
                return Item.Mid(Prefix.Len());
            }
        }
        return FString();
    };
    auto FindNodeItem = [MachineName, StateName](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& ClassPath)
    {
        const FString Prefix = TEXT("StateGraphNode Machine=") + MachineName
            + TEXT(" State=") + StateName + TEXT(" State=");
        const FString ClassMarker = TEXT("|Class=") + ClassPath + TEXT("|");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(Prefix) && Item.Contains(ClassMarker))
            {
                return Item;
            }
        }
        return FString();
    };
    auto FindNodePropertyItem = [MachineName, StateName](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& NodeGuid,
        const FString& PropertyPath)
    {
        const FString Prefix = TEXT("StateGraphNodeProperty Machine=")
            + MachineName + TEXT(" State=") + StateName
            + TEXT(" NodeGuid=") + NodeGuid + TEXT(" ");
        const FString PropertyMarker = TEXT("State=Path=")
            + PropertyPath + TEXT("|");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(Prefix)
                && Item.Contains(PropertyMarker))
            {
                return Item;
            }
        }
        return FString();
    };
    auto FindPoseArrayItem = [MachineName, StateName](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& NodeGuid,
        const FString& PosePath)
    {
        const FString Prefix = TEXT("StateGraphPoseArray Machine=")
            + MachineName + TEXT(" State=") + StateName
            + TEXT(" NodeGuid=") + NodeGuid + TEXT(" ");
        const FString PathMarker = TEXT("|PosePath=")
            + PosePath + TEXT("|");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(Prefix)
                && Item.Contains(PathMarker))
            {
                return Item;
            }
        }
        return FString();
    };
    auto FindConstraintArrayItem = [MachineName, StateName](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& NodeGuid,
        const FString& ConstraintPath)
    {
        const FString Prefix = TEXT("StateGraphConstraintArray Machine=")
            + MachineName + TEXT(" State=") + StateName
            + TEXT(" NodeGuid=") + NodeGuid + TEXT(" ");
        const FString PathMarker = TEXT("|ConstraintPath=")
            + ConstraintPath + TEXT("|");
        for (const FString& Item : Inspection.Items)
        {
            if (Item.StartsWith(Prefix)
                && Item.Contains(PathMarker))
            {
                return Item;
            }
        }
        return FString();
    };
    auto FindPinItem = [MachineName, StateName](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& NodeGuid,
        const FString& Direction,
        const FString& PinName,
        const bool bPosePin)
    {
        const FString Prefix = TEXT("StateGraphPin Machine=") + MachineName
            + TEXT(" State=") + StateName
            + TEXT(" NodeGuid=") + NodeGuid + TEXT(" State=");
        for (const FString& Item : Inspection.Items)
        {
            if (!Item.StartsWith(Prefix)
                || !Item.Contains(TEXT("|Direction=") + Direction + TEXT("|")))
            {
                continue;
            }
            if (!PinName.IsEmpty()
                && !Item.Contains(TEXT("|Name=") + PinName + TEXT("|")))
            {
                continue;
            }
            if (bPosePin
                && !Item.Contains(
                    TEXT("|TypeObject=/Script/Engine.PoseLink|")))
            {
                continue;
            }
            return Item;
        }
        return FString();
    };
    auto ReloadAndInspect = [&Test, &AnimBlueprintPath](
        const FString& Label)
    {
        UObject* SavedBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    AnimBlueprintPath)));
        FText ReloadError;
        Test.TestTrue(
            Label + TEXT(" reloads from saved file"),
            SavedBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedBlueprint->GetOutermost()},
                    ReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        return UThomasDomainsToolset::InspectAnimationAsset(
            AnimBlueprintPath, 1000);
    };
    auto ApplyAndReload = [
        &Test,
        &AnimBlueprintPath,
        &GetGraphState,
        &ReloadAndInspect](
        const FString& Label,
        const FThomasSpecializedAssetInspectionResult& Before,
        FThomasAnimationOperation Operation,
        const bool bConfirmDestructive)
    {
        Operation.ExpectedValue = GetGraphState(Before);
        FThomasAnimationPatchRequest Request;
        Request.AssetPath = AnimBlueprintPath;
        Request.ExpectedRevision = Before.Revision;
        Request.bConfirmDestructive = bConfirmDestructive;
        Request.Operations.Add(Operation);
        const FThomasAnimationPlanResult Plan =
            UThomasDomainsToolset::PlanAnimationPatch(Request);
        Test.AddInfo(FString::Printf(
            TEXT("%s plan: ok=%d code=%s message=%s"),
            *Label,
            Plan.bOk ? 1 : 0,
            *Plan.Code,
            *Plan.Message));
        Test.TestTrue(Label + TEXT(" plan succeeds"), Plan.bOk);
        if (!Plan.bOk)
        {
            return FThomasSpecializedAssetInspectionResult();
        }
        const FThomasAnimationApplyResult Apply =
            UThomasDomainsToolset::ApplyAnimationPlan(
                Plan.PlanId, true);
        Test.AddInfo(FString::Printf(
            TEXT("%s apply: ok=%d code=%s message=%s diagnostics=%s"),
            *Label,
            Apply.bOk ? 1 : 0,
            *Apply.Code,
            *Apply.Message,
            *FString::Join(Apply.Diagnostics, TEXT(" | "))));
        Test.TestTrue(
            Label + TEXT(" compiles and saves"), Apply.bOk);
        return Apply.bOk
            ? ReloadAndInspect(Label)
            : UThomasDomainsToolset::InspectAnimationAsset(
                AnimBlueprintPath, 1000);
    };

    const FString BaselineGraphState = GetGraphState(BaselineInspection);
    Test.TestTrue(
        TEXT("Generic state graph baseline exposes exact content hash"),
        BaselineInspection.bOk
            && !BaselineGraphState.IsEmpty()
            && BaselineGraphState.Contains(TEXT("NodeCount=1|"))
            && BaselineGraphState.Contains(TEXT("LinkCount=0|"))
            && BaselineGraphState.Contains(TEXT("ContentHash=")));
    Test.TestTrue(
        TEXT("Generic state graph native node inventory is exposed"),
        BaselineInspection.Metrics.ContainsByPredicate(
            [](const FString& Metric)
            {
                return Metric.StartsWith(
                    TEXT("AvailableStateGraphNodeClassCount="))
                    && !Metric.EndsWith(TEXT("=0"));
            })
            && BaselineInspection.Items.Contains(
                TEXT("StateGraphNodeClass Path=") + LocalRefPoseClass)
            && BaselineInspection.Items.Contains(
                TEXT("StateGraphNodeClass Path=") + RotateRootBoneClass)
            && BaselineInspection.Items.Contains(
                TEXT("StateGraphNodeClass Path=") + ModifyCurveClass)
            && BaselineInspection.Items.Contains(
                TEXT("StateGraphNodeClass Path=") + RemoveCurveClass)
            && BaselineInspection.Items.Contains(
                TEXT("StateGraphNodeClass Path=") + LinkedAnimGraphClass)
            && BaselineInspection.Items.Contains(
                TEXT("StateGraphNodeClass Path=") + LinkedAnimLayerClass)
            && BaselineInspection.Items.Contains(
                TEXT("StateGraphNodeClass Path=") + BlendListByBoolClass)
            && BaselineInspection.Items.Contains(
                TEXT("StateGraphNodeClass Path=") + RigidBodyClass)
            && BaselineInspection.Items.Contains(
                TEXT("StateGraphNodeClass Path=") + MirrorClass)
            && BaselineInspection.Items.Contains(
                TEXT("StateGraphNodeClass Path=") + IKRigNodeClass)
            && BaselineInspection.Items.Contains(
                TEXT("StateGraphNodeClass Path=") + IKRetargetNodeClass)
            && BaselineInspection.Items.Contains(
                TEXT("StateGraphNodeClass Path=")
                    + RigidBodyWithControlClass));
    Test.TestTrue(
        TEXT("R41 scalar TMap and TSet are no longer reported as property gaps"),
        !BaselineInspection.Metrics.ContainsByPredicate(
            [](const FString& Metric)
            {
                return Metric.StartsWith(TEXT("StateGraphPropertyGap.map="))
                    || Metric.StartsWith(TEXT("StateGraphPropertyGap.set="));
            })
            && !BaselineInspection.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.StartsWith(TEXT("StateGraphPropertyGap "))
                        && (Item.Contains(TEXT(" Kind=map "))
                            || Item.Contains(TEXT(" Kind=set ")));
                }));
    Test.TestTrue(
        TEXT("R43 CurveFloat references are no longer object gaps"),
        !BaselineInspection.Items.ContainsByPredicate(
            [](const FString& Item)
            {
                return Item.StartsWith(
                        TEXT("StateGraphPropertyGap "))
                    && Item.Contains(TEXT(" Kind=object "))
                    && Item.Contains(
                        TEXT("Type=TObjectPtr<UCurveFloat>"));
            }));
    Test.TestTrue(
        TEXT("R44 PhysicsAsset and MirrorDataTable references are no longer gaps"),
        !BaselineInspection.Items.ContainsByPredicate(
            [](const FString& Item)
            {
                return Item.StartsWith(
                        TEXT("StateGraphPropertyGap "))
                    && Item.Contains(TEXT(" Kind=object "))
                    && (Item.Contains(
                            TEXT("Type=TObjectPtr<UPhysicsAsset>"))
                        || Item.Contains(
                            TEXT("Type=TObjectPtr<UMirrorDataTable>")));
            }));
    Test.TestTrue(
        TEXT("R45 closes every remaining strong object-reference gap"),
        !BaselineInspection.Metrics.ContainsByPredicate(
            [](const FString& Metric)
            {
                return Metric.StartsWith(
                    TEXT("StateGraphPropertyGap.object="));
            })
            && !BaselineInspection.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.StartsWith(
                            TEXT("StateGraphPropertyGap "))
                        && Item.Contains(TEXT(" Kind=object "));
                }));
    Test.TestTrue(
        TEXT("R46 closes the measured nested AnimGraph settings gaps"),
        !BaselineInspection.Metrics.ContainsByPredicate(
            [](const FString& Metric)
            {
                return Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.struct:InputAlphaBoolBlend="))
                    || Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.struct:InputScaleBiasClampConstants="))
                    || Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.struct:BoneSocketTarget="))
                    || Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.struct:SocketReference="))
                    || Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.struct:Axis="))
                    || Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.struct:ReferenceBoneFrame="));
            })
            && !BaselineInspection.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.StartsWith(TEXT("StateGraphPropertyGap "))
                        && (Item.Contains(
                                TEXT(" Kind=struct:InputAlphaBoolBlend "))
                            || Item.Contains(
                                TEXT(" Kind=struct:InputScaleBiasClampConstants "))
                            || Item.Contains(
                                TEXT(" Kind=struct:BoneSocketTarget "))
                            || Item.Contains(
                                TEXT(" Kind=struct:SocketReference "))
                            || Item.Contains(
                                TEXT(" Kind=struct:Axis "))
                            || Item.Contains(
                                TEXT(" Kind=struct:ReferenceBoneFrame ")));
                }));
    for (const FString& Metric : BaselineInspection.Metrics)
    {
        if (Metric == TEXT("StateGraphTopologyProperty.pose_link=34")
            || Metric
                == TEXT("StateGraphTopologyProperty.component_space_pose_link=23")
            || Metric == TEXT("StateGraphTopologyProperty.pose_link_array=5")
            || Metric == TEXT("StateGraphTopologyPropertyCount=62")
            || Metric == TEXT("StateGraphWholeArrayPropertyCount=10")
            || Metric == TEXT("StateGraphDedicatedArray.bool_blend=2")
            || Metric == TEXT("StateGraphDedicatedArray.int_blend=2")
            || Metric
                == TEXT("StateGraphDedicatedArray.layered_blend_per_bone=3")
            || Metric == TEXT("StateGraphDedicatedArrayCount=7")
            || Metric == TEXT("StateGraphCoupledPoseArray.blend_list_by_enum=2")
            || Metric == TEXT("StateGraphCoupledPoseArray.multi_way_blend=2")
            || Metric == TEXT("StateGraphCoupledPoseArrayPropertyCount=4")
            || Metric == TEXT("StateGraphCoupledPoseArrayLifecycleCount=2")
            || Metric == TEXT("StateGraphCoupledConstraintArray.constraint_setup_weights=2")
            || Metric == TEXT("StateGraphCoupledConstraintArrayPropertyCount=2")
            || Metric == TEXT("StateGraphCoupledConstraintArrayLifecycleCount=1")
            || Metric == TEXT("StateGraphAssetBoundArray.ik_retarget_override_sets=1")
            || Metric == TEXT("StateGraphAssetBoundArrayPropertyCount=1")
            || Metric == TEXT("StateGraphAssetBoundArrayLifecycleCount=1")
            || Metric == TEXT("StateGraphResolvedStructuralArrayCount=24")
            || Metric == TEXT("StateGraphPropertyGapCount=31")
            || Metric == TEXT("StateGraphPropertyGap.structural_array=19"))
        {
            Test.AddInfo(TEXT("R51 inventory metric: ") + Metric);
        }
    }
    for (const FString& Item : BaselineInspection.Items)
    {
        if (Item.StartsWith(TEXT("StateGraphPropertyGap ")))
        {
            Test.AddInfo(TEXT("R51 inventory gap: ") + Item);
        }
    }
    Test.TestTrue(
        TEXT("R47 classifies pose links as graph topology instead of property gaps"),
        BaselineInspection.Metrics.Contains(
            TEXT("StateGraphTopologyProperty.pose_link=34"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphTopologyProperty.component_space_pose_link=23"))
            && BaselineInspection.Metrics.ContainsByPredicate(
                [](const FString& Metric)
                {
                    return Metric.StartsWith(
                        TEXT("StateGraphTopologyPropertyCount="));
                })
            && !BaselineInspection.Metrics.ContainsByPredicate(
                [](const FString& Metric)
                {
                    return Metric.StartsWith(
                            TEXT("StateGraphPropertyGap.struct:PoseLink="))
                        || Metric.StartsWith(
                            TEXT("StateGraphPropertyGap.struct:ComponentSpacePoseLink="));
                })
            && !BaselineInspection.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.StartsWith(TEXT("StateGraphPropertyGap "))
                        && (Item.Contains(TEXT(" Kind=struct:PoseLink "))
                            || Item.Contains(
                                TEXT(" Kind=struct:ComponentSpacePoseLink ")));
                }));
    Test.TestTrue(
        TEXT("R47 closes the measured simulation RBF blend and runtime-curve settings gaps"),
        !BaselineInspection.Metrics.ContainsByPredicate(
            [](const FString& Metric)
            {
                return Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.struct:RuntimeFloatCurve="))
                    || Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.struct:RotationRetargetingInfo="))
                    || Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.struct:AnimPhysSimSpaceSettings="))
                    || Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.struct:SimSpaceSettings="))
                    || Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.struct:RBFParams="))
                    || Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.struct:AlphaBlend="));
            })
            && !BaselineInspection.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.StartsWith(TEXT("StateGraphPropertyGap "))
                        && (Item.Contains(
                                TEXT(" Kind=struct:RuntimeFloatCurve "))
                            || Item.Contains(
                                TEXT(" Kind=struct:RotationRetargetingInfo "))
                            || Item.Contains(
                                TEXT(" Kind=struct:AnimPhysSimSpaceSettings "))
                            || Item.Contains(
                                TEXT(" Kind=struct:SimSpaceSettings "))
                            || Item.Contains(
                                TEXT(" Kind=struct:RBFParams "))
                            || Item.Contains(
                                TEXT(" Kind=struct:AlphaBlend ")));
                }));
    Test.TestTrue(
        TEXT("R47 keeps the measured specialized backlog explicit"),
        BaselineInspection.Metrics.ContainsByPredicate(
            [](const FString& Metric)
            {
                return Metric.StartsWith(
                    TEXT("StateGraphPropertyGapCount="));
            })
            && BaselineInspection.Metrics.ContainsByPredicate(
                [](const FString& Metric)
                {
                    return Metric.StartsWith(
                        TEXT("StateGraphPropertyGap.structural_array="));
                }));
    Test.TestTrue(
        TEXT("R48 closes ten autonomous top-level arrays and preserves the specialized backlog"),
        BaselineInspection.Metrics.Contains(
            TEXT("StateGraphWholeArrayPropertyCount=10"))
            && !BaselineInspection.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.StartsWith(TEXT("StateGraphPropertyGap "))
                        && Item.Contains(TEXT(" Kind=structural_array "))
                        && (Item.Contains(
                                TEXT(" Path=Node.ExtrapolationFilteredCurves "))
                            || Item.Contains(
                                TEXT(" Path=Node.FilteredBones "))
                            || Item.Contains(
                                TEXT(" Path=Node.FilteredCurves "))
                            || Item.Contains(
                                TEXT(" Path=Node.IKBonesToMove "))
                            || Item.Contains(
                                TEXT(" Path=Node.OnlyDriveBones "))
                            || Item.Contains(
                                TEXT(" Path=Node.SourceBones "))
                            || Item.Contains(
                                TEXT(" Path=Node.InputBonesToTransfer "))
                            || Item.Contains(
                                TEXT(" Path=Node.OutputBonesToTransfer ")));
                }));
    Test.TestTrue(
        TEXT("R49 separates pose-array topology and reconciles dedicated blend lifecycles"),
        BaselineInspection.Metrics.Contains(
            TEXT("StateGraphTopologyProperty.pose_link_array=5"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphTopologyPropertyCount=62"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphDedicatedArray.bool_blend=2"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphDedicatedArray.int_blend=2"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphDedicatedArray.layered_blend_per_bone=3"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphDedicatedArrayCount=7"))
            && !BaselineInspection.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    if (!Item.StartsWith(TEXT("StateGraphPropertyGap "))
                        || !Item.Contains(TEXT(" Kind=structural_array ")))
                    {
                        return false;
                    }
                    return Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_BlendListByBool Path=Node.BlendPose "))
                        || Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_BlendListByBool Path=Node.BlendTime "))
                        || Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_BlendListByInt Path=Node.BlendPose "))
                        || Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_BlendListByInt Path=Node.BlendTime "))
                        || Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_BlendListByEnum Path=Node.BlendPose "))
                        || Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_LayeredBoneBlend Path=Node.BlendPoses "))
                        || Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_LayeredBoneBlend Path=Node.BlendWeights "))
                        || Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_LayeredBoneBlend Path=Node.LayerSetup "))
                        || Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_MultiWayBlend Path=Node.Poses "));
                }));
    Test.TestTrue(
        TEXT("R50 closes both coupled dynamic-pose lifecycles and their companion-array gaps"),
        BaselineInspection.Metrics.Contains(
            TEXT("StateGraphCoupledPoseArray.blend_list_by_enum=2"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphCoupledPoseArray.multi_way_blend=2"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphCoupledPoseArrayPropertyCount=4"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphCoupledPoseArrayLifecycleCount=2"))
            && !BaselineInspection.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    if (!Item.StartsWith(TEXT("StateGraphPropertyGap "))
                        || !Item.Contains(TEXT(" Kind=structural_array ")))
                    {
                        return false;
                    }
                    return Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_BlendListByEnum Path=Node.BlendPose "))
                        || Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_BlendListByEnum Path=Node.BlendTime "))
                        || Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_MultiWayBlend Path=Node.Poses "))
                        || Item.Contains(
                            TEXT("Class=/Script/AnimGraph.AnimGraphNode_MultiWayBlend Path=Node.DesiredAlphas "));
                }));
    Test.TestTrue(
        TEXT("R51 closes the asset-bound IK Retargeter and coupled Constraint array gaps"),
        BaselineInspection.Metrics.Contains(
            TEXT("StateGraphAssetBoundArray.ik_retarget_override_sets=1"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphAssetBoundArrayPropertyCount=1"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphAssetBoundArrayLifecycleCount=1"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphCoupledConstraintArray.constraint_setup_weights=2"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphCoupledConstraintArrayPropertyCount=2"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphCoupledConstraintArrayLifecycleCount=1"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphResolvedStructuralArrayCount=24"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphPropertyGapCount=31"))
            && BaselineInspection.Metrics.Contains(
                TEXT("StateGraphPropertyGap.structural_array=19"))
            && !BaselineInspection.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.StartsWith(TEXT("StateGraphPropertyGap "))
                        && (Item.Contains(
                                TEXT("Class=/Script/IKRigDeveloper.AnimGraphNode_RetargetPoseFromMesh Path=Node.OverrideSetsToApply "))
                            || Item.Contains(
                                TEXT("Class=/Script/AnimGraph.AnimGraphNode_Constraint Path=Node.ConstraintSetup "))
                            || Item.Contains(
                                TEXT("Class=/Script/AnimGraph.AnimGraphNode_Constraint Path=Node.ConstraintWeights ")));
                }));
    Test.TestTrue(
        TEXT("R42 AnimInstance class references are no longer property gaps"),
        !BaselineInspection.Metrics.ContainsByPredicate(
            [](const FString& Metric)
            {
                return Metric.StartsWith(
                    TEXT("StateGraphPropertyGap.class="));
            })
            && !BaselineInspection.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.StartsWith(
                            TEXT("StateGraphPropertyGap "))
                        && Item.Contains(TEXT(" Kind=class "));
                }));

    FThomasAnimationPatchRequest UnsupportedClassRequest;
    UnsupportedClassRequest.AssetPath = AnimBlueprintPath;
    UnsupportedClassRequest.ExpectedRevision = BaselineInspection.Revision;
    FThomasAnimationOperation UnsupportedClass;
    UnsupportedClass.Action = TEXT("add_state_graph_node");
    UnsupportedClass.MachineName = MachineName;
    UnsupportedClass.StateName = StateName;
    UnsupportedClass.ClassPath = StateResultClass;
    UnsupportedClass.ExpectedValue = BaselineGraphState;
    UnsupportedClassRequest.Operations.Add(UnsupportedClass);
    Test.TestEqual(
        TEXT("Protected state-result node class is refused"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnsupportedClassRequest).Code,
        FString(TEXT("state_graph_node_class_unavailable")));

    FThomasAnimationOperation AddLocalRefPose;
    AddLocalRefPose.Action = TEXT("add_state_graph_node");
    AddLocalRefPose.MachineName = MachineName;
    AddLocalRefPose.StateName = StateName;
    AddLocalRefPose.ClassPath = LocalRefPoseClass;
    AddLocalRefPose.PositionX = -450;
    AddLocalRefPose.PositionY = 50;
    FThomasSpecializedAssetInspectionResult AfterLocalRefPose =
        ApplyAndReload(
            TEXT("Generic state graph local-ref-pose add"),
            BaselineInspection,
            AddLocalRefPose,
            false);
    const FString LocalRefPoseItem = FindNodeItem(
        AfterLocalRefPose, LocalRefPoseClass);
    const FString LocalRefPoseGuid = GetDelimitedField(
        LocalRefPoseItem, TEXT("State=Guid="), TEXT("|Class="));
    const FString LocalPosePinItem = FindPinItem(
        AfterLocalRefPose,
        LocalRefPoseGuid,
        TEXT("Output"),
        FString(),
        true);
    const FString LocalPosePinGuid = GetDelimitedField(
        LocalPosePinItem, TEXT("State=PinGuid="), TEXT("|Name="));
    Test.TestTrue(
        TEXT("Generic local-ref-pose node and output pose pin survive reload"),
        !LocalRefPoseGuid.IsEmpty() && !LocalPosePinGuid.IsEmpty());

    FThomasAnimationOperation AddRotateRootBone;
    AddRotateRootBone.Action = TEXT("add_state_graph_node");
    AddRotateRootBone.MachineName = MachineName;
    AddRotateRootBone.StateName = StateName;
    AddRotateRootBone.ClassPath = RotateRootBoneClass;
    AddRotateRootBone.PositionX = -200;
    AddRotateRootBone.PositionY = 50;
    FThomasSpecializedAssetInspectionResult AfterRotateRootBone =
        ApplyAndReload(
            TEXT("Generic state graph rotate-root-bone add"),
            AfterLocalRefPose,
            AddRotateRootBone,
            false);
    const FString RotateRootBoneItem = FindNodeItem(
        AfterRotateRootBone, RotateRootBoneClass);
    const FString RotateRootBoneGuid = GetDelimitedField(
        RotateRootBoneItem, TEXT("State=Guid="), TEXT("|Class="));
    const FString RotateInputPosePinGuid = GetDelimitedField(
        FindPinItem(
            AfterRotateRootBone,
            RotateRootBoneGuid,
            TEXT("Input"),
            FString(),
            true),
        TEXT("State=PinGuid="),
        TEXT("|Name="));
    const FString RotateOutputPosePinGuid = GetDelimitedField(
        FindPinItem(
            AfterRotateRootBone,
            RotateRootBoneGuid,
            TEXT("Output"),
            FString(),
            true),
        TEXT("State=PinGuid="),
        TEXT("|Name="));
    const FString PitchPinGuid = GetDelimitedField(
        FindPinItem(
            AfterRotateRootBone,
            RotateRootBoneGuid,
            TEXT("Input"),
            TEXT("Pitch"),
            false),
        TEXT("State=PinGuid="),
        TEXT("|Name="));
    Test.TestTrue(
        TEXT("Generic rotate-root-bone node exposes exact pose and value pins"),
        !RotateRootBoneGuid.IsEmpty()
            && !RotateInputPosePinGuid.IsEmpty()
            && !RotateOutputPosePinGuid.IsEmpty()
            && !PitchPinGuid.IsEmpty());

    const FString RotateRootMotionPropertyPath =
        TEXT("Node.bRotateRootMotionAttribute");
    const FString PitchScalePropertyPath =
        TEXT("Node.PitchScaleBiasClamp.Scale");
    const FString RotateRootMotionPropertyItem =
        FindNodePropertyItem(
            AfterRotateRootBone,
            RotateRootBoneGuid,
            RotateRootMotionPropertyPath);
    const FString InitialRotateRootMotionValue = GetDelimitedField(
        RotateRootMotionPropertyItem,
        TEXT("|Value="),
        TEXT("\x1f"));
    const FString UpdatedRotateRootMotionValue =
        InitialRotateRootMotionValue == TEXT("True")
            ? TEXT("False")
            : TEXT("True");
    Test.TestTrue(
        TEXT("Specialized AnimGraph direct property is inspected and hashed"),
        AfterRotateRootBone.Metrics.ContainsByPredicate(
            [](const FString& Metric)
            {
                return Metric.StartsWith(
                    TEXT("StateGraphNodePropertyCount="))
                    && !Metric.EndsWith(TEXT("=0"));
            })
            && RotateRootMotionPropertyItem.Contains(
                TEXT("|Type=bool|"))
            && (InitialRotateRootMotionValue == TEXT("True")
                || InitialRotateRootMotionValue == TEXT("False"))
            && RotateRootBoneItem.Contains(TEXT("|PropertyCount="))
            && RotateRootBoneItem.Contains(TEXT("|PropertyHash=")));
    const FString PitchScalePropertyItem = FindNodePropertyItem(
        AfterRotateRootBone,
        RotateRootBoneGuid,
        PitchScalePropertyPath);
    const FString InitialPitchScaleValue = GetDelimitedField(
        PitchScalePropertyItem,
        TEXT("|Value="),
        TEXT("\x1f"));
    Test.TestTrue(
        TEXT("Allowlisted nested AnimGraph struct leaf is inspected and hashed"),
        PitchScalePropertyItem.Contains(TEXT("|Type=float|"))
            && !InitialPitchScaleValue.IsEmpty());

    auto PlanRejectedProperty = [
        &Test,
        &AnimBlueprintPath,
        &GetGraphState,
        &MachineName,
        &StateName,
        &RotateRootBoneGuid](
        const FString& Label,
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& PropertyPath,
        const FString& SettingValue,
        const FString& ExpectedCode)
    {
        FThomasAnimationPatchRequest Request;
        Request.AssetPath = AnimBlueprintPath;
        Request.ExpectedRevision = Before.Revision;
        FThomasAnimationOperation Operation;
        Operation.Action = TEXT("set_state_graph_node_property");
        Operation.MachineName = MachineName;
        Operation.StateName = StateName;
        Operation.StateGraphNodeGuid = RotateRootBoneGuid;
        Operation.PropertyName = PropertyPath;
        Operation.SettingValue = SettingValue;
        Operation.ExpectedValue = GetGraphState(Before);
        Request.Operations.Add(Operation);
        Test.TestEqual(
            Label,
            UThomasDomainsToolset::PlanAnimationPatch(Request).Code,
            ExpectedCode);
    };
    PlanRejectedProperty(
        TEXT("Pin-backed AnimGraph property is not exposed twice"),
        AfterRotateRootBone,
        TEXT("Node.Pitch"),
        TEXT("30.0"),
        TEXT("state_graph_node_property_not_found"));
    PlanRejectedProperty(
        TEXT("Whole nested AnimGraph struct cannot be overwritten atomically"),
        AfterRotateRootBone,
        TEXT("Node.PitchScaleBiasClamp"),
        TEXT("()"),
        TEXT("state_graph_node_property_not_found"));
    PlanRejectedProperty(
        TEXT("Invalid nested AnimGraph leaf value is refused in preflight"),
        AfterRotateRootBone,
        PitchScalePropertyPath,
        TEXT("not_a_float"),
        TEXT("invalid_state_graph_node_property_value"));
    PlanRejectedProperty(
        TEXT("Invalid specialized AnimGraph value is refused in preflight"),
        AfterRotateRootBone,
        RotateRootMotionPropertyPath,
        TEXT("not_a_bool"),
        TEXT("invalid_state_graph_node_property_value"));

    FThomasAnimationOperation SetRotateRootMotionProperty;
    SetRotateRootMotionProperty.Action =
        TEXT("set_state_graph_node_property");
    SetRotateRootMotionProperty.MachineName = MachineName;
    SetRotateRootMotionProperty.StateName = StateName;
    SetRotateRootMotionProperty.StateGraphNodeGuid =
        RotateRootBoneGuid;
    SetRotateRootMotionProperty.PropertyName =
        RotateRootMotionPropertyPath;
    SetRotateRootMotionProperty.SettingValue =
        UpdatedRotateRootMotionValue;
    const FThomasSpecializedAssetInspectionResult
        AfterRotateRootMotionProperty = ApplyAndReload(
            TEXT("Specialized AnimGraph rotate-root-motion property"),
            AfterRotateRootBone,
            SetRotateRootMotionProperty,
            false);
    Test.TestTrue(
        TEXT("Specialized AnimGraph property survives compile save and reload"),
        FindNodePropertyItem(
            AfterRotateRootMotionProperty,
            RotateRootBoneGuid,
            RotateRootMotionPropertyPath).EndsWith(
                TEXT("|Value=") + UpdatedRotateRootMotionValue)
            && GetGraphState(AfterRotateRootMotionProperty)
                != GetGraphState(AfterRotateRootBone));

    FThomasAnimationPatchRequest StalePropertyRequest;
    StalePropertyRequest.AssetPath = AnimBlueprintPath;
    StalePropertyRequest.ExpectedRevision =
        AfterRotateRootMotionProperty.Revision;
    FThomasAnimationOperation StaleProperty =
        SetRotateRootMotionProperty;
    StaleProperty.SettingValue = InitialRotateRootMotionValue;
    StaleProperty.ExpectedValue = GetGraphState(AfterRotateRootBone);
    StalePropertyRequest.Operations.Add(StaleProperty);
    Test.TestEqual(
        TEXT("Stale specialized AnimGraph property hash is refused"),
        UThomasDomainsToolset::PlanAnimationPatch(
            StalePropertyRequest).Code,
        FString(TEXT("state_graph_conflict")));
    PlanRejectedProperty(
        TEXT("Unchanged specialized AnimGraph property is refused"),
        AfterRotateRootMotionProperty,
        RotateRootMotionPropertyPath,
        UpdatedRotateRootMotionValue,
        TEXT("state_graph_node_property_unchanged"));

    FThomasAnimationOperation RestoreRotateRootMotionProperty =
        SetRotateRootMotionProperty;
    RestoreRotateRootMotionProperty.SettingValue =
        InitialRotateRootMotionValue;
    const FThomasSpecializedAssetInspectionResult
        AfterRotatePropertyRestore = ApplyAndReload(
            TEXT("Specialized AnimGraph property restore"),
            AfterRotateRootMotionProperty,
            RestoreRotateRootMotionProperty,
            false);
    Test.TestEqual(
        TEXT("Specialized AnimGraph property restore returns exact node hash"),
        GetGraphState(AfterRotatePropertyRestore),
        GetGraphState(AfterRotateRootBone));

    FThomasAnimationOperation SetPitchScaleProperty;
    SetPitchScaleProperty.Action =
        TEXT("set_state_graph_node_property");
    SetPitchScaleProperty.MachineName = MachineName;
    SetPitchScaleProperty.StateName = StateName;
    SetPitchScaleProperty.StateGraphNodeGuid = RotateRootBoneGuid;
    SetPitchScaleProperty.PropertyName = PitchScalePropertyPath;
    SetPitchScaleProperty.SettingValue = TEXT("1.25");
    const FThomasSpecializedAssetInspectionResult AfterPitchScaleProperty =
        ApplyAndReload(
            TEXT("Nested AnimGraph pitch scale property"),
            AfterRotatePropertyRestore,
            SetPitchScaleProperty,
            false);
    Test.TestTrue(
        TEXT("Nested AnimGraph struct leaf survives compile save and reload"),
        FindNodePropertyItem(
            AfterPitchScaleProperty,
            RotateRootBoneGuid,
            PitchScalePropertyPath).Contains(TEXT("|Value=1.250000"))
            && GetGraphState(AfterPitchScaleProperty)
                != GetGraphState(AfterRotatePropertyRestore));
    FThomasAnimationOperation RestorePitchScaleProperty =
        SetPitchScaleProperty;
    RestorePitchScaleProperty.SettingValue = InitialPitchScaleValue;
    const FThomasSpecializedAssetInspectionResult
        AfterPitchScalePropertyRestore = ApplyAndReload(
            TEXT("Nested AnimGraph pitch scale property restore"),
            AfterPitchScaleProperty,
            RestorePitchScaleProperty,
            false);
    Test.TestEqual(
        TEXT("Nested AnimGraph property restore returns exact node hash"),
        GetGraphState(AfterPitchScalePropertyRestore),
        GetGraphState(AfterRotateRootBone));

    FThomasAnimationOperation AddModifyCurve;
    AddModifyCurve.Action = TEXT("add_state_graph_node");
    AddModifyCurve.MachineName = MachineName;
    AddModifyCurve.StateName = StateName;
    AddModifyCurve.ClassPath = ModifyCurveClass;
    AddModifyCurve.PositionX = 50;
    AddModifyCurve.PositionY = 220;
    const FThomasSpecializedAssetInspectionResult AfterModifyCurveAdd =
        ApplyAndReload(
            TEXT("R41 ModifyCurve node add"),
            AfterPitchScalePropertyRestore,
            AddModifyCurve,
            false);
    const FString ModifyCurveGuid = GetDelimitedField(
        FindNodeItem(AfterModifyCurveAdd, ModifyCurveClass),
        TEXT("State=Guid="),
        TEXT("|Class="));

    FThomasAnimationOperation AddRemoveCurve;
    AddRemoveCurve.Action = TEXT("add_state_graph_node");
    AddRemoveCurve.MachineName = MachineName;
    AddRemoveCurve.StateName = StateName;
    AddRemoveCurve.ClassPath = RemoveCurveClass;
    AddRemoveCurve.PositionX = 300;
    AddRemoveCurve.PositionY = 220;
    const FThomasSpecializedAssetInspectionResult AfterRemoveCurveAdd =
        ApplyAndReload(
            TEXT("R41 RemoveCurve node add"),
            AfterModifyCurveAdd,
            AddRemoveCurve,
            false);
    const FString RemoveCurveGuid = GetDelimitedField(
        FindNodeItem(AfterRemoveCurveAdd, RemoveCurveClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    const FString CurveMapPath = TEXT("Node.CurveMap");
    const FString CurveSetPath = TEXT("Node.Curves");
    const FString EmptyCurveMapItem = FindNodePropertyItem(
        AfterRemoveCurveAdd, ModifyCurveGuid, CurveMapPath);
    const FString EmptyCurveSetItem = FindNodePropertyItem(
        AfterRemoveCurveAdd, RemoveCurveGuid, CurveSetPath);
    Test.TestTrue(
        TEXT("R41 bounded scalar TMap and TSet are inspected and hashed"),
        !ModifyCurveGuid.IsEmpty()
            && !RemoveCurveGuid.IsEmpty()
            && EmptyCurveMapItem.Contains(TEXT("|Type=TMap|Value=()"))
            && EmptyCurveSetItem.Contains(TEXT("|Type=TSet|Value=()"))
            && FindNodeItem(
                AfterRemoveCurveAdd,
                ModifyCurveClass).Contains(TEXT("|PropertyHash="))
            && FindNodeItem(
                AfterRemoveCurveAdd,
                RemoveCurveClass).Contains(TEXT("|PropertyHash=")));

    auto PlanStateGraphProperty = [
        &AnimBlueprintPath,
        &GetGraphState,
        &MachineName,
        &StateName](
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& NodeGuid,
        const FString& PropertyPath,
        const FString& Value,
        const bool bConfirmDestructive)
    {
        FThomasAnimationPatchRequest Request;
        Request.AssetPath = AnimBlueprintPath;
        Request.ExpectedRevision = Before.Revision;
        Request.bConfirmDestructive = bConfirmDestructive;
        FThomasAnimationOperation Operation;
        Operation.Action = TEXT("set_state_graph_node_property");
        Operation.MachineName = MachineName;
        Operation.StateName = StateName;
        Operation.StateGraphNodeGuid = NodeGuid;
        Operation.PropertyName = PropertyPath;
        Operation.SettingValue = Value;
        Operation.ExpectedValue = GetGraphState(Before);
        Request.Operations.Add(Operation);
        return UThomasDomainsToolset::PlanAnimationPatch(Request);
    };
    const FString PopulatedCurveMap =
        TEXT("((ThomasSpeed,1.25),(ThomasTurn,-0.5))");
    const FString PopulatedCurveSet =
        TEXT("(ThomasSpeed,ThomasTurn)");
    Test.TestEqual(
        TEXT("R41 associative replacement always requires R2 confirmation"),
        PlanStateGraphProperty(
            AfterRemoveCurveAdd,
            ModifyCurveGuid,
            CurveMapPath,
            PopulatedCurveMap,
            false).Code,
        FString(TEXT("confirmation_required")));
    Test.TestEqual(
        TEXT("R41 malformed map text is refused in preflight"),
        PlanStateGraphProperty(
            AfterRemoveCurveAdd,
            ModifyCurveGuid,
            CurveMapPath,
            TEXT("not_a_map"),
            true).Code,
        FString(TEXT("invalid_state_graph_node_property_value")));
    Test.TestEqual(
        TEXT("R41 unchanged empty set is refused in preflight"),
        PlanStateGraphProperty(
            AfterRemoveCurveAdd,
            RemoveCurveGuid,
            CurveSetPath,
            TEXT("()"),
            true).Code,
        FString(TEXT("state_graph_node_property_unchanged")));
    FString OversizedCurveMap = TEXT("(");
    for (int32 Index = 0; Index < 17; ++Index)
    {
        if (Index > 0)
        {
            OversizedCurveMap += TEXT(",");
        }
        OversizedCurveMap += FString::Printf(
            TEXT("(R41_%02d,%d)"), Index, Index);
    }
    OversizedCurveMap += TEXT(")");
    Test.TestEqual(
        TEXT("R41 map item count above the hard cap is refused"),
        PlanStateGraphProperty(
            AfterRemoveCurveAdd,
            ModifyCurveGuid,
            CurveMapPath,
            OversizedCurveMap,
            true).Code,
        FString(TEXT("invalid_state_graph_node_property_value")));
    const FThomasSpecializedAssetInspectionResult AfterContainerRefusals =
        ReloadAndInspect(TEXT("R41 associative preflight refusals"));
    Test.TestEqual(
        TEXT("R41 associative preflight refusals preserve exact graph state"),
        GetGraphState(AfterContainerRefusals),
        GetGraphState(AfterRemoveCurveAdd));

    FThomasAnimationOperation SetCurveMap;
    SetCurveMap.Action = TEXT("set_state_graph_node_property");
    SetCurveMap.MachineName = MachineName;
    SetCurveMap.StateName = StateName;
    SetCurveMap.StateGraphNodeGuid = ModifyCurveGuid;
    SetCurveMap.PropertyName = CurveMapPath;
    SetCurveMap.SettingValue = PopulatedCurveMap;
    const FThomasSpecializedAssetInspectionResult AfterCurveMap =
        ApplyAndReload(
            TEXT("R41 bounded curve map replacement"),
            AfterContainerRefusals,
            SetCurveMap,
            true);
    const FString PopulatedCurveMapItem = FindNodePropertyItem(
        AfterCurveMap, ModifyCurveGuid, CurveMapPath);
    Test.TestTrue(
        TEXT("R41 TMap values survive compile save reload and canonical readback"),
        PopulatedCurveMapItem.Contains(TEXT("ThomasSpeed"))
            && PopulatedCurveMapItem.Contains(TEXT("1.250000"))
            && PopulatedCurveMapItem.Contains(TEXT("ThomasTurn"))
            && PopulatedCurveMapItem.Contains(TEXT("-0.500000"))
            && GetGraphState(AfterCurveMap)
                != GetGraphState(AfterContainerRefusals));
    Test.TestEqual(
        TEXT("R41 unchanged populated map is refused"),
        PlanStateGraphProperty(
            AfterCurveMap,
            ModifyCurveGuid,
            CurveMapPath,
            PopulatedCurveMap,
            true).Code,
        FString(TEXT("state_graph_node_property_unchanged")));

    FThomasAnimationPatchRequest StaleMapRequest;
    StaleMapRequest.AssetPath = AnimBlueprintPath;
    StaleMapRequest.ExpectedRevision = AfterCurveMap.Revision;
    StaleMapRequest.bConfirmDestructive = true;
    FThomasAnimationOperation StaleMap = SetCurveMap;
    StaleMap.SettingValue = TEXT("((ThomasSpeed,2.0))");
    StaleMap.ExpectedValue = GetGraphState(AfterContainerRefusals);
    StaleMapRequest.Operations.Add(StaleMap);
    Test.TestEqual(
        TEXT("R41 stale associative graph hash is refused"),
        UThomasDomainsToolset::PlanAnimationPatch(StaleMapRequest).Code,
        FString(TEXT("state_graph_conflict")));

    FThomasAnimationOperation SetCurveSet;
    SetCurveSet.Action = TEXT("set_state_graph_node_property");
    SetCurveSet.MachineName = MachineName;
    SetCurveSet.StateName = StateName;
    SetCurveSet.StateGraphNodeGuid = RemoveCurveGuid;
    SetCurveSet.PropertyName = CurveSetPath;
    SetCurveSet.SettingValue = PopulatedCurveSet;
    const FThomasSpecializedAssetInspectionResult AfterCurveSet =
        ApplyAndReload(
            TEXT("R41 bounded curve set replacement"),
            AfterCurveMap,
            SetCurveSet,
            true);
    const FString PopulatedCurveSetItem = FindNodePropertyItem(
        AfterCurveSet, RemoveCurveGuid, CurveSetPath);
    Test.TestTrue(
        TEXT("R41 TSet values survive compile save reload and sorted readback"),
        PopulatedCurveSetItem.Contains(
            TEXT("|Value=(ThomasSpeed,ThomasTurn)"))
            && GetGraphState(AfterCurveSet)
                != GetGraphState(AfterCurveMap));

    FThomasAnimationOperation RestoreCurveMap = SetCurveMap;
    RestoreCurveMap.SettingValue = TEXT("()");
    const FThomasSpecializedAssetInspectionResult AfterCurveMapRestore =
        ApplyAndReload(
            TEXT("R41 curve map restore"),
            AfterCurveSet,
            RestoreCurveMap,
            true);
    FThomasAnimationOperation RestoreCurveSet = SetCurveSet;
    RestoreCurveSet.SettingValue = TEXT("()");
    const FThomasSpecializedAssetInspectionResult AfterCurveSetRestore =
        ApplyAndReload(
            TEXT("R41 curve set restore"),
            AfterCurveMapRestore,
            RestoreCurveSet,
            true);
    Test.TestEqual(
        TEXT("R41 associative restore returns exact two-node graph hash"),
        GetGraphState(AfterCurveSetRestore),
        GetGraphState(AfterRemoveCurveAdd));

    FThomasAnimationOperation RemoveRemoveCurve;
    RemoveRemoveCurve.Action = TEXT("remove_state_graph_node");
    RemoveRemoveCurve.MachineName = MachineName;
    RemoveRemoveCurve.StateName = StateName;
    RemoveRemoveCurve.StateGraphNodeGuid = RemoveCurveGuid;
    const FThomasSpecializedAssetInspectionResult AfterRemoveCurveCleanup =
        ApplyAndReload(
            TEXT("R41 RemoveCurve node cleanup"),
            AfterCurveSetRestore,
            RemoveRemoveCurve,
            true);
    FThomasAnimationOperation RemoveModifyCurve;
    RemoveModifyCurve.Action = TEXT("remove_state_graph_node");
    RemoveModifyCurve.MachineName = MachineName;
    RemoveModifyCurve.StateName = StateName;
    RemoveModifyCurve.StateGraphNodeGuid = ModifyCurveGuid;
    const FThomasSpecializedAssetInspectionResult AfterR41Cleanup =
        ApplyAndReload(
            TEXT("R41 ModifyCurve node cleanup"),
            AfterRemoveCurveCleanup,
            RemoveModifyCurve,
            true);
    Test.TestEqual(
        TEXT("R41 node cleanup restores the exact pre-R41 graph hash"),
        GetGraphState(AfterR41Cleanup),
        GetGraphState(AfterPitchScalePropertyRestore));

    FThomasAnimationOperation AddLinkedAnimGraph;
    AddLinkedAnimGraph.Action = TEXT("add_state_graph_node");
    AddLinkedAnimGraph.MachineName = MachineName;
    AddLinkedAnimGraph.StateName = StateName;
    AddLinkedAnimGraph.ClassPath = LinkedAnimGraphClass;
    AddLinkedAnimGraph.PositionX = 50;
    AddLinkedAnimGraph.PositionY = 350;
    const FThomasSpecializedAssetInspectionResult AfterLinkedNodeAdd =
        ApplyAndReload(
            TEXT("R42 LinkedAnimGraph node add"),
            AfterR41Cleanup,
            AddLinkedAnimGraph,
            false);
    const FString LinkedNodeGuid = GetDelimitedField(
        FindNodeItem(AfterLinkedNodeAdd, LinkedAnimGraphClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    const FString InstanceClassPath = TEXT("Node.InstanceClass");
    const FString EmptyInstanceClassItem = FindNodePropertyItem(
        AfterLinkedNodeAdd,
        LinkedNodeGuid,
        InstanceClassPath);
    Test.TestTrue(
        TEXT("R42 LinkedAnimGraph class property is inspected and hashed"),
        !LinkedNodeGuid.IsEmpty()
            && EmptyInstanceClassItem.Contains(
                TEXT("|Type=TSubclassOf<UAnimInstance>|Value=None"))
            && FindNodeItem(
                AfterLinkedNodeAdd,
                LinkedAnimGraphClass).Contains(TEXT("|PropertyHash=")));
    Test.TestEqual(
        TEXT("R42 AnimInstance class replacement requires R2 confirmation"),
        PlanStateGraphProperty(
            AfterLinkedNodeAdd,
            LinkedNodeGuid,
            InstanceClassPath,
            LinkedTargetClassPath,
            false).Code,
        FString(TEXT("confirmation_required")));
    Test.TestEqual(
        TEXT("R42 package path without exact generated class is refused"),
        PlanStateGraphProperty(
            AfterLinkedNodeAdd,
            LinkedNodeGuid,
            InstanceClassPath,
            OutLinkedTargetPath,
            true).Code,
        FString(TEXT("invalid_state_graph_node_property_value")));
    Test.TestEqual(
        TEXT("R42 non-AnimBlueprint asset is refused as class source"),
        PlanStateGraphProperty(
            AfterLinkedNodeAdd,
            LinkedNodeGuid,
            InstanceClassPath,
            SkeletonContextPath,
            true).Code,
        FString(TEXT("invalid_state_graph_node_property_value")));
    Test.TestEqual(
        TEXT("R42 script class outside PropHunt is refused"),
        PlanStateGraphProperty(
            AfterLinkedNodeAdd,
            LinkedNodeGuid,
            InstanceClassPath,
            TEXT("/Script/Engine.AnimInstance"),
            true).Code,
        FString(TEXT("invalid_state_graph_node_property_value")));
    const FThomasSpecializedAssetInspectionResult AfterClassRefusals =
        ReloadAndInspect(TEXT("R42 class-reference preflight refusals"));
    Test.TestEqual(
        TEXT("R42 class-reference refusals preserve exact graph state"),
        GetGraphState(AfterClassRefusals),
        GetGraphState(AfterLinkedNodeAdd));

    FThomasAnimationOperation SetLinkedInstanceClass;
    SetLinkedInstanceClass.Action =
        TEXT("set_state_graph_node_property");
    SetLinkedInstanceClass.MachineName = MachineName;
    SetLinkedInstanceClass.StateName = StateName;
    SetLinkedInstanceClass.StateGraphNodeGuid = LinkedNodeGuid;
    SetLinkedInstanceClass.PropertyName = InstanceClassPath;
    SetLinkedInstanceClass.SettingValue = LinkedTargetClassPath;
    const FThomasSpecializedAssetInspectionResult AfterLinkedClass =
        ApplyAndReload(
            TEXT("R42 linked AnimInstance class assignment"),
            AfterClassRefusals,
            SetLinkedInstanceClass,
            true);
    const FString PopulatedInstanceClassItem = FindNodePropertyItem(
        AfterLinkedClass,
        LinkedNodeGuid,
        InstanceClassPath);
    Test.TestTrue(
        TEXT("R42 generated AnimInstance class survives compile save and reload"),
        PopulatedInstanceClassItem.Contains(
            TEXT("|Value=") + LinkedTargetClassPath)
            && GetGraphState(AfterLinkedClass)
                != GetGraphState(AfterClassRefusals));
    Test.TestEqual(
        TEXT("R42 unchanged generated class is refused"),
        PlanStateGraphProperty(
            AfterLinkedClass,
            LinkedNodeGuid,
            InstanceClassPath,
            LinkedTargetClassPath,
            true).Code,
        FString(TEXT("state_graph_node_property_unchanged")));

    FThomasAnimationPatchRequest StaleLinkedClassRequest;
    StaleLinkedClassRequest.AssetPath = AnimBlueprintPath;
    StaleLinkedClassRequest.ExpectedRevision = AfterLinkedClass.Revision;
    StaleLinkedClassRequest.bConfirmDestructive = true;
    FThomasAnimationOperation StaleLinkedClass =
        SetLinkedInstanceClass;
    StaleLinkedClass.SettingValue = TEXT("None");
    StaleLinkedClass.ExpectedValue = GetGraphState(AfterClassRefusals);
    StaleLinkedClassRequest.Operations.Add(StaleLinkedClass);
    Test.TestEqual(
        TEXT("R42 stale class-reference graph hash is refused"),
        UThomasDomainsToolset::PlanAnimationPatch(
            StaleLinkedClassRequest).Code,
        FString(TEXT("state_graph_conflict")));

    FThomasAnimationOperation RestoreLinkedInstanceClass =
        SetLinkedInstanceClass;
    RestoreLinkedInstanceClass.SettingValue = TEXT("None");
    const FThomasSpecializedAssetInspectionResult AfterLinkedClassRestore =
        ApplyAndReload(
            TEXT("R42 linked AnimInstance class restore"),
            AfterLinkedClass,
            RestoreLinkedInstanceClass,
            true);
    Test.TestEqual(
        TEXT("R42 class restore returns exact linked-node graph hash"),
        GetGraphState(AfterLinkedClassRestore),
        GetGraphState(AfterLinkedNodeAdd));

    FThomasAnimationOperation RemoveLinkedAnimGraph;
    RemoveLinkedAnimGraph.Action = TEXT("remove_state_graph_node");
    RemoveLinkedAnimGraph.MachineName = MachineName;
    RemoveLinkedAnimGraph.StateName = StateName;
    RemoveLinkedAnimGraph.StateGraphNodeGuid = LinkedNodeGuid;
    const FThomasSpecializedAssetInspectionResult AfterR42Cleanup =
        ApplyAndReload(
            TEXT("R42 LinkedAnimGraph node cleanup"),
            AfterLinkedClassRestore,
            RemoveLinkedAnimGraph,
            true);
    Test.TestEqual(
        TEXT("R42 cleanup restores the exact pre-R42 graph hash"),
        GetGraphState(AfterR42Cleanup),
        GetGraphState(AfterR41Cleanup));

    FThomasAnimationOperation AddBlendListByBool;
    AddBlendListByBool.Action = TEXT("add_state_graph_node");
    AddBlendListByBool.MachineName = MachineName;
    AddBlendListByBool.StateName = StateName;
    AddBlendListByBool.ClassPath = BlendListByBoolClass;
    AddBlendListByBool.PositionX = 300;
    AddBlendListByBool.PositionY = 350;
    const FThomasSpecializedAssetInspectionResult AfterCurveNodeAdd =
        ApplyAndReload(
            TEXT("R43 BlendListByBool node add"),
            AfterR42Cleanup,
            AddBlendListByBool,
            false);
    const FString BlendListByBoolGuid = GetDelimitedField(
        FindNodeItem(AfterCurveNodeAdd, BlendListByBoolClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    const FString CustomBlendCurvePath =
        TEXT("Node.CustomBlendCurve");
    const FString EmptyCurvePropertyItem = FindNodePropertyItem(
        AfterCurveNodeAdd,
        BlendListByBoolGuid,
        CustomBlendCurvePath);
    Test.TestTrue(
        TEXT("R43 CurveFloat property is inspected and hashed"),
        !BlendListByBoolGuid.IsEmpty()
            && EmptyCurvePropertyItem.Contains(
                TEXT("|Type=TObjectPtr<UCurveFloat>|Value=None"))
            && FindNodeItem(
                AfterCurveNodeAdd,
                BlendListByBoolClass).Contains(TEXT("|PropertyHash=")));
    const FThomasAnimationPlanResult CurveR1Plan =
        PlanStateGraphProperty(
            AfterCurveNodeAdd,
            BlendListByBoolGuid,
            CustomBlendCurvePath,
            OutCurveFloatPath,
            false);
    Test.TestTrue(
        TEXT("R43 additive CurveFloat assignment remains R1"),
        CurveR1Plan.bOk && CurveR1Plan.Risk == TEXT("R1"));
    Test.TestEqual(
        TEXT("R43 CurveFloat outside PropHunt is refused"),
        PlanStateGraphProperty(
            AfterCurveNodeAdd,
            BlendListByBoolGuid,
            CustomBlendCurvePath,
            TEXT("/Engine/EngineCurves/Linear.Linear"),
            false).Code,
        FString(TEXT("invalid_state_graph_node_property_value")));
    Test.TestEqual(
        TEXT("R43 incompatible Anim Blueprint asset is refused as curve"),
        PlanStateGraphProperty(
            AfterCurveNodeAdd,
            BlendListByBoolGuid,
            CustomBlendCurvePath,
            OutLinkedTargetPath,
            false).Code,
        FString(TEXT("invalid_state_graph_node_property_value")));
    const FThomasSpecializedAssetInspectionResult AfterCurveRefusals =
        ReloadAndInspect(TEXT("R43 CurveFloat preflight refusals"));
    Test.TestEqual(
        TEXT("R43 CurveFloat refusals preserve exact graph state"),
        GetGraphState(AfterCurveRefusals),
        GetGraphState(AfterCurveNodeAdd));

    FThomasAnimationOperation SetCustomBlendCurve;
    SetCustomBlendCurve.Action =
        TEXT("set_state_graph_node_property");
    SetCustomBlendCurve.MachineName = MachineName;
    SetCustomBlendCurve.StateName = StateName;
    SetCustomBlendCurve.StateGraphNodeGuid = BlendListByBoolGuid;
    SetCustomBlendCurve.PropertyName = CustomBlendCurvePath;
    SetCustomBlendCurve.SettingValue = OutCurveFloatPath;
    const FThomasSpecializedAssetInspectionResult AfterCustomBlendCurve =
        ApplyAndReload(
            TEXT("R43 custom blend CurveFloat assignment"),
            AfterCurveRefusals,
            SetCustomBlendCurve,
            false);
    const FString ExpectedCurveObjectPath = OutCurveFloatPath
        + TEXT(".")
        + FPackageName::GetLongPackageAssetName(OutCurveFloatPath);
    Test.TestTrue(
        TEXT("R43 CurveFloat reference survives compile save and reload"),
        FindNodePropertyItem(
            AfterCustomBlendCurve,
            BlendListByBoolGuid,
            CustomBlendCurvePath).Contains(
                TEXT("|Value=") + ExpectedCurveObjectPath)
            && GetGraphState(AfterCustomBlendCurve)
                != GetGraphState(AfterCurveRefusals));
    Test.TestEqual(
        TEXT("R43 unchanged CurveFloat reference is refused"),
        PlanStateGraphProperty(
            AfterCustomBlendCurve,
            BlendListByBoolGuid,
            CustomBlendCurvePath,
            OutCurveFloatPath,
            false).Code,
        FString(TEXT("state_graph_node_property_unchanged")));

    FThomasAnimationPatchRequest StaleCurveRequest;
    StaleCurveRequest.AssetPath = AnimBlueprintPath;
    StaleCurveRequest.ExpectedRevision = AfterCustomBlendCurve.Revision;
    FThomasAnimationOperation StaleCurve = SetCustomBlendCurve;
    StaleCurve.SettingValue = TEXT("None");
    StaleCurve.ExpectedValue = GetGraphState(AfterCurveRefusals);
    StaleCurveRequest.Operations.Add(StaleCurve);
    Test.TestEqual(
        TEXT("R43 stale CurveFloat graph hash is refused"),
        UThomasDomainsToolset::PlanAnimationPatch(
            StaleCurveRequest).Code,
        FString(TEXT("state_graph_conflict")));

    FThomasAnimationOperation RestoreCustomBlendCurve =
        SetCustomBlendCurve;
    RestoreCustomBlendCurve.SettingValue = TEXT("None");
    const FThomasSpecializedAssetInspectionResult AfterCurveRestore =
        ApplyAndReload(
            TEXT("R43 custom blend CurveFloat restore"),
            AfterCustomBlendCurve,
            RestoreCustomBlendCurve,
            false);
    Test.TestEqual(
        TEXT("R43 CurveFloat restore returns exact node graph hash"),
        GetGraphState(AfterCurveRestore),
        GetGraphState(AfterCurveNodeAdd));

    FThomasAnimationOperation RemoveBlendListByBool;
    RemoveBlendListByBool.Action = TEXT("remove_state_graph_node");
    RemoveBlendListByBool.MachineName = MachineName;
    RemoveBlendListByBool.StateName = StateName;
    RemoveBlendListByBool.StateGraphNodeGuid = BlendListByBoolGuid;
    const FThomasSpecializedAssetInspectionResult AfterR43Cleanup =
        ApplyAndReload(
            TEXT("R43 BlendListByBool node cleanup"),
            AfterCurveRestore,
            RemoveBlendListByBool,
            true);
    Test.TestEqual(
        TEXT("R43 cleanup restores the exact pre-R43 graph hash"),
        GetGraphState(AfterR43Cleanup),
        GetGraphState(AfterR42Cleanup));

    auto RunStrongObjectReferenceCycle = [
        &Test,
        &AnimBlueprintPath,
        &MachineName,
        &StateName,
        &FindNodeItem,
        &FindNodePropertyItem,
        &GetDelimitedField,
        &GetGraphState,
        &PlanStateGraphProperty,
        &ApplyAndReload,
        &ReloadAndInspect,
        &OutCurveFloatPath](
        const FString& Label,
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& NodeClass,
        const FString& PropertyPath,
        const FString& PropertyType,
        const FString& ObjectInput,
        const FString& ExpectedObjectPath,
        const FString& ExpectedRisk,
        const int32 PositionX)
    {
        FThomasAnimationOperation AddNode;
        AddNode.Action = TEXT("add_state_graph_node");
        AddNode.MachineName = MachineName;
        AddNode.StateName = StateName;
        AddNode.ClassPath = NodeClass;
        AddNode.PositionX = PositionX;
        AddNode.PositionY = 480;
        const FThomasSpecializedAssetInspectionResult AfterNodeAdd =
            ApplyAndReload(
                Label + TEXT(" node add"),
                Before,
                AddNode,
                false);
        const FString NodeGuid = GetDelimitedField(
            FindNodeItem(AfterNodeAdd, NodeClass),
            TEXT("State=Guid="),
            TEXT("|Class="));
        Test.TestTrue(
            Label + TEXT(" property is inspected and hashed"),
            !NodeGuid.IsEmpty()
                && FindNodePropertyItem(
                    AfterNodeAdd,
                    NodeGuid,
                    PropertyPath).Contains(
                        TEXT("|Type=") + PropertyType
                        + TEXT("|Value=None"))
                && FindNodeItem(
                    AfterNodeAdd,
                    NodeClass).Contains(TEXT("|PropertyHash=")));
        const bool bR2 = ExpectedRisk == TEXT("R2");
        if (bR2)
        {
            Test.TestEqual(
                Label + TEXT(" requires explicit R2 confirmation"),
                PlanStateGraphProperty(
                    AfterNodeAdd,
                    NodeGuid,
                    PropertyPath,
                    ObjectInput,
                    false).Code,
                FString(TEXT("confirmation_required")));
        }
        const FThomasAnimationPlanResult AssignmentPlan =
            PlanStateGraphProperty(
                AfterNodeAdd,
                NodeGuid,
                PropertyPath,
                ObjectInput,
                bR2);
        Test.TestTrue(
            Label + TEXT(" assignment exposes its exact risk"),
            AssignmentPlan.bOk
                && AssignmentPlan.Risk == ExpectedRisk);
        Test.TestEqual(
            Label + TEXT(" refuses an asset outside PropHunt"),
            PlanStateGraphProperty(
                AfterNodeAdd,
                NodeGuid,
                PropertyPath,
                TEXT("/Engine/Transient.External"),
                bR2).Code,
            FString(TEXT("invalid_state_graph_node_property_value")));
        Test.TestEqual(
            Label + TEXT(" refuses an incompatible CurveFloat"),
            PlanStateGraphProperty(
                AfterNodeAdd,
                NodeGuid,
                PropertyPath,
                OutCurveFloatPath,
                bR2).Code,
            FString(TEXT("invalid_state_graph_node_property_value")));
        const FThomasSpecializedAssetInspectionResult AfterRefusals =
            ReloadAndInspect(Label + TEXT(" preflight refusals"));
        Test.TestEqual(
            Label + TEXT(" refusals preserve exact graph state"),
            GetGraphState(AfterRefusals),
            GetGraphState(AfterNodeAdd));

        FThomasAnimationOperation SetReference;
        SetReference.Action =
            TEXT("set_state_graph_node_property");
        SetReference.MachineName = MachineName;
        SetReference.StateName = StateName;
        SetReference.StateGraphNodeGuid = NodeGuid;
        SetReference.PropertyName = PropertyPath;
        SetReference.SettingValue = ObjectInput;
        const FThomasSpecializedAssetInspectionResult AfterReference =
            ApplyAndReload(
                Label + TEXT(" assignment"),
                AfterRefusals,
                SetReference,
                bR2);
        Test.TestTrue(
            Label + TEXT(" survives compile save and reload"),
            FindNodePropertyItem(
                AfterReference,
                NodeGuid,
                PropertyPath).Contains(
                    TEXT("|Value=") + ExpectedObjectPath)
                && GetGraphState(AfterReference)
                    != GetGraphState(AfterRefusals));
        Test.TestEqual(
            Label + TEXT(" unchanged value is refused"),
            PlanStateGraphProperty(
                AfterReference,
                NodeGuid,
                PropertyPath,
                ObjectInput,
                bR2).Code,
            FString(TEXT("state_graph_node_property_unchanged")));

        FThomasAnimationPatchRequest StaleRequest;
        StaleRequest.AssetPath = AnimBlueprintPath;
        StaleRequest.ExpectedRevision = AfterReference.Revision;
        StaleRequest.bConfirmDestructive = bR2;
        FThomasAnimationOperation StaleReference = SetReference;
        StaleReference.SettingValue = TEXT("None");
        StaleReference.ExpectedValue = GetGraphState(AfterRefusals);
        StaleRequest.Operations.Add(StaleReference);
        Test.TestEqual(
            Label + TEXT(" stale graph hash is refused"),
            UThomasDomainsToolset::PlanAnimationPatch(
                StaleRequest).Code,
            FString(TEXT("state_graph_conflict")));

        FThomasAnimationOperation RestoreReference = SetReference;
        RestoreReference.SettingValue = TEXT("None");
        const FThomasSpecializedAssetInspectionResult AfterRestore =
            ApplyAndReload(
                Label + TEXT(" restore"),
                AfterReference,
                RestoreReference,
                bR2);
        Test.TestEqual(
            Label + TEXT(" restore returns exact node graph hash"),
            GetGraphState(AfterRestore),
            GetGraphState(AfterNodeAdd));

        FThomasAnimationOperation RemoveNode;
        RemoveNode.Action = TEXT("remove_state_graph_node");
        RemoveNode.MachineName = MachineName;
        RemoveNode.StateName = StateName;
        RemoveNode.StateGraphNodeGuid = NodeGuid;
        const FThomasSpecializedAssetInspectionResult AfterCleanup =
            ApplyAndReload(
                Label + TEXT(" node cleanup"),
                AfterRestore,
                RemoveNode,
                true);
        Test.TestEqual(
            Label + TEXT(" cleanup restores exact baseline hash"),
            GetGraphState(AfterCleanup),
            GetGraphState(Before));
        return AfterCleanup;
    };

    const FThomasSpecializedAssetInspectionResult AfterPhysicsAssetCleanup =
        RunStrongObjectReferenceCycle(
            TEXT("R44 PhysicsAsset override"),
            AfterR43Cleanup,
            RigidBodyClass,
            TEXT("Node.OverridePhysicsAsset"),
            TEXT("TObjectPtr<UPhysicsAsset>"),
            OutPhysicsAssetPath,
            OutPhysicsAssetPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    OutPhysicsAssetPath),
            TEXT("R1"),
            50);
    const FThomasSpecializedAssetInspectionResult AfterR44Cleanup =
        RunStrongObjectReferenceCycle(
            TEXT("R44 MirrorDataTable"),
            AfterPhysicsAssetCleanup,
            MirrorClass,
            TEXT("Node.MirrorDataTable"),
            TEXT("TObjectPtr<UMirrorDataTable>"),
            OutMirrorDataTablePath,
            OutMirrorDataTablePath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    OutMirrorDataTablePath),
            TEXT("R1"),
            300);
    Test.TestEqual(
        TEXT("R44 combined cleanup restores the exact pre-R44 graph hash"),
        GetGraphState(AfterR44Cleanup),
        GetGraphState(AfterR43Cleanup));

    const FThomasSpecializedAssetInspectionResult AfterBlendProfileCleanup =
        RunStrongObjectReferenceCycle(
            TEXT("R45 BlendProfile subobject"),
            AfterR44Cleanup,
            BlendListByBoolClass,
            TEXT("Node.BlendProfile"),
            TEXT("TObjectPtr<UBlendProfile>"),
            BlendProfileObjectPath,
            BlendProfileObjectPath,
            TEXT("R1"),
            550);
    const FThomasSpecializedAssetInspectionResult AfterIKRigCleanup =
        RunStrongObjectReferenceCycle(
            TEXT("R45 IK Rig definition"),
            AfterBlendProfileCleanup,
            IKRigNodeClass,
            TEXT("Node.RigDefinitionAsset"),
            TEXT("TObjectPtr<UIKRigDefinition>"),
            IKRigPath,
            IKRigPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(IKRigPath),
            TEXT("R2"),
            800);
    const FThomasSpecializedAssetInspectionResult AfterIKRetargeterCleanup =
        RunStrongObjectReferenceCycle(
            TEXT("R45 IK Retargeter"),
            AfterIKRigCleanup,
            IKRetargetNodeClass,
            TEXT("Node.IKRetargeterAsset"),
            TEXT("TObjectPtr<UIKRetargeter>"),
            IKRetargeterPath,
            IKRetargeterPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    IKRetargeterPath),
            TEXT("R2"),
            1050);
    const FThomasSpecializedAssetInspectionResult AfterR45Cleanup =
        RunStrongObjectReferenceCycle(
            TEXT("R45 Physics Control Asset"),
            AfterIKRetargeterCleanup,
            RigidBodyWithControlClass,
            TEXT("Node.PhysicsControlAsset"),
            TEXT("TObjectPtr<UPhysicsControlAsset>"),
            OutPhysicsControlAssetPath,
            OutPhysicsControlAssetPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    OutPhysicsControlAssetPath),
            TEXT("R1"),
            1300);
    Test.TestEqual(
        TEXT("R45 combined cleanup restores the exact pre-R45 graph hash"),
        GetGraphState(AfterR45Cleanup),
        GetGraphState(AfterR44Cleanup));

    FThomasAnimationOperation InvalidPitchDefault;
    InvalidPitchDefault.Action = TEXT("set_state_graph_pin_default");
    InvalidPitchDefault.MachineName = MachineName;
    InvalidPitchDefault.StateName = StateName;
    InvalidPitchDefault.StateGraphPinGuid = PitchPinGuid;
    InvalidPitchDefault.StateGraphPinDefaultValue = TEXT("not_a_number");
    InvalidPitchDefault.ExpectedValue = GetGraphState(
        AfterR45Cleanup);
    FThomasAnimationPatchRequest InvalidPitchRequest;
    InvalidPitchRequest.AssetPath = AnimBlueprintPath;
    InvalidPitchRequest.ExpectedRevision =
        AfterR45Cleanup.Revision;
    InvalidPitchRequest.Operations.Add(InvalidPitchDefault);
    const FThomasAnimationPlanResult InvalidPitchPlan =
        UThomasDomainsToolset::PlanAnimationPatch(InvalidPitchRequest);
    Test.TestTrue(
        TEXT("Invalid generic pin default reaches transactional apply validation"),
        InvalidPitchPlan.bOk);
    if (InvalidPitchPlan.bOk)
    {
        Test.TestFalse(
            TEXT("Invalid generic pin default is rejected and rolled back"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                InvalidPitchPlan.PlanId, true).bOk);
    }
    const FThomasSpecializedAssetInspectionResult AfterInvalidRollback =
        ReloadAndInspect(TEXT("Invalid generic pin default rollback"));
    Test.TestEqual(
        TEXT("Invalid generic pin default rollback preserves exact graph state"),
        GetGraphState(AfterInvalidRollback),
        GetGraphState(AfterR45Cleanup));

    FThomasAnimationOperation SetPitchDefault;
    SetPitchDefault.Action = TEXT("set_state_graph_pin_default");
    SetPitchDefault.MachineName = MachineName;
    SetPitchDefault.StateName = StateName;
    SetPitchDefault.StateGraphPinGuid = PitchPinGuid;
    SetPitchDefault.StateGraphPinDefaultValue = TEXT("15.0");
    FThomasSpecializedAssetInspectionResult AfterPitchDefault =
        ApplyAndReload(
            TEXT("Generic state graph pitch default"),
            AfterInvalidRollback,
            SetPitchDefault,
            false);
    Test.TestTrue(
        TEXT("Generic state graph pin default survives save and reload"),
        FindPinItem(
            AfterPitchDefault,
            RotateRootBoneGuid,
            TEXT("Input"),
            TEXT("Pitch"),
            false).Contains(TEXT("|Default=15.0|")));

    FThomasAnimationOperation StaleDefault = SetPitchDefault;
    StaleDefault.StateGraphPinDefaultValue = TEXT("30.0");
    StaleDefault.ExpectedValue = GetGraphState(AfterRotateRootBone);
    FThomasAnimationPatchRequest StaleDefaultRequest;
    StaleDefaultRequest.AssetPath = AnimBlueprintPath;
    StaleDefaultRequest.ExpectedRevision = AfterPitchDefault.Revision;
    StaleDefaultRequest.Operations.Add(StaleDefault);
    Test.TestEqual(
        TEXT("Stale generic state graph content hash is refused"),
        UThomasDomainsToolset::PlanAnimationPatch(
            StaleDefaultRequest).Code,
        FString(TEXT("state_graph_conflict")));

    FThomasAnimationOperation LinkLocalToRotate;
    LinkLocalToRotate.Action = TEXT("add_state_graph_link");
    LinkLocalToRotate.MachineName = MachineName;
    LinkLocalToRotate.StateName = StateName;
    LinkLocalToRotate.StateGraphSourcePinGuid = LocalPosePinGuid;
    LinkLocalToRotate.StateGraphTargetPinGuid = RotateInputPosePinGuid;
    FThomasSpecializedAssetInspectionResult AfterFirstLink =
        ApplyAndReload(
            TEXT("Generic state graph first pose link"),
            AfterPitchDefault,
            LinkLocalToRotate,
            false);

    const FString StateResultItem = FindNodeItem(
        AfterFirstLink, StateResultClass);
    const FString StateResultGuid = GetDelimitedField(
        StateResultItem, TEXT("State=Guid="), TEXT("|Class="));
    const FString StateResultInputPinGuid = GetDelimitedField(
        FindPinItem(
            AfterFirstLink,
            StateResultGuid,
            TEXT("Input"),
            FString(),
            true),
        TEXT("State=PinGuid="),
        TEXT("|Name="));
    Test.TestTrue(
        TEXT("Protected state-result sink remains inspectable by persistent GUID"),
        !StateResultGuid.IsEmpty() && !StateResultInputPinGuid.IsEmpty());

    FThomasAnimationOperation LinkRotateToResult;
    LinkRotateToResult.Action = TEXT("add_state_graph_link");
    LinkRotateToResult.MachineName = MachineName;
    LinkRotateToResult.StateName = StateName;
    LinkRotateToResult.StateGraphSourcePinGuid = RotateOutputPosePinGuid;
    LinkRotateToResult.StateGraphTargetPinGuid = StateResultInputPinGuid;
    FThomasSpecializedAssetInspectionResult AfterSecondLink =
        ApplyAndReload(
            TEXT("Generic state graph result pose link"),
            AfterFirstLink,
            LinkRotateToResult,
            false);
    Test.TestTrue(
        TEXT("Generic two-link state graph survives compile save and reload"),
        GetGraphState(AfterSecondLink).Contains(TEXT("LinkCount=2|"))
            && AfterSecondLink.Items.ContainsByPredicate(
                [LocalPosePinGuid, RotateInputPosePinGuid](
                    const FString& Item)
                {
                    return Item.StartsWith(TEXT("StateGraphLink "))
                        && Item.Contains(
                            TEXT("SourcePinGuid=") + LocalPosePinGuid)
                        && Item.Contains(
                            TEXT("TargetPinGuid=") + RotateInputPosePinGuid);
                })
            && AfterSecondLink.Items.ContainsByPredicate(
                [RotateOutputPosePinGuid, StateResultInputPinGuid](
                    const FString& Item)
                {
                    return Item.StartsWith(TEXT("StateGraphLink "))
                        && Item.Contains(
                            TEXT("SourcePinGuid=") + RotateOutputPosePinGuid)
                        && Item.Contains(
                            TEXT("TargetPinGuid=") + StateResultInputPinGuid);
                }));

    FThomasAnimationOperation RemoveResultLink;
    RemoveResultLink.Action = TEXT("remove_state_graph_link");
    RemoveResultLink.MachineName = MachineName;
    RemoveResultLink.StateName = StateName;
    RemoveResultLink.StateGraphSourcePinGuid = RotateOutputPosePinGuid;
    RemoveResultLink.StateGraphTargetPinGuid = StateResultInputPinGuid;
    RemoveResultLink.ExpectedValue = GetGraphState(AfterSecondLink);
    FThomasAnimationPatchRequest UnconfirmedLinkRemovalRequest;
    UnconfirmedLinkRemovalRequest.AssetPath = AnimBlueprintPath;
    UnconfirmedLinkRemovalRequest.ExpectedRevision = AfterSecondLink.Revision;
    UnconfirmedLinkRemovalRequest.Operations.Add(RemoveResultLink);
    Test.TestEqual(
        TEXT("Generic state graph link removal requires R2 confirmation"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedLinkRemovalRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterResultLinkRemoval =
        ApplyAndReload(
            TEXT("Generic state graph result link removal"),
            AfterSecondLink,
            RemoveResultLink,
            true);

    FThomasAnimationOperation RemoveRotateRootBone;
    RemoveRotateRootBone.Action = TEXT("remove_state_graph_node");
    RemoveRotateRootBone.MachineName = MachineName;
    RemoveRotateRootBone.StateName = StateName;
    RemoveRotateRootBone.StateGraphNodeGuid = RotateRootBoneGuid;
    FThomasSpecializedAssetInspectionResult AfterRotateRemoval =
        ApplyAndReload(
            TEXT("Generic state graph rotate-root-bone removal"),
            AfterResultLinkRemoval,
            RemoveRotateRootBone,
            true);
    Test.TestTrue(
        TEXT("Generic node removal breaks attached links and survives reload"),
        FindNodeItem(AfterRotateRemoval, RotateRootBoneClass).IsEmpty()
            && GetGraphState(AfterRotateRemoval).Contains(
                TEXT("LinkCount=0|")));

    FThomasAnimationOperation RemoveLocalRefPose;
    RemoveLocalRefPose.Action = TEXT("remove_state_graph_node");
    RemoveLocalRefPose.MachineName = MachineName;
    RemoveLocalRefPose.StateName = StateName;
    RemoveLocalRefPose.StateGraphNodeGuid = LocalRefPoseGuid;
    FThomasSpecializedAssetInspectionResult AfterCleanup =
        ApplyAndReload(
            TEXT("Generic state graph local-ref-pose cleanup"),
            AfterRotateRemoval,
            RemoveLocalRefPose,
            true);
    Test.TestEqual(
        TEXT("Generic state graph cleanup restores exact baseline state"),
        GetGraphState(AfterCleanup),
        BaselineGraphState);

    auto RunNestedLeafCycle = [
        &Test,
        &AnimBlueprintPath,
        &MachineName,
        &StateName,
        &GetGraphState,
        &GetDelimitedField,
        &FindNodePropertyItem,
        &PlanStateGraphProperty,
        &ApplyAndReload](
        const FString& Label,
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& NodeGuid,
        const FString& PropertyPath,
        const FString& ExpectedType,
        const FString& InvalidValue,
        const FString& UpdatedValue,
        const FString& ExpectedCanonicalValue)
    {
        const FString InitialItem = FindNodePropertyItem(
            Before, NodeGuid, PropertyPath);
        const FString InitialValue = GetDelimitedField(
            InitialItem, TEXT("|Value="), TEXT("\x1f"));
        Test.AddInfo(Label + TEXT(" inspection: ") + InitialItem);
        Test.TestTrue(
            Label + TEXT(" is inspected as a typed nested leaf"),
            !NodeGuid.IsEmpty()
                && !InitialValue.IsEmpty()
                && InitialItem.Contains(TEXT("|Type=") + ExpectedType + TEXT("|")));
        Test.TestEqual(
            Label + TEXT(" refuses malformed input in preflight"),
            PlanStateGraphProperty(
                Before,
                NodeGuid,
                PropertyPath,
                InvalidValue,
                false).Code,
            FString(TEXT("invalid_state_graph_node_property_value")));
        const FThomasAnimationPlanResult UpdatePlan =
            PlanStateGraphProperty(
                Before,
                NodeGuid,
                PropertyPath,
                UpdatedValue,
                false);
        Test.TestTrue(
            Label + TEXT(" exposes an exact R1 mutation"),
            UpdatePlan.bOk && UpdatePlan.Risk == TEXT("R1"));

        FThomasAnimationOperation SetProperty;
        SetProperty.Action = TEXT("set_state_graph_node_property");
        SetProperty.MachineName = MachineName;
        SetProperty.StateName = StateName;
        SetProperty.StateGraphNodeGuid = NodeGuid;
        SetProperty.PropertyName = PropertyPath;
        SetProperty.SettingValue = UpdatedValue;
        const FThomasSpecializedAssetInspectionResult AfterUpdate =
            ApplyAndReload(
                Label + TEXT(" update"),
                Before,
                SetProperty,
                false);
        Test.TestTrue(
            Label + TEXT(" survives compile save and reload"),
            FindNodePropertyItem(
                AfterUpdate,
                NodeGuid,
                PropertyPath).Contains(
                    TEXT("|Value=") + ExpectedCanonicalValue)
                && GetGraphState(AfterUpdate) != GetGraphState(Before));
        Test.TestEqual(
            Label + TEXT(" refuses an unchanged value"),
            PlanStateGraphProperty(
                AfterUpdate,
                NodeGuid,
                PropertyPath,
                UpdatedValue,
                false).Code,
            FString(TEXT("state_graph_node_property_unchanged")));

        FThomasAnimationPatchRequest StaleRequest;
        StaleRequest.AssetPath = AnimBlueprintPath;
        StaleRequest.ExpectedRevision = AfterUpdate.Revision;
        FThomasAnimationOperation StaleProperty = SetProperty;
        StaleProperty.SettingValue = InitialValue;
        StaleProperty.ExpectedValue = GetGraphState(Before);
        StaleRequest.Operations.Add(StaleProperty);
        Test.TestEqual(
            Label + TEXT(" refuses a stale graph hash"),
            UThomasDomainsToolset::PlanAnimationPatch(
                StaleRequest).Code,
            FString(TEXT("state_graph_conflict")));

        FThomasAnimationOperation RestoreProperty = SetProperty;
        RestoreProperty.SettingValue = InitialValue;
        const FThomasSpecializedAssetInspectionResult AfterRestore =
            ApplyAndReload(
                Label + TEXT(" restore"),
                AfterUpdate,
                RestoreProperty,
                false);
        Test.TestEqual(
            Label + TEXT(" restore returns the exact graph hash"),
            GetGraphState(AfterRestore),
            GetGraphState(Before));
        return AfterRestore;
    };

    FThomasAnimationOperation SetR46SequencePlayer;
    SetR46SequencePlayer.Action = TEXT("set_state_sequence_player");
    SetR46SequencePlayer.MachineName = MachineName;
    SetR46SequencePlayer.StateName = StateName;
    SetR46SequencePlayer.ReferencedAssetPath = SequenceAssetPath;
    SetR46SequencePlayer.PlayRate = 1.0f;
    SetR46SequencePlayer.bLoopAnimation = true;
    SetR46SequencePlayer.PositionX = -300;
    SetR46SequencePlayer.PositionY = 0;
    const FThomasSpecializedAssetInspectionResult AfterR46SequenceAdd =
        ApplyAndReload(
            TEXT("R46 valid Sequence Player setup"),
            AfterCleanup,
            SetR46SequencePlayer,
            false);
    const FString R46SequenceGuid = GetDelimitedField(
        FindNodeItem(AfterR46SequenceAdd, SequencePlayerClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    Test.TestTrue(
        TEXT("R46 Sequence Player exposes constants and nested InputRange leaves"),
        FindNodePropertyItem(
            AfterR46SequenceAdd,
            R46SequenceGuid,
            TEXT("Node.PlayRateScaleBiasClampConstants.InRange.Min"))
                .Contains(TEXT("|Type=float|"))
            && FindNodePropertyItem(
                AfterR46SequenceAdd,
                R46SequenceGuid,
                TEXT("Node.PlayRateScaleBiasClampConstants.OutRange.Max"))
                .Contains(TEXT("|Type=float|")));
    const FThomasSpecializedAssetInspectionResult AfterR46ScaleRestore =
        RunNestedLeafCycle(
            TEXT("R46 InputScaleBiasClampConstants scale"),
            AfterR46SequenceAdd,
            R46SequenceGuid,
            TEXT("Node.PlayRateScaleBiasClampConstants.Scale"),
            TEXT("float"),
            TEXT("not_a_float"),
            TEXT("1.5"),
            TEXT("1.500000"));
    FThomasAnimationOperation RemoveR46SequencePlayer;
    RemoveR46SequencePlayer.Action = TEXT("remove_state_sequence_player");
    RemoveR46SequencePlayer.MachineName = MachineName;
    RemoveR46SequencePlayer.StateName = StateName;
    const FThomasSpecializedAssetInspectionResult AfterR46SequenceCleanup =
        ApplyAndReload(
            TEXT("R46 Sequence Player cleanup"),
            AfterR46ScaleRestore,
            RemoveR46SequencePlayer,
            true);
    Test.TestEqual(
        TEXT("R46 Sequence Player cleanup restores the exact baseline hash"),
        GetGraphState(AfterR46SequenceCleanup),
        BaselineGraphState);

    FThomasAnimationOperation AddR46LookAt;
    AddR46LookAt.Action = TEXT("add_state_graph_node");
    AddR46LookAt.MachineName = MachineName;
    AddR46LookAt.StateName = StateName;
    AddR46LookAt.ClassPath = LookAtClass;
    AddR46LookAt.PositionX = -250;
    AddR46LookAt.PositionY = 200;
    const FThomasSpecializedAssetInspectionResult AfterR46LookAtAdd =
        ApplyAndReload(
            TEXT("R46 LookAt node add"),
            AfterR46SequenceCleanup,
            AddR46LookAt,
            false);
    const FString R46LookAtGuid = GetDelimitedField(
        FindNodeItem(AfterR46LookAtAdd, LookAtClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    Test.TestTrue(
        TEXT("R46 LookAt exposes Axis, BoneSocketTarget and SocketReference leaves"),
        FindNodePropertyItem(
            AfterR46LookAtAdd,
            R46LookAtGuid,
            TEXT("Node.LookAt_Axis.Axis")).Contains(TEXT("|Type=FVector|"))
            && FindNodePropertyItem(
                AfterR46LookAtAdd,
                R46LookAtGuid,
                TEXT("Node.LookAtTarget.bUseSocket")).Contains(TEXT("|Type=bool|"))
            && FindNodePropertyItem(
                AfterR46LookAtAdd,
                R46LookAtGuid,
                TEXT("Node.LookAtTarget.SocketReference.SocketName"))
                    .Contains(TEXT("|Type=FName|"))
            && FindNodePropertyItem(
                AfterR46LookAtAdd,
                R46LookAtGuid,
                TEXT("Node.AlphaBoolBlend.CustomCurve"))
                    .Contains(TEXT("|Type=TObjectPtr<UCurveFloat>|Value=None")));
    const FThomasSpecializedAssetInspectionResult AfterR46AlphaRestore =
        RunNestedLeafCycle(
            TEXT("R46 InputAlphaBoolBlend blend-in"),
            AfterR46LookAtAdd,
            R46LookAtGuid,
            TEXT("Node.AlphaBoolBlend.BlendInTime"),
            TEXT("float"),
            TEXT("not_a_float"),
            TEXT("0.35"),
            TEXT("0.350000"));
    const FThomasSpecializedAssetInspectionResult AfterR46TargetRestore =
        RunNestedLeafCycle(
            TEXT("R46 BoneSocketTarget selector"),
            AfterR46AlphaRestore,
            R46LookAtGuid,
            TEXT("Node.LookAtTarget.bUseSocket"),
            TEXT("bool"),
            TEXT("not_a_bool"),
            TEXT("True"),
            TEXT("True"));
    FThomasAnimationOperation RemoveR46LookAt;
    RemoveR46LookAt.Action = TEXT("remove_state_graph_node");
    RemoveR46LookAt.MachineName = MachineName;
    RemoveR46LookAt.StateName = StateName;
    RemoveR46LookAt.StateGraphNodeGuid = R46LookAtGuid;
    const FThomasSpecializedAssetInspectionResult AfterR46LookAtCleanup =
        ApplyAndReload(
            TEXT("R46 LookAt cleanup"),
            AfterR46TargetRestore,
            RemoveR46LookAt,
            true);
    Test.TestEqual(
        TEXT("R46 LookAt cleanup restores the exact baseline hash"),
        GetGraphState(AfterR46LookAtCleanup),
        BaselineGraphState);

    FThomasAnimationOperation AddR46TwistCorrective;
    AddR46TwistCorrective.Action = TEXT("add_state_graph_node");
    AddR46TwistCorrective.MachineName = MachineName;
    AddR46TwistCorrective.StateName = StateName;
    AddR46TwistCorrective.ClassPath = TwistCorrectiveClass;
    AddR46TwistCorrective.PositionX = 50;
    AddR46TwistCorrective.PositionY = 200;
    const FThomasSpecializedAssetInspectionResult AfterR46TwistAdd =
        ApplyAndReload(
            TEXT("R46 TwistCorrective node add"),
            AfterR46LookAtCleanup,
            AddR46TwistCorrective,
            false);
    const FString R46TwistGuid = GetDelimitedField(
        FindNodeItem(AfterR46TwistAdd, TwistCorrectiveClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    Test.TestTrue(
        TEXT("R46 ReferenceBoneFrame exposes nested bone and Axis leaves"),
        FindNodePropertyItem(
            AfterR46TwistAdd,
            R46TwistGuid,
            TEXT("Node.BaseFrame.Bone.BoneName")).Contains(TEXT("|Type=FName|"))
            && FindNodePropertyItem(
                AfterR46TwistAdd,
                R46TwistGuid,
                TEXT("Node.BaseFrame.Axis.bInLocalSpace"))
                    .Contains(TEXT("|Type=bool|")));
    const FThomasSpecializedAssetInspectionResult AfterR46FrameRestore =
        RunNestedLeafCycle(
            TEXT("R46 ReferenceBoneFrame local-space axis"),
            AfterR46TwistAdd,
            R46TwistGuid,
            TEXT("Node.BaseFrame.Axis.bInLocalSpace"),
            TEXT("bool"),
            TEXT("not_a_bool"),
            TEXT("False"),
            TEXT("False"));
    FThomasAnimationOperation RemoveR46TwistCorrective;
    RemoveR46TwistCorrective.Action = TEXT("remove_state_graph_node");
    RemoveR46TwistCorrective.MachineName = MachineName;
    RemoveR46TwistCorrective.StateName = StateName;
    RemoveR46TwistCorrective.StateGraphNodeGuid = R46TwistGuid;
    const FThomasSpecializedAssetInspectionResult AfterR46Cleanup =
        ApplyAndReload(
            TEXT("R46 TwistCorrective cleanup"),
            AfterR46FrameRestore,
            RemoveR46TwistCorrective,
            true);
    Test.TestEqual(
        TEXT("R46 combined cleanup restores the exact pre-R46 graph hash"),
        GetGraphState(AfterR46Cleanup),
        BaselineGraphState);

    auto AddR47Node = [
        &ApplyAndReload,
        &MachineName,
        &StateName](
        const FString& Label,
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& ClassPath,
        const int32 PositionX,
        const int32 PositionY)
    {
        FThomasAnimationOperation AddNode;
        AddNode.Action = TEXT("add_state_graph_node");
        AddNode.MachineName = MachineName;
        AddNode.StateName = StateName;
        AddNode.ClassPath = ClassPath;
        AddNode.PositionX = PositionX;
        AddNode.PositionY = PositionY;
        return ApplyAndReload(Label, Before, AddNode, false);
    };
    auto RemoveR47Node = [
        &ApplyAndReload,
        &MachineName,
        &StateName](
        const FString& Label,
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& NodeGuid)
    {
        FThomasAnimationOperation RemoveNode;
        RemoveNode.Action = TEXT("remove_state_graph_node");
        RemoveNode.MachineName = MachineName;
        RemoveNode.StateName = StateName;
        RemoveNode.StateGraphNodeGuid = NodeGuid;
        return ApplyAndReload(Label, Before, RemoveNode, true);
    };

    const FThomasSpecializedAssetInspectionResult AfterR47AnimDynamicsAdd =
        AddR47Node(
            TEXT("R47 AnimDynamics node add"),
            AfterR46Cleanup,
            AnimDynamicsClass,
            -500,
            400);
    const FString R47AnimDynamicsGuid = GetDelimitedField(
        FindNodeItem(AfterR47AnimDynamicsAdd, AnimDynamicsClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    Test.TestTrue(
        TEXT("R47 AnimDynamics exposes simulation retargeting and nested runtime-curve leaves"),
        FindNodePropertyItem(
            AfterR47AnimDynamicsAdd,
            R47AnimDynamicsGuid,
            TEXT("Node.SimSpaceSettings.SimSpaceAngularAlpha"))
                .Contains(TEXT("|Type=float|"))
            && FindNodePropertyItem(
                AfterR47AnimDynamicsAdd,
                R47AnimDynamicsGuid,
                TEXT("Node.RetargetingSettings.SourceMinimum"))
                    .Contains(TEXT("|Type=float|"))
            && FindNodePropertyItem(
                AfterR47AnimDynamicsAdd,
                R47AnimDynamicsGuid,
                TEXT("Node.RetargetingSettings.CustomCurve.ExternalCurve"))
                    .Contains(
                        TEXT("|Type=TObjectPtr<UCurveFloat>|Value=None")));
    const FThomasSpecializedAssetInspectionResult AfterR47AnimSimRestore =
        RunNestedLeafCycle(
            TEXT("R47 AnimDynamics simulation-space alpha"),
            AfterR47AnimDynamicsAdd,
            R47AnimDynamicsGuid,
            TEXT("Node.SimSpaceSettings.SimSpaceAngularAlpha"),
            TEXT("float"),
            TEXT("not_a_float"),
            TEXT("0.25"),
            TEXT("0.250000"));
    const FThomasSpecializedAssetInspectionResult AfterR47RetargetRestore =
        RunNestedLeafCycle(
            TEXT("R47 AnimDynamics retarget source minimum"),
            AfterR47AnimSimRestore,
            R47AnimDynamicsGuid,
            TEXT("Node.RetargetingSettings.SourceMinimum"),
            TEXT("float"),
            TEXT("not_a_float"),
            TEXT("-15.0"),
            TEXT("-15.000000"));
    const FThomasSpecializedAssetInspectionResult AfterR47AnimDynamicsCleanup =
        RemoveR47Node(
            TEXT("R47 AnimDynamics cleanup"),
            AfterR47RetargetRestore,
            R47AnimDynamicsGuid);
    Test.TestEqual(
        TEXT("R47 AnimDynamics cleanup restores exact baseline hash"),
        GetGraphState(AfterR47AnimDynamicsCleanup),
        BaselineGraphState);

    const FThomasSpecializedAssetInspectionResult AfterR47RigidBodyAdd =
        AddR47Node(
            TEXT("R47 RigidBody node add"),
            AfterR47AnimDynamicsCleanup,
            RigidBodyClass,
            -250,
            400);
    const FString R47RigidBodyGuid = GetDelimitedField(
        FindNodeItem(AfterR47RigidBodyAdd, RigidBodyClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    Test.TestTrue(
        TEXT("R47 RigidBody exposes complete simulation-space leaves"),
        FindNodePropertyItem(
            AfterR47RigidBodyAdd,
            R47RigidBodyGuid,
            TEXT("Node.SimSpaceSettings.WorldAlpha"))
                .Contains(TEXT("|Type=float|"))
            && FindNodePropertyItem(
                AfterR47RigidBodyAdd,
                R47RigidBodyGuid,
                TEXT("Node.SimSpaceSettings.ExternalAngularVelocity"))
                    .Contains(TEXT("|Type=FVector|")));
    const FThomasSpecializedAssetInspectionResult AfterR47RigidBodyRestore =
        RunNestedLeafCycle(
            TEXT("R47 RigidBody simulation-space world alpha"),
            AfterR47RigidBodyAdd,
            R47RigidBodyGuid,
            TEXT("Node.SimSpaceSettings.WorldAlpha"),
            TEXT("float"),
            TEXT("not_a_float"),
            TEXT("0.2"),
            TEXT("0.200000"));
    const FThomasSpecializedAssetInspectionResult AfterR47RigidBodyCleanup =
        RemoveR47Node(
            TEXT("R47 RigidBody cleanup"),
            AfterR47RigidBodyRestore,
            R47RigidBodyGuid);
    Test.TestEqual(
        TEXT("R47 RigidBody cleanup restores exact baseline hash"),
        GetGraphState(AfterR47RigidBodyCleanup),
        BaselineGraphState);

    const FThomasSpecializedAssetInspectionResult AfterR47SplineIKAdd =
        AddR47Node(
            TEXT("R47 SplineIK node add"),
            AfterR47RigidBodyCleanup,
            SplineIKClass,
            0,
            400);
    const FString R47SplineIKGuid = GetDelimitedField(
        FindNodeItem(AfterR47SplineIKAdd, SplineIKClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    Test.TestTrue(
        TEXT("R47 SplineIK exposes AlphaBlend leaves"),
        FindNodePropertyItem(
            AfterR47SplineIKAdd,
            R47SplineIKGuid,
            TEXT("Node.TwistBlend.BlendTime"))
                .Contains(TEXT("|Type=float|"))
            && FindNodePropertyItem(
                AfterR47SplineIKAdd,
                R47SplineIKGuid,
                TEXT("Node.TwistBlend.CustomCurve"))
                    .Contains(
                        TEXT("|Type=TObjectPtr<UCurveFloat>|Value=None")));
    const FThomasSpecializedAssetInspectionResult AfterR47SplineIKRestore =
        RunNestedLeafCycle(
            TEXT("R47 SplineIK twist blend time"),
            AfterR47SplineIKAdd,
            R47SplineIKGuid,
            TEXT("Node.TwistBlend.BlendTime"),
            TEXT("float"),
            TEXT("not_a_float"),
            TEXT("0.4"),
            TEXT("0.400000"));
    const FThomasSpecializedAssetInspectionResult AfterR47SplineIKCleanup =
        RemoveR47Node(
            TEXT("R47 SplineIK cleanup"),
            AfterR47SplineIKRestore,
            R47SplineIKGuid);
    Test.TestEqual(
        TEXT("R47 SplineIK cleanup restores exact baseline hash"),
        GetGraphState(AfterR47SplineIKCleanup),
        BaselineGraphState);

    const FThomasSpecializedAssetInspectionResult AfterR47TrailAdd =
        AddR47Node(
            TEXT("R47 Trail node add"),
            AfterR47SplineIKCleanup,
            TrailClass,
            250,
            400);
    const FString R47TrailGuid = GetDelimitedField(
        FindNodeItem(AfterR47TrailAdd, TrailClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    Test.TestTrue(
        TEXT("R47 Trail exposes RuntimeFloatCurve external reference"),
        FindNodePropertyItem(
            AfterR47TrailAdd,
            R47TrailGuid,
            TEXT("Node.TrailRelaxationSpeed.ExternalCurve"))
                .Contains(
                    TEXT("|Type=TObjectPtr<UCurveFloat>|Value=None")));
    const FString R47CurveObjectPath = OutCurveFloatPath + TEXT(".")
        + FPackageName::GetLongPackageAssetName(OutCurveFloatPath);
    const FThomasSpecializedAssetInspectionResult AfterR47TrailRestore =
        RunNestedLeafCycle(
            TEXT("R47 Trail runtime curve reference"),
            AfterR47TrailAdd,
            R47TrailGuid,
            TEXT("Node.TrailRelaxationSpeed.ExternalCurve"),
            TEXT("TObjectPtr<UCurveFloat>"),
            OutLinkedTargetPath,
            OutCurveFloatPath,
            R47CurveObjectPath);
    const FThomasSpecializedAssetInspectionResult AfterR47TrailCleanup =
        RemoveR47Node(
            TEXT("R47 Trail cleanup"),
            AfterR47TrailRestore,
            R47TrailGuid);
    Test.TestEqual(
        TEXT("R47 Trail cleanup restores exact baseline hash"),
        GetGraphState(AfterR47TrailCleanup),
        BaselineGraphState);

    const FThomasSpecializedAssetInspectionResult AfterR47PoseDriverAdd =
        AddR47Node(
            TEXT("R47 PoseDriver node add"),
            AfterR47TrailCleanup,
            PoseDriverClass,
            500,
            400);
    const FString R47PoseDriverGuid = GetDelimitedField(
        FindNodeItem(AfterR47PoseDriverAdd, PoseDriverClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    Test.TestTrue(
        TEXT("R47 PoseDriver exposes typed RBF solver settings"),
        FindNodePropertyItem(
            AfterR47PoseDriverAdd,
            R47PoseDriverGuid,
            TEXT("Node.RBFParams.Radius"))
                .Contains(TEXT("|Type=float|"))
            && FindNodePropertyItem(
                AfterR47PoseDriverAdd,
                R47PoseDriverGuid,
                TEXT("Node.RBFParams.MedianReference"))
                    .Contains(TEXT("|Type=FVector|")));
    const FThomasSpecializedAssetInspectionResult AfterR47PoseDriverRestore =
        RunNestedLeafCycle(
            TEXT("R47 PoseDriver RBF radius"),
            AfterR47PoseDriverAdd,
            R47PoseDriverGuid,
            TEXT("Node.RBFParams.Radius"),
            TEXT("float"),
            TEXT("not_a_float"),
            TEXT("42.0"),
            TEXT("42.000000"));
    const FThomasSpecializedAssetInspectionResult AfterR47Cleanup =
        RemoveR47Node(
            TEXT("R47 PoseDriver cleanup"),
            AfterR47PoseDriverRestore,
            R47PoseDriverGuid);
    Test.TestEqual(
        TEXT("R47 combined cleanup restores the exact pre-R47 graph hash"),
        GetGraphState(AfterR47Cleanup),
        BaselineGraphState);

    auto RunR48WholeArrayCycle = [
        &Test,
        &AnimBlueprintPath,
        &MachineName,
        &StateName,
        &GetGraphState,
        &GetDelimitedField,
        &FindNodePropertyItem,
        &PlanStateGraphProperty,
        &ApplyAndReload](
        const FString& Label,
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& NodeGuid,
        const FString& PropertyPath,
        const FString& InvalidValue,
        const FString& UpdatedValue,
        const FString& ExpectedValueFragment)
    {
        const FString InitialItem = FindNodePropertyItem(
            Before, NodeGuid, PropertyPath);
        const FString InitialValue = GetDelimitedField(
            InitialItem, TEXT("|Value="), TEXT("\x1f"));
        Test.TestTrue(
            Label + TEXT(" is inspected as a bounded whole array"),
            !NodeGuid.IsEmpty()
                && InitialValue.StartsWith(TEXT("("))
                && InitialValue.EndsWith(TEXT(")"))
                && InitialValue.Len() <= 512
                && InitialItem.Contains(TEXT("|Type=TArray")));
        Test.TestEqual(
            Label + TEXT(" refuses invalid complete-array input"),
            PlanStateGraphProperty(
                Before,
                NodeGuid,
                PropertyPath,
                InvalidValue,
                true).Code,
            FString(TEXT("invalid_state_graph_node_property_value")));
        Test.TestEqual(
            Label + TEXT(" requires R2 confirmation"),
            PlanStateGraphProperty(
                Before,
                NodeGuid,
                PropertyPath,
                UpdatedValue,
                false).Code,
            FString(TEXT("confirmation_required")));
        const FThomasAnimationPlanResult UpdatePlan =
            PlanStateGraphProperty(
                Before,
                NodeGuid,
                PropertyPath,
                UpdatedValue,
                true);
        Test.TestTrue(
            Label + TEXT(" exposes an exact R2 replacement"),
            UpdatePlan.bOk && UpdatePlan.Risk == TEXT("R2"));

        FThomasAnimationOperation SetArray;
        SetArray.Action = TEXT("set_state_graph_node_property");
        SetArray.MachineName = MachineName;
        SetArray.StateName = StateName;
        SetArray.StateGraphNodeGuid = NodeGuid;
        SetArray.PropertyName = PropertyPath;
        SetArray.SettingValue = UpdatedValue;
        const FThomasSpecializedAssetInspectionResult AfterUpdate =
            ApplyAndReload(
                Label + TEXT(" replacement"),
                Before,
                SetArray,
                true);
        Test.TestTrue(
            Label + TEXT(" survives compile save and reload"),
            FindNodePropertyItem(
                AfterUpdate,
                NodeGuid,
                PropertyPath).Contains(ExpectedValueFragment)
                && GetGraphState(AfterUpdate) != GetGraphState(Before));
        Test.TestEqual(
            Label + TEXT(" refuses an unchanged replacement"),
            PlanStateGraphProperty(
                AfterUpdate,
                NodeGuid,
                PropertyPath,
                UpdatedValue,
                true).Code,
            FString(TEXT("state_graph_node_property_unchanged")));

        FThomasAnimationPatchRequest StaleRequest;
        StaleRequest.AssetPath = AnimBlueprintPath;
        StaleRequest.ExpectedRevision = AfterUpdate.Revision;
        StaleRequest.bConfirmDestructive = true;
        FThomasAnimationOperation StaleArray = SetArray;
        StaleArray.SettingValue = InitialValue;
        StaleArray.ExpectedValue = GetGraphState(Before);
        StaleRequest.Operations.Add(StaleArray);
        Test.TestEqual(
            Label + TEXT(" refuses a stale graph hash"),
            UThomasDomainsToolset::PlanAnimationPatch(
                StaleRequest).Code,
            FString(TEXT("state_graph_conflict")));

        FThomasAnimationOperation RestoreArray = SetArray;
        RestoreArray.SettingValue = InitialValue;
        const FThomasSpecializedAssetInspectionResult AfterRestore =
            ApplyAndReload(
                Label + TEXT(" restore"),
                AfterUpdate,
                RestoreArray,
                true);
        Test.TestEqual(
            Label + TEXT(" restore returns the exact graph hash"),
            GetGraphState(AfterRestore),
            GetGraphState(Before));
        return AfterRestore;
    };

    const FThomasSpecializedAssetInspectionResult AfterR48InertializationAdd =
        AddR47Node(
            TEXT("R48 Inertialization node add"),
            AfterR47Cleanup,
            InertializationClass,
            -250,
            600);
    const FString R48InertializationGuid = GetDelimitedField(
        FindNodeItem(AfterR48InertializationAdd, InertializationClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    Test.TestTrue(
        TEXT("R48 Inertialization exposes complete curve and bone filter arrays"),
        FindNodePropertyItem(
            AfterR48InertializationAdd,
            R48InertializationGuid,
            TEXT("Node.FilteredCurves"))
                .Contains(TEXT("|Type=TArray"))
            && FindNodePropertyItem(
                AfterR48InertializationAdd,
                R48InertializationGuid,
                TEXT("Node.FilteredBones"))
                    .Contains(TEXT("|Type=TArray")));
    FString R48OversizedCurveArray = TEXT("(");
    for (int32 Index = 0; Index < 17; ++Index)
    {
        if (Index > 0)
        {
            R48OversizedCurveArray += TEXT(",");
        }
        R48OversizedCurveArray += FString::Printf(
            TEXT("TE_R48_Curve%d"), Index);
    }
    R48OversizedCurveArray += TEXT(")");
    Test.TestEqual(
        TEXT("R48 whole arrays refuse more than sixteen entries"),
        PlanStateGraphProperty(
            AfterR48InertializationAdd,
            R48InertializationGuid,
            TEXT("Node.FilteredCurves"),
            R48OversizedCurveArray,
            true).Code,
        FString(TEXT("invalid_state_graph_node_property_value")));
    const FThomasSpecializedAssetInspectionResult AfterR48CurveRestore =
        RunR48WholeArrayCycle(
            TEXT("R48 inertialization curve filters"),
            AfterR48InertializationAdd,
            R48InertializationGuid,
            TEXT("Node.FilteredCurves"),
            TEXT("not_an_array"),
            TEXT("(TE_R48_CurveA,TE_R48_CurveB)"),
            TEXT("TE_R48_CurveA"));

    const FString R48SkeletonObjectPath = SkeletonContextPath + TEXT(".")
        + FPackageName::GetLongPackageAssetName(SkeletonContextPath);
    const USkeleton* R48Skeleton = LoadObject<USkeleton>(
        nullptr, *R48SkeletonObjectPath);
    const FString R48BoneName = R48Skeleton
        && R48Skeleton->GetReferenceSkeleton().GetNum() > 0
        ? R48Skeleton->GetReferenceSkeleton().GetBoneName(0).ToString()
        : FString();
    Test.TestTrue(
        TEXT("R48 whole bone arrays have an exact fixture Skeleton bone"),
        !R48BoneName.IsEmpty());
    const FString R48BoneArray = FString::Printf(
        TEXT("((BoneName=%s))"), *R48BoneName);
    const FThomasSpecializedAssetInspectionResult AfterR48BoneRestore =
        RunR48WholeArrayCycle(
            TEXT("R48 inertialization bone filters"),
            AfterR48CurveRestore,
            R48InertializationGuid,
            TEXT("Node.FilteredBones"),
            TEXT("((BoneName=TE_R48_MissingBone))"),
            R48BoneArray,
            R48BoneName);
    const FThomasSpecializedAssetInspectionResult AfterR48InertializationCleanup =
        RemoveR47Node(
            TEXT("R48 Inertialization cleanup"),
            AfterR48BoneRestore,
            R48InertializationGuid);
    Test.TestEqual(
        TEXT("R48 Inertialization cleanup restores exact baseline hash"),
        GetGraphState(AfterR48InertializationCleanup),
        BaselineGraphState);

    const FThomasSpecializedAssetInspectionResult AfterR48PoseDriverAdd =
        AddR47Node(
            TEXT("R48 PoseDriver node add"),
            AfterR48InertializationCleanup,
            PoseDriverClass,
            250,
            600);
    const FString R48PoseDriverGuid = GetDelimitedField(
        FindNodeItem(AfterR48PoseDriverAdd, PoseDriverClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    Test.TestTrue(
        TEXT("R48 PoseDriver exposes complete source and output bone arrays"),
        FindNodePropertyItem(
            AfterR48PoseDriverAdd,
            R48PoseDriverGuid,
            TEXT("Node.SourceBones"))
                .Contains(TEXT("|Type=TArray"))
            && FindNodePropertyItem(
                AfterR48PoseDriverAdd,
                R48PoseDriverGuid,
                TEXT("Node.OnlyDriveBones"))
                    .Contains(TEXT("|Type=TArray")));
    const FThomasSpecializedAssetInspectionResult AfterR48SourceBonesRestore =
        RunR48WholeArrayCycle(
            TEXT("R48 PoseDriver source bones"),
            AfterR48PoseDriverAdd,
            R48PoseDriverGuid,
            TEXT("Node.SourceBones"),
            TEXT("((BoneName=TE_R48_MissingBone))"),
            R48BoneArray,
            R48BoneName);
    const FThomasSpecializedAssetInspectionResult AfterR48OnlyDriveBonesRestore =
        RunR48WholeArrayCycle(
            TEXT("R48 PoseDriver driven bones"),
            AfterR48SourceBonesRestore,
            R48PoseDriverGuid,
            TEXT("Node.OnlyDriveBones"),
            TEXT("((BoneName=TE_R48_MissingBone))"),
            R48BoneArray,
            R48BoneName);
    const FThomasSpecializedAssetInspectionResult AfterR48Cleanup =
        RemoveR47Node(
            TEXT("R48 PoseDriver cleanup"),
            AfterR48OnlyDriveBonesRestore,
            R48PoseDriverGuid);
    Test.TestEqual(
        TEXT("R48 combined cleanup restores the exact pre-R48 graph hash"),
        GetGraphState(AfterR48Cleanup),
        BaselineGraphState);

    auto PlanR50PoseArray = [
        &AnimBlueprintPath,
        &MachineName,
        &StateName,
        &GetGraphState](
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& NodeGuid,
        const FString& PosePath,
        const int32 TargetSize,
        const TArray<float>& CompanionValues,
        const TArray<FString>& EnumEntries,
        const bool bConfirmDestructive)
    {
        FThomasAnimationOperation Operation;
        Operation.Action = TEXT("resize_state_graph_pose_array");
        Operation.MachineName = MachineName;
        Operation.StateName = StateName;
        Operation.StateGraphNodeGuid = NodeGuid;
        Operation.PropertyName = PosePath;
        Operation.StateGraphContainerSize = TargetSize;
        Operation.StateGraphCompanionValues = CompanionValues;
        Operation.StateGraphEnumEntries = EnumEntries;
        Operation.ExpectedValue = GetGraphState(Before);
        FThomasAnimationPatchRequest Request;
        Request.AssetPath = AnimBlueprintPath;
        Request.ExpectedRevision = Before.Revision;
        Request.bConfirmDestructive = bConfirmDestructive;
        Request.Operations.Add(Operation);
        return UThomasDomainsToolset::PlanAnimationPatch(Request);
    };
    auto RunR50PoseArrayCycle = [
        &Test,
        &AnimBlueprintPath,
        &MachineName,
        &StateName,
        &GetGraphState,
        &GetDelimitedField,
        &FindNodeItem,
        &FindPoseArrayItem,
        &ApplyAndReload,
        &AddR47Node,
        &RemoveR47Node,
        &PlanR50PoseArray](
        const FString& Label,
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& ClassPath,
        const FString& EnumPath,
        const FString& Kind,
        const FString& PosePath,
        const TArray<float>& NormalizedValues,
        const TArray<float>& ExpandedValues,
        const TArray<float>& OutOfRangeValues,
        const TArray<FString>& NormalizedEnumEntries,
        const TArray<FString>& ExpandedEnumEntries,
        const int32 PositionX)
    {
        FThomasSpecializedAssetInspectionResult AfterAdd;
        if (EnumPath.IsEmpty())
        {
            AfterAdd = AddR47Node(
                Label + TEXT(" node add"),
                Before,
                ClassPath,
                PositionX,
                800);
        }
        else
        {
            FThomasAnimationOperation AddNode;
            AddNode.Action = TEXT("add_state_graph_node");
            AddNode.MachineName = MachineName;
            AddNode.StateName = StateName;
            AddNode.ClassPath = ClassPath;
            AddNode.StateGraphEnumPath = EnumPath;
            AddNode.PositionX = PositionX;
            AddNode.PositionY = 800;
            AfterAdd = ApplyAndReload(
                Label + TEXT(" enum-bound node add"),
                Before,
                AddNode,
                false);
        }
        const FString NodeGuid = GetDelimitedField(
            FindNodeItem(AfterAdd, ClassPath),
            TEXT("State=Guid="),
            TEXT("|Class="));
        const FString InitialItem = FindPoseArrayItem(
            AfterAdd, NodeGuid, PosePath);
        Test.AddInfo(Label + TEXT(" initial pose array: ") + InitialItem);
        Test.TestTrue(
            Label + TEXT(" exposes an exact coupled pose-array contract"),
            !NodeGuid.IsEmpty()
                && InitialItem.Contains(TEXT("State=Kind=") + Kind + TEXT("|"))
                && InitialItem.Contains(TEXT("|PosePath=") + PosePath + TEXT("|"))
                && InitialItem.Contains(TEXT("|Min=2|Max=16")));
        Test.TestEqual(
            Label + TEXT(" refuses a non-dedicated pose path"),
            PlanR50PoseArray(
                AfterAdd,
                NodeGuid,
                TEXT("Node.MissingPoseArray"),
                2,
                NormalizedValues,
                NormalizedEnumEntries,
                true).Code,
            FString(TEXT("state_graph_pose_array_not_found")));
        Test.TestEqual(
            Label + TEXT(" refuses fewer than two poses"),
            PlanR50PoseArray(
                AfterAdd,
                NodeGuid,
                PosePath,
                1,
                {NormalizedValues[0]},
                TArray<FString>(),
                true).Code,
            FString(TEXT("invalid_state_graph_pose_array_size")));
        Test.TestEqual(
            Label + TEXT(" refuses more than sixteen poses"),
            PlanR50PoseArray(
                AfterAdd,
                NodeGuid,
                PosePath,
                17,
                TArray<float>(),
                TArray<FString>(),
                true).Code,
            FString(TEXT("invalid_state_graph_pose_array_size")));
        Test.TestEqual(
            Label + TEXT(" refuses mismatched companion values"),
            PlanR50PoseArray(
                AfterAdd,
                NodeGuid,
                PosePath,
                2,
                {NormalizedValues[0]},
                NormalizedEnumEntries,
                true).Code,
            FString(TEXT("invalid_state_graph_pose_array_values")));
        Test.TestEqual(
            Label + TEXT(" refuses out-of-range companion values"),
            PlanR50PoseArray(
                AfterAdd,
                NodeGuid,
                PosePath,
                2,
                OutOfRangeValues,
                NormalizedEnumEntries,
                true).Code,
            FString(TEXT("invalid_state_graph_pose_array_values")));
        if (!EnumPath.IsEmpty())
        {
            Test.TestEqual(
                Label + TEXT(" refuses an unknown enum entry"),
                PlanR50PoseArray(
                    AfterAdd,
                    NodeGuid,
                    PosePath,
                    2,
                    NormalizedValues,
                    {TEXT("TE_R50_MissingEnumEntry")},
                    true).Code,
                FString(TEXT("invalid_state_graph_pose_array_values")));
        }
        Test.TestEqual(
            Label + TEXT(" requires R2 confirmation"),
            PlanR50PoseArray(
                AfterAdd,
                NodeGuid,
                PosePath,
                2,
                NormalizedValues,
                NormalizedEnumEntries,
                false).Code,
            FString(TEXT("confirmation_required")));
        const FThomasAnimationPlanResult NormalizePlan =
            PlanR50PoseArray(
                AfterAdd,
                NodeGuid,
                PosePath,
                2,
                NormalizedValues,
                NormalizedEnumEntries,
                true);
        Test.TestTrue(
            Label + TEXT(" exposes an exact R2 coupled replacement"),
            NormalizePlan.bOk && NormalizePlan.Risk == TEXT("R2"));

        FThomasAnimationOperation Resize;
        Resize.Action = TEXT("resize_state_graph_pose_array");
        Resize.MachineName = MachineName;
        Resize.StateName = StateName;
        Resize.StateGraphNodeGuid = NodeGuid;
        Resize.PropertyName = PosePath;
        Resize.StateGraphContainerSize = 2;
        Resize.StateGraphCompanionValues = NormalizedValues;
        Resize.StateGraphEnumEntries = NormalizedEnumEntries;
        const FThomasSpecializedAssetInspectionResult AfterNormalize =
            ApplyAndReload(
                Label + TEXT(" normalize"),
                AfterAdd,
                Resize,
                true);
        const FString NormalizedItem = FindPoseArrayItem(
            AfterNormalize, NodeGuid, PosePath);
        Test.AddInfo(Label + TEXT(" normalized pose array: ")
            + NormalizedItem);
        Test.TestTrue(
            Label + TEXT(" normalized arrays and pins survive reload"),
            NormalizedItem.Contains(TEXT("|Num=2|CompanionNum=2|"))
                && NormalizedItem.Contains(TEXT("|PosePinCount=2|"))
                && (EnumPath.IsEmpty()
                    || (NormalizedItem.Contains(
                            TEXT("|EnumEntries=("))
                        && NormalizedEnumEntries.Num() == 1
                        && NormalizedItem.Contains(
                            NormalizedEnumEntries[0]))));

        Resize.StateGraphContainerSize = 3;
        Resize.StateGraphCompanionValues = ExpandedValues;
        Resize.StateGraphEnumEntries = ExpandedEnumEntries;
        const FThomasSpecializedAssetInspectionResult AfterExpand =
            ApplyAndReload(
                Label + TEXT(" expand"),
                AfterNormalize,
                Resize,
                true);
        const FString ExpandedItem = FindPoseArrayItem(
            AfterExpand, NodeGuid, PosePath);
        Test.AddInfo(Label + TEXT(" expanded pose array: ")
            + ExpandedItem);
        Test.TestTrue(
            Label + TEXT(" expanded arrays and pins survive reload"),
            ExpandedItem.Contains(TEXT("|Num=3|CompanionNum=3|"))
                && ExpandedItem.Contains(TEXT("|PosePinCount=3|"))
                && GetGraphState(AfterExpand)
                    != GetGraphState(AfterNormalize));
        Test.TestEqual(
            Label + TEXT(" refuses an unchanged coupled replacement"),
            PlanR50PoseArray(
                AfterExpand,
                NodeGuid,
                PosePath,
                3,
                ExpandedValues,
                ExpandedEnumEntries,
                true).Code,
            FString(TEXT("state_graph_pose_array_unchanged")));

        FThomasAnimationPatchRequest StaleRequest;
        StaleRequest.AssetPath = AnimBlueprintPath;
        StaleRequest.ExpectedRevision = AfterExpand.Revision;
        StaleRequest.bConfirmDestructive = true;
        FThomasAnimationOperation StaleResize = Resize;
        StaleResize.StateGraphContainerSize = 2;
        StaleResize.StateGraphCompanionValues = NormalizedValues;
        StaleResize.StateGraphEnumEntries = NormalizedEnumEntries;
        StaleResize.ExpectedValue = GetGraphState(AfterAdd);
        StaleRequest.Operations.Add(StaleResize);
        Test.TestEqual(
            Label + TEXT(" refuses a stale graph hash"),
            UThomasDomainsToolset::PlanAnimationPatch(
                StaleRequest).Code,
            FString(TEXT("state_graph_conflict")));

        Resize.StateGraphContainerSize = 2;
        Resize.StateGraphCompanionValues = NormalizedValues;
        Resize.StateGraphEnumEntries = NormalizedEnumEntries;
        const FThomasSpecializedAssetInspectionResult AfterRestore =
            ApplyAndReload(
                Label + TEXT(" restore"),
                AfterExpand,
                Resize,
                true);
        Test.TestEqual(
            Label + TEXT(" restore returns the normalized graph hash"),
            GetGraphState(AfterRestore),
            GetGraphState(AfterNormalize));
        const FThomasSpecializedAssetInspectionResult AfterCleanup =
            RemoveR47Node(
                Label + TEXT(" cleanup"),
                AfterRestore,
                NodeGuid);
        Test.TestEqual(
            Label + TEXT(" cleanup restores the exact baseline hash"),
            GetGraphState(AfterCleanup),
            GetGraphState(Before));
        return AfterCleanup;
    };

    const UEnum* R50NativeEnum = nullptr;
    TArray<FString> R50NativeEnumEntries;
    for (TObjectIterator<UEnum> It; It; ++It)
    {
        const FString CandidatePath = It->GetPathName();
        if (CandidatePath.StartsWith(TEXT("/Script/"))
            && It->NumEnums() >= 3
            && It->NumEnums() <= 8)
        {
            TArray<FString> CandidateEntries;
            for (int32 Index = 0; Index < It->NumEnums(); ++Index)
            {
                const FString Entry = It->GetNameStringByIndex(Index);
                if (!It->HasMetaData(TEXT("Hidden"), Index)
                    && !Entry.EndsWith(TEXT("_MAX"))
                    && !Entry.EndsWith(TEXT("::MAX")))
                {
                    CandidateEntries.Add(Entry);
                }
            }
            if (CandidateEntries.Num() >= 2)
            {
                R50NativeEnum = *It;
                R50NativeEnumEntries = {
                    CandidateEntries[0], CandidateEntries[1]};
                break;
            }
        }
    }
    Test.TestNotNull(
        TEXT("R50 BlendListByEnum has an exact reflected enum fixture"),
        R50NativeEnum && R50NativeEnumEntries.Num() == 2
            ? R50NativeEnum : nullptr);
    Test.AddInfo(TEXT("R50 BlendListByEnum enum fixture: ")
        + (R50NativeEnum ? R50NativeEnum->GetPathName() : TEXT("None")));
    FThomasAnimationOperation MissingEnumAdd;
    MissingEnumAdd.Action = TEXT("add_state_graph_node");
    MissingEnumAdd.MachineName = MachineName;
    MissingEnumAdd.StateName = StateName;
    MissingEnumAdd.ClassPath = BlendListByEnumClass;
    MissingEnumAdd.ExpectedValue = GetGraphState(AfterR48Cleanup);
    FThomasAnimationPatchRequest MissingEnumRequest;
    MissingEnumRequest.AssetPath = AnimBlueprintPath;
    MissingEnumRequest.ExpectedRevision = AfterR48Cleanup.Revision;
    MissingEnumRequest.Operations.Add(MissingEnumAdd);
    Test.TestEqual(
        TEXT("R50 BlendListByEnum creation refuses a missing enum"),
        UThomasDomainsToolset::PlanAnimationPatch(
            MissingEnumRequest).Code,
        FString(TEXT("invalid_state_graph_enum")));

    const FThomasSpecializedAssetInspectionResult AfterR50EnumCleanup =
        RunR50PoseArrayCycle(
            TEXT("R50 BlendListByEnum"),
            AfterR48Cleanup,
            BlendListByEnumClass,
            R50NativeEnum ? R50NativeEnum->GetPathName() : FString(),
            TEXT("blend_list_by_enum"),
            TEXT("Node.BlendPose"),
            {0.11f, 0.22f},
            {0.11f, 0.22f, 0.33f},
            {-0.1f, 0.2f},
            R50NativeEnumEntries.Num() == 2
                ? TArray<FString>{R50NativeEnumEntries[0]}
                : TArray<FString>(),
            R50NativeEnumEntries,
            -150);
    const FThomasSpecializedAssetInspectionResult AfterR50Cleanup =
        RunR50PoseArrayCycle(
            TEXT("R50 MultiWayBlend"),
            AfterR50EnumCleanup,
            MultiWayBlendClass,
            FString(),
            TEXT("multi_way_blend"),
            TEXT("Node.Poses"),
            {0.2f, 0.8f},
            {0.2f, 0.5f, 0.8f},
            {-0.1f, 1.1f},
            TArray<FString>(),
            TArray<FString>(),
            150);
    Test.TestEqual(
        TEXT("R50 combined cleanup restores the exact pre-R50 graph hash"),
        GetGraphState(AfterR50Cleanup),
        BaselineGraphState);

    const FString R51OverrideSetName = TEXT("TE_R51_NodeSet");
    const FThomasSpecializedAssetInspectionResult R51RetargeterBefore =
        UThomasDomainsToolset::InspectAnimationAsset(
            IKRetargeterPath,
            300);
    FThomasAnimationPatchRequest AddR51OverrideSetRequest;
    AddR51OverrideSetRequest.AssetPath = IKRetargeterPath;
    AddR51OverrideSetRequest.ExpectedRevision =
        R51RetargeterBefore.Revision;
    FThomasAnimationOperation AddR51OverrideSet;
    AddR51OverrideSet.Action = TEXT("add_ik_retarget_override_set");
    AddR51OverrideSet.Name = R51OverrideSetName;
    AddR51OverrideSetRequest.Operations.Add(AddR51OverrideSet);
    const FThomasAnimationPlanResult AddR51OverrideSetPlan =
        UThomasDomainsToolset::PlanAnimationPatch(
            AddR51OverrideSetRequest);
    Test.TestTrue(
        TEXT("R51 IK Retargeter override-set fixture plan succeeds"),
        AddR51OverrideSetPlan.bOk);
    Test.TestTrue(
        TEXT("R51 IK Retargeter override-set fixture saves"),
        AddR51OverrideSetPlan.bOk
            && UThomasDomainsToolset::ApplyAnimationPlan(
                AddR51OverrideSetPlan.PlanId,
                true).bOk);
    const FThomasSpecializedAssetInspectionResult R51RetargeterWithSet =
        UThomasDomainsToolset::InspectAnimationAsset(
            IKRetargeterPath,
            300);
    Test.TestTrue(
        TEXT("R51 IK Retargeter fixture exposes the exact override set"),
        R51RetargeterWithSet.bOk
            && R51RetargeterWithSet.Items.ContainsByPredicate(
                [&R51OverrideSetName](const FString& Item)
                {
                    return Item.StartsWith(
                            TEXT("IKRetargetOverrideSet Name=")
                                + R51OverrideSetName + TEXT(" "));
                }));

    const FThomasSpecializedAssetInspectionResult AfterR51NodeAdd =
        AddR47Node(
            TEXT("R51 IK Retarget node add"),
            AfterR50Cleanup,
            IKRetargetNodeClass,
            450,
            800);
    const FString R51NodeGuid = GetDelimitedField(
        FindNodeItem(AfterR51NodeAdd, IKRetargetNodeClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    FThomasAnimationOperation AssignR51Retargeter;
    AssignR51Retargeter.Action = TEXT("set_state_graph_node_property");
    AssignR51Retargeter.MachineName = MachineName;
    AssignR51Retargeter.StateName = StateName;
    AssignR51Retargeter.StateGraphNodeGuid = R51NodeGuid;
    AssignR51Retargeter.PropertyName = TEXT("Node.IKRetargeterAsset");
    AssignR51Retargeter.SettingValue = IKRetargeterPath;
    const FThomasSpecializedAssetInspectionResult AfterR51AssetAssign =
        ApplyAndReload(
            TEXT("R51 IK Retarget asset assignment"),
            AfterR51NodeAdd,
            AssignR51Retargeter,
            true);
    const FString R51OverrideArrayPath =
        TEXT("Node.OverrideSetsToApply");
    Test.TestTrue(
        TEXT("R51 asset-bound override-set array is inspected exactly"),
        FindNodePropertyItem(
            AfterR51AssetAssign,
            R51NodeGuid,
            R51OverrideArrayPath).Contains(
                TEXT("|Type=TArray|Value=()")));
    Test.TestEqual(
        TEXT("R51 asset-bound array refuses an unknown override set"),
        PlanStateGraphProperty(
            AfterR51AssetAssign,
            R51NodeGuid,
            R51OverrideArrayPath,
            TEXT("(TE_R51_MissingSet)"),
            true).Code,
        FString(TEXT("invalid_state_graph_node_property_value")));
    Test.TestEqual(
        TEXT("R51 asset-bound array refuses duplicate override sets"),
        PlanStateGraphProperty(
            AfterR51AssetAssign,
            R51NodeGuid,
            R51OverrideArrayPath,
            TEXT("(TE_R51_NodeSet,TE_R51_NodeSet)"),
            true).Code,
        FString(TEXT("invalid_state_graph_node_property_value")));
    const FThomasSpecializedAssetInspectionResult AfterR51ArrayRestore =
        RunR48WholeArrayCycle(
            TEXT("R51 IK Retarget override sets"),
            AfterR51AssetAssign,
            R51NodeGuid,
            R51OverrideArrayPath,
            TEXT("not_an_array"),
            TEXT("(TE_R51_NodeSet)"),
            R51OverrideSetName);
    const FThomasSpecializedAssetInspectionResult AfterR51Cleanup =
        RemoveR47Node(
            TEXT("R51 IK Retarget node cleanup"),
            AfterR51ArrayRestore,
            R51NodeGuid);
    Test.TestEqual(
        TEXT("R51 graph cleanup restores the exact pre-R51 hash"),
        GetGraphState(AfterR51Cleanup),
        BaselineGraphState);

    FThomasAnimationPatchRequest RemoveR51OverrideSetRequest;
    RemoveR51OverrideSetRequest.AssetPath = IKRetargeterPath;
    RemoveR51OverrideSetRequest.ExpectedRevision =
        R51RetargeterWithSet.Revision;
    RemoveR51OverrideSetRequest.bConfirmDestructive = true;
    FThomasAnimationOperation RemoveR51OverrideSet;
    RemoveR51OverrideSet.Action =
        TEXT("remove_ik_retarget_override_set");
    RemoveR51OverrideSet.Name = R51OverrideSetName;
    RemoveR51OverrideSetRequest.Operations.Add(RemoveR51OverrideSet);
    const FThomasAnimationPlanResult RemoveR51OverrideSetPlan =
        UThomasDomainsToolset::PlanAnimationPatch(
            RemoveR51OverrideSetRequest);
    Test.TestTrue(
        TEXT("R51 IK Retargeter override-set cleanup plan succeeds"),
        RemoveR51OverrideSetPlan.bOk);
    Test.TestTrue(
        TEXT("R51 IK Retargeter override-set cleanup saves"),
        RemoveR51OverrideSetPlan.bOk
            && UThomasDomainsToolset::ApplyAnimationPlan(
                RemoveR51OverrideSetPlan.PlanId,
                true).bOk);
    const FThomasSpecializedAssetInspectionResult R51RetargeterAfterCleanup =
        UThomasDomainsToolset::InspectAnimationAsset(
            IKRetargeterPath,
            300);
    Test.TestFalse(
        TEXT("R51 IK Retargeter override-set fixture leaves no residue"),
        R51RetargeterAfterCleanup.Items.ContainsByPredicate(
            [&R51OverrideSetName](const FString& Item)
            {
                return Item.StartsWith(
                    TEXT("IKRetargetOverrideSet Name=")
                        + R51OverrideSetName + TEXT(" "));
            }));

    auto PlanR51ConstraintArray = [
        &AnimBlueprintPath,
        &MachineName,
        &StateName,
        &GetGraphState](
        const FThomasSpecializedAssetInspectionResult& Before,
        const FString& NodeGuid,
        const FString& PropertyPath,
        const int32 TargetSize,
        const FString& ConstraintValues,
        const TArray<float>& Weights,
        const bool bConfirmDestructive)
    {
        FThomasAnimationOperation Operation;
        Operation.Action = TEXT("replace_state_graph_constraint_array");
        Operation.MachineName = MachineName;
        Operation.StateName = StateName;
        Operation.StateGraphNodeGuid = NodeGuid;
        Operation.PropertyName = PropertyPath;
        Operation.StateGraphContainerSize = TargetSize;
        Operation.SettingValue = ConstraintValues;
        Operation.StateGraphCompanionValues = Weights;
        Operation.ExpectedValue = GetGraphState(Before);
        FThomasAnimationPatchRequest Request;
        Request.AssetPath = AnimBlueprintPath;
        Request.ExpectedRevision = Before.Revision;
        Request.bConfirmDestructive = bConfirmDestructive;
        Request.Operations.Add(Operation);
        return UThomasDomainsToolset::PlanAnimationPatch(Request);
    };

    const FThomasSpecializedAssetInspectionResult AfterR51ConstraintAdd =
        AddR47Node(
            TEXT("R51 Constraint node add"),
            AfterR51Cleanup,
            ConstraintClass,
            750,
            800);
    const FString R51ConstraintNodeGuid = GetDelimitedField(
        FindNodeItem(AfterR51ConstraintAdd, ConstraintClass),
        TEXT("State=Guid="),
        TEXT("|Class="));
    const FString R51ConstraintPath = TEXT("Node.ConstraintSetup");
    const FString InitialConstraintItem = FindConstraintArrayItem(
        AfterR51ConstraintAdd,
        R51ConstraintNodeGuid,
        R51ConstraintPath);
    Test.AddInfo(TEXT("R51 initial coupled Constraint array: ")
        + InitialConstraintItem);
    Test.TestTrue(
        TEXT("R51 exposes the exact empty ConstraintSetup/ConstraintWeights contract"),
        !R51ConstraintNodeGuid.IsEmpty()
            && InitialConstraintItem.Contains(
                TEXT("State=Kind=constraint_setup_weights|"))
            && InitialConstraintItem.Contains(
                TEXT("|ConstraintPath=Node.ConstraintSetup|"))
            && InitialConstraintItem.Contains(
                TEXT("|WeightPath=Node.ConstraintWeights|"))
            && InitialConstraintItem.Contains(
                TEXT("|ConstraintValues=()|Num=0|WeightNum=0|"))
            && InitialConstraintItem.Contains(TEXT("|Min=0|Max=16")));

    const FString R51ConstraintBoneA = R48BoneName;
    const FString R51ConstraintBoneB = R48Skeleton
        && R48Skeleton->GetReferenceSkeleton().GetNum() > 1
        ? R48Skeleton->GetReferenceSkeleton().GetBoneName(1).ToString()
        : FString();
    Test.TestTrue(
        TEXT("R51 coupled Constraint array has two exact Skeleton bones"),
        !R51ConstraintBoneA.IsEmpty()
            && !R51ConstraintBoneB.IsEmpty()
            && R51ConstraintBoneA != R51ConstraintBoneB);
    const FString R51OneConstraint = FString::Printf(
        TEXT("((TargetBone=(BoneName=%s)))"),
        *R51ConstraintBoneA);
    const FString R51TwoConstraints = FString::Printf(
        TEXT("((TargetBone=(BoneName=%s)),(TargetBone=(BoneName=%s)))"),
        *R51ConstraintBoneA,
        *R51ConstraintBoneB);
    Test.TestEqual(
        TEXT("R51 coupled Constraint array refuses a non-dedicated path"),
        PlanR51ConstraintArray(
            AfterR51ConstraintAdd,
            R51ConstraintNodeGuid,
            TEXT("Node.ConstraintWeights"),
            1,
            R51OneConstraint,
            {0.65f},
            true).Code,
        FString(TEXT("state_graph_constraint_array_not_found")));
    Test.TestEqual(
        TEXT("R51 coupled Constraint array refuses more than sixteen entries"),
        PlanR51ConstraintArray(
            AfterR51ConstraintAdd,
            R51ConstraintNodeGuid,
            R51ConstraintPath,
            17,
            TEXT("()"),
            TArray<float>(),
            true).Code,
        FString(TEXT("invalid_state_graph_constraint_array_size")));
    Test.TestEqual(
        TEXT("R51 coupled Constraint array refuses mismatched weights"),
        PlanR51ConstraintArray(
            AfterR51ConstraintAdd,
            R51ConstraintNodeGuid,
            R51ConstraintPath,
            1,
            R51OneConstraint,
            TArray<float>(),
            true).Code,
        FString(TEXT("invalid_state_graph_constraint_array_values")));
    Test.TestEqual(
        TEXT("R51 coupled Constraint array refuses an unknown target bone"),
        PlanR51ConstraintArray(
            AfterR51ConstraintAdd,
            R51ConstraintNodeGuid,
            R51ConstraintPath,
            1,
            TEXT("((TargetBone=(BoneName=TE_R51_MissingBone)))"),
            {0.65f},
            true).Code,
        FString(TEXT("invalid_state_graph_constraint_array_values")));
    Test.TestEqual(
        TEXT("R51 coupled Constraint array refuses out-of-range weights"),
        PlanR51ConstraintArray(
            AfterR51ConstraintAdd,
            R51ConstraintNodeGuid,
            R51ConstraintPath,
            1,
            R51OneConstraint,
            {1.1f},
            true).Code,
        FString(TEXT("invalid_state_graph_constraint_array_values")));
    Test.TestEqual(
        TEXT("R51 coupled Constraint array requires R2 confirmation"),
        PlanR51ConstraintArray(
            AfterR51ConstraintAdd,
            R51ConstraintNodeGuid,
            R51ConstraintPath,
            1,
            R51OneConstraint,
            {0.65f},
            false).Code,
        FString(TEXT("confirmation_required")));
    const FThomasAnimationPlanResult R51ConstraintPlan =
        PlanR51ConstraintArray(
            AfterR51ConstraintAdd,
            R51ConstraintNodeGuid,
            R51ConstraintPath,
            1,
            R51OneConstraint,
            {0.65f},
            true);
    Test.TestTrue(
        TEXT("R51 coupled Constraint replacement exposes exact R2 risk"),
        R51ConstraintPlan.bOk && R51ConstraintPlan.Risk == TEXT("R2"));

    FThomasAnimationOperation ReplaceR51Constraints;
    ReplaceR51Constraints.Action =
        TEXT("replace_state_graph_constraint_array");
    ReplaceR51Constraints.MachineName = MachineName;
    ReplaceR51Constraints.StateName = StateName;
    ReplaceR51Constraints.StateGraphNodeGuid = R51ConstraintNodeGuid;
    ReplaceR51Constraints.PropertyName = R51ConstraintPath;
    ReplaceR51Constraints.StateGraphContainerSize = 1;
    ReplaceR51Constraints.SettingValue = R51OneConstraint;
    ReplaceR51Constraints.StateGraphCompanionValues = {0.65f};
    const FThomasSpecializedAssetInspectionResult AfterR51OneConstraint =
        ApplyAndReload(
            TEXT("R51 one Constraint replacement"),
            AfterR51ConstraintAdd,
            ReplaceR51Constraints,
            true);
    const FString OneConstraintItem = FindConstraintArrayItem(
        AfterR51OneConstraint,
        R51ConstraintNodeGuid,
        R51ConstraintPath);
    Test.AddInfo(TEXT("R51 one coupled Constraint after reload: ")
        + OneConstraintItem);
    Test.TestTrue(
        TEXT("R51 one Constraint and weight survive save/reload"),
        OneConstraintItem.Contains(TEXT("|Num=1|WeightNum=1|"))
            && OneConstraintItem.Contains(TEXT("|Weights=(0.650000)|"))
            && OneConstraintItem.Contains(R51ConstraintBoneA));
    Test.TestEqual(
        TEXT("R51 coupled Constraint array refuses an unchanged replacement"),
        PlanR51ConstraintArray(
            AfterR51OneConstraint,
            R51ConstraintNodeGuid,
            R51ConstraintPath,
            1,
            R51OneConstraint,
            {0.65f},
            true).Code,
        FString(TEXT("state_graph_constraint_array_unchanged")));

    ReplaceR51Constraints.StateGraphContainerSize = 2;
    ReplaceR51Constraints.SettingValue = R51TwoConstraints;
    ReplaceR51Constraints.StateGraphCompanionValues = {0.25f, 0.75f};
    const FThomasSpecializedAssetInspectionResult AfterR51TwoConstraints =
        ApplyAndReload(
            TEXT("R51 two Constraint replacement"),
            AfterR51OneConstraint,
            ReplaceR51Constraints,
            true);
    const FString TwoConstraintItem = FindConstraintArrayItem(
        AfterR51TwoConstraints,
        R51ConstraintNodeGuid,
        R51ConstraintPath);
    Test.AddInfo(TEXT("R51 two coupled Constraints after reload: ")
        + TwoConstraintItem);
    Test.TestTrue(
        TEXT("R51 two Constraints and parallel weights survive save/reload"),
        TwoConstraintItem.Contains(TEXT("|Num=2|WeightNum=2|"))
            && TwoConstraintItem.Contains(
                TEXT("|Weights=(0.250000,0.750000)|"))
            && TwoConstraintItem.Contains(R51ConstraintBoneA)
            && TwoConstraintItem.Contains(R51ConstraintBoneB)
            && GetGraphState(AfterR51TwoConstraints)
                != GetGraphState(AfterR51OneConstraint));

    FThomasAnimationPatchRequest R51StaleConstraintRequest;
    R51StaleConstraintRequest.AssetPath = AnimBlueprintPath;
    R51StaleConstraintRequest.ExpectedRevision =
        AfterR51TwoConstraints.Revision;
    R51StaleConstraintRequest.bConfirmDestructive = true;
    FThomasAnimationOperation R51StaleConstraint =
        ReplaceR51Constraints;
    R51StaleConstraint.StateGraphContainerSize = 0;
    R51StaleConstraint.SettingValue = TEXT("()");
    R51StaleConstraint.StateGraphCompanionValues.Reset();
    R51StaleConstraint.ExpectedValue =
        GetGraphState(AfterR51ConstraintAdd);
    R51StaleConstraintRequest.Operations.Add(R51StaleConstraint);
    Test.TestEqual(
        TEXT("R51 coupled Constraint array refuses a stale graph hash"),
        UThomasDomainsToolset::PlanAnimationPatch(
            R51StaleConstraintRequest).Code,
        FString(TEXT("state_graph_conflict")));

    ReplaceR51Constraints.StateGraphContainerSize = 0;
    ReplaceR51Constraints.SettingValue = TEXT("()");
    ReplaceR51Constraints.StateGraphCompanionValues.Reset();
    const FThomasSpecializedAssetInspectionResult AfterR51ConstraintRestore =
        ApplyAndReload(
            TEXT("R51 empty Constraint restore"),
            AfterR51TwoConstraints,
            ReplaceR51Constraints,
            true);
    Test.TestEqual(
        TEXT("R51 coupled Constraint restore returns the node-add hash"),
        GetGraphState(AfterR51ConstraintRestore),
        GetGraphState(AfterR51ConstraintAdd));
    const FThomasSpecializedAssetInspectionResult AfterR51CombinedCleanup =
        RemoveR47Node(
            TEXT("R51 Constraint node cleanup"),
            AfterR51ConstraintRestore,
            R51ConstraintNodeGuid);
    Test.TestEqual(
        TEXT("R51 combined cleanup restores the exact pre-R51 graph hash"),
        GetGraphState(AfterR51CombinedCleanup),
        BaselineGraphState);
    return AfterR51CombinedCleanup;
}

static USkeleton* CreateR45TemporarySkeletonFixture(
    FThomasEditorNativeCoreTest& Test,
    const USkeleton* SourceSkeleton,
    const FString& AssetPath)
{
    UPackage* Package = CreatePackage(*AssetPath);
    const FString AssetName =
        FPackageName::GetLongPackageAssetName(AssetPath);
    USkeleton* Skeleton = SourceSkeleton && Package
        ? Cast<USkeleton>(StaticDuplicateObject(
            SourceSkeleton,
            Package,
            FName(*AssetName),
            RF_AllFlags))
        : nullptr;
    if (Skeleton)
    {
        Skeleton->SetFlags(
            RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Skeleton);
        Package->MarkPackageDirty();
    }
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    SaveArgs.Error = GError;
    const FString Filename =
        FPackageName::LongPackageNameToFilename(
            AssetPath,
            FPackageName::GetAssetPackageExtension());
    Test.TestTrue(
        TEXT("R45 temporary Skeleton duplicates and saves through Unreal"),
        Skeleton
            && UPackage::SavePackage(
                Package, Skeleton, *Filename, SaveArgs));
    return Skeleton;
}

static void CleanupR45TemporaryAnimationFixtures(
    FThomasEditorNativeCoreTest& Test,
    const FString& PhysicsControlAssetPath,
    const FString& SkeletonPath)
{
    const TArray<FString> Paths = {
        PhysicsControlAssetPath,
        SkeletonPath
    };
    TArray<UObject*> Assets;
    for (const FString& Path : Paths)
    {
        if (UObject* Asset = LoadObject<UObject>(
            nullptr,
            *(Path + TEXT(".")
                + FPackageName::GetLongPackageAssetName(Path))))
        {
            Assets.Add(Asset);
        }
    }
    Test.TestEqual(
        TEXT("R45 temporary Animation assets loaded for cleanup"),
        Assets.Num(),
        Paths.Num());
    Test.TestEqual(
        TEXT("R45 temporary Animation assets deleted through Unreal API"),
        ObjectTools::DeleteObjectsUnchecked(Assets),
        Assets.Num());
    for (const FString& Path : Paths)
    {
        Test.TestFalse(
            TEXT("R45 temporary Animation asset removed from disk: ")
                + Path,
            IFileManager::Get().FileExists(
                *FPackageName::LongPackageNameToFilename(
                    Path,
                    FPackageName::GetAssetPackageExtension())));
    }
}

#if PLATFORM_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4883)
#endif
bool FThomasEditorNativeCoreTest::RunTest(const FString& Parameters)
{
    TestTrue(
        TEXT("Discovery toolset is registered"),
        UToolsetRegistry::IsToolsetClassRegistered(UThomasDiscoveryToolset::StaticClass()));
    TestTrue(
        TEXT("Assets toolset is registered"),
        UToolsetRegistry::IsToolsetClassRegistered(UThomasAssetsToolset::StaticClass()));
    TestTrue(
        TEXT("Blueprint toolset is registered"),
        UToolsetRegistry::IsToolsetClassRegistered(UThomasBlueprintToolset::StaticClass()));
    TestTrue(
        TEXT("Compatibility toolset is registered"),
        UToolsetRegistry::IsToolsetClassRegistered(UThomasCompatibilityToolset::StaticClass()));
    TestTrue(
        TEXT("Project toolset is registered"),
        UToolsetRegistry::IsToolsetClassRegistered(UThomasProjectToolset::StaticClass()));
    TestTrue(
        TEXT("World toolset is registered"),
        UToolsetRegistry::IsToolsetClassRegistered(UThomasWorldToolset::StaticClass()));
    TestTrue(
        TEXT("Render toolset is registered"),
        UToolsetRegistry::IsToolsetClassRegistered(UThomasRenderToolset::StaticClass()));
    TestTrue(
        TEXT("Specialized domains toolset is registered"),
        UToolsetRegistry::IsToolsetClassRegistered(UThomasDomainsToolset::StaticClass()));
    TestTrue(
        TEXT("Validation toolset is registered"),
        UToolsetRegistry::IsToolsetClassRegistered(UThomasValidationToolset::StaticClass()));
    TestTrue(
        TEXT("Materials toolset is registered"),
        UToolsetRegistry::IsToolsetClassRegistered(UThomasMaterialsToolset::StaticClass()));

    const FThomasCoreStatus Before = IThomasEditorCoreModule::Get().GetStatus();
    TestFalse(
        TEXT("Assets provider was not loaded when the Core started"),
        Before.bAssetsProviderLoadedAtCoreStartup);
    for (const FThomasProviderStatus& Provider : Before.Providers)
    {
        TestTrue(
            *FString::Printf(TEXT("Provider module %s is available"), *Provider.ModuleName),
            Provider.bAvailable);
        TestFalse(
            *FString::Printf(TEXT("Provider module %s is lazy"), *Provider.ModuleName),
            Provider.bLoaded);
    }

    const FString DiscoverySchema =
        UToolsetRegistry::GetToolsetJsonSchema(UThomasDiscoveryToolset::StaticClass());
    const FString AssetsSchema =
        UToolsetRegistry::GetToolsetJsonSchema(UThomasAssetsToolset::StaticClass());
    const FString BlueprintSchema =
        UToolsetRegistry::GetToolsetJsonSchema(UThomasBlueprintToolset::StaticClass());
    TestTrue(TEXT("Discovery schema exposes GetStatus"), DiscoverySchema.Contains(TEXT("GetStatus")));
    TestTrue(TEXT("Assets schema exposes inventory"), AssetsSchema.Contains(TEXT("GetAssetInventory")));
    TestTrue(TEXT("Assets schema exposes Details planning"), AssetsSchema.Contains(TEXT("PlanObjectPatch")));
    TestTrue(TEXT("Assets schema exposes Details apply"), AssetsSchema.Contains(TEXT("ApplyObjectPatch")));
    TestTrue(TEXT("Assets schema exposes explicit retryable save"), AssetsSchema.Contains(TEXT("SaveAssets")));
    TestTrue(TEXT("Blueprint schema exposes inspection"), BlueprintSchema.Contains(TEXT("InspectBlueprint")));
    TestTrue(TEXT("Blueprint schema exposes planning"), BlueprintSchema.Contains(TEXT("PlanBlueprintBatch")));
    TestTrue(TEXT("Blueprint schema exposes apply"), BlueprintSchema.Contains(TEXT("ApplyBlueprintPlan")));
    TestTrue(TEXT("Blueprint schema exposes Actor Components"), BlueprintSchema.Contains(TEXT("add_component")));
    TestTrue(TEXT("Blueprint schema exposes interfaces"), BlueprintSchema.Contains(TEXT("add/remove_interface")));
    TestTrue(TEXT("Blueprint schema exposes advanced variable containers"), BlueprintSchema.Contains(TEXT("ContainerType")));
    TestTrue(TEXT("Blueprint schema exposes Utility/Interface kinds"), BlueprintSchema.Contains(TEXT("AssetKind")));

    const FString ProjectSchema =
        UToolsetRegistry::GetToolsetJsonSchema(UThomasProjectToolset::StaticClass());
    const FString WorldSchema =
        UToolsetRegistry::GetToolsetJsonSchema(UThomasWorldToolset::StaticClass());
    const FString RenderSchema =
        UToolsetRegistry::GetToolsetJsonSchema(UThomasRenderToolset::StaticClass());
    const FString DomainsSchema =
        UToolsetRegistry::GetToolsetJsonSchema(UThomasDomainsToolset::StaticClass());
    const FString ValidationSchema =
        UToolsetRegistry::GetToolsetJsonSchema(UThomasValidationToolset::StaticClass());
    const FString MaterialsSchema =
        UToolsetRegistry::GetToolsetJsonSchema(UThomasMaterialsToolset::StaticClass());
    TestTrue(TEXT("Project schema exposes plugin Details"), ProjectSchema.Contains(TEXT("GetPluginDetails")));
    TestTrue(TEXT("World schema exposes level inspection"), WorldSchema.Contains(TEXT("InspectCurrentLevel")));
    TestTrue(TEXT("World schema exposes modular environment inspection"), WorldSchema.Contains(TEXT("InspectEnvironment")));
    TestTrue(TEXT("World schema exposes bounded Landscape region inspection"), WorldSchema.Contains(TEXT("InspectLandscapeRegion")));
    TestTrue(TEXT("World schema exposes typed Landscape LayerInfo creation"), WorldSchema.Contains(TEXT("PlanLandscapeLayerInfoCreate")));
    TestTrue(TEXT("World schema exposes typed Landscape data operations"), WorldSchema.Contains(TEXT("LandscapeValues")));
    TestTrue(TEXT("World schema exposes guarded actor planning"), WorldSchema.Contains(TEXT("PlanWorldPatch")));
    TestTrue(TEXT("World schema exposes guarded renderer/Lumen mutation"), WorldSchema.Contains(TEXT("set_renderer_property")));
    TestTrue(TEXT("World schema exposes Post Process inspection"), WorldSchema.Contains(TEXT("InspectPostProcessVolume")));
    TestTrue(TEXT("World schema exposes Post Process planning"), WorldSchema.Contains(TEXT("PlanPostProcessPatch")));
    TestTrue(TEXT("World schema exposes Post Process apply"), WorldSchema.Contains(TEXT("ApplyPostProcessPlan")));
    TestTrue(TEXT("World schema exposes component Details inspection"), WorldSchema.Contains(TEXT("InspectActorComponent")));
    TestTrue(TEXT("World schema exposes component property mutation"), WorldSchema.Contains(TEXT("set_component_property")));
    TestTrue(TEXT("World schema exposes native map lifecycle planning"), WorldSchema.Contains(TEXT("PlanMapLifecycle")));
    TestTrue(TEXT("World schema exposes native map lifecycle apply"), WorldSchema.Contains(TEXT("ApplyMapLifecyclePlan")));
    TestTrue(TEXT("World schema exposes environment builder planning"), WorldSchema.Contains(TEXT("PlanEnvironmentBuild")));
    TestTrue(TEXT("World schema exposes async environment builder start"), WorldSchema.Contains(TEXT("StartEnvironmentBuildJob")));
    TestTrue(TEXT("World schema exposes environment builder status"), WorldSchema.Contains(TEXT("GetEnvironmentBuildJob")));
    TestTrue(TEXT("World schema exposes environment builder cancellation"), WorldSchema.Contains(TEXT("CancelEnvironmentBuildJob")));
    TestTrue(TEXT("Render schema exposes Nanite planning"), RenderSchema.Contains(TEXT("PlanNanite")));
    TestTrue(TEXT("Domains schema exposes MetaSound inventory"), DomainsSchema.Contains(TEXT("InspectAudioAssets")));
    TestTrue(TEXT("Domains schema exposes typed Animation inspection"), DomainsSchema.Contains(TEXT("InspectAnimationAsset")));
    TestTrue(TEXT("Domains schema exposes native Animation creation planning"), DomainsSchema.Contains(TEXT("PlanAnimationAssetCreate")));
    TestTrue(TEXT("Domains schema exposes native Animation creation apply"), DomainsSchema.Contains(TEXT("ApplyAnimationAssetCreate")));
    TestTrue(TEXT("Domains schema exposes guarded Animation planning"), DomainsSchema.Contains(TEXT("PlanAnimationPatch")));
    TestTrue(TEXT("Domains schema exposes guarded Animation apply"), DomainsSchema.Contains(TEXT("ApplyAnimationPlan")));
    TestTrue(TEXT("Domains schema exposes typed Niagara inspection"), DomainsSchema.Contains(TEXT("InspectFxAsset")));
    TestTrue(TEXT("Domains schema exposes native Niagara creation planning"), DomainsSchema.Contains(TEXT("PlanFxAssetCreate")));
    TestTrue(TEXT("Domains schema exposes native Niagara creation apply"), DomainsSchema.Contains(TEXT("ApplyFxAssetCreate")));
    TestTrue(TEXT("Domains schema exposes Niagara module discovery"), DomainsSchema.Contains(TEXT("SearchNiagaraModules")));
    TestTrue(TEXT("Domains schema exposes guarded Niagara graph planning"), DomainsSchema.Contains(TEXT("PlanNiagaraPatch")));
    TestTrue(TEXT("Domains schema exposes guarded Niagara graph apply"), DomainsSchema.Contains(TEXT("ApplyNiagaraPlan")));
    TestTrue(TEXT("Domains schema exposes typed Audio/MetaSound inspection"), DomainsSchema.Contains(TEXT("InspectAudioAsset")));
    TestTrue(TEXT("Domains schema exposes MetaSound node discovery"), DomainsSchema.Contains(TEXT("SearchMetaSoundNodeClasses")));
    TestTrue(TEXT("Domains schema exposes guarded MetaSound graph planning"), DomainsSchema.Contains(TEXT("PlanMetaSoundPatch")));
    TestTrue(TEXT("Domains schema exposes guarded MetaSound graph apply"), DomainsSchema.Contains(TEXT("ApplyMetaSoundPlan")));
    TestTrue(TEXT("Domains schema exposes Enhanced Input inspection"), DomainsSchema.Contains(TEXT("InspectEnhancedInputAsset")));
    TestTrue(TEXT("Domains schema exposes Enhanced Input planning"), DomainsSchema.Contains(TEXT("PlanEnhancedInputPatch")));
    TestTrue(TEXT("Domains schema exposes Enhanced Input apply"), DomainsSchema.Contains(TEXT("ApplyEnhancedInputPlan")));
    TestTrue(TEXT("Domains schema exposes Data Asset/Data Table inspection"), DomainsSchema.Contains(TEXT("InspectDataAsset")));
    TestTrue(TEXT("Domains schema exposes typed AI inspection"), DomainsSchema.Contains(TEXT("InspectAIAsset")));
    TestTrue(TEXT("Domains schema exposes Data/AI planning"), DomainsSchema.Contains(TEXT("PlanDataAIPatch")));
    TestTrue(TEXT("Domains schema exposes Data/AI apply"), DomainsSchema.Contains(TEXT("ApplyDataAIPlan")));
    TestTrue(TEXT("Domains schema exposes native audio asset creation planning"), DomainsSchema.Contains(TEXT("PlanAudioAssetCreate")));
    TestTrue(TEXT("Domains schema exposes native audio asset creation apply"), DomainsSchema.Contains(TEXT("ApplyAudioAssetCreate")));
    TestTrue(TEXT("Domains schema exposes Cinematics inventory"), DomainsSchema.Contains(TEXT("InspectCinematicAssets")));
    TestTrue(TEXT("Domains schema exposes Level Sequence inspection"), DomainsSchema.Contains(TEXT("InspectLevelSequence")));
    TestTrue(TEXT("Domains schema exposes Cinematics planning"), DomainsSchema.Contains(TEXT("PlanCinematicPatch")));
    TestTrue(TEXT("Domains schema exposes Cinematics apply"), DomainsSchema.Contains(TEXT("ApplyCinematicPlan")));
    TestTrue(TEXT("Domains schema exposes Paper2D inventory"), DomainsSchema.Contains(TEXT("InspectPaper2DAssets")));
    TestTrue(TEXT("Domains schema exposes typed Paper2D inspection"), DomainsSchema.Contains(TEXT("InspectPaper2DAsset")));
    TestTrue(TEXT("Domains schema exposes Paper2D planning"), DomainsSchema.Contains(TEXT("PlanPaper2DPatch")));
    TestTrue(TEXT("Domains schema exposes Paper2D apply"), DomainsSchema.Contains(TEXT("ApplyPaper2DPlan")));
    TestTrue(TEXT("Domains schema exposes PCG inventory"), DomainsSchema.Contains(TEXT("InspectPCGAssets")));
    TestTrue(TEXT("Domains schema exposes typed PCG graph inspection"), DomainsSchema.Contains(TEXT("InspectPCGGraph")));
    TestTrue(TEXT("Domains schema exposes native PCG creation planning"), DomainsSchema.Contains(TEXT("PlanPCGAssetCreate")));
    TestTrue(TEXT("Domains schema exposes native PCG creation apply"), DomainsSchema.Contains(TEXT("ApplyPCGAssetCreate")));
    TestTrue(TEXT("Domains schema exposes PCG settings discovery"), DomainsSchema.Contains(TEXT("SearchPCGSettingsClasses")));
    TestTrue(TEXT("Domains schema exposes guarded PCG planning"), DomainsSchema.Contains(TEXT("PlanPCGPatch")));
    TestTrue(TEXT("Domains schema exposes guarded PCG apply"), DomainsSchema.Contains(TEXT("ApplyPCGPlan")));
    TestTrue(TEXT("Domains schema exposes guarded PCG execution planning"), DomainsSchema.Contains(TEXT("PlanPCGExecution")));
    TestTrue(TEXT("Domains schema exposes async PCG execution start"), DomainsSchema.Contains(TEXT("StartPCGExecution")));
    TestTrue(TEXT("Domains schema exposes PCG execution status"), DomainsSchema.Contains(TEXT("GetPCGExecutionStatus")));
    TestTrue(TEXT("Domains schema exposes PCG execution cancellation"), DomainsSchema.Contains(TEXT("CancelPCGExecution")));
    TestTrue(TEXT("Validation schema exposes asset validation"), ValidationSchema.Contains(TEXT("ValidateAssets")));
    TestTrue(TEXT("Validation schema exposes Map Check"), ValidationSchema.Contains(TEXT("RunCurrentMapCheck")));
    TestTrue(TEXT("Materials schema exposes graph planning"), MaterialsSchema.Contains(TEXT("PlanMaterialPatch")));

    const FString CompatibilitySchema =
        UToolsetRegistry::GetToolsetJsonSchema(UThomasCompatibilityToolset::StaticClass());
    TestTrue(TEXT("Compatibility schema exposes editor status"), CompatibilitySchema.Contains(TEXT("EditorStatus")));
    TestTrue(TEXT("Compatibility schema exposes Blueprint summary"), CompatibilitySchema.Contains(TEXT("BlueprintSummary")));
    TestTrue(TEXT("Compatibility schema exposes Blueprint patch"), CompatibilitySchema.Contains(TEXT("BlueprintPatch")));
    TestTrue(TEXT("Compatibility schema exposes recent messages"), CompatibilitySchema.Contains(TEXT("RecentMessages")));
    TestTrue(TEXT("Compatibility schema exposes Data Asset summary"), CompatibilitySchema.Contains(TEXT("DataAssetSummary")));
    TestTrue(TEXT("Compatibility schema exposes Data Asset patch"), CompatibilitySchema.Contains(TEXT("DataAssetPatch")));
    TestTrue(TEXT("Compatibility schema exposes level audit"), CompatibilitySchema.Contains(TEXT("LevelAudit")));

    const FString StatusJson = UThomasCompatibilityToolset::EditorStatus();
    TestTrue(TEXT("Compatibility status succeeds"), StatusJson.Contains(TEXT("\"ok\":true")));
    TestTrue(TEXT("Compatibility status targets PropHunt"), StatusJson.Contains(TEXT("PropHunt")));

    const FString BlueprintSummaryJson = UThomasCompatibilityToolset::BlueprintSummary(
        TEXT("/Game/PropHunt/Core/BP_PH_PlayerState"), {}, false);
    TestTrue(
        TEXT("Compatibility Blueprint summary succeeds"),
        BlueprintSummaryJson.Contains(TEXT("\"ok\":true")));
    TestTrue(
        TEXT("Compatibility Blueprint summary refuses non-project assets"),
        UThomasCompatibilityToolset::BlueprintSummary(
            TEXT("/Engine/EngineMaterials/DefaultMaterial"), {}, false)
            .Contains(TEXT("\"code\":\"path_denied\"")));

    const FString BlueprintNoOpJson = UThomasCompatibilityToolset::BlueprintPatch(
        TEXT("/Game/PropHunt/Core/BP_PH_PlayerState"), TEXT(""), {}, false, false);
    TestTrue(
        TEXT("Compatibility Blueprint no-op is accepted"),
        BlueprintNoOpJson.Contains(TEXT("\"ok\":true")));
    TestTrue(
        TEXT("Compatibility Blueprint patch refuses non-project assets"),
        UThomasCompatibilityToolset::BlueprintPatch(
            TEXT("/Engine/EngineMaterials/DefaultMaterial"),
            TEXT(""), {}, false, false)
            .Contains(TEXT("\"code\":\"path_denied\"")));

    FThomasPCGPatchRequest OutsidePCGRequest;
    OutsidePCGRequest.AssetPath = TEXT("/Engine/Transient/PCG_OutsideProject");
    TestEqual(
        TEXT("PCG patch implementation refuses a path outside PropHunt before asset lookup"),
        UThomasDomainsToolset::PlanPCGPatch(OutsidePCGRequest).Code,
        FString(TEXT("asset_path_not_allowed")));

    const FString DataAssetSummaryJson = UThomasCompatibilityToolset::DataAssetSummary(
        TEXT("/Game/PropHunt/Data/DA_PH_MatchRules_Default"), {TEXT("ObjectiveScore")});
    TestTrue(
        TEXT("Compatibility Data Asset summary succeeds"),
        DataAssetSummaryJson.Contains(TEXT("\"ok\":true")));
    TestTrue(
        TEXT("Compatibility Data Asset summary contains the requested property"),
        DataAssetSummaryJson.Contains(TEXT("ObjectiveScore")));

    IThomasEditorLegacyProviderModule* LegacyProvider =
        FModuleManager::GetModulePtr<IThomasEditorLegacyProviderModule>(TEXT("ThomasEditor"));
    TestNotNull(TEXT("Compatibility provider loaded on first compatibility call"), LegacyProvider);
#if WITH_DEV_AUTOMATION_TESTS
    if (LegacyProvider)
    {
        TestTrue(
            TEXT("Legacy mutation rollback and save gates pass through the lazy provider"),
            LegacyProvider->RunLegacyMutationGateAutomation());
    }
#endif

    // Recover only ThomasEditor automation assets left by an interrupted/crashed prior run.
    {
        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<FAssetData> StaleTestAssets;
        Registry.GetAssetsByPath(
            FName(TEXT("/Game/PropHunt/Tests/ThomasEditor")),
            StaleTestAssets,
            false,
            false);
        TArray<UObject*> StaleObjects;
        for (const FAssetData& AssetData : StaleTestAssets)
        {
            const FString AssetName = AssetData.AssetName.ToString();
            if (AssetName.StartsWith(TEXT("WBP_TE_Native_"))
                || AssetName.StartsWith(TEXT("BP_TE_Native_"))
                || AssetName.StartsWith(TEXT("BPI_TE_Native_"))
                || AssetName.StartsWith(TEXT("M_TE_Native_"))
                || AssetName.StartsWith(TEXT("IA_TE_Native_"))
                || AssetName.StartsWith(TEXT("IMC_TE_Native_"))
                || AssetName.StartsWith(TEXT("DA_TE_Native_"))
                || AssetName.StartsWith(TEXT("DT_TE_Native_"))
                || AssetName.StartsWith(TEXT("BB_TE_Native_"))
                || AssetName.StartsWith(TEXT("BT_TE_Native_"))
                || AssetName.StartsWith(TEXT("SA_TE_Native_"))
                || AssetName.StartsWith(TEXT("SC_TE_Native_"))
                || AssetName.StartsWith(TEXT("MS_TE_Native_"))
                || AssetName.StartsWith(TEXT("NS_TE_Native_"))
                || AssetName.StartsWith(TEXT("AM_TE_Native_"))
                || AssetName.StartsWith(TEXT("AS_TE_Native_"))
                || AssetName.StartsWith(TEXT("ASA_TE_Native_"))
                || AssetName.StartsWith(TEXT("BS_TE_Native_"))
                || AssetName.StartsWith(TEXT("ABP_TE_StateMachine_"))
                || AssetName.StartsWith(TEXT("LS_TE_Native_"))
                || AssetName.StartsWith(TEXT("SPR_TE_Native_"))
                || AssetName.StartsWith(TEXT("FB_TE_Native_"))
                || AssetName.StartsWith(TEXT("PCG_TE_Native_"))
                || AssetName.StartsWith(TEXT("PCGI_TE_Native_"))
                || AssetName.StartsWith(TEXT("SM_TE_Nanite_")))
            {
                if (UObject* Asset = AssetData.GetAsset())
                {
                    StaleObjects.Add(Asset);
                }
            }
        }
        if (!StaleObjects.IsEmpty())
        {
            TestEqual(
                TEXT("Interrupted ThomasEditor test assets cleaned through Unreal API"),
                ObjectTools::DeleteObjectsUnchecked(StaleObjects),
                StaleObjects.Num());
        }
    }

    const FString WidgetTestSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString WidgetTestPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/WBP_TE_Native_") + WidgetTestSuffix;
    FThomasBlueprintBatchRequest WidgetRequest;
    WidgetRequest.AssetPath = WidgetTestPath;
    WidgetRequest.ExpectedRevision = TEXT("missing");
    WidgetRequest.ParentClassPath = TEXT("/Script/UMG.UserWidget");
    WidgetRequest.bCreateIfMissing = true;
    WidgetRequest.bWidgetBlueprint = true;

    FThomasBlueprintOperation VariableOperation;
    VariableOperation.Action = TEXT("add_variable");
    VariableOperation.Name = TEXT("TestValue");
    VariableOperation.Type = TEXT("int");
    VariableOperation.Value = TEXT("7");
    VariableOperation.bInstanceEditable = true;
    WidgetRequest.Operations.Add(VariableOperation);

    FThomasBlueprintOperation BoundEnabledVariable;
    BoundEnabledVariable.Action = TEXT("add_variable");
    BoundEnabledVariable.Name = TEXT("IsButtonEnabled");
    BoundEnabledVariable.Type = TEXT("bool");
    BoundEnabledVariable.Value = TEXT("true");
    WidgetRequest.Operations.Add(BoundEnabledVariable);

    FThomasBlueprintOperation FunctionGraphOperation;
    FunctionGraphOperation.Action = TEXT("add_function_graph");
    FunctionGraphOperation.Name = TEXT("ThomasGeneratedFunction");
    WidgetRequest.Operations.Add(FunctionGraphOperation);

    FThomasBlueprintOperation EventOperation;
    EventOperation.Action = TEXT("add_custom_event");
    EventOperation.Name = TEXT("ThomasGeneratedEvent");
    EventOperation.GraphName = TEXT("EventGraph");
    EventOperation.Handle = TEXT("generated_event");
    EventOperation.PositionX = 0;
    EventOperation.PositionY = 0;
    WidgetRequest.Operations.Add(EventOperation);

    FThomasBlueprintOperation BranchOperation;
    BranchOperation.Action = TEXT("add_branch");
    BranchOperation.GraphName = TEXT("EventGraph");
    BranchOperation.Handle = TEXT("generated_branch");
    BranchOperation.PositionX = 260;
    BranchOperation.PositionY = 0;
    WidgetRequest.Operations.Add(BranchOperation);

    FThomasBlueprintOperation SetOperation;
    SetOperation.Action = TEXT("add_variable_set");
    SetOperation.Name = TEXT("TestValue");
    SetOperation.GraphName = TEXT("EventGraph");
    SetOperation.Handle = TEXT("generated_set");
    SetOperation.PositionX = 520;
    SetOperation.PositionY = 0;
    WidgetRequest.Operations.Add(SetOperation);

    FThomasBlueprintOperation BranchDefaultOperation;
    BranchDefaultOperation.Action = TEXT("set_pin_default");
    BranchDefaultOperation.Handle = TEXT("generated_branch");
    BranchDefaultOperation.PinName = TEXT("Condition");
    BranchDefaultOperation.PinDirection = TEXT("input");
    BranchDefaultOperation.Value = TEXT("true");
    WidgetRequest.Operations.Add(BranchDefaultOperation);

    FThomasBlueprintOperation VariableDefaultOperation;
    VariableDefaultOperation.Action = TEXT("set_pin_default");
    VariableDefaultOperation.Handle = TEXT("generated_set");
    VariableDefaultOperation.PinName = TEXT("TestValue");
    VariableDefaultOperation.PinDirection = TEXT("input");
    VariableDefaultOperation.Value = TEXT("42");
    WidgetRequest.Operations.Add(VariableDefaultOperation);

    FThomasBlueprintOperation ConnectEventOperation;
    ConnectEventOperation.Action = TEXT("connect_pins");
    ConnectEventOperation.Handle = TEXT("generated_event");
    ConnectEventOperation.PinName = TEXT("then");
    ConnectEventOperation.PinDirection = TEXT("output");
    ConnectEventOperation.OtherHandle = TEXT("generated_branch");
    ConnectEventOperation.OtherPinName = TEXT("execute");
    ConnectEventOperation.OtherPinDirection = TEXT("input");
    WidgetRequest.Operations.Add(ConnectEventOperation);

    FThomasBlueprintOperation ConnectBranchOperation;
    ConnectBranchOperation.Action = TEXT("connect_pins");
    ConnectBranchOperation.Handle = TEXT("generated_branch");
    ConnectBranchOperation.PinName = TEXT("then");
    ConnectBranchOperation.PinDirection = TEXT("output");
    ConnectBranchOperation.OtherHandle = TEXT("generated_set");
    ConnectBranchOperation.OtherPinName = TEXT("execute");
    ConnectBranchOperation.OtherPinDirection = TEXT("input");
    WidgetRequest.Operations.Add(ConnectBranchOperation);

    FThomasWidgetElementSpec RootWidget;
    RootWidget.Name = TEXT("TestRoot");
    RootWidget.ClassPath = TEXT("/Script/UMG.CanvasPanel");
    WidgetRequest.WidgetElements.Add(RootWidget);

    FThomasWidgetElementSpec LabelWidget;
    LabelWidget.Name = TEXT("TestLabel");
    LabelWidget.ClassPath = TEXT("/Script/UMG.TextBlock");
    LabelWidget.ParentName = TEXT("TestRoot");
    LabelWidget.Text = TEXT("ThomasEditor native Widget proof");
    LabelWidget.Color = TEXT("0.2,0.8,1.0,1.0");
    LabelWidget.PositionX = 20.0f;
    LabelWidget.PositionY = 20.0f;
    LabelWidget.SizeX = 360.0f;
    LabelWidget.SizeY = 40.0f;
    WidgetRequest.WidgetElements.Add(LabelWidget);

    FThomasWidgetElementSpec ButtonWidget;
    ButtonWidget.Name = TEXT("TestButton");
    ButtonWidget.ClassPath = TEXT("/Script/UMG.Button");
    ButtonWidget.ParentName = TEXT("TestRoot");
    ButtonWidget.bVariable = true;
    ButtonWidget.PositionX = 20.0f;
    ButtonWidget.PositionY = 80.0f;
    ButtonWidget.SizeX = 180.0f;
    ButtonWidget.SizeY = 48.0f;
    WidgetRequest.WidgetElements.Add(ButtonWidget);

    const FThomasBlueprintPlanResult WidgetPlan =
        UThomasBlueprintToolset::PlanBlueprintBatch(WidgetRequest);
    TestTrue(TEXT("Widget Blueprint create plan succeeds"), WidgetPlan.bOk);
    const FThomasBlueprintApplyResult WidgetApply =
        UThomasBlueprintToolset::ApplyBlueprintPlan(WidgetPlan.PlanId, true, true);
    TestTrue(TEXT("Widget Blueprint create/compile/save succeeds"), WidgetApply.bOk);
    TestTrue(TEXT("Widget Blueprint was created"), WidgetApply.bCreated);
    TestTrue(TEXT("Widget Blueprint was compiled"), WidgetApply.bCompiled);
    TestTrue(TEXT("Widget Blueprint was saved"), WidgetApply.bSaved);
    TestEqual(
        TEXT("A consumed Blueprint plan cannot be replayed"),
        UThomasBlueprintToolset::ApplyBlueprintPlan(
            WidgetPlan.PlanId, true, true).Code,
        FString(TEXT("plan_not_found")));

    const FThomasBlueprintInspectionResult WidgetInspection =
        UThomasBlueprintToolset::InspectBlueprint(WidgetTestPath, true, 50);
    TestTrue(TEXT("Created Widget Blueprint inspection succeeds"), WidgetInspection.bOk);
    TestTrue(TEXT("Created asset is a Widget Blueprint"), WidgetInspection.bWidgetBlueprint);
    TestEqual(TEXT("Created Widget Tree element count"), WidgetInspection.Widgets.Num(), 3);
    TestTrue(
        TEXT("Created Blueprint variable is inspectable"),
        WidgetInspection.Variables.ContainsByPredicate(
            [](const FThomasBlueprintVariableRecord& Variable)
            {
                return Variable.Name == TEXT("TestValue");
            }));
    TestTrue(
        TEXT("Created function graph is inspectable"),
        WidgetInspection.Nodes.ContainsByPredicate(
            [](const FThomasBlueprintNodeRecord& Node)
            {
                return Node.GraphName == TEXT("ThomasGeneratedFunction");
            }));
    TestTrue(
        TEXT("Created graph pins and links are inspectable"),
        WidgetInspection.Nodes.ContainsByPredicate(
            [](const FThomasBlueprintNodeRecord& Node)
            {
                return Node.Title.Contains(TEXT("ThomasGeneratedEvent"))
                    && Node.Pins.ContainsByPredicate(
                        [](const FThomasBlueprintPinRecord& Pin)
                        {
                            return Pin.Name == TEXT("then") && !Pin.LinkedTo.IsEmpty();
                        });
            }));

    FThomasBlueprintBatchRequest ConsoleCallRequest;
    ConsoleCallRequest.AssetPath = WidgetTestPath;
    ConsoleCallRequest.ExpectedRevision = WidgetInspection.Revision;
    ConsoleCallRequest.bWidgetBlueprint = true;
    ConsoleCallRequest.bConfirmDestructive = true;
    FThomasBlueprintOperation ConsoleCallOperation;
    ConsoleCallOperation.Action = TEXT("add_call_function");
    ConsoleCallOperation.GraphName = TEXT("EventGraph");
    ConsoleCallOperation.Handle = TEXT("denied_console_call");
    ConsoleCallOperation.ClassPath = TEXT("/Script/Engine.KismetSystemLibrary");
    ConsoleCallOperation.FunctionName = TEXT("ExecuteConsoleCommand");
    ConsoleCallRequest.Operations.Add(ConsoleCallOperation);
    TestEqual(
        TEXT("Blueprint authoring rejects ExecuteConsoleCommand despite R2 confirmation"),
        UThomasBlueprintToolset::PlanBlueprintBatch(ConsoleCallRequest).Code,
        FString(TEXT("function_not_allowed")));

    const FThomasBlueprintValidationResult WidgetValidation =
        UThomasBlueprintToolset::ValidateBlueprint(
            WidgetTestPath,
            {TEXT("TestRoot"), TEXT("TestLabel"), TEXT("TestButton")});
    TestTrue(TEXT("Created Widget Blueprint contract validates"), WidgetValidation.bOk);
    const FThomasValidationResult NativeWidgetValidation =
        UThomasValidationToolset::ValidateAssets({WidgetTestPath}, 20);
    TestTrue(
        TEXT("Native Data Validation provider validates the temporary Widget Blueprint"),
        NativeWidgetValidation.bOk
            && NativeWidgetValidation.RequestedCount == 1
            && NativeWidgetValidation.CheckedCount == 1);
    const FThomasValidationResult NativeMapCheck =
        UThomasValidationToolset::RunCurrentMapCheck(50);
    TestTrue(
        TEXT("Native Map Check provider executes against the current PropHunt map"),
        NativeMapCheck.CheckedCount == 1
            && NativeMapCheck.Scope.StartsWith(TEXT("/Game/PropHunt/")));

    FThomasBlueprintBatchRequest WidgetAnimationRequest;
    WidgetAnimationRequest.AssetPath = WidgetTestPath;
    WidgetAnimationRequest.ExpectedRevision = WidgetInspection.Revision;
    WidgetAnimationRequest.bWidgetBlueprint = true;

    FThomasBlueprintOperation AddWidgetAnimation;
    AddWidgetAnimation.Action = TEXT("add_widget_animation");
    AddWidgetAnimation.AnimationName = TEXT("Pulse");
    AddWidgetAnimation.StartFrame = 0;
    AddWidgetAnimation.EndFrame = 120;
    AddWidgetAnimation.DisplayRateNumerator = 60;
    AddWidgetAnimation.DisplayRateDenominator = 1;
    WidgetAnimationRequest.Operations.Add(AddWidgetAnimation);

    FThomasBlueprintOperation AddOpacityStartKey;
    AddOpacityStartKey.Action = TEXT("set_widget_animation_opacity_key");
    AddOpacityStartKey.AnimationName = TEXT("Pulse");
    AddOpacityStartKey.WidgetName = TEXT("TestLabel");
    AddOpacityStartKey.Frame = 0;
    AddOpacityStartKey.Value = TEXT("0.25");
    AddOpacityStartKey.Interpolation = TEXT("linear");
    WidgetAnimationRequest.Operations.Add(AddOpacityStartKey);

    FThomasBlueprintOperation AddOpacityEndKey = AddOpacityStartKey;
    AddOpacityEndKey.Frame = 60;
    AddOpacityEndKey.Value = TEXT("1.0");
    WidgetAnimationRequest.Operations.Add(AddOpacityEndKey);

    FThomasBlueprintOperation AddButtonBinding;
    AddButtonBinding.Action = TEXT("add_widget_property_binding");
    AddButtonBinding.WidgetName = TEXT("TestButton");
    AddButtonBinding.PropertyName = TEXT("bIsEnabled");
    AddButtonBinding.SourcePropertyName = TEXT("IsButtonEnabled");
    WidgetAnimationRequest.Operations.Add(AddButtonBinding);

    const FThomasBlueprintPlanResult WidgetAnimationPlan =
        UThomasBlueprintToolset::PlanBlueprintBatch(WidgetAnimationRequest);
    TestTrue(TEXT("Widget animation and binding plan succeeds"), WidgetAnimationPlan.bOk);
    TestEqual(TEXT("Widget animation and binding plan is R1"), WidgetAnimationPlan.Risk, FString(TEXT("R1")));
    const FThomasBlueprintApplyResult WidgetAnimationApply =
        UThomasBlueprintToolset::ApplyBlueprintPlan(
            WidgetAnimationPlan.PlanId, true, true);
    TestTrue(TEXT("Widget animation and binding apply succeeds"), WidgetAnimationApply.bOk);
    TestTrue(TEXT("Widget animation and binding compile succeeds"), WidgetAnimationApply.bCompiled);
    TestTrue(TEXT("Widget animation and binding save succeeds"), WidgetAnimationApply.bSaved);

    UObject* WidgetAnimationSavedAsset = LoadObject<UObject>(
        nullptr,
        *FString::Printf(
            TEXT("%s.%s"),
            *WidgetTestPath,
            *FPackageName::GetLongPackageAssetName(WidgetTestPath)));
    FText WidgetAnimationReloadError;
    TestTrue(
        TEXT("Widget animation/binding package reloads from the saved file"),
        WidgetAnimationSavedAsset
            && UPackageTools::ReloadPackages(
                {WidgetAnimationSavedAsset->GetOutermost()},
                WidgetAnimationReloadError,
                EReloadPackagesInteractionMode::AssumePositive));

    const FThomasBlueprintInspectionResult WidgetAnimationInspection =
        UThomasBlueprintToolset::InspectBlueprint(WidgetTestPath, false, 50);
    TestTrue(TEXT("Widget animation inspection succeeds"), WidgetAnimationInspection.bOk);
    TestEqual(TEXT("One Widget animation is inspectable"), WidgetAnimationInspection.WidgetAnimations.Num(), 1);
    TestTrue(
        TEXT("Pulse opacity animation, binding, range, and keys are exact"),
        WidgetAnimationInspection.WidgetAnimations.ContainsByPredicate(
            [](const FThomasWidgetAnimationRecord& Animation)
            {
                return Animation.Name == TEXT("Pulse")
                    && Animation.DisplayLabel == TEXT("Pulse")
                    && Animation.DisplayRateNumerator == 60
                    && Animation.DisplayRateDenominator == 1
                    && Animation.PlaybackStartFrame == 0
                    && Animation.PlaybackEndFrame == 120
                    && Animation.Bindings.Num() == 1
                    && Animation.Bindings[0].WidgetName == TEXT("TestLabel")
                    && Animation.Bindings[0].TrackCount == 1
                    && Animation.Keys.Num() == 2
                    && Animation.Keys.ContainsByPredicate(
                        [](const FThomasWidgetAnimationKeyRecord& Key)
                        {
                            return Key.WidgetName == TEXT("TestLabel")
                                && Key.PropertyName == TEXT("RenderOpacity")
                                && Key.Frame == 0
                                && FMath::IsNearlyEqual(Key.Value, 0.25f)
                                && Key.Interpolation == TEXT("linear");
                        })
                    && Animation.Keys.ContainsByPredicate(
                        [](const FThomasWidgetAnimationKeyRecord& Key)
                        {
                            return Key.Frame == 60
                                && FMath::IsNearlyEqual(Key.Value, 1.0f);
                        });
            }));
    TestTrue(
        TEXT("Button enabled-state property binding is inspectable"),
        WidgetAnimationInspection.WidgetBindings.ContainsByPredicate(
            [](const FThomasWidgetFunctionBindingRecord& Binding)
            {
                return Binding.WidgetName == TEXT("TestButton")
                    && Binding.PropertyName == TEXT("bIsEnabled")
                    && Binding.SourcePropertyName == TEXT("IsButtonEnabled")
                    && Binding.Kind == TEXT("property");
            }));

    FThomasBlueprintBatchRequest UnconfirmedAnimationRemoval;
    UnconfirmedAnimationRemoval.AssetPath = WidgetTestPath;
    UnconfirmedAnimationRemoval.ExpectedRevision = WidgetAnimationInspection.Revision;
    UnconfirmedAnimationRemoval.bWidgetBlueprint = true;
    FThomasBlueprintOperation RemoveWidgetAnimation;
    RemoveWidgetAnimation.Action = TEXT("remove_widget_animation");
    RemoveWidgetAnimation.AnimationName = TEXT("Pulse");
    UnconfirmedAnimationRemoval.Operations.Add(RemoveWidgetAnimation);
    const FThomasBlueprintPlanResult UnconfirmedAnimationRemovalPlan =
        UThomasBlueprintToolset::PlanBlueprintBatch(UnconfirmedAnimationRemoval);
    TestFalse(TEXT("Widget animation removal requires confirmation"), UnconfirmedAnimationRemovalPlan.bOk);
    TestEqual(
        TEXT("Widget animation removal reports confirmation gate"),
        UnconfirmedAnimationRemovalPlan.Code,
        FString(TEXT("confirmation_required")));

    FThomasBlueprintBatchRequest UnconfirmedBindingRemoval;
    UnconfirmedBindingRemoval.AssetPath = WidgetTestPath;
    UnconfirmedBindingRemoval.ExpectedRevision = WidgetAnimationInspection.Revision;
    UnconfirmedBindingRemoval.bWidgetBlueprint = true;
    FThomasBlueprintOperation RemoveButtonBinding;
    RemoveButtonBinding.Action = TEXT("remove_widget_binding");
    RemoveButtonBinding.WidgetName = TEXT("TestButton");
    RemoveButtonBinding.PropertyName = TEXT("bIsEnabled");
    UnconfirmedBindingRemoval.Operations.Add(RemoveButtonBinding);
    const FThomasBlueprintPlanResult UnconfirmedBindingRemovalPlan =
        UThomasBlueprintToolset::PlanBlueprintBatch(UnconfirmedBindingRemoval);
    TestFalse(TEXT("Widget function binding removal requires confirmation"), UnconfirmedBindingRemovalPlan.bOk);
    TestEqual(
        TEXT("Widget function binding removal reports confirmation gate"),
        UnconfirmedBindingRemovalPlan.Code,
        FString(TEXT("confirmation_required")));

    FThomasBlueprintBatchRequest ConfirmedWidgetRemoval = UnconfirmedAnimationRemoval;
    ConfirmedWidgetRemoval.bConfirmDestructive = true;
    ConfirmedWidgetRemoval.Operations.Add(RemoveButtonBinding);
    const FThomasBlueprintPlanResult ConfirmedWidgetRemovalPlan =
        UThomasBlueprintToolset::PlanBlueprintBatch(ConfirmedWidgetRemoval);
    TestTrue(TEXT("Confirmed Widget animation/binding removal plan succeeds"), ConfirmedWidgetRemovalPlan.bOk);
    TestEqual(TEXT("Confirmed Widget removal plan is R2"), ConfirmedWidgetRemovalPlan.Risk, FString(TEXT("R2")));
    const FThomasBlueprintApplyResult ConfirmedWidgetRemovalApply =
        UThomasBlueprintToolset::ApplyBlueprintPlan(
            ConfirmedWidgetRemovalPlan.PlanId, true, true);
    TestTrue(TEXT("Confirmed Widget animation/binding removal succeeds"), ConfirmedWidgetRemovalApply.bOk);

    UObject* WidgetRemovalSavedAsset = LoadObject<UObject>(
        nullptr,
        *FString::Printf(
            TEXT("%s.%s"),
            *WidgetTestPath,
            *FPackageName::GetLongPackageAssetName(WidgetTestPath)));
    FText WidgetRemovalReloadError;
    TestTrue(
        TEXT("Widget animation/binding removal reloads from the saved file"),
        WidgetRemovalSavedAsset
            && UPackageTools::ReloadPackages(
                {WidgetRemovalSavedAsset->GetOutermost()},
                WidgetRemovalReloadError,
                EReloadPackagesInteractionMode::AssumePositive));

    const FThomasBlueprintInspectionResult WidgetRemovalInspection =
        UThomasBlueprintToolset::InspectBlueprint(WidgetTestPath, false, 50);
    TestTrue(TEXT("Widget removal inspection succeeds"), WidgetRemovalInspection.bOk);
    TestTrue(TEXT("Widget animations are absent after confirmed removal"), WidgetRemovalInspection.WidgetAnimations.IsEmpty());
    TestTrue(TEXT("Widget bindings are absent after confirmed removal"), WidgetRemovalInspection.WidgetBindings.IsEmpty());

    FThomasBlueprintBatchRequest FailedRequest;
    FailedRequest.AssetPath = WidgetTestPath;
    FailedRequest.ExpectedRevision = WidgetRemovalInspection.Revision;
    FailedRequest.bWidgetBlueprint = true;
    FThomasBlueprintOperation FailedOperation;
    FailedOperation.Action = TEXT("set_cdo_property");
    FailedOperation.Name = TEXT("DefinitelyMissingProperty");
    FailedOperation.Value = TEXT("1");
    FailedRequest.Operations.Add(FailedOperation);
    const FThomasBlueprintPlanResult FailedPlan =
        UThomasBlueprintToolset::PlanBlueprintBatch(FailedRequest);
    TestTrue(TEXT("Invalid-at-apply batch still receives guarded plan"), FailedPlan.bOk);
    const FThomasBlueprintApplyResult FailedApply =
        UThomasBlueprintToolset::ApplyBlueprintPlan(FailedPlan.PlanId, true, false);
    TestFalse(TEXT("Invalid property batch fails"), FailedApply.bOk);
    TestEqual(TEXT("Invalid property batch reports apply failure"), FailedApply.Code, FString(TEXT("apply_failed")));
    TestTrue(TEXT("Invalid property batch rolls back"), FailedApply.bRolledBack);

    UObject* WidgetTestAsset = LoadObject<UObject>(
        nullptr,
        *FString::Printf(
            TEXT("%s.%s"),
            *WidgetTestPath,
            *FPackageName::GetLongPackageAssetName(WidgetTestPath)));
    TestNotNull(TEXT("Temporary Widget Blueprint loaded for cleanup"), WidgetTestAsset);
    if (WidgetTestAsset)
    {
        TestEqual(
            TEXT("Temporary Widget Blueprint deleted through Unreal Editor API"),
            ObjectTools::DeleteObjectsUnchecked({WidgetTestAsset}),
            1);
    }
    CollectGarbage(RF_NoFlags);
    const FString WidgetTestFilename = FPackageName::LongPackageNameToFilename(
        WidgetTestPath, FPackageName::GetAssetPackageExtension());
    TestFalse(
        TEXT("Temporary Widget Blueprint removed from disk"),
        IFileManager::Get().FileExists(*WidgetTestFilename));

    const FString BlueprintAuthoringSuffix =
        FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString InterfaceTestPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/BPI_TE_Native_")
        + BlueprintAuthoringSuffix;
    FThomasBlueprintBatchRequest InterfaceRequest;
    InterfaceRequest.AssetPath = InterfaceTestPath;
    InterfaceRequest.ExpectedRevision = TEXT("missing");
    InterfaceRequest.AssetKind = TEXT("interface");
    InterfaceRequest.bCreateIfMissing = true;
    FThomasBlueprintOperation InterfaceFunction;
    InterfaceFunction.Action = TEXT("add_function_graph");
    InterfaceFunction.Name = TEXT("ThomasInteract");
    InterfaceRequest.Operations.Add(InterfaceFunction);
    const FThomasBlueprintPlanResult InterfacePlan =
        UThomasBlueprintToolset::PlanBlueprintBatch(InterfaceRequest);
    TestTrue(TEXT("Blueprint Interface create plan succeeds"), InterfacePlan.bOk);
    const FThomasBlueprintApplyResult InterfaceApply =
        UThomasBlueprintToolset::ApplyBlueprintPlan(InterfacePlan.PlanId, true, true);
    TestTrue(TEXT("Blueprint Interface create/compile/save succeeds"), InterfaceApply.bOk);
    const FThomasBlueprintInspectionResult InterfaceInspection =
        UThomasBlueprintToolset::InspectBlueprint(InterfaceTestPath, true, 50);
    TestTrue(TEXT("Blueprint Interface inspection succeeds"), InterfaceInspection.bOk);
    TestEqual(
        TEXT("Blueprint Interface kind is explicit"),
        InterfaceInspection.AssetKind,
        FString(TEXT("interface")));

    const FString ActorBlueprintTestPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/BP_TE_Native_")
        + BlueprintAuthoringSuffix;
    FThomasBlueprintBatchRequest ActorRequest;
    ActorRequest.AssetPath = ActorBlueprintTestPath;
    ActorRequest.ExpectedRevision = TEXT("missing");
    ActorRequest.ParentClassPath = TEXT("/Script/Engine.Actor");
    ActorRequest.AssetKind = TEXT("normal");
    ActorRequest.bCreateIfMissing = true;

    FThomasBlueprintOperation ArrayVariable;
    ArrayVariable.Action = TEXT("add_variable");
    ArrayVariable.Name = TEXT("Scores");
    ArrayVariable.Type = TEXT("float");
    ArrayVariable.ContainerType = TEXT("array");
    ActorRequest.Operations.Add(ArrayVariable);

    FThomasBlueprintOperation MapVariable;
    MapVariable.Action = TEXT("add_variable");
    MapVariable.Name = TEXT("NamedScores");
    MapVariable.Type = TEXT("int");
    MapVariable.ContainerType = TEXT("map");
    MapVariable.KeyType = TEXT("name");
    ActorRequest.Operations.Add(MapVariable);

    FThomasBlueprintOperation AddInterface;
    AddInterface.Action = TEXT("add_interface");
    AddInterface.ClassPath = InterfaceInspection.GeneratedClassPath;
    ActorRequest.Operations.Add(AddInterface);

    FThomasBlueprintOperation AddRootComponent;
    AddRootComponent.Action = TEXT("add_component");
    AddRootComponent.Name = TEXT("ThomasRoot");
    AddRootComponent.ClassPath = TEXT("/Script/Engine.SceneComponent");
    ActorRequest.Operations.Add(AddRootComponent);

    FThomasBlueprintOperation AddAudioComponent;
    AddAudioComponent.Action = TEXT("add_component");
    AddAudioComponent.Name = TEXT("ThomasAudio");
    AddAudioComponent.ClassPath = TEXT("/Script/Engine.AudioComponent");
    AddAudioComponent.ParentName = TEXT("ThomasRoot");
    ActorRequest.Operations.Add(AddAudioComponent);

    const FThomasBlueprintPlanResult ActorPlan =
        UThomasBlueprintToolset::PlanBlueprintBatch(ActorRequest);
    TestTrue(TEXT("Actor Blueprint structural plan succeeds"), ActorPlan.bOk);
    const FThomasBlueprintApplyResult ActorApply =
        UThomasBlueprintToolset::ApplyBlueprintPlan(ActorPlan.PlanId, true, true);
    TestTrue(TEXT("Actor Blueprint components/interfaces save succeeds"), ActorApply.bOk);
    FThomasBlueprintInspectionResult ActorInspection =
        UThomasBlueprintToolset::InspectBlueprint(ActorBlueprintTestPath, false, 50);
    TestTrue(TEXT("Actor Blueprint structural inspection succeeds"), ActorInspection.bOk);
    TestTrue(
        TEXT("Actor Blueprint interface is inspectable"),
        ActorInspection.Interfaces.Contains(InterfaceInspection.GeneratedClassPath));
    TestTrue(
        TEXT("Actor Component class and parent are inspectable"),
        ActorInspection.ComponentDetails.ContainsByPredicate(
            [](const FThomasBlueprintComponentRecord& Component)
            {
                return Component.Name == TEXT("ThomasAudio")
                    && Component.ClassPath == TEXT("/Script/Engine.AudioComponent")
                    && Component.ParentName == TEXT("ThomasRoot");
            }));
    TestTrue(
        TEXT("Array and map Blueprint variables are inspectable"),
        ActorInspection.Variables.ContainsByPredicate(
            [](const FThomasBlueprintVariableRecord& Variable)
            {
                return Variable.Name == TEXT("Scores")
                    && Variable.Type.StartsWith(TEXT("array<"));
            })
        && ActorInspection.Variables.ContainsByPredicate(
            [](const FThomasBlueprintVariableRecord& Variable)
            {
                return Variable.Name == TEXT("NamedScores")
                    && Variable.Type.StartsWith(TEXT("map<"));
            }));

    FThomasBlueprintBatchRequest ComponentPropertyRequest;
    ComponentPropertyRequest.AssetPath = ActorBlueprintTestPath;
    ComponentPropertyRequest.ExpectedRevision = ActorInspection.Revision;
    ComponentPropertyRequest.AssetKind = TEXT("normal");
    FThomasBlueprintOperation SetComponentProperty;
    SetComponentProperty.Action = TEXT("set_component_property");
    SetComponentProperty.Name = TEXT("ThomasAudio");
    SetComponentProperty.PropertyName = TEXT("bAutoActivate");
    SetComponentProperty.ExpectedValue = TEXT("True");
    SetComponentProperty.Value = TEXT("False");
    ComponentPropertyRequest.Operations.Add(SetComponentProperty);
    const FThomasBlueprintPlanResult ComponentPropertyPlan =
        UThomasBlueprintToolset::PlanBlueprintBatch(ComponentPropertyRequest);
    TestTrue(TEXT("Component property plan succeeds"), ComponentPropertyPlan.bOk);
    const FThomasBlueprintApplyResult ComponentPropertyApply =
        UThomasBlueprintToolset::ApplyBlueprintPlan(
            ComponentPropertyPlan.PlanId, true, true);
    TestTrue(TEXT("Component property patch succeeds"), ComponentPropertyApply.bOk);

    ActorInspection = UThomasBlueprintToolset::InspectBlueprint(
        ActorBlueprintTestPath, false, 50);
    FThomasBlueprintBatchRequest UnconfirmedRemoval;
    UnconfirmedRemoval.AssetPath = ActorBlueprintTestPath;
    UnconfirmedRemoval.ExpectedRevision = ActorInspection.Revision;
    UnconfirmedRemoval.AssetKind = TEXT("normal");
    FThomasBlueprintOperation RemoveComponent;
    RemoveComponent.Action = TEXT("remove_component");
    RemoveComponent.Name = TEXT("ThomasAudio");
    UnconfirmedRemoval.Operations.Add(RemoveComponent);
    const FThomasBlueprintPlanResult UnconfirmedRemovalPlan =
        UThomasBlueprintToolset::PlanBlueprintBatch(UnconfirmedRemoval);
    TestFalse(TEXT("Component removal requires R2 confirmation"), UnconfirmedRemovalPlan.bOk);
    TestEqual(
        TEXT("Component removal reports confirmation gate"),
        UnconfirmedRemovalPlan.Code,
        FString(TEXT("confirmation_required")));

    FThomasBlueprintBatchRequest ConfirmedRemoval = UnconfirmedRemoval;
    ConfirmedRemoval.bConfirmDestructive = true;
    FThomasBlueprintOperation RemoveInterface;
    RemoveInterface.Action = TEXT("remove_interface");
    RemoveInterface.ClassPath = InterfaceInspection.GeneratedClassPath;
    ConfirmedRemoval.Operations.Add(RemoveInterface);
    const FThomasBlueprintPlanResult ConfirmedRemovalPlan =
        UThomasBlueprintToolset::PlanBlueprintBatch(ConfirmedRemoval);
    TestTrue(TEXT("Confirmed component/interface removal plan succeeds"), ConfirmedRemovalPlan.bOk);
    const FThomasBlueprintApplyResult ConfirmedRemovalApply =
        UThomasBlueprintToolset::ApplyBlueprintPlan(
            ConfirmedRemovalPlan.PlanId, true, true);
    TestTrue(TEXT("Confirmed component/interface removal succeeds"), ConfirmedRemovalApply.bOk);

    UObject* ActorBlueprintTestAsset = LoadObject<UObject>(
        nullptr,
        *FString::Printf(
            TEXT("%s.%s"),
            *ActorBlueprintTestPath,
            *FPackageName::GetLongPackageAssetName(ActorBlueprintTestPath)));
    UObject* InterfaceTestAsset = LoadObject<UObject>(
        nullptr,
        *FString::Printf(
            TEXT("%s.%s"),
            *InterfaceTestPath,
            *FPackageName::GetLongPackageAssetName(InterfaceTestPath)));
    TArray<UObject*> BlueprintAuthoringAssets;
    if (ActorBlueprintTestAsset) BlueprintAuthoringAssets.Add(ActorBlueprintTestAsset);
    if (InterfaceTestAsset) BlueprintAuthoringAssets.Add(InterfaceTestAsset);
    TestEqual(
        TEXT("Temporary component/interface assets deleted through Unreal API"),
        ObjectTools::DeleteObjectsUnchecked(BlueprintAuthoringAssets),
        BlueprintAuthoringAssets.Num());
    CollectGarbage(RF_NoFlags);
    TestFalse(
        TEXT("Temporary Actor Blueprint removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            ActorBlueprintTestPath, FPackageName::GetAssetPackageExtension())));
    TestFalse(
        TEXT("Temporary Blueprint Interface removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            InterfaceTestPath, FPackageName::GetAssetPackageExtension())));

    const FString MaterialTestPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/M_TE_Native_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FThomasMaterialPatchRequest MaterialRequest;
    MaterialRequest.AssetPath = MaterialTestPath;
    MaterialRequest.ExpectedRevision = TEXT("missing");
    MaterialRequest.bCreateIfMissing = true;

    FThomasMaterialOperation CreateConstant;
    CreateConstant.Action = TEXT("create_expression");
    CreateConstant.Handle = TEXT("constant");
    CreateConstant.ClassPath = TEXT("/Script/Engine.MaterialExpressionConstant");
    CreateConstant.PositionX = -300;
    MaterialRequest.Operations.Add(CreateConstant);

    FThomasMaterialOperation CreateUnusedConstant;
    CreateUnusedConstant.Action = TEXT("create_expression");
    CreateUnusedConstant.Handle = TEXT("unused_constant");
    CreateUnusedConstant.ClassPath = TEXT("/Script/Engine.MaterialExpressionConstant");
    CreateUnusedConstant.PositionX = -300;
    MaterialRequest.Operations.Add(CreateUnusedConstant);

    FThomasMaterialOperation SetConstant;
    SetConstant.Action = TEXT("set_expression_property");
    SetConstant.Handle = TEXT("constant");
    SetConstant.PropertyName = TEXT("R");
    SetConstant.Value = TEXT("0.42");
    MaterialRequest.Operations.Add(SetConstant);

    FThomasMaterialOperation ConnectBaseColor;
    ConnectBaseColor.Action = TEXT("connect_material_property");
    ConnectBaseColor.Handle = TEXT("constant");
    ConnectBaseColor.MaterialProperty = TEXT("BaseColor");
    MaterialRequest.Operations.Add(ConnectBaseColor);

    FThomasMaterialOperation ConnectRoughness;
    ConnectRoughness.Action = TEXT("connect_material_property");
    ConnectRoughness.Handle = TEXT("constant");
    ConnectRoughness.MaterialProperty = TEXT("Roughness");
    MaterialRequest.Operations.Add(ConnectRoughness);

    const FThomasMaterialPlanResult MaterialPlan =
        UThomasMaterialsToolset::PlanMaterialPatch(MaterialRequest);
    TestTrue(TEXT("Native material graph create plan succeeds"), MaterialPlan.bOk);
    const FThomasMaterialApplyResult MaterialApply =
        UThomasMaterialsToolset::ApplyMaterialPlan(MaterialPlan.PlanId, true, true);
    TestTrue(TEXT("Native material graph create/compile/save succeeds"), MaterialApply.bOk);
    TestTrue(TEXT("Native material asset was created"), MaterialApply.bCreated);
    TestTrue(TEXT("Native material graph was compiled"), MaterialApply.bCompiled);
    TestTrue(TEXT("Native material asset was saved"), MaterialApply.bSaved);
    const FThomasMaterialInspectionResult MaterialInspection =
        UThomasMaterialsToolset::InspectMaterial(MaterialTestPath, 20);
    TestTrue(TEXT("Created native material is inspectable"), MaterialInspection.bOk);
    TestEqual(TEXT("Created material expression count"), MaterialInspection.ExpressionCount, 2);
    TestTrue(
        TEXT("Material expression exposes native inputs/outputs"),
        !MaterialInspection.Expressions.IsEmpty()
            && !MaterialInspection.Expressions[0].Outputs.IsEmpty());
    TestEqual(TEXT("Material root connection count is exact"), MaterialInspection.MaterialRootConnectionCount, 2);
    TestEqual(TEXT("Material graph reachable expression count is exact"), MaterialInspection.ReachableExpressionCount, 1);
    TestEqual(TEXT("Material graph unreachable expression count is exact"), MaterialInspection.UnreachableExpressionCount, 1);
    TestEqual(TEXT("Material graph orphan expression count is exact"), MaterialInspection.OrphanExpressionCount, 1);
    TestEqual(TEXT("Material graph overlap group count is exact"), MaterialInspection.OverlapGroupCount, 1);
    TestTrue(
        TEXT("Material graph reports the orphan expression"),
        MaterialInspection.Diagnostics.ContainsByPredicate(
            [](const FThomasMaterialGraphDiagnostic& Diagnostic)
            {
                return Diagnostic.Code == TEXT("orphan_expression");
            }));
    TestTrue(
        TEXT("Material graph reports overlapping nodes"),
        MaterialInspection.Diagnostics.ContainsByPredicate(
            [](const FThomasMaterialGraphDiagnostic& Diagnostic)
            {
                return Diagnostic.Code == TEXT("overlapping_nodes");
            }));

    UObject* MaterialTestAsset = LoadObject<UObject>(
        nullptr,
        *FString::Printf(
            TEXT("%s.%s"),
            *MaterialTestPath,
            *FPackageName::GetLongPackageAssetName(MaterialTestPath)));
    TestNotNull(TEXT("Temporary native material loaded for cleanup"), MaterialTestAsset);
    if (MaterialTestAsset)
    {
        TestEqual(
            TEXT("Temporary native material deleted through Unreal API"),
            ObjectTools::DeleteObjectsUnchecked({MaterialTestAsset}),
            1);
    }
    CollectGarbage(RF_NoFlags);
    TestFalse(
        TEXT("Temporary native material removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            MaterialTestPath, FPackageName::GetAssetPackageExtension())));

    const FString DecalMaterialPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/M_TE_Native_Decal_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FThomasMaterialPatchRequest DecalMaterialRequest;
    DecalMaterialRequest.AssetPath = DecalMaterialPath;
    DecalMaterialRequest.ExpectedRevision = TEXT("missing");
    DecalMaterialRequest.bCreateIfMissing = true;
    FThomasMaterialOperation DecalConstant;
    DecalConstant.Action = TEXT("create_expression");
    DecalConstant.Handle = TEXT("decal_constant");
    DecalConstant.ClassPath = TEXT("/Script/Engine.MaterialExpressionConstant3Vector");
    DecalMaterialRequest.Operations.Add(DecalConstant);
    const FThomasMaterialPlanResult DecalMaterialPlan =
        UThomasMaterialsToolset::PlanMaterialPatch(DecalMaterialRequest);
    TestTrue(TEXT("Native Decal material create plan succeeds"), DecalMaterialPlan.bOk);
    TestTrue(
        TEXT("Native Decal material create/compile/save succeeds"),
        UThomasMaterialsToolset::ApplyMaterialPlan(DecalMaterialPlan.PlanId, true, true).bOk);
    const FThomasObjectDetailsResult DecalMaterialDetails =
        UThomasAssetsToolset::GetObjectDetails(
            DecalMaterialPath, {TEXT("MaterialDomain")}, 10);
    TestTrue(TEXT("Decal material domain Details inspection succeeds"), DecalMaterialDetails.bOk);
    if (DecalMaterialDetails.bOk && DecalMaterialDetails.Properties.Num() == 1)
    {
        FThomasObjectPatchRequest DecalDomainRequest;
        DecalDomainRequest.ObjectPath = DecalMaterialPath;
        DecalDomainRequest.ExpectedRevision = DecalMaterialDetails.Revision;
        FThomasObjectPatchOperation SetDecalDomain;
        SetDecalDomain.Name = TEXT("MaterialDomain");
        SetDecalDomain.ExpectedValue = DecalMaterialDetails.Properties[0].Value;
        SetDecalDomain.Value = TEXT("MD_DeferredDecal");
        DecalDomainRequest.Operations.Add(SetDecalDomain);
        const FThomasObjectPatchPlanResult DecalDomainPlan =
            UThomasAssetsToolset::PlanObjectPatch(DecalDomainRequest);
        TestTrue(TEXT("Native Decal material domain plan succeeds"), DecalDomainPlan.bOk);
        TestTrue(
            TEXT("Native Decal material domain patch/save succeeds"),
            UThomasAssetsToolset::ApplyObjectPatch(DecalDomainPlan.PlanId, true).bOk);
    }
    TestEqual(
        TEXT("Native Decal material domain is exact"),
        UThomasMaterialsToolset::InspectMaterial(DecalMaterialPath, 20).MaterialDomain,
        FString(TEXT("MD_DeferredDecal")));

    const FThomasWorldInspectionResult OriginalMapInspection =
        UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
    TestTrue(TEXT("Map lifecycle preflight inspects the canonical current map"), OriginalMapInspection.bOk);
    TestFalse(
        TEXT("Map lifecycle automation starts from a clean canonical map"),
        OriginalMapInspection.bDirty);
    if (OriginalMapInspection.bOk && !OriginalMapInspection.bDirty)
    {
        const FString TemporaryMapPath =
            TEXT("/Game/PropHunt/Tests/ThomasEditor/L_TE_Native_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        FThomasMapLifecycleRequest UnconfirmedMapCreate;
        UnconfirmedMapCreate.Action = TEXT("create_blank_map");
        UnconfirmedMapCreate.CurrentMapPath = OriginalMapInspection.MapPath;
        UnconfirmedMapCreate.ExpectedRevision = OriginalMapInspection.Revision;
        UnconfirmedMapCreate.TargetMapPath = TemporaryMapPath;
        TestEqual(
            TEXT("Map world switch requires explicit R2 confirmation"),
            UThomasWorldToolset::PlanMapLifecycle(UnconfirmedMapCreate).Code,
            FString(TEXT("confirmation_required")));

        FThomasMapLifecycleRequest ConfirmedMapCreate = UnconfirmedMapCreate;
        ConfirmedMapCreate.bConfirmWorldSwitch = true;
        const FThomasMapLifecyclePlanResult MapCreatePlan =
            UThomasWorldToolset::PlanMapLifecycle(ConfirmedMapCreate);
        TestTrue(TEXT("Native blank map create plan succeeds"), MapCreatePlan.bOk);
        TestEqual(TEXT("Native blank map create is R2"), MapCreatePlan.Risk, FString(TEXT("R2")));
        const FThomasMapLifecycleApplyResult MapCreateApply =
            UThomasWorldToolset::ApplyMapLifecyclePlan(MapCreatePlan.PlanId);
        TestTrue(TEXT("Native blank map create/save/open succeeds"), MapCreateApply.bOk);
        TestTrue(TEXT("Native blank map create reports save"), MapCreateApply.bSaved);
        TestTrue(TEXT("Native blank map create reports world switch"), MapCreateApply.bWorldSwitched);
        TestEqual(TEXT("Native blank map is the active map"), MapCreateApply.MapPath, TemporaryMapPath);
        TestTrue(
            TEXT("Native blank map exists on disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                TemporaryMapPath, FPackageName::GetMapPackageExtension())));

        const FThomasWorldInspectionResult TemporaryMapInspection =
            UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
        TestTrue(TEXT("Created native blank map is inspectable"), TemporaryMapInspection.bOk);
        TestFalse(TEXT("Created native blank map is clean after save"), TemporaryMapInspection.bDirty);
        FThomasMapLifecycleRequest ReopenOriginalMap;
        ReopenOriginalMap.Action = TEXT("open_map");
        ReopenOriginalMap.CurrentMapPath = TemporaryMapInspection.MapPath;
        ReopenOriginalMap.ExpectedRevision = TemporaryMapInspection.Revision;
        ReopenOriginalMap.TargetMapPath = OriginalMapInspection.MapPath;
        ReopenOriginalMap.bConfirmWorldSwitch = true;
        const FThomasMapLifecyclePlanResult ReopenOriginalPlan =
            UThomasWorldToolset::PlanMapLifecycle(ReopenOriginalMap);
        TestTrue(TEXT("Native existing map open plan succeeds"), ReopenOriginalPlan.bOk);
        const FThomasMapLifecycleApplyResult ReopenOriginalApply =
            UThomasWorldToolset::ApplyMapLifecyclePlan(ReopenOriginalPlan.PlanId);
        TestTrue(TEXT("Native existing map open succeeds"), ReopenOriginalApply.bOk);
        TestEqual(
            TEXT("Map lifecycle returns to the exact original map"),
            ReopenOriginalApply.MapPath,
            OriginalMapInspection.MapPath);

        UObject* TemporaryMapAsset = LoadObject<UWorld>(
            nullptr,
            *(TemporaryMapPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(TemporaryMapPath)));
        TestNotNull(TEXT("Temporary native map loaded for cleanup"), TemporaryMapAsset);
        if (TemporaryMapAsset)
        {
            TestEqual(
                TEXT("Temporary native map deleted through Unreal API"),
                ObjectTools::DeleteObjectsUnchecked({TemporaryMapAsset}),
                1);
        }
        CollectGarbage(RF_NoFlags);
        TestFalse(
            TEXT("Temporary native map removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                TemporaryMapPath, FPackageName::GetMapPackageExtension())));
    }

    const FThomasWorldInspectionResult WorldBeforeDecal =
        UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
    const FThomasEnvironmentInspectionResult EnvironmentInspection =
        UThomasWorldToolset::InspectEnvironment(100);
    TestTrue(
        TEXT("World Partition/Data Layers/Landscape/Foliage/HLOD/navigation inspection succeeds"),
        EnvironmentInspection.bOk);
    TestEqual(
        TEXT("Environment inspection targets the open canonical map"),
        EnvironmentInspection.MapPath,
        WorldBeforeDecal.MapPath);
    TestEqual(
        TEXT("HLOD inspection returns one typed record per bounded actor"),
        EnvironmentInspection.HLODDetails.Num(),
        FMath::Min(EnvironmentInspection.HLODActorCount, 100));
    TestEqual(
        TEXT("Navigation bounds inspection returns bounded typed records"),
        EnvironmentInspection.NavigationBounds.Num(),
        FMath::Min(EnvironmentInspection.NavigationBoundsVolumeCount, 100));
    TestEqual(
        TEXT("Navigation data inspection returns bounded typed records"),
        EnvironmentInspection.NavigationData.Num(),
        FMath::Min(EnvironmentInspection.NavigationDataCount, 100));
    if (!EnvironmentInspection.NavigationData.IsEmpty())
    {
        TestTrue(
            TEXT("Typed navigation data exposes native class and runtime generation"),
            !EnvironmentInspection.NavigationData[0].ClassPath.IsEmpty()
                && !EnvironmentInspection.NavigationData[0].RuntimeGeneration.IsEmpty());
    }

    FThomasEnvironmentBuildRequest EnvironmentBuildRequest;
    EnvironmentBuildRequest.Action = TEXT("build_navigation");
    EnvironmentBuildRequest.MapPath = EnvironmentInspection.MapPath;
    EnvironmentBuildRequest.ExpectedRevision = EnvironmentInspection.Revision;
    const FThomasEnvironmentBuildPlanResult UnconfirmedEnvironmentBuild =
        UThomasWorldToolset::PlanEnvironmentBuild(EnvironmentBuildRequest);
    TestEqual(
        TEXT("Environment builder refuses an unconfirmed world switch"),
        UnconfirmedEnvironmentBuild.Code,
        EnvironmentInspection.bWorldPartition
            ? FString(TEXT("confirmation_required"))
            : FString(TEXT("world_partition_required")));
    if (EnvironmentInspection.bWorldPartition)
    {
        EnvironmentBuildRequest.bConfirmWorldSwitch = true;
        const FThomasEnvironmentBuildPlanResult EnvironmentBuildPlan =
            UThomasWorldToolset::PlanEnvironmentBuild(EnvironmentBuildRequest);
        TestTrue(TEXT("World Partition navigation builder plan succeeds"), EnvironmentBuildPlan.bOk);
        TestEqual(
            TEXT("Navigation plan uses the native WP navigation builder"),
            EnvironmentBuildPlan.BuilderClass,
            FString(TEXT("WorldPartitionNavigationDataBuilder")));

        FThomasEnvironmentBuildRequest DeleteHlodRequest = EnvironmentBuildRequest;
        DeleteHlodRequest.Action = TEXT("delete_hlods");
        const FThomasEnvironmentBuildPlanResult UnconfirmedDeleteHlod =
            UThomasWorldToolset::PlanEnvironmentBuild(DeleteHlodRequest);
        TestEqual(
            TEXT("HLOD deletion requires explicit destructive confirmation"),
            UnconfirmedDeleteHlod.Code,
            FString(TEXT("destructive_confirmation_required")));
    }

    TMap<FString, int32> FoliageCountsBefore;
    TSet<FString> FoliageActorsBefore;
    for (const FThomasFoliageRecord& Record : EnvironmentInspection.Foliage)
    {
        FoliageCountsBefore.Add(
            Record.ActorPath + TEXT("|") + Record.FoliageTypePath,
            Record.InstanceCount);
        FoliageActorsBefore.Add(Record.ActorPath);
    }
    const FString FoliageTypePackageName =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/FT_TE_Cube_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* FoliageTypePackage = CreatePackage(*FoliageTypePackageName);
    UClass* FoliageTypeClass = LoadObject<UClass>(
        nullptr, TEXT("/Script/Foliage.FoliageType_InstancedStaticMesh"));
    UObject* FoliageTypeAsset = FoliageTypeClass
        ? NewObject<UObject>(
            FoliageTypePackage,
            FoliageTypeClass,
            *FPackageName::GetLongPackageAssetName(FoliageTypePackageName),
            RF_Public | RF_Standalone | RF_Transactional)
        : nullptr;
    UObject* FoliageMesh = LoadObject<UObject>(
        nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    FObjectPropertyBase* FoliageMeshProperty = FoliageTypeClass
        ? FindFProperty<FObjectPropertyBase>(FoliageTypeClass, FName(TEXT("Mesh")))
        : nullptr;
    if (FoliageTypeAsset && FoliageMesh && FoliageMeshProperty)
    {
        FoliageMeshProperty->SetObjectPropertyValue_InContainer(
            FoliageTypeAsset, FoliageMesh);
        FAssetRegistryModule::AssetCreated(FoliageTypeAsset);
        FoliageTypePackage->MarkPackageDirty();
    }
    TestNotNull(TEXT("Automation creates a native transient Foliage Type fixture"), FoliageTypeAsset);
    const FString FoliageTypeObjectPath = FoliageTypeAsset
        ? FoliageTypeAsset->GetPathName() : FString();
    FThomasWorldPatchRequest AddFoliageRequest;
    AddFoliageRequest.MapPath = EnvironmentInspection.MapPath;
    AddFoliageRequest.ExpectedRevision = EnvironmentInspection.Revision;
    AddFoliageRequest.bConfirmDestructive = true;
    FThomasWorldOperation AddFoliage;
    AddFoliage.Action = TEXT("add_foliage_instance");
    AddFoliage.AssetPath = FoliageTypeObjectPath;
    AddFoliage.LocationX = 17325.0f;
    AddFoliage.LocationY = -9480.0f;
    AddFoliage.LocationZ = 725.0f;
    AddFoliage.ScaleX = 0.35f;
    AddFoliage.ScaleY = 0.45f;
    AddFoliage.ScaleZ = 0.55f;
    AddFoliageRequest.Operations.Add(AddFoliage);
    const FThomasWorldPatchPlanResult AddFoliagePlan =
        UThomasWorldToolset::PlanWorldPatch(AddFoliageRequest);
    TestTrue(TEXT("Native foliage instance add plan succeeds"), AddFoliagePlan.bOk);
    const FThomasWorldPatchApplyResult AddFoliageApply =
        UThomasWorldToolset::ApplyWorldPlan(AddFoliagePlan.PlanId, false);
    TestTrue(TEXT("Native foliage instance add succeeds"), AddFoliageApply.bOk);
    TestEqual(
        TEXT("Native foliage add returns one stable domain handle"),
        AddFoliageApply.CreatedObjectPaths.Num(),
        1);

    FString AddedFoliageActorPath;
    FString AddedFoliageTypePath;
    int32 AddedFoliageInstanceIndex = INDEX_NONE;
    if (AddFoliageApply.CreatedObjectPaths.Num() == 1)
    {
        TArray<FString> HandleParts;
        AddFoliageApply.CreatedObjectPaths[0].ParseIntoArray(HandleParts, TEXT("|"));
        if (HandleParts.Num() == 3)
        {
            AddedFoliageActorPath = HandleParts[0];
            AddedFoliageTypePath = HandleParts[1];
            AddedFoliageInstanceIndex = FCString::Atoi(*HandleParts[2]);
        }
    }
    TestFalse(TEXT("Foliage handle contains the exact IFA path"), AddedFoliageActorPath.IsEmpty());
    TestFalse(TEXT("Foliage handle contains the exact type path"), AddedFoliageTypePath.IsEmpty());
    TestTrue(TEXT("Foliage handle contains a valid instance index"), AddedFoliageInstanceIndex >= 0);

    const FString AddedFoliageKey =
        AddedFoliageActorPath + TEXT("|") + AddedFoliageTypePath;
    const bool bFoliageTypeExistedBefore = FoliageCountsBefore.Contains(AddedFoliageKey);
    FThomasEnvironmentInspectionResult EnvironmentAfterFoliageAdd =
        UThomasWorldToolset::InspectEnvironment(100);
    const FThomasFoliageRecord* AddedFoliageRecord =
        EnvironmentAfterFoliageAdd.Foliage.FindByPredicate(
            [&AddedFoliageActorPath, &AddedFoliageTypePath](const FThomasFoliageRecord& Record)
            {
                return Record.ActorPath == AddedFoliageActorPath
                    && Record.FoliageTypePath == AddedFoliageTypePath;
            });
    TestNotNull(TEXT("Foliage inspection returns the added type"), AddedFoliageRecord);
    const int32 FoliageCountBefore = FoliageCountsBefore.FindRef(AddedFoliageKey);
    if (AddedFoliageRecord)
    {
        TestEqual(
            TEXT("Foliage inspection reports the added instance"),
            AddedFoliageRecord->InstanceCount,
            FoliageCountBefore + 1);
        TestEqual(
            TEXT("Foliage inspection reports the Static Mesh source"),
            AddedFoliageRecord->SourcePath,
            FString(TEXT("/Engine/BasicShapes/Cube.Cube")));
    }

    const FThomasFoliageInstanceRecord* AddedInstance = AddedFoliageRecord
        ? AddedFoliageRecord->Instances.FindByPredicate(
            [AddedFoliageInstanceIndex](const FThomasFoliageInstanceRecord& Instance)
            {
                return Instance.Index == AddedFoliageInstanceIndex;
            })
        : nullptr;
    TestNotNull(TEXT("Foliage inspection returns the added instance transform"), AddedInstance);
    if (AddedInstance)
    {
        FThomasWorldPatchRequest MoveFoliageRequest;
        MoveFoliageRequest.MapPath = EnvironmentAfterFoliageAdd.MapPath;
        MoveFoliageRequest.ExpectedRevision = EnvironmentAfterFoliageAdd.Revision;
        FThomasWorldOperation MoveFoliage;
        MoveFoliage.Action = TEXT("set_foliage_instance_transform");
        MoveFoliage.ActorPath = AddedFoliageActorPath;
        MoveFoliage.AssetPath = AddedFoliageTypePath;
        MoveFoliage.InstanceIndex = AddedFoliageInstanceIndex;
        MoveFoliage.ExpectedValue = AddedInstance->Transform;
        MoveFoliage.LocationX = 17650.0f;
        MoveFoliage.LocationY = -9250.0f;
        MoveFoliage.LocationZ = 810.0f;
        MoveFoliage.RotationPitch = 5.0f;
        MoveFoliage.RotationYaw = 37.0f;
        MoveFoliage.RotationRoll = -3.0f;
        MoveFoliage.ScaleX = 0.4f;
        MoveFoliage.ScaleY = 0.5f;
        MoveFoliage.ScaleZ = 0.6f;
        MoveFoliageRequest.Operations.Add(MoveFoliage);
        const FThomasWorldPatchPlanResult MoveFoliagePlan =
            UThomasWorldToolset::PlanWorldPatch(MoveFoliageRequest);
        TestTrue(TEXT("Native foliage transform plan succeeds"), MoveFoliagePlan.bOk);
        const FThomasWorldPatchApplyResult MoveFoliageApply =
            UThomasWorldToolset::ApplyWorldPlan(MoveFoliagePlan.PlanId, false);
        AddInfo(FString::Printf(
            TEXT("Foliage transform apply: ok=%d code=%s message=%s rollback=%d"),
            MoveFoliageApply.bOk ? 1 : 0,
            *MoveFoliageApply.Code,
            *MoveFoliageApply.Message,
            MoveFoliageApply.bRolledBack ? 1 : 0));
        TestTrue(
            TEXT("Native foliage transform apply succeeds"),
            MoveFoliageApply.bOk);
    }

    FThomasEnvironmentInspectionResult EnvironmentAfterFoliageMove =
        UThomasWorldToolset::InspectEnvironment(100);
    const FThomasFoliageRecord* MovedFoliageRecord =
        EnvironmentAfterFoliageMove.Foliage.FindByPredicate(
            [&AddedFoliageActorPath, &AddedFoliageTypePath](const FThomasFoliageRecord& Record)
            {
                return Record.ActorPath == AddedFoliageActorPath
                    && Record.FoliageTypePath == AddedFoliageTypePath;
            });
    const FThomasFoliageInstanceRecord* MovedInstance = MovedFoliageRecord
        ? MovedFoliageRecord->Instances.FindByPredicate(
            [AddedFoliageInstanceIndex](const FThomasFoliageInstanceRecord& Instance)
            {
                return Instance.Index == AddedFoliageInstanceIndex;
            })
        : nullptr;
    TestNotNull(TEXT("Moved foliage instance remains inspectable"), MovedInstance);
    if (MovedInstance)
    {
        TestTrue(
            TEXT("Foliage transform mutation is exact"),
            FMath::IsNearlyEqual(MovedInstance->LocationX, 17650.0f, 0.1f)
                && FMath::IsNearlyEqual(MovedInstance->LocationY, -9250.0f, 0.1f)
                && FMath::IsNearlyEqual(MovedInstance->LocationZ, 810.0f, 0.1f));
        FThomasWorldPatchRequest RemoveFoliageInstanceRequest;
        RemoveFoliageInstanceRequest.MapPath = EnvironmentAfterFoliageMove.MapPath;
        RemoveFoliageInstanceRequest.ExpectedRevision = EnvironmentAfterFoliageMove.Revision;
        RemoveFoliageInstanceRequest.bConfirmDestructive = true;
        FThomasWorldOperation RemoveFoliageInstance;
        RemoveFoliageInstance.Action = TEXT("remove_foliage_instance");
        RemoveFoliageInstance.ActorPath = AddedFoliageActorPath;
        RemoveFoliageInstance.AssetPath = AddedFoliageTypePath;
        RemoveFoliageInstance.InstanceIndex = AddedFoliageInstanceIndex;
        RemoveFoliageInstance.ExpectedValue = MovedInstance->Transform;
        RemoveFoliageInstanceRequest.Operations.Add(RemoveFoliageInstance);
        const FThomasWorldPatchPlanResult RemoveFoliageInstancePlan =
            UThomasWorldToolset::PlanWorldPatch(RemoveFoliageInstanceRequest);
        TestTrue(
            TEXT("Native foliage instance removal plan succeeds"),
            RemoveFoliageInstancePlan.bOk);
        TestTrue(
            TEXT("Native foliage instance removal succeeds"),
            UThomasWorldToolset::ApplyWorldPlan(
                RemoveFoliageInstancePlan.PlanId, false).bOk);
    }

    FThomasEnvironmentInspectionResult EnvironmentAfterFoliageRemove =
        UThomasWorldToolset::InspectEnvironment(100);
    const FThomasFoliageRecord* RestoredFoliageRecord =
        EnvironmentAfterFoliageRemove.Foliage.FindByPredicate(
            [&AddedFoliageActorPath, &AddedFoliageTypePath](const FThomasFoliageRecord& Record)
            {
                return Record.ActorPath == AddedFoliageActorPath
                    && Record.FoliageTypePath == AddedFoliageTypePath;
            });
    TestTrue(
        TEXT("Foliage instance count returns to its baseline"),
        RestoredFoliageRecord
            && RestoredFoliageRecord->InstanceCount == FoliageCountBefore);

    if (!bFoliageTypeExistedBefore && RestoredFoliageRecord)
    {
        FThomasWorldPatchRequest RemoveFoliageTypeRequest;
        RemoveFoliageTypeRequest.MapPath = EnvironmentAfterFoliageRemove.MapPath;
        RemoveFoliageTypeRequest.ExpectedRevision = EnvironmentAfterFoliageRemove.Revision;
        RemoveFoliageTypeRequest.bConfirmDestructive = true;
        FThomasWorldOperation RemoveFoliageType;
        RemoveFoliageType.Action = TEXT("remove_foliage_type");
        RemoveFoliageType.ActorPath = AddedFoliageActorPath;
        RemoveFoliageType.AssetPath = AddedFoliageTypePath;
        RemoveFoliageType.ExpectedValue = FString::FromInt(RestoredFoliageRecord->InstanceCount);
        RemoveFoliageTypeRequest.Operations.Add(RemoveFoliageType);
        const FThomasWorldPatchPlanResult RemoveFoliageTypePlan =
            UThomasWorldToolset::PlanWorldPatch(RemoveFoliageTypeRequest);
        TestTrue(TEXT("Native foliage type removal plan succeeds"), RemoveFoliageTypePlan.bOk);
        TestTrue(
            TEXT("Native foliage type removal succeeds"),
            UThomasWorldToolset::ApplyWorldPlan(RemoveFoliageTypePlan.PlanId, false).bOk);
    }

    FThomasEnvironmentInspectionResult EnvironmentAfterFoliageTypeCleanup =
        UThomasWorldToolset::InspectEnvironment(100);
    if (!FoliageActorsBefore.Contains(AddedFoliageActorPath))
    {
        const bool bAddedFoliageActorIsEmpty =
            !EnvironmentAfterFoliageTypeCleanup.Foliage.ContainsByPredicate(
                [&AddedFoliageActorPath](const FThomasFoliageRecord& Record)
                {
                    return Record.ActorPath == AddedFoliageActorPath;
                });
        if (bAddedFoliageActorIsEmpty)
        {
            FThomasWorldPatchRequest DeleteEmptyFoliageActorRequest;
            DeleteEmptyFoliageActorRequest.MapPath =
                EnvironmentAfterFoliageTypeCleanup.MapPath;
            DeleteEmptyFoliageActorRequest.ExpectedRevision =
                EnvironmentAfterFoliageTypeCleanup.Revision;
            DeleteEmptyFoliageActorRequest.bConfirmDestructive = true;
            FThomasWorldOperation DeleteEmptyFoliageActor;
            DeleteEmptyFoliageActor.Action = TEXT("delete_actor");
            DeleteEmptyFoliageActor.ActorPath = AddedFoliageActorPath;
            DeleteEmptyFoliageActorRequest.Operations.Add(DeleteEmptyFoliageActor);
            const FThomasWorldPatchPlanResult DeleteEmptyFoliageActorPlan =
                UThomasWorldToolset::PlanWorldPatch(DeleteEmptyFoliageActorRequest);
            TestTrue(
                TEXT("Empty automation foliage actor cleanup plan succeeds"),
                DeleteEmptyFoliageActorPlan.bOk);
            TestTrue(
                TEXT("Empty automation foliage actor cleanup succeeds"),
                UThomasWorldToolset::ApplyWorldPlan(
                    DeleteEmptyFoliageActorPlan.PlanId, false).bOk);
        }
    }
    const FThomasEnvironmentInspectionResult EnvironmentAfterFoliageCleanup =
        UThomasWorldToolset::InspectEnvironment(100);
    TestEqual(
        TEXT("Foliage automation restores the baseline instance count"),
        EnvironmentAfterFoliageCleanup.FoliageInstanceCount,
        EnvironmentInspection.FoliageInstanceCount);
    TestEqual(
        TEXT("Foliage automation restores the baseline type count"),
        EnvironmentAfterFoliageCleanup.FoliageTypeCount,
        EnvironmentInspection.FoliageTypeCount);
    if (FoliageTypeAsset)
    {
        TestEqual(
            TEXT("Transient Foliage Type fixture cleanup succeeds"),
            ObjectTools::DeleteObjectsUnchecked({FoliageTypeAsset}),
            1);
    }

    const FThomasEnvironmentInspectionResult DataLayerBaseline =
        UThomasWorldToolset::InspectEnvironment(100);
    const FString ParentDataLayerLabel = TEXT("TE Environment Parent ")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
    FThomasWorldPatchRequest CreateParentDataLayerRequest;
    CreateParentDataLayerRequest.MapPath = DataLayerBaseline.MapPath;
    CreateParentDataLayerRequest.ExpectedRevision = DataLayerBaseline.Revision;
    CreateParentDataLayerRequest.bConfirmDestructive = true;
    FThomasWorldOperation CreateParentDataLayer;
    CreateParentDataLayer.Action = TEXT("create_private_data_layer");
    CreateParentDataLayer.Label = ParentDataLayerLabel;
    CreateParentDataLayerRequest.Operations.Add(CreateParentDataLayer);
    const FThomasWorldPatchPlanResult CreateParentDataLayerPlan =
        UThomasWorldToolset::PlanWorldPatch(CreateParentDataLayerRequest);
    TestTrue(TEXT("Private parent Data Layer creation plan succeeds"), CreateParentDataLayerPlan.bOk);
    const FThomasWorldPatchApplyResult CreateParentDataLayerApply =
        UThomasWorldToolset::ApplyWorldPlan(CreateParentDataLayerPlan.PlanId, false);
    TestTrue(TEXT("Private parent Data Layer creation succeeds"), CreateParentDataLayerApply.bOk);
    TestEqual(
        TEXT("Private parent Data Layer returns its exact object path"),
        CreateParentDataLayerApply.CreatedObjectPaths.Num(),
        1);
    const FString ParentDataLayerPath = CreateParentDataLayerApply.CreatedObjectPaths.IsEmpty()
        ? FString() : CreateParentDataLayerApply.CreatedObjectPaths[0];

    const FThomasEnvironmentInspectionResult AfterParentDataLayerCreate =
        UThomasWorldToolset::InspectEnvironment(100);
    const FString ChildDataLayerLabel = TEXT("TE Environment Child ")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
    FThomasWorldPatchRequest CreateChildDataLayerRequest;
    CreateChildDataLayerRequest.MapPath = AfterParentDataLayerCreate.MapPath;
    CreateChildDataLayerRequest.ExpectedRevision = AfterParentDataLayerCreate.Revision;
    CreateChildDataLayerRequest.bConfirmDestructive = true;
    FThomasWorldOperation CreateChildDataLayer;
    CreateChildDataLayer.Action = TEXT("create_private_data_layer");
    CreateChildDataLayer.Label = ChildDataLayerLabel;
    CreateChildDataLayerRequest.Operations.Add(CreateChildDataLayer);
    const FThomasWorldPatchPlanResult CreateChildDataLayerPlan =
        UThomasWorldToolset::PlanWorldPatch(CreateChildDataLayerRequest);
    TestTrue(TEXT("Private child Data Layer creation plan succeeds"), CreateChildDataLayerPlan.bOk);
    const FThomasWorldPatchApplyResult CreateChildDataLayerApply =
        UThomasWorldToolset::ApplyWorldPlan(CreateChildDataLayerPlan.PlanId, false);
    TestTrue(TEXT("Private child Data Layer creation succeeds"), CreateChildDataLayerApply.bOk);
    const FString ChildDataLayerPath = CreateChildDataLayerApply.CreatedObjectPaths.IsEmpty()
        ? FString() : CreateChildDataLayerApply.CreatedObjectPaths[0];

    const FThomasEnvironmentInspectionResult AfterChildDataLayerCreate =
        UThomasWorldToolset::InspectEnvironment(100);
    const FThomasDataLayerRecord* ChildDataLayerBefore =
        AfterChildDataLayerCreate.DataLayers.FindByPredicate(
            [&ChildDataLayerPath](const FThomasDataLayerRecord& Record)
            {
                return Record.ObjectPath == ChildDataLayerPath;
            });
    TestNotNull(TEXT("Created child Data Layer is inspectable"), ChildDataLayerBefore);
    const FString RenamedChildDataLayerLabel = ChildDataLayerLabel + TEXT(" Renamed");
    if (ChildDataLayerBefore)
    {
        FThomasWorldPatchRequest ConfigureChildDataLayerRequest;
        ConfigureChildDataLayerRequest.MapPath = AfterChildDataLayerCreate.MapPath;
        ConfigureChildDataLayerRequest.ExpectedRevision = AfterChildDataLayerCreate.Revision;
        ConfigureChildDataLayerRequest.bConfirmDestructive = true;
        FThomasWorldOperation SetDataLayerParent;
        SetDataLayerParent.Action = TEXT("set_data_layer_property");
        SetDataLayerParent.AssetPath = ChildDataLayerPath;
        SetDataLayerParent.PropertyName = TEXT("Parent");
        SetDataLayerParent.ExpectedValue = TEXT("");
        SetDataLayerParent.Value = ParentDataLayerPath;
        ConfigureChildDataLayerRequest.Operations.Add(SetDataLayerParent);
        FThomasWorldOperation RenameDataLayer;
        RenameDataLayer.Action = TEXT("set_data_layer_property");
        RenameDataLayer.AssetPath = ChildDataLayerPath;
        RenameDataLayer.PropertyName = TEXT("ShortName");
        RenameDataLayer.ExpectedValue = ChildDataLayerBefore->ShortName;
        RenameDataLayer.Value = RenamedChildDataLayerLabel;
        ConfigureChildDataLayerRequest.Operations.Add(RenameDataLayer);
        FThomasWorldOperation SetDataLayerVisible;
        SetDataLayerVisible.Action = TEXT("set_data_layer_property");
        SetDataLayerVisible.AssetPath = ChildDataLayerPath;
        SetDataLayerVisible.PropertyName = TEXT("Visible");
        SetDataLayerVisible.ExpectedValue = ChildDataLayerBefore->bVisible
            ? TEXT("true") : TEXT("false");
        SetDataLayerVisible.Value = ChildDataLayerBefore->bVisible
            ? TEXT("false") : TEXT("true");
        ConfigureChildDataLayerRequest.Operations.Add(SetDataLayerVisible);
        FThomasWorldOperation SetDataLayerInitiallyVisible;
        SetDataLayerInitiallyVisible.Action = TEXT("set_data_layer_property");
        SetDataLayerInitiallyVisible.AssetPath = ChildDataLayerPath;
        SetDataLayerInitiallyVisible.PropertyName = TEXT("InitiallyVisible");
        SetDataLayerInitiallyVisible.ExpectedValue = ChildDataLayerBefore->bInitiallyVisible
            ? TEXT("true") : TEXT("false");
        SetDataLayerInitiallyVisible.Value = ChildDataLayerBefore->bInitiallyVisible
            ? TEXT("false") : TEXT("true");
        ConfigureChildDataLayerRequest.Operations.Add(SetDataLayerInitiallyVisible);
        const FThomasWorldPatchPlanResult ConfigureChildDataLayerPlan =
            UThomasWorldToolset::PlanWorldPatch(ConfigureChildDataLayerRequest);
        TestTrue(TEXT("Private Data Layer hierarchy/state plan succeeds"), ConfigureChildDataLayerPlan.bOk);
        TestTrue(
            TEXT("Private Data Layer hierarchy/state apply succeeds"),
            UThomasWorldToolset::ApplyWorldPlan(
                ConfigureChildDataLayerPlan.PlanId, false).bOk);
    }

    const FThomasEnvironmentInspectionResult AfterChildDataLayerConfigure =
        UThomasWorldToolset::InspectEnvironment(100);
    const FThomasDataLayerRecord* ChildDataLayerAfter =
        AfterChildDataLayerConfigure.DataLayers.FindByPredicate(
            [&ChildDataLayerPath](const FThomasDataLayerRecord& Record)
            {
                return Record.ObjectPath == ChildDataLayerPath;
            });
    TestTrue(
        TEXT("Private Data Layer hierarchy/state mutations are exact"),
        ChildDataLayerBefore && ChildDataLayerAfter
            && ChildDataLayerAfter->ShortName == RenamedChildDataLayerLabel
            && !ChildDataLayerAfter->ParentName.IsEmpty()
            && ChildDataLayerAfter->bVisible != ChildDataLayerBefore->bVisible
            && ChildDataLayerAfter->bInitiallyVisible
                != ChildDataLayerBefore->bInitiallyVisible);

    FThomasWorldPatchRequest SpawnDataLayerActorRequest;
    SpawnDataLayerActorRequest.MapPath = AfterChildDataLayerConfigure.MapPath;
    SpawnDataLayerActorRequest.ExpectedRevision = AfterChildDataLayerConfigure.Revision;
    SpawnDataLayerActorRequest.bConfirmDestructive = true;
    FThomasWorldOperation SpawnDataLayerActor;
    SpawnDataLayerActor.Action = TEXT("spawn_actor");
    SpawnDataLayerActor.ClassPath = TEXT("/Script/Engine.Actor");
    SpawnDataLayerActor.Label = TEXT("TE Data Layer Assignment Proof");
    SpawnDataLayerActorRequest.Operations.Add(SpawnDataLayerActor);
    const FThomasWorldPatchPlanResult SpawnDataLayerActorPlan =
        UThomasWorldToolset::PlanWorldPatch(SpawnDataLayerActorRequest);
    TestTrue(TEXT("Data Layer proof actor spawn plan succeeds"), SpawnDataLayerActorPlan.bOk);
    const FThomasWorldPatchApplyResult SpawnDataLayerActorApply =
        UThomasWorldToolset::ApplyWorldPlan(SpawnDataLayerActorPlan.PlanId, false);
    TestTrue(TEXT("Data Layer proof actor spawn succeeds"), SpawnDataLayerActorApply.bOk);
    const FString DataLayerActorPath = SpawnDataLayerActorApply.CreatedActorPaths.IsEmpty()
        ? FString() : SpawnDataLayerActorApply.CreatedActorPaths[0];

    const FThomasWorldInspectionResult BeforeDataLayerAssignment =
        UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
    FThomasWorldPatchRequest AddActorToDataLayerRequest;
    AddActorToDataLayerRequest.MapPath = BeforeDataLayerAssignment.MapPath;
    AddActorToDataLayerRequest.ExpectedRevision = BeforeDataLayerAssignment.Revision;
    AddActorToDataLayerRequest.bConfirmDestructive = true;
    FThomasWorldOperation AddActorToDataLayer;
    AddActorToDataLayer.Action = TEXT("add_actor_to_data_layer");
    AddActorToDataLayer.ActorPath = DataLayerActorPath;
    AddActorToDataLayer.AssetPath = ChildDataLayerPath;
    AddActorToDataLayer.ExpectedValue = TEXT("false");
    AddActorToDataLayerRequest.Operations.Add(AddActorToDataLayer);
    const FThomasWorldPatchPlanResult AddActorToDataLayerPlan =
        UThomasWorldToolset::PlanWorldPatch(AddActorToDataLayerRequest);
    TestTrue(TEXT("Actor to Data Layer assignment plan succeeds"), AddActorToDataLayerPlan.bOk);
    TestTrue(
        TEXT("Actor to Data Layer assignment succeeds"),
        UThomasWorldToolset::ApplyWorldPlan(AddActorToDataLayerPlan.PlanId, false).bOk);

    const FThomasEnvironmentInspectionResult AfterDataLayerAssignment =
        UThomasWorldToolset::InspectEnvironment(100);
    const FThomasDataLayerRecord* AssignedDataLayer =
        AfterDataLayerAssignment.DataLayers.FindByPredicate(
            [&ChildDataLayerPath](const FThomasDataLayerRecord& Record)
            {
                return Record.ObjectPath == ChildDataLayerPath;
            });
    TestTrue(
        TEXT("Data Layer inspection reports assigned actor count"),
        AssignedDataLayer && AssignedDataLayer->ActorCount == 1);

    const FThomasWorldInspectionResult BeforeDataLayerUnassignment =
        UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
    FThomasWorldPatchRequest RemoveActorFromDataLayerRequest;
    RemoveActorFromDataLayerRequest.MapPath = BeforeDataLayerUnassignment.MapPath;
    RemoveActorFromDataLayerRequest.ExpectedRevision = BeforeDataLayerUnassignment.Revision;
    RemoveActorFromDataLayerRequest.bConfirmDestructive = true;
    FThomasWorldOperation RemoveActorFromDataLayer;
    RemoveActorFromDataLayer.Action = TEXT("remove_actor_from_data_layer");
    RemoveActorFromDataLayer.ActorPath = DataLayerActorPath;
    RemoveActorFromDataLayer.AssetPath = ChildDataLayerPath;
    RemoveActorFromDataLayer.ExpectedValue = TEXT("true");
    RemoveActorFromDataLayerRequest.Operations.Add(RemoveActorFromDataLayer);
    const FThomasWorldPatchPlanResult RemoveActorFromDataLayerPlan =
        UThomasWorldToolset::PlanWorldPatch(RemoveActorFromDataLayerRequest);
    TestTrue(TEXT("Actor from Data Layer removal plan succeeds"), RemoveActorFromDataLayerPlan.bOk);
    TestTrue(
        TEXT("Actor from Data Layer removal succeeds"),
        UThomasWorldToolset::ApplyWorldPlan(
            RemoveActorFromDataLayerPlan.PlanId, false).bOk);

    const FThomasWorldInspectionResult BeforeDataLayerActorDelete =
        UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
    FThomasWorldPatchRequest DeleteDataLayerActorRequest;
    DeleteDataLayerActorRequest.MapPath = BeforeDataLayerActorDelete.MapPath;
    DeleteDataLayerActorRequest.ExpectedRevision = BeforeDataLayerActorDelete.Revision;
    DeleteDataLayerActorRequest.bConfirmDestructive = true;
    FThomasWorldOperation DeleteDataLayerActor;
    DeleteDataLayerActor.Action = TEXT("delete_actor");
    DeleteDataLayerActor.ActorPath = DataLayerActorPath;
    DeleteDataLayerActorRequest.Operations.Add(DeleteDataLayerActor);
    const FThomasWorldPatchPlanResult DeleteDataLayerActorPlan =
        UThomasWorldToolset::PlanWorldPatch(DeleteDataLayerActorRequest);
    TestTrue(TEXT("Data Layer proof actor cleanup plan succeeds"), DeleteDataLayerActorPlan.bOk);
    TestTrue(
        TEXT("Data Layer proof actor cleanup succeeds"),
        UThomasWorldToolset::ApplyWorldPlan(DeleteDataLayerActorPlan.PlanId, false).bOk);

    FThomasEnvironmentInspectionResult BeforeChildDataLayerDelete =
        UThomasWorldToolset::InspectEnvironment(100);
    FThomasWorldPatchRequest DeleteChildDataLayerRequest;
    DeleteChildDataLayerRequest.MapPath = BeforeChildDataLayerDelete.MapPath;
    DeleteChildDataLayerRequest.ExpectedRevision = BeforeChildDataLayerDelete.Revision;
    DeleteChildDataLayerRequest.bConfirmDestructive = true;
    FThomasWorldOperation DeleteChildDataLayer;
    DeleteChildDataLayer.Action = TEXT("delete_data_layer");
    DeleteChildDataLayer.AssetPath = ChildDataLayerPath;
    DeleteChildDataLayer.ExpectedValue = TEXT("0");
    DeleteChildDataLayerRequest.Operations.Add(DeleteChildDataLayer);
    const FThomasWorldPatchPlanResult DeleteChildDataLayerPlan =
        UThomasWorldToolset::PlanWorldPatch(DeleteChildDataLayerRequest);
    TestTrue(TEXT("Private child Data Layer cleanup plan succeeds"), DeleteChildDataLayerPlan.bOk);
    TestTrue(
        TEXT("Private child Data Layer cleanup succeeds"),
        UThomasWorldToolset::ApplyWorldPlan(DeleteChildDataLayerPlan.PlanId, false).bOk);

    FThomasEnvironmentInspectionResult BeforeParentDataLayerDelete =
        UThomasWorldToolset::InspectEnvironment(100);
    FThomasWorldPatchRequest DeleteParentDataLayerRequest;
    DeleteParentDataLayerRequest.MapPath = BeforeParentDataLayerDelete.MapPath;
    DeleteParentDataLayerRequest.ExpectedRevision = BeforeParentDataLayerDelete.Revision;
    DeleteParentDataLayerRequest.bConfirmDestructive = true;
    FThomasWorldOperation DeleteParentDataLayer;
    DeleteParentDataLayer.Action = TEXT("delete_data_layer");
    DeleteParentDataLayer.AssetPath = ParentDataLayerPath;
    DeleteParentDataLayer.ExpectedValue = TEXT("0");
    DeleteParentDataLayerRequest.Operations.Add(DeleteParentDataLayer);
    const FThomasWorldPatchPlanResult DeleteParentDataLayerPlan =
        UThomasWorldToolset::PlanWorldPatch(DeleteParentDataLayerRequest);
    TestTrue(TEXT("Private parent Data Layer cleanup plan succeeds"), DeleteParentDataLayerPlan.bOk);
    TestTrue(
        TEXT("Private parent Data Layer cleanup succeeds"),
        UThomasWorldToolset::ApplyWorldPlan(DeleteParentDataLayerPlan.PlanId, false).bOk);

    const FThomasEnvironmentInspectionResult AfterDataLayerCleanup =
        UThomasWorldToolset::InspectEnvironment(100);
    TestEqual(
        TEXT("Data Layer automation restores the baseline count"),
        AfterDataLayerCleanup.DataLayerCount,
        DataLayerBaseline.DataLayerCount);
    TestFalse(
        TEXT("Data Layer automation leaves no child instance"),
        AfterDataLayerCleanup.DataLayers.ContainsByPredicate(
            [&ChildDataLayerPath](const FThomasDataLayerRecord& Record)
            {
                return Record.ObjectPath == ChildDataLayerPath;
            }));

    const FThomasEnvironmentInspectionResult LandscapeBaseline =
        UThomasWorldToolset::InspectEnvironment(100);
    FThomasWorldPatchRequest UnsafeLandscapeSpawnRequest;
    UnsafeLandscapeSpawnRequest.MapPath = LandscapeBaseline.MapPath;
    UnsafeLandscapeSpawnRequest.ExpectedRevision = LandscapeBaseline.Revision;
    UnsafeLandscapeSpawnRequest.bConfirmDestructive = true;
    FThomasWorldOperation UnsafeLandscapeSpawn;
    UnsafeLandscapeSpawn.Action = TEXT("spawn_actor");
    UnsafeLandscapeSpawn.ClassPath = TEXT("/Script/Landscape.Landscape");
    UnsafeLandscapeSpawnRequest.Operations.Add(UnsafeLandscapeSpawn);
    TestEqual(
        TEXT("Generic spawn refuses invalid raw Landscape actors"),
        UThomasWorldToolset::PlanWorldPatch(UnsafeLandscapeSpawnRequest).Code,
        FString(TEXT("landscape_requires_typed_create")));
    FThomasWorldPatchRequest SpawnLandscapeRequest;
    SpawnLandscapeRequest.MapPath = LandscapeBaseline.MapPath;
    SpawnLandscapeRequest.ExpectedRevision = LandscapeBaseline.Revision;
    SpawnLandscapeRequest.bConfirmDestructive = true;
    FThomasWorldOperation SpawnLandscape;
    SpawnLandscape.Action = TEXT("create_landscape");
    SpawnLandscape.Label = TEXT("TE Native Landscape Proof");
    SpawnLandscape.LandscapeComponentCountX = 1;
    SpawnLandscape.LandscapeComponentCountY = 1;
    SpawnLandscape.LandscapeSectionsPerComponent = 1;
    SpawnLandscape.LandscapeQuadsPerSection = 63;
    SpawnLandscape.LandscapeHeightValue = 32768;
    SpawnLandscapeRequest.Operations.Add(SpawnLandscape);
    const FThomasWorldPatchPlanResult SpawnLandscapePlan =
        UThomasWorldToolset::PlanWorldPatch(SpawnLandscapeRequest);
    TestTrue(TEXT("Native empty Landscape spawn plan succeeds"), SpawnLandscapePlan.bOk);
    const FThomasWorldPatchApplyResult SpawnLandscapeApply =
        UThomasWorldToolset::ApplyWorldPlan(SpawnLandscapePlan.PlanId, false);
    TestTrue(TEXT("Native empty Landscape spawn succeeds"), SpawnLandscapeApply.bOk);
    TestEqual(TEXT("Native Landscape actor path returned"), SpawnLandscapeApply.CreatedActorPaths.Num(), 1);
    const FString LandscapeActorPath = SpawnLandscapeApply.CreatedActorPaths.IsEmpty()
        ? FString() : SpawnLandscapeApply.CreatedActorPaths[0];

    FThomasLandscapeRegionInspectionResult HeightRegion =
        UThomasWorldToolset::InspectLandscapeRegion(
            LandscapeActorPath, 0, 0, 1, 1, TEXT(""), TEXT(""), 16);
    TestTrue(TEXT("Landscape height region inspection succeeds"), HeightRegion.bOk);
    TestTrue(
        TEXT("Landscape flat import exposes exact height samples/hash"),
        HeightRegion.SampleCount == 4
            && HeightRegion.HeightSamples.Num() == 4
            && HeightRegion.MinHeightValue == 32768
            && HeightRegion.MaxHeightValue == 32768
            && !HeightRegion.HeightDataHash.IsEmpty());

    FThomasWorldPatchRequest StaleHeightRequest;
    StaleHeightRequest.MapPath = HeightRegion.MapPath;
    StaleHeightRequest.ExpectedRevision = HeightRegion.Revision;
    StaleHeightRequest.bConfirmDestructive = true;
    FThomasWorldOperation StaleHeightOperation;
    StaleHeightOperation.Action = TEXT("set_landscape_height_region");
    StaleHeightOperation.ActorPath = LandscapeActorPath;
    StaleHeightOperation.LandscapeMinX = 0;
    StaleHeightOperation.LandscapeMinY = 0;
    StaleHeightOperation.LandscapeMaxX = 1;
    StaleHeightOperation.LandscapeMaxY = 1;
    StaleHeightOperation.ExpectedValue = TEXT("stale-hash");
    StaleHeightOperation.LandscapeValues = {32768, 32768, 32768, 32768};
    StaleHeightRequest.Operations.Add(StaleHeightOperation);
    TestEqual(
        TEXT("Landscape height mutation rejects a stale region hash"),
        UThomasWorldToolset::PlanWorldPatch(StaleHeightRequest).Code,
        FString(TEXT("landscape_data_hash_conflict")));

    FThomasWorldPatchRequest SetHeightRequest;
    SetHeightRequest.MapPath = HeightRegion.MapPath;
    SetHeightRequest.ExpectedRevision = HeightRegion.Revision;
    SetHeightRequest.bConfirmDestructive = true;
    FThomasWorldOperation SetHeightOperation;
    SetHeightOperation.Action = TEXT("set_landscape_height_region");
    SetHeightOperation.ActorPath = LandscapeActorPath;
    SetHeightOperation.LandscapeMinX = 0;
    SetHeightOperation.LandscapeMinY = 0;
    SetHeightOperation.LandscapeMaxX = 1;
    SetHeightOperation.LandscapeMaxY = 1;
    SetHeightOperation.ExpectedValue = HeightRegion.HeightDataHash;
    SetHeightOperation.LandscapeValues = {32768, 32769, 32770, 32771};
    SetHeightRequest.Operations.Add(SetHeightOperation);
    const FThomasWorldPatchPlanResult SetHeightPlan =
        UThomasWorldToolset::PlanWorldPatch(SetHeightRequest);
    TestTrue(TEXT("Exact Landscape height plan succeeds"), SetHeightPlan.bOk);
    TestEqual(
        TEXT("Landscape data mutation is guarded R2"),
        SetHeightPlan.Risk,
        FString(TEXT("R2")));
    TestTrue(
        TEXT("Exact Landscape height apply succeeds"),
        UThomasWorldToolset::ApplyWorldPlan(SetHeightPlan.PlanId, false).bOk);
    HeightRegion = UThomasWorldToolset::InspectLandscapeRegion(
        LandscapeActorPath, 0, 0, 1, 1, TEXT(""), TEXT(""), 16);
    TestTrue(
        TEXT("Exact Landscape heights round-trip"),
        HeightRegion.bOk
            && HeightRegion.HeightSamples == TArray<int32>({32768, 32769, 32770, 32771}));

    const FString LandscapeImportDirectory = FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("ThomasEditor/Imports/Landscape"));
    IFileManager::Get().MakeDirectory(*LandscapeImportDirectory, true);
    const FString HeightImportFilename = FPaths::Combine(
        LandscapeImportDirectory, TEXT("ThomasEditorAutomationHeight.r16"));
    const TArray<int32> ImportedHeights = {33000, 33001, 33002, 33003};
    TArray<uint8> HeightImportBytes;
    for (const int32 HeightValue : ImportedHeights)
    {
        HeightImportBytes.Add(static_cast<uint8>(HeightValue & 0xff));
        HeightImportBytes.Add(static_cast<uint8>((HeightValue >> 8) & 0xff));
    }
    TestTrue(
        TEXT("Guarded R16 fixture is written"),
        FFileHelper::SaveArrayToFile(HeightImportBytes, *HeightImportFilename));
    FThomasWorldPatchRequest ImportHeightRequest;
    ImportHeightRequest.MapPath = HeightRegion.MapPath;
    ImportHeightRequest.ExpectedRevision = HeightRegion.Revision;
    ImportHeightRequest.bConfirmDestructive = true;
    FThomasWorldOperation ImportHeightOperation;
    ImportHeightOperation.Action = TEXT("import_landscape_height_r16");
    ImportHeightOperation.ActorPath = LandscapeActorPath;
    ImportHeightOperation.LandscapeMinX = 0;
    ImportHeightOperation.LandscapeMinY = 0;
    ImportHeightOperation.LandscapeMaxX = 1;
    ImportHeightOperation.LandscapeMaxY = 1;
    ImportHeightOperation.ExpectedValue = HeightRegion.HeightDataHash;
    ImportHeightOperation.SourceFile = TEXT("ThomasEditorAutomationHeight.r16");
    ImportHeightRequest.Operations.Add(ImportHeightOperation);
    const FThomasWorldPatchPlanResult ImportHeightPlan =
        UThomasWorldToolset::PlanWorldPatch(ImportHeightRequest);
    TestTrue(TEXT("Landscape R16 import plan succeeds"), ImportHeightPlan.bOk);
    TestTrue(
        TEXT("Landscape R16 import apply succeeds"),
        UThomasWorldToolset::ApplyWorldPlan(ImportHeightPlan.PlanId, false).bOk);
    HeightRegion = UThomasWorldToolset::InspectLandscapeRegion(
        LandscapeActorPath, 0, 0, 1, 1, TEXT(""), TEXT(""), 16);
    TestTrue(
        TEXT("Landscape R16 import round-trips little-endian heights"),
        HeightRegion.bOk && HeightRegion.HeightSamples == ImportedHeights);

    FThomasLandscapeRegionInspectionResult SculptRegion =
        UThomasWorldToolset::InspectLandscapeRegion(
            LandscapeActorPath, 2, 2, 6, 6, TEXT(""), TEXT(""), 25);
    const int32 SculptCenterBefore = SculptRegion.HeightSamples.IsValidIndex(12)
        ? SculptRegion.HeightSamples[12] : 0;
    FThomasWorldPatchRequest SculptRequest;
    SculptRequest.MapPath = SculptRegion.MapPath;
    SculptRequest.ExpectedRevision = SculptRegion.Revision;
    SculptRequest.bConfirmDestructive = true;
    FThomasWorldOperation SculptOperation;
    SculptOperation.Action = TEXT("sculpt_landscape_height");
    SculptOperation.ActorPath = LandscapeActorPath;
    SculptOperation.ExpectedValue = SculptRegion.HeightDataHash;
    SculptOperation.LandscapeBrushCenterX = 4;
    SculptOperation.LandscapeBrushCenterY = 4;
    SculptOperation.LandscapeBrushRadius = 2;
    SculptOperation.LandscapeBrushStrength = 128;
    SculptOperation.LandscapeBrushFalloff = 0.5f;
    SculptRequest.Operations.Add(SculptOperation);
    const FThomasWorldPatchPlanResult SculptPlan =
        UThomasWorldToolset::PlanWorldPatch(SculptRequest);
    TestTrue(TEXT("Landscape sculpt brush plan succeeds"), SculptPlan.bOk);
    TestTrue(
        TEXT("Landscape sculpt brush apply succeeds"),
        UThomasWorldToolset::ApplyWorldPlan(SculptPlan.PlanId, false).bOk);
    SculptRegion = UThomasWorldToolset::InspectLandscapeRegion(
        LandscapeActorPath, 2, 2, 6, 6, TEXT(""), TEXT(""), 25);
    TestTrue(
        TEXT("Landscape sculpt brush raises its exact center"),
        SculptRegion.bOk
            && SculptRegion.HeightSamples.IsValidIndex(12)
            && SculptRegion.HeightSamples[12] == SculptCenterBefore + 128);

    FThomasEnvironmentInspectionResult PaintLayerEnvironment =
        UThomasWorldToolset::InspectEnvironment(100);
    const FThomasLandscapeRecord* PaintLayerLandscape =
        PaintLayerEnvironment.Landscapes.FindByPredicate(
            [&LandscapeActorPath](const FThomasLandscapeRecord& Record)
            {
                return Record.ActorPath == LandscapeActorPath;
            });
    FThomasWorldPatchRequest CreatePaintEditLayerRequest;
    CreatePaintEditLayerRequest.MapPath = PaintLayerEnvironment.MapPath;
    CreatePaintEditLayerRequest.ExpectedRevision = PaintLayerEnvironment.Revision;
    CreatePaintEditLayerRequest.bConfirmDestructive = true;
    FThomasWorldOperation CreatePaintEditLayer;
    CreatePaintEditLayer.Action = TEXT("create_landscape_edit_layer");
    CreatePaintEditLayer.ActorPath = LandscapeActorPath;
    CreatePaintEditLayer.Label = TEXT("ThomasPaintLayer");
    CreatePaintEditLayer.ExpectedValue = PaintLayerLandscape
        ? FString::FromInt(PaintLayerLandscape->EditLayerCount) : FString();
    CreatePaintEditLayerRequest.Operations.Add(CreatePaintEditLayer);
    const FThomasWorldPatchPlanResult CreatePaintEditLayerPlan =
        UThomasWorldToolset::PlanWorldPatch(CreatePaintEditLayerRequest);
    TestTrue(
        TEXT("Landscape paint edit layer plan succeeds"),
        CreatePaintEditLayerPlan.bOk);
    TestTrue(
        TEXT("Landscape paint edit layer creation succeeds"),
        UThomasWorldToolset::ApplyWorldPlan(
            CreatePaintEditLayerPlan.PlanId, false).bOk);
    PaintLayerEnvironment = UThomasWorldToolset::InspectEnvironment(100);
    PaintLayerLandscape = PaintLayerEnvironment.Landscapes.FindByPredicate(
        [&LandscapeActorPath](const FThomasLandscapeRecord& Record)
        {
            return Record.ActorPath == LandscapeActorPath;
        });
    FString PaintEditLayerGuid;
    if (PaintLayerLandscape)
    {
        if (const FThomasLandscapeEditLayerRecord* PaintEditLayer =
                PaintLayerLandscape->EditLayers.FindByPredicate(
                    [](const FThomasLandscapeEditLayerRecord& Layer)
                    {
                        return Layer.Name == TEXT("ThomasPaintLayer");
                    }))
        {
            PaintEditLayerGuid = PaintEditLayer->Guid;
        }
    }
    TestFalse(
        TEXT("Landscape paint edit layer exposes an exact GUID"),
        PaintEditLayerGuid.IsEmpty());

    const FString LandscapePaintTargetName = TEXT("ThomasPaintTarget");
    const FString LandscapePaintMaterialPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/M_TE_LandscapePaint_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString LandscapePaintMaterialObjectPath = LandscapePaintMaterialPath
        + TEXT(".") + FPackageName::GetLongPackageAssetName(LandscapePaintMaterialPath);
    FThomasMaterialPatchRequest LandscapePaintMaterialRequest;
    LandscapePaintMaterialRequest.AssetPath = LandscapePaintMaterialPath;
    LandscapePaintMaterialRequest.ExpectedRevision = TEXT("missing");
    LandscapePaintMaterialRequest.bCreateIfMissing = true;
    FThomasMaterialOperation CreateLandscapeLayerSample;
    CreateLandscapeLayerSample.Action = TEXT("create_expression");
    CreateLandscapeLayerSample.Handle = TEXT("paint_target");
    CreateLandscapeLayerSample.ClassPath =
        TEXT("/Script/Landscape.MaterialExpressionLandscapeLayerSample");
    CreateLandscapeLayerSample.PositionX = -300;
    LandscapePaintMaterialRequest.Operations.Add(CreateLandscapeLayerSample);
    FThomasMaterialOperation NameLandscapeLayerSample;
    NameLandscapeLayerSample.Action = TEXT("set_expression_property");
    NameLandscapeLayerSample.Handle = TEXT("paint_target");
    NameLandscapeLayerSample.PropertyName = TEXT("ParameterName");
    NameLandscapeLayerSample.Value = LandscapePaintTargetName;
    LandscapePaintMaterialRequest.Operations.Add(NameLandscapeLayerSample);
    FThomasMaterialOperation ConnectLandscapeLayerSample;
    ConnectLandscapeLayerSample.Action = TEXT("connect_material_property");
    ConnectLandscapeLayerSample.Handle = TEXT("paint_target");
    ConnectLandscapeLayerSample.MaterialProperty = TEXT("Roughness");
    LandscapePaintMaterialRequest.Operations.Add(ConnectLandscapeLayerSample);
    const FThomasMaterialPlanResult LandscapePaintMaterialPlan =
        UThomasMaterialsToolset::PlanMaterialPatch(LandscapePaintMaterialRequest);
    TestTrue(
        TEXT("Landscape target-layer material creation plan succeeds"),
        LandscapePaintMaterialPlan.bOk);
    const FThomasMaterialApplyResult LandscapePaintMaterialApply =
        UThomasMaterialsToolset::ApplyMaterialPlan(
            LandscapePaintMaterialPlan.PlanId, true, true);
    TestTrue(
        TEXT("Landscape target-layer material create/compile/save succeeds"),
        LandscapePaintMaterialApply.bOk);

    const FString LandscapeLayerInfoPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/LI_TE_LandscapePaint_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FThomasLandscapeLayerInfoCreateRequest LandscapeLayerInfoRequest;
    LandscapeLayerInfoRequest.AssetPath = LandscapeLayerInfoPath;
    LandscapeLayerInfoRequest.ExpectedRevision = TEXT("missing");
    LandscapeLayerInfoRequest.LayerName = LandscapePaintTargetName;
    LandscapeLayerInfoRequest.bNoWeightBlend = true;
    const FThomasLandscapeLayerInfoCreatePlanResult LandscapeLayerInfoPlan =
        UThomasWorldToolset::PlanLandscapeLayerInfoCreate(
            LandscapeLayerInfoRequest);
    TestTrue(
        TEXT("Native Landscape LayerInfo creation plan succeeds"),
        LandscapeLayerInfoPlan.bOk);
    const FThomasLandscapeLayerInfoCreateApplyResult LandscapeLayerInfoApply =
        UThomasWorldToolset::ApplyLandscapeLayerInfoCreate(
            LandscapeLayerInfoPlan.PlanId);
    TestTrue(
        TEXT("Native Landscape LayerInfo create/save succeeds"),
        LandscapeLayerInfoApply.bOk
            && LandscapeLayerInfoApply.bCreated
            && LandscapeLayerInfoApply.bSaved);
    const FString LandscapeLayerInfoObjectPath = LandscapeLayerInfoPath
        + TEXT(".") + FPackageName::GetLongPackageAssetName(LandscapeLayerInfoPath);
    const FThomasObjectDetailsResult LandscapeLayerInfoDetails =
        UThomasAssetsToolset::GetObjectDetails(
            LandscapeLayerInfoObjectPath,
            {TEXT("LayerName"), TEXT("BlendMethod")},
            10);
    TestTrue(
        TEXT("Created Landscape LayerInfo exposes native Details"),
        LandscapeLayerInfoDetails.bOk);

    PaintLayerEnvironment = UThomasWorldToolset::InspectEnvironment(100);
    PaintLayerLandscape = PaintLayerEnvironment.Landscapes.FindByPredicate(
        [&LandscapeActorPath](const FThomasLandscapeRecord& Record)
        {
            return Record.ActorPath == LandscapeActorPath;
        });
    FThomasWorldPatchRequest SetPaintMaterialRequest;
    SetPaintMaterialRequest.MapPath = PaintLayerEnvironment.MapPath;
    SetPaintMaterialRequest.ExpectedRevision = PaintLayerEnvironment.Revision;
    FThomasWorldOperation SetPaintMaterial;
    SetPaintMaterial.Action = TEXT("set_landscape_material");
    SetPaintMaterial.ActorPath = LandscapeActorPath;
    SetPaintMaterial.ExpectedValue = PaintLayerLandscape
        ? PaintLayerLandscape->MaterialPath : FString();
    SetPaintMaterial.Value = LandscapePaintMaterialObjectPath;
    SetPaintMaterialRequest.Operations.Add(SetPaintMaterial);
    const FThomasWorldPatchPlanResult SetPaintMaterialPlan =
        UThomasWorldToolset::PlanWorldPatch(SetPaintMaterialRequest);
    TestTrue(
        TEXT("Landscape target-layer material assignment plan succeeds"),
        SetPaintMaterialPlan.bOk);
    TestTrue(
        TEXT("Landscape target-layer material assignment succeeds"),
        UThomasWorldToolset::ApplyWorldPlan(
            SetPaintMaterialPlan.PlanId, false).bOk);

    FThomasLandscapeRegionInspectionResult PaintRegion =
        UThomasWorldToolset::InspectLandscapeRegion(
            LandscapeActorPath,
            8,
            8,
            10,
            10,
            PaintEditLayerGuid,
            LandscapeLayerInfoObjectPath,
            9);
    TestTrue(TEXT("Landscape weight region inspection succeeds"), PaintRegion.bOk);
    FThomasWorldPatchRequest PaintRequest;
    PaintRequest.MapPath = PaintRegion.MapPath;
    PaintRequest.ExpectedRevision = PaintRegion.Revision;
    PaintRequest.bConfirmDestructive = true;
    FThomasWorldOperation PaintOperation;
    PaintOperation.Action = TEXT("paint_landscape_weight");
    PaintOperation.ActorPath = LandscapeActorPath;
    PaintOperation.AssetPath = LandscapeLayerInfoObjectPath;
    PaintOperation.ExpectedValue = PaintRegion.WeightDataHash;
    PaintOperation.LandscapeEditLayerGuid = PaintEditLayerGuid;
    PaintOperation.LandscapeBrushCenterX = 9;
    PaintOperation.LandscapeBrushCenterY = 9;
    PaintOperation.LandscapeBrushRadius = 1;
    PaintOperation.LandscapeBrushStrength = 255;
    PaintOperation.LandscapeBrushFalloff = 0.0f;
    PaintRequest.Operations.Add(PaintOperation);
    const FThomasWorldPatchPlanResult PaintPlan =
        UThomasWorldToolset::PlanWorldPatch(PaintRequest);
    TestTrue(TEXT("Landscape paint brush plan succeeds"), PaintPlan.bOk);
    const FThomasWorldPatchApplyResult PaintApply =
        UThomasWorldToolset::ApplyWorldPlan(PaintPlan.PlanId, false);
    if (!PaintApply.bOk)
    {
        AddInfo(TEXT("Landscape paint apply: code=") + PaintApply.Code
            + TEXT(" message=") + PaintApply.Message);
    }
    TestTrue(
        TEXT("Landscape paint brush apply succeeds"),
        PaintApply.bOk);
    PaintRegion = UThomasWorldToolset::InspectLandscapeRegion(
        LandscapeActorPath,
        8,
        8,
        10,
        10,
        PaintEditLayerGuid,
        LandscapeLayerInfoObjectPath,
        9);
    TestTrue(
        TEXT("Landscape paint brush writes the exact center weight"),
        PaintRegion.bOk
            && PaintRegion.WeightSamples.IsValidIndex(4)
            && PaintRegion.WeightSamples[4] == 255);

    const FString WeightImportFilename = FPaths::Combine(
        LandscapeImportDirectory, TEXT("ThomasEditorAutomationWeight.r8"));
    TArray<uint8> WeightImportBytes;
    WeightImportBytes.Init(64, 9);
    TestTrue(
        TEXT("Guarded R8 fixture is written"),
        FFileHelper::SaveArrayToFile(WeightImportBytes, *WeightImportFilename));
    FThomasWorldPatchRequest ImportWeightRequest;
    ImportWeightRequest.MapPath = PaintRegion.MapPath;
    ImportWeightRequest.ExpectedRevision = PaintRegion.Revision;
    ImportWeightRequest.bConfirmDestructive = true;
    FThomasWorldOperation ImportWeightOperation;
    ImportWeightOperation.Action = TEXT("import_landscape_weight_r8");
    ImportWeightOperation.ActorPath = LandscapeActorPath;
    ImportWeightOperation.AssetPath = LandscapeLayerInfoObjectPath;
    ImportWeightOperation.LandscapeMinX = 8;
    ImportWeightOperation.LandscapeMinY = 8;
    ImportWeightOperation.LandscapeMaxX = 10;
    ImportWeightOperation.LandscapeMaxY = 10;
    ImportWeightOperation.LandscapeEditLayerGuid = PaintEditLayerGuid;
    ImportWeightOperation.ExpectedValue = PaintRegion.WeightDataHash;
    ImportWeightOperation.SourceFile = TEXT("ThomasEditorAutomationWeight.r8");
    ImportWeightRequest.Operations.Add(ImportWeightOperation);
    const FThomasWorldPatchPlanResult ImportWeightPlan =
        UThomasWorldToolset::PlanWorldPatch(ImportWeightRequest);
    if (!ImportWeightPlan.bOk)
    {
        AddInfo(TEXT("Landscape R8 plan: code=") + ImportWeightPlan.Code
            + TEXT(" message=") + ImportWeightPlan.Message);
    }
    TestTrue(TEXT("Landscape R8 import plan succeeds"), ImportWeightPlan.bOk);
    const FThomasWorldPatchApplyResult ImportWeightApply =
        UThomasWorldToolset::ApplyWorldPlan(ImportWeightPlan.PlanId, false);
    if (!ImportWeightApply.bOk)
    {
        AddInfo(TEXT("Landscape R8 apply: code=") + ImportWeightApply.Code
            + TEXT(" message=") + ImportWeightApply.Message);
    }
    TestTrue(
        TEXT("Landscape R8 import apply succeeds"),
        ImportWeightApply.bOk);
    PaintRegion = UThomasWorldToolset::InspectLandscapeRegion(
        LandscapeActorPath,
        8,
        8,
        10,
        10,
        PaintEditLayerGuid,
        LandscapeLayerInfoObjectPath,
        9);
    TestTrue(
        TEXT("Landscape R8 import writes the exact weight region"),
        PaintRegion.bOk
            && PaintRegion.MinWeightValue == 64
            && PaintRegion.MaxWeightValue == 64);
    IFileManager::Get().Delete(*HeightImportFilename, false, true);
    IFileManager::Get().Delete(*WeightImportFilename, false, true);

    int32 LandscapeInitialEditLayerCount = 0;
    FThomasEnvironmentInspectionResult LandscapeInspection =
        UThomasWorldToolset::InspectEnvironment(100);
    const FThomasLandscapeRecord* LandscapeRecord =
        LandscapeInspection.Landscapes.FindByPredicate(
            [&LandscapeActorPath](const FThomasLandscapeRecord& Record)
            {
                return Record.ActorPath == LandscapeActorPath;
            });
    TestNotNull(TEXT("Native Landscape has typed environment inspection"), LandscapeRecord);
    if (LandscapeRecord)
    {
        LandscapeInitialEditLayerCount = LandscapeRecord->EditLayerCount;
        TestEqual(TEXT("Imported Landscape topology component count"), LandscapeRecord->ComponentCount, 1);
        TestEqual(TEXT("Imported Landscape collision component count"), LandscapeRecord->CollisionComponentCount, 1);
        TestTrue(
            TEXT("Imported Landscape exposes its exact 64x64 extent"),
            LandscapeRecord->bHasValidExtent
                && LandscapeRecord->VertexSizeX == 64
                && LandscapeRecord->VertexSizeY == 64);
        TestTrue(
            TEXT("Landscape inspection exposes exact class"),
            LandscapeRecord->ClassPath == TEXT("/Script/Landscape.Landscape"));

        FThomasWorldPatchRequest ConfigureLandscapeRequest;
        ConfigureLandscapeRequest.MapPath = LandscapeInspection.MapPath;
        ConfigureLandscapeRequest.ExpectedRevision = LandscapeInspection.Revision;
        ConfigureLandscapeRequest.bConfirmDestructive = true;
        FThomasWorldOperation SetLandscapeMaterial;
        SetLandscapeMaterial.Action = TEXT("set_landscape_material");
        SetLandscapeMaterial.ActorPath = LandscapeActorPath;
        SetLandscapeMaterial.ExpectedValue = LandscapeRecord->MaterialPath;
        SetLandscapeMaterial.Value =
            TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial");
        ConfigureLandscapeRequest.Operations.Add(SetLandscapeMaterial);
        FThomasWorldOperation EnableLandscapeNanite;
        EnableLandscapeNanite.Action = TEXT("set_landscape_property");
        EnableLandscapeNanite.ActorPath = LandscapeActorPath;
        EnableLandscapeNanite.PropertyName = TEXT("bEnableNanite");
        EnableLandscapeNanite.ExpectedValue = LandscapeRecord->bEnableNanite
            ? TEXT("True") : TEXT("False");
        EnableLandscapeNanite.Value = TEXT("True");
        ConfigureLandscapeRequest.Operations.Add(EnableLandscapeNanite);
        const FThomasWorldPatchPlanResult ConfigureLandscapePlan =
            UThomasWorldToolset::PlanWorldPatch(ConfigureLandscapeRequest);
        TestTrue(TEXT("Landscape material/Nanite plan succeeds"), ConfigureLandscapePlan.bOk);
        TestEqual(TEXT("Landscape Nanite setting is R2"), ConfigureLandscapePlan.Risk, FString(TEXT("R2")));
        TestTrue(
            TEXT("Landscape material/Nanite apply succeeds"),
            UThomasWorldToolset::ApplyWorldPlan(ConfigureLandscapePlan.PlanId, false).bOk);
    }

    LandscapeInspection = UThomasWorldToolset::InspectEnvironment(100);
    LandscapeRecord = LandscapeInspection.Landscapes.FindByPredicate(
        [&LandscapeActorPath](const FThomasLandscapeRecord& Record)
        {
            return Record.ActorPath == LandscapeActorPath;
        });
    TestTrue(
        TEXT("Landscape material/Nanite changes are exact"),
            LandscapeRecord
            && LandscapeRecord->bEnableNanite
            && LandscapeRecord->MaterialPath
                == TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));

    FString LandscapeLayerA;
    FString LandscapeLayerB;
    if (LandscapeRecord)
    {
        FThomasWorldPatchRequest CreateLandscapeLayerARequest;
        CreateLandscapeLayerARequest.MapPath = LandscapeInspection.MapPath;
        CreateLandscapeLayerARequest.ExpectedRevision = LandscapeInspection.Revision;
        CreateLandscapeLayerARequest.bConfirmDestructive = true;
        FThomasWorldOperation CreateLandscapeLayerA;
        CreateLandscapeLayerA.Action = TEXT("create_landscape_edit_layer");
        CreateLandscapeLayerA.ActorPath = LandscapeActorPath;
        CreateLandscapeLayerA.Label = TEXT("ThomasLayerA");
        CreateLandscapeLayerA.ExpectedValue = FString::FromInt(LandscapeRecord->EditLayerCount);
        CreateLandscapeLayerARequest.Operations.Add(CreateLandscapeLayerA);
        const FThomasWorldPatchPlanResult CreateLandscapeLayerAPlan =
            UThomasWorldToolset::PlanWorldPatch(CreateLandscapeLayerARequest);
        TestTrue(TEXT("Landscape edit layer A create plan succeeds"), CreateLandscapeLayerAPlan.bOk);
        TestTrue(
            TEXT("Landscape edit layer A create succeeds"),
            UThomasWorldToolset::ApplyWorldPlan(CreateLandscapeLayerAPlan.PlanId, false).bOk);
    }

    LandscapeInspection = UThomasWorldToolset::InspectEnvironment(100);
    LandscapeRecord = LandscapeInspection.Landscapes.FindByPredicate(
        [&LandscapeActorPath](const FThomasLandscapeRecord& Record)
        {
            return Record.ActorPath == LandscapeActorPath;
        });
    if (LandscapeRecord)
    {
        if (const FThomasLandscapeEditLayerRecord* LayerA =
            LandscapeRecord->EditLayers.FindByPredicate(
                [](const FThomasLandscapeEditLayerRecord& Layer)
                {
                    return Layer.Name == TEXT("ThomasLayerA");
                }))
        {
            LandscapeLayerA = LayerA->Guid;
        }
        FThomasWorldPatchRequest CreateLandscapeLayerBRequest;
        CreateLandscapeLayerBRequest.MapPath = LandscapeInspection.MapPath;
        CreateLandscapeLayerBRequest.ExpectedRevision = LandscapeInspection.Revision;
        CreateLandscapeLayerBRequest.bConfirmDestructive = true;
        FThomasWorldOperation CreateLandscapeLayerB;
        CreateLandscapeLayerB.Action = TEXT("create_landscape_edit_layer");
        CreateLandscapeLayerB.ActorPath = LandscapeActorPath;
        CreateLandscapeLayerB.Label = TEXT("ThomasLayerB");
        CreateLandscapeLayerB.ExpectedValue = FString::FromInt(LandscapeRecord->EditLayerCount);
        CreateLandscapeLayerBRequest.Operations.Add(CreateLandscapeLayerB);
        const FThomasWorldPatchPlanResult CreateLandscapeLayerBPlan =
            UThomasWorldToolset::PlanWorldPatch(CreateLandscapeLayerBRequest);
        TestTrue(TEXT("Landscape edit layer B create plan succeeds"), CreateLandscapeLayerBPlan.bOk);
        TestTrue(
            TEXT("Landscape edit layer B create succeeds"),
            UThomasWorldToolset::ApplyWorldPlan(CreateLandscapeLayerBPlan.PlanId, false).bOk);
    }

    LandscapeInspection = UThomasWorldToolset::InspectEnvironment(100);
    LandscapeRecord = LandscapeInspection.Landscapes.FindByPredicate(
        [&LandscapeActorPath](const FThomasLandscapeRecord& Record)
        {
            return Record.ActorPath == LandscapeActorPath;
        });
    if (LandscapeRecord)
    {
        const FThomasLandscapeEditLayerRecord* LayerB =
            LandscapeRecord->EditLayers.FindByPredicate(
                [](const FThomasLandscapeEditLayerRecord& Layer)
                {
                    return Layer.Name == TEXT("ThomasLayerB");
                });
        if (LayerB) LandscapeLayerB = LayerB->Guid;
        TestTrue(
            TEXT("Landscape exposes both typed edit layers"),
            !LandscapeLayerA.IsEmpty()
                && LayerB
                && LandscapeRecord->EditLayerCount == LandscapeInitialEditLayerCount + 2);
        if (LayerB)
        {
            FThomasWorldPatchRequest ConfigureLandscapeLayerRequest;
            ConfigureLandscapeLayerRequest.MapPath = LandscapeInspection.MapPath;
            ConfigureLandscapeLayerRequest.ExpectedRevision = LandscapeInspection.Revision;
            FThomasWorldOperation HideLandscapeLayer;
            HideLandscapeLayer.Action = TEXT("set_landscape_edit_layer_property");
            HideLandscapeLayer.ActorPath = LandscapeActorPath;
            HideLandscapeLayer.AssetPath = LayerB->Guid;
            HideLandscapeLayer.PropertyName = TEXT("Visible");
            HideLandscapeLayer.ExpectedValue = LayerB->bVisible ? TEXT("true") : TEXT("false");
            HideLandscapeLayer.Value = TEXT("false");
            ConfigureLandscapeLayerRequest.Operations.Add(HideLandscapeLayer);
            FThomasWorldOperation SetLandscapeLayerAlpha;
            SetLandscapeLayerAlpha.Action = TEXT("set_landscape_edit_layer_property");
            SetLandscapeLayerAlpha.ActorPath = LandscapeActorPath;
            SetLandscapeLayerAlpha.AssetPath = LayerB->Guid;
            SetLandscapeLayerAlpha.PropertyName = TEXT("HeightmapAlpha");
            SetLandscapeLayerAlpha.ExpectedValue = FString::SanitizeFloat(LayerB->HeightmapAlpha);
            SetLandscapeLayerAlpha.Value = TEXT("0.5");
            ConfigureLandscapeLayerRequest.Operations.Add(SetLandscapeLayerAlpha);
            const FThomasWorldPatchPlanResult ConfigureLandscapeLayerPlan =
                UThomasWorldToolset::PlanWorldPatch(ConfigureLandscapeLayerRequest);
            TestTrue(TEXT("Landscape edit layer property plan succeeds"), ConfigureLandscapeLayerPlan.bOk);
            TestTrue(
                TEXT("Landscape edit layer property apply succeeds"),
                UThomasWorldToolset::ApplyWorldPlan(ConfigureLandscapeLayerPlan.PlanId, false).bOk);
        }
    }

    LandscapeInspection = UThomasWorldToolset::InspectEnvironment(100);
    LandscapeRecord = LandscapeInspection.Landscapes.FindByPredicate(
        [&LandscapeActorPath](const FThomasLandscapeRecord& Record)
        {
            return Record.ActorPath == LandscapeActorPath;
        });
    if (LandscapeRecord)
    {
        const FThomasLandscapeEditLayerRecord* LayerB =
            LandscapeRecord->EditLayers.FindByPredicate(
                [&LandscapeLayerB](const FThomasLandscapeEditLayerRecord& Layer)
                {
                    return Layer.Guid == LandscapeLayerB;
                });
        TestTrue(
            TEXT("Landscape edit layer visibility/alpha are exact"),
            LayerB && !LayerB->bVisible && FMath::IsNearlyEqual(LayerB->HeightmapAlpha, 0.5f));
        if (LayerB && LayerB->Index != 0)
        {
            FThomasWorldPatchRequest MoveLandscapeLayerRequest;
            MoveLandscapeLayerRequest.MapPath = LandscapeInspection.MapPath;
            MoveLandscapeLayerRequest.ExpectedRevision = LandscapeInspection.Revision;
            MoveLandscapeLayerRequest.bConfirmDestructive = true;
            FThomasWorldOperation MoveLandscapeLayer;
            MoveLandscapeLayer.Action = TEXT("move_landscape_edit_layer");
            MoveLandscapeLayer.ActorPath = LandscapeActorPath;
            MoveLandscapeLayer.AssetPath = LayerB->Guid;
            MoveLandscapeLayer.ExpectedValue = FString::FromInt(LayerB->Index);
            MoveLandscapeLayer.InstanceIndex = 0;
            MoveLandscapeLayerRequest.Operations.Add(MoveLandscapeLayer);
            const FThomasWorldPatchPlanResult MoveLandscapeLayerPlan =
                UThomasWorldToolset::PlanWorldPatch(MoveLandscapeLayerRequest);
            TestTrue(TEXT("Landscape edit layer move plan succeeds"), MoveLandscapeLayerPlan.bOk);
            TestTrue(
                TEXT("Landscape edit layer move succeeds"),
                UThomasWorldToolset::ApplyWorldPlan(MoveLandscapeLayerPlan.PlanId, false).bOk);
        }
    }

    LandscapeInspection = UThomasWorldToolset::InspectEnvironment(100);
    LandscapeRecord = LandscapeInspection.Landscapes.FindByPredicate(
        [&LandscapeActorPath](const FThomasLandscapeRecord& Record)
        {
            return Record.ActorPath == LandscapeActorPath;
        });
    if (LandscapeRecord)
    {
        const FThomasLandscapeEditLayerRecord* LayerA =
            LandscapeRecord->EditLayers.FindByPredicate(
                [&LandscapeLayerA](const FThomasLandscapeEditLayerRecord& Layer)
                {
                    return Layer.Guid == LandscapeLayerA;
                });
        TestTrue(
            TEXT("Landscape edit layer move has exact order"),
            LandscapeRecord->EditLayers.Num() == LandscapeInitialEditLayerCount + 2
                && LandscapeRecord->EditLayers[0].Guid == LandscapeLayerB);
        if (LayerA)
        {
            FThomasWorldPatchRequest DeleteLandscapeLayerRequest;
            DeleteLandscapeLayerRequest.MapPath = LandscapeInspection.MapPath;
            DeleteLandscapeLayerRequest.ExpectedRevision = LandscapeInspection.Revision;
            DeleteLandscapeLayerRequest.bConfirmDestructive = true;
            FThomasWorldOperation DeleteLandscapeLayer;
            DeleteLandscapeLayer.Action = TEXT("delete_landscape_edit_layer");
            DeleteLandscapeLayer.ActorPath = LandscapeActorPath;
            DeleteLandscapeLayer.AssetPath = LayerA->Guid;
            DeleteLandscapeLayer.ExpectedValue = LayerA->Name;
            DeleteLandscapeLayerRequest.Operations.Add(DeleteLandscapeLayer);
            const FThomasWorldPatchPlanResult DeleteLandscapeLayerPlan =
                UThomasWorldToolset::PlanWorldPatch(DeleteLandscapeLayerRequest);
            TestTrue(TEXT("Landscape edit layer delete plan succeeds"), DeleteLandscapeLayerPlan.bOk);
            TestTrue(
                TEXT("Landscape edit layer delete succeeds"),
                UThomasWorldToolset::ApplyWorldPlan(DeleteLandscapeLayerPlan.PlanId, false).bOk);
        }
    }

    LandscapeInspection = UThomasWorldToolset::InspectEnvironment(100);
    TestFalse(
        TEXT("Landscape edit layer deletion is exact"),
        LandscapeInspection.Landscapes.ContainsByPredicate(
            [&LandscapeActorPath, &LandscapeLayerA](const FThomasLandscapeRecord& Record)
            {
                return Record.ActorPath == LandscapeActorPath
                    && Record.EditLayers.ContainsByPredicate(
                        [&LandscapeLayerA](const FThomasLandscapeEditLayerRecord& Layer)
                        {
                            return Layer.Guid == LandscapeLayerA;
                        });
            }));
    FThomasWorldPatchRequest DeleteLandscapeRequest;
    DeleteLandscapeRequest.MapPath = LandscapeInspection.MapPath;
    DeleteLandscapeRequest.ExpectedRevision = LandscapeInspection.Revision;
    DeleteLandscapeRequest.bConfirmDestructive = true;
    FThomasWorldOperation DeleteLandscape;
    DeleteLandscape.Action = TEXT("delete_actor");
    DeleteLandscape.ActorPath = LandscapeActorPath;
    DeleteLandscapeRequest.Operations.Add(DeleteLandscape);
    const FThomasWorldPatchPlanResult DeleteLandscapePlan =
        UThomasWorldToolset::PlanWorldPatch(DeleteLandscapeRequest);
    TestTrue(TEXT("Temporary Landscape cleanup plan succeeds"), DeleteLandscapePlan.bOk);
    TestTrue(
        TEXT("Temporary Landscape cleanup succeeds"),
        UThomasWorldToolset::ApplyWorldPlan(DeleteLandscapePlan.PlanId, false).bOk);
    TestEqual(
        TEXT("Landscape automation restores baseline actor count"),
        UThomasWorldToolset::InspectEnvironment(100).LandscapeActorCount,
        LandscapeBaseline.LandscapeActorCount);

    UObject* LandscapePaintMaterialAsset = LoadObject<UObject>(
        nullptr, *LandscapePaintMaterialObjectPath);
    UObject* LandscapeLayerInfoAsset = LoadObject<UObject>(
        nullptr, *LandscapeLayerInfoObjectPath);
    TArray<UObject*> LandscapeAuthoringAssets;
    if (LandscapePaintMaterialAsset)
    {
        LandscapeAuthoringAssets.Add(LandscapePaintMaterialAsset);
    }
    if (LandscapeLayerInfoAsset)
    {
        LandscapeAuthoringAssets.Add(LandscapeLayerInfoAsset);
    }
    TestEqual(
        TEXT("Temporary Landscape authoring assets deleted through Unreal API"),
        ObjectTools::DeleteObjectsUnchecked(LandscapeAuthoringAssets),
        LandscapeAuthoringAssets.Num());
    CollectGarbage(RF_NoFlags);
    TestFalse(
        TEXT("Temporary Landscape material removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            LandscapePaintMaterialPath,
            FPackageName::GetAssetPackageExtension())));
    TestFalse(
        TEXT("Temporary Landscape LayerInfo removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            LandscapeLayerInfoPath,
            FPackageName::GetAssetPackageExtension())));

    const FThomasWorldInspectionResult WorldAfterFoliageCleanup =
        UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
    FThomasWorldPatchRequest SpawnDecalRequest;
    SpawnDecalRequest.MapPath = WorldAfterFoliageCleanup.MapPath;
    SpawnDecalRequest.ExpectedRevision = WorldAfterFoliageCleanup.Revision;
    SpawnDecalRequest.bConfirmDestructive = true;
    FThomasWorldOperation SpawnDecal;
    SpawnDecal.Action = TEXT("spawn_actor");
    SpawnDecal.ClassPath = TEXT("/Script/Engine.DecalActor");
    SpawnDecal.Label = TEXT("TE Native Decal Proof");
    SpawnDecalRequest.Operations.Add(SpawnDecal);
    const FThomasWorldPatchPlanResult SpawnDecalPlan =
        UThomasWorldToolset::PlanWorldPatch(SpawnDecalRequest);
    TestTrue(TEXT("Native Decal actor spawn plan succeeds"), SpawnDecalPlan.bOk);
    const FThomasWorldPatchApplyResult SpawnDecalApply =
        UThomasWorldToolset::ApplyWorldPlan(SpawnDecalPlan.PlanId, false);
    TestTrue(TEXT("Native Decal actor spawn succeeds"), SpawnDecalApply.bOk);
    TestEqual(TEXT("Native Decal actor path returned"), SpawnDecalApply.CreatedActorPaths.Num(), 1);
    const FString DecalActorPath = SpawnDecalApply.CreatedActorPaths.IsEmpty()
        ? FString() : SpawnDecalApply.CreatedActorPaths[0];
    AActor* DecalActor = FindObject<AActor>(nullptr, *DecalActorPath);
    FString DecalComponentPath;
    if (DecalActor)
    {
        for (UActorComponent* Component : DecalActor->GetComponents())
        {
            if (Component
                && Component->GetClass()->GetPathName() == TEXT("/Script/Engine.DecalComponent"))
            {
                DecalComponentPath = Component->GetPathName();
                break;
            }
        }
    }
    TestFalse(TEXT("Spawned Decal actor exposes its Decal component path"), DecalComponentPath.IsEmpty());
    FThomasWorldComponentInspectionResult DecalComponentBefore =
        UThomasWorldToolset::InspectActorComponent(
            DecalActorPath,
            DecalComponentPath,
            {TEXT("DecalMaterial"), TEXT("DecalSize"), TEXT("SortOrder")},
            10);
    TestTrue(TEXT("Native Decal component Details inspection succeeds"), DecalComponentBefore.bOk);
    const FThomasPropertyRecord* DecalMaterialProperty =
        DecalComponentBefore.Properties.FindByPredicate(
            [](const FThomasPropertyRecord& Property)
            {
                return Property.Name == TEXT("DecalMaterial");
            });
    const FThomasPropertyRecord* DecalSizeProperty =
        DecalComponentBefore.Properties.FindByPredicate(
            [](const FThomasPropertyRecord& Property)
            {
                return Property.Name == TEXT("DecalSize");
            });
    const FThomasPropertyRecord* DecalSortProperty =
        DecalComponentBefore.Properties.FindByPredicate(
            [](const FThomasPropertyRecord& Property)
            {
                return Property.Name == TEXT("SortOrder");
            });
    TestNotNull(TEXT("Decal material component property is inspectable"), DecalMaterialProperty);
    TestNotNull(TEXT("Decal size component property is inspectable"), DecalSizeProperty);
    TestNotNull(TEXT("Decal sort component property is inspectable"), DecalSortProperty);
    if (DecalMaterialProperty && DecalSizeProperty && DecalSortProperty)
    {
        FThomasWorldPatchRequest ConfigureDecalRequest;
        ConfigureDecalRequest.MapPath = DecalComponentBefore.MapPath;
        ConfigureDecalRequest.ExpectedRevision = DecalComponentBefore.Revision;
        FThomasWorldOperation SetDecalMaterial;
        SetDecalMaterial.Action = TEXT("set_component_property");
        SetDecalMaterial.ActorPath = DecalActorPath;
        SetDecalMaterial.ComponentPath = DecalComponentPath;
        SetDecalMaterial.PropertyName = TEXT("DecalMaterial");
        SetDecalMaterial.ExpectedValue = DecalMaterialProperty->Value;
        SetDecalMaterial.Value = FString::Printf(
            TEXT("/Script/Engine.Material'%s.%s'"),
            *DecalMaterialPath,
            *FPackageName::GetLongPackageAssetName(DecalMaterialPath));
        ConfigureDecalRequest.Operations.Add(SetDecalMaterial);
        FThomasWorldOperation SetDecalSize;
        SetDecalSize.Action = TEXT("set_component_property");
        SetDecalSize.ActorPath = DecalActorPath;
        SetDecalSize.ComponentPath = DecalComponentPath;
        SetDecalSize.PropertyName = TEXT("DecalSize");
        SetDecalSize.ExpectedValue = DecalSizeProperty->Value;
        SetDecalSize.Value = TEXT("(X=64.0,Y=128.0,Z=256.0)");
        ConfigureDecalRequest.Operations.Add(SetDecalSize);
        FThomasWorldOperation SetDecalSort;
        SetDecalSort.Action = TEXT("set_component_property");
        SetDecalSort.ActorPath = DecalActorPath;
        SetDecalSort.ComponentPath = DecalComponentPath;
        SetDecalSort.PropertyName = TEXT("SortOrder");
        SetDecalSort.ExpectedValue = DecalSortProperty->Value;
        SetDecalSort.Value = TEXT("7");
        ConfigureDecalRequest.Operations.Add(SetDecalSort);
        const FThomasWorldPatchPlanResult ConfigureDecalPlan =
            UThomasWorldToolset::PlanWorldPatch(ConfigureDecalRequest);
        TestTrue(TEXT("Native Decal component configuration plan succeeds"), ConfigureDecalPlan.bOk);
        TestTrue(
            TEXT("Native Decal component configuration apply succeeds"),
            UThomasWorldToolset::ApplyWorldPlan(ConfigureDecalPlan.PlanId, false).bOk);
        const FThomasWorldComponentInspectionResult DecalComponentAfter =
            UThomasWorldToolset::InspectActorComponent(
                DecalActorPath,
                DecalComponentPath,
                {TEXT("DecalMaterial"), TEXT("DecalSize"), TEXT("SortOrder")},
                10);
        TestTrue(
            TEXT("Native Decal component configuration is inspectable"),
            DecalComponentAfter.bOk
                && DecalComponentAfter.Properties.ContainsByPredicate(
                    [&DecalMaterialPath](const FThomasPropertyRecord& Property)
                    {
                        return Property.Name == TEXT("DecalMaterial")
                            && Property.Value.Contains(DecalMaterialPath);
                    })
                && DecalComponentAfter.Properties.ContainsByPredicate(
                    [](const FThomasPropertyRecord& Property)
                    {
                        return Property.Name == TEXT("DecalSize")
                            && Property.Value.Contains(TEXT("X=64.000000"))
                            && Property.Value.Contains(TEXT("Y=128.000000"))
                            && Property.Value.Contains(TEXT("Z=256.000000"));
                    })
                && DecalComponentAfter.Properties.ContainsByPredicate(
                    [](const FThomasPropertyRecord& Property)
                    {
                        return Property.Name == TEXT("SortOrder")
                            && FCString::Atoi(*Property.Value) == 7;
                    }));
    }

    const FThomasWorldInspectionResult WorldBeforeDecalDelete =
        UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
    FThomasWorldPatchRequest DeleteDecalRequest;
    DeleteDecalRequest.MapPath = WorldBeforeDecalDelete.MapPath;
    DeleteDecalRequest.ExpectedRevision = WorldBeforeDecalDelete.Revision;
    DeleteDecalRequest.bConfirmDestructive = true;
    FThomasWorldOperation DeleteDecal;
    DeleteDecal.Action = TEXT("delete_actor");
    DeleteDecal.ActorPath = DecalActorPath;
    DeleteDecalRequest.Operations.Add(DeleteDecal);
    const FThomasWorldPatchPlanResult DeleteDecalPlan =
        UThomasWorldToolset::PlanWorldPatch(DeleteDecalRequest);
    TestTrue(TEXT("Native Decal actor delete plan succeeds"), DeleteDecalPlan.bOk);
    TestTrue(
        TEXT("Native Decal actor delete succeeds"),
        UThomasWorldToolset::ApplyWorldPlan(DeleteDecalPlan.PlanId, false).bOk);
    UObject* DecalMaterialAsset = LoadObject<UObject>(
        nullptr,
        *(DecalMaterialPath + TEXT(".") + FPackageName::GetLongPackageAssetName(DecalMaterialPath)));
    TestNotNull(TEXT("Temporary native Decal material loaded for cleanup"), DecalMaterialAsset);
    if (DecalMaterialAsset)
    {
        TestEqual(
            TEXT("Temporary native Decal material deleted through Unreal API"),
            ObjectTools::DeleteObjectsUnchecked({DecalMaterialAsset}),
            1);
    }

    const FThomasWorldInspectionResult WorldBeforePostProcess =
        UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
    TestTrue(TEXT("Editor world is available for Post Process proof"), WorldBeforePostProcess.bOk);
    FThomasWorldPatchRequest SpawnPostProcessRequest;
    SpawnPostProcessRequest.MapPath = WorldBeforePostProcess.MapPath;
    SpawnPostProcessRequest.ExpectedRevision = WorldBeforePostProcess.Revision;
    SpawnPostProcessRequest.bConfirmDestructive = true;
    FThomasWorldOperation SpawnPostProcess;
    SpawnPostProcess.Action = TEXT("spawn_actor");
    SpawnPostProcess.ClassPath = TEXT("/Script/Engine.PostProcessVolume");
    SpawnPostProcess.Label = TEXT("TE Native Post Process Proof");
    SpawnPostProcessRequest.Operations.Add(SpawnPostProcess);
    const FThomasWorldPatchPlanResult SpawnPostProcessPlan =
        UThomasWorldToolset::PlanWorldPatch(SpawnPostProcessRequest);
    TestTrue(TEXT("Post Process proof actor spawn plan succeeds"), SpawnPostProcessPlan.bOk);
    const FThomasWorldPatchApplyResult SpawnPostProcessApply =
        UThomasWorldToolset::ApplyWorldPlan(SpawnPostProcessPlan.PlanId, false);
    TestTrue(TEXT("Post Process proof actor spawn succeeds"), SpawnPostProcessApply.bOk);
    TestEqual(TEXT("Post Process proof actor path returned"), SpawnPostProcessApply.CreatedActorPaths.Num(), 1);

    const FString PostProcessActorPath = SpawnPostProcessApply.CreatedActorPaths.IsEmpty()
        ? FString() : SpawnPostProcessApply.CreatedActorPaths[0];
    const FThomasPostProcessInspectionResult PostProcessBefore =
        UThomasWorldToolset::InspectPostProcessVolume(
            PostProcessActorPath, TEXT("BloomIntensity"), 10);
    TestTrue(TEXT("Post Process settings inspection succeeds"), PostProcessBefore.bOk);
    TestEqual(TEXT("Post Process setting filter is exact"), PostProcessBefore.Settings.Num(), 1);
    TestTrue(
        TEXT("BloomIntensity is editable and exposes its override"),
        PostProcessBefore.Settings.Num() == 1
            && PostProcessBefore.Settings[0].Name == TEXT("BloomIntensity")
            && PostProcessBefore.Settings[0].bEditable
            && PostProcessBefore.Settings[0].bHasOverride);

    const FThomasPostProcessInspectionResult FullPostProcessInspection =
        UThomasWorldToolset::InspectPostProcessVolume(PostProcessActorPath, TEXT(""), 500);
    TestTrue(TEXT("Full reflected Post Process inspection succeeds"), FullPostProcessInspection.bOk);
    TestTrue(TEXT("Full Post Process surface is broad"), FullPostProcessInspection.MatchedSettingCount > 100);
    TestEqual(TEXT("Full Post Process surface is not truncated at the supported bound"),
        FullPostProcessInspection.Settings.Num(), FullPostProcessInspection.MatchedSettingCount);
    TestTrue(TEXT("Full Post Process surface exposes editable values"),
        FullPostProcessInspection.Settings.ContainsByPredicate(
            [](const FThomasPostProcessSettingRecord& Setting)
            {
                return Setting.bEditable && Setting.bHasOverride;
            }));
    const FThomasPostProcessInspectionResult LumenPostProcessInspection =
        UThomasWorldToolset::InspectPostProcessVolume(PostProcessActorPath, TEXT("lumen"), 500);
    TestTrue(TEXT("Lumen-specific Post Process inspection succeeds"), LumenPostProcessInspection.bOk);
    TestTrue(TEXT("Lumen-specific Post Process settings are discovered"),
        !LumenPostProcessInspection.Settings.IsEmpty());
    TestTrue(TEXT("Lumen filter returns Lumen-related settings"),
        LumenPostProcessInspection.Settings.ContainsByPredicate(
            [](const FThomasPostProcessSettingRecord& Setting)
            {
                return Setting.bLumenRelated;
            }));

    if (PostProcessBefore.Settings.Num() == 1)
    {
        const FThomasPostProcessSettingRecord& BloomBefore = PostProcessBefore.Settings[0];
        const FString DesiredBloom = FMath::IsNearlyEqual(FCString::Atof(*BloomBefore.Value), 1.234f)
            ? TEXT("1.123") : TEXT("1.234");
        FThomasPostProcessPatchRequest PostProcessRequest;
        PostProcessRequest.MapPath = PostProcessBefore.MapPath;
        PostProcessRequest.ExpectedRevision = PostProcessBefore.Revision;
        PostProcessRequest.ActorPath = PostProcessActorPath;
        FThomasPostProcessOperation SetBloom;
        SetBloom.Action = TEXT("set_setting");
        SetBloom.SettingName = TEXT("BloomIntensity");
        SetBloom.ExpectedValue = BloomBefore.Value;
        SetBloom.Value = DesiredBloom;
        PostProcessRequest.Operations.Add(SetBloom);
        FThomasPostProcessOperation SetBloomOverride;
        SetBloomOverride.Action = TEXT("set_override");
        SetBloomOverride.SettingName = TEXT("BloomIntensity");
        SetBloomOverride.bExpectedOverride = BloomBefore.bOverrideEnabled;
        SetBloomOverride.bOverride = !BloomBefore.bOverrideEnabled;
        PostProcessRequest.Operations.Add(SetBloomOverride);
        const FString OutlineMaterialObjectPath =
            TEXT("/Game/PropHunt/UI/Materials/M_PH_InteractableOutline_PP_V2.M_PH_InteractableOutline_PP_V2");
        FThomasPostProcessOperation AddOutlineBlendable;
        AddOutlineBlendable.Action = TEXT("add_blendable");
        AddOutlineBlendable.ObjectPath = OutlineMaterialObjectPath;
        AddOutlineBlendable.Weight = 0.5f;
        PostProcessRequest.Operations.Add(AddOutlineBlendable);
        const FThomasPostProcessPlanResult PostProcessPlan =
            UThomasWorldToolset::PlanPostProcessPatch(PostProcessRequest);
        TestTrue(TEXT("Post Process setting/override plan succeeds"), PostProcessPlan.bOk);
        const FThomasPostProcessApplyResult PostProcessApply =
            UThomasWorldToolset::ApplyPostProcessPlan(PostProcessPlan.PlanId, false);
        TestTrue(TEXT("Post Process setting/override apply succeeds"), PostProcessApply.bOk);
        const FThomasPostProcessInspectionResult PostProcessAfter =
            UThomasWorldToolset::InspectPostProcessVolume(
                PostProcessActorPath, TEXT("BloomIntensity"), 10);
        TestTrue(TEXT("Post Process setting is inspectable after mutation"), PostProcessAfter.bOk);
        TestTrue(
            TEXT("Post Process value and override were changed"),
            PostProcessAfter.Settings.Num() == 1
                && FMath::IsNearlyEqual(
                    FCString::Atof(*PostProcessAfter.Settings[0].Value),
                    FCString::Atof(*DesiredBloom))
                && PostProcessAfter.Settings[0].bOverrideEnabled
                    == !BloomBefore.bOverrideEnabled);
        TestTrue(
            TEXT("Post Process material blendable was added"),
            PostProcessAfter.Blendables.Num() == 1
                && PostProcessAfter.Blendables[0].ObjectPath == OutlineMaterialObjectPath
                && FMath::IsNearlyEqual(PostProcessAfter.Blendables[0].Weight, 0.5f));

        FThomasPostProcessPatchRequest WeightRequest;
        WeightRequest.MapPath = PostProcessAfter.MapPath;
        WeightRequest.ExpectedRevision = PostProcessAfter.Revision;
        WeightRequest.ActorPath = PostProcessActorPath;
        FThomasPostProcessOperation SetBlendableWeight;
        SetBlendableWeight.Action = TEXT("set_blendable_weight");
        SetBlendableWeight.BlendableIndex = 0;
        SetBlendableWeight.ObjectPath = OutlineMaterialObjectPath;
        SetBlendableWeight.ExpectedWeight = 0.5f;
        SetBlendableWeight.Weight = 0.8f;
        WeightRequest.Operations.Add(SetBlendableWeight);
        const FThomasPostProcessPlanResult WeightPlan =
            UThomasWorldToolset::PlanPostProcessPatch(WeightRequest);
        TestTrue(TEXT("Post Process blendable weight plan succeeds"), WeightPlan.bOk);
        const FThomasPostProcessApplyResult WeightApply =
            UThomasWorldToolset::ApplyPostProcessPlan(WeightPlan.PlanId, false);
        TestTrue(TEXT("Post Process blendable weight apply succeeds"), WeightApply.bOk);
        const FThomasPostProcessInspectionResult PostProcessAfterWeight =
            UThomasWorldToolset::InspectPostProcessVolume(
                PostProcessActorPath, TEXT("BloomIntensity"), 10);
        TestTrue(
            TEXT("Post Process blendable weight was changed"),
            PostProcessAfterWeight.Blendables.Num() == 1
                && FMath::IsNearlyEqual(PostProcessAfterWeight.Blendables[0].Weight, 0.8f));

        FThomasPostProcessPatchRequest RemoveBlendableRequest;
        RemoveBlendableRequest.MapPath = PostProcessAfterWeight.MapPath;
        RemoveBlendableRequest.ExpectedRevision = PostProcessAfterWeight.Revision;
        RemoveBlendableRequest.ActorPath = PostProcessActorPath;
        RemoveBlendableRequest.bConfirmDestructive = true;
        FThomasPostProcessOperation RemoveBlendable;
        RemoveBlendable.Action = TEXT("remove_blendable");
        RemoveBlendable.BlendableIndex = 0;
        RemoveBlendable.ObjectPath = OutlineMaterialObjectPath;
        RemoveBlendable.ExpectedWeight = 0.8f;
        RemoveBlendableRequest.Operations.Add(RemoveBlendable);
        const FThomasPostProcessPlanResult RemoveBlendablePlan =
            UThomasWorldToolset::PlanPostProcessPatch(RemoveBlendableRequest);
        TestTrue(TEXT("Post Process blendable removal plan succeeds"), RemoveBlendablePlan.bOk);
        TestEqual(TEXT("Post Process blendable removal is R2"), RemoveBlendablePlan.Risk, FString(TEXT("R2")));
        const FThomasPostProcessApplyResult RemoveBlendableApply =
            UThomasWorldToolset::ApplyPostProcessPlan(RemoveBlendablePlan.PlanId, false);
        TestTrue(TEXT("Post Process blendable removal apply succeeds"), RemoveBlendableApply.bOk);
        TestEqual(
            TEXT("Post Process blendable was removed"),
            UThomasWorldToolset::InspectPostProcessVolume(
                PostProcessActorPath, TEXT("BloomIntensity"), 10).Blendables.Num(),
            0);

        const FThomasLightingInspectionResult LumenInspection =
            UThomasWorldToolset::InspectLighting();
        TestTrue(TEXT("Expanded Lumen/renderer inspection succeeds"), LumenInspection.bOk);
        TestTrue(TEXT("Expanded Lumen/renderer surface contains multiple settings"),
            LumenInspection.RendererSettings.Num() >= 6);
        TestTrue(
            TEXT("Expanded renderer inspection exposes the effective GI method"),
            LumenInspection.RendererSettings.ContainsByPredicate(
                [](const FThomasPropertyRecord& Property)
                {
                    return Property.Name == TEXT("DynamicGlobalIllumination");
                }));
        if (const FThomasPropertyRecord* GiSetting =
            LumenInspection.RendererSettings.FindByPredicate(
                [](const FThomasPropertyRecord& Property)
                {
                    return Property.Name == TEXT("DynamicGlobalIllumination");
                }))
        {
            const FThomasWorldInspectionResult LumenWorld =
                UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
            FThomasWorldPatchRequest RendererRequest;
            RendererRequest.MapPath = LumenWorld.MapPath;
            RendererRequest.ExpectedRevision = LumenWorld.Revision;
            FThomasWorldOperation RendererOperation;
            RendererOperation.Action = TEXT("set_renderer_property");
            RendererOperation.PropertyName = GiSetting->Name;
            RendererOperation.ExpectedValue = GiSetting->Value;
            RendererOperation.Value = GiSetting->Value;
            RendererRequest.Operations.Add(RendererOperation);
            TestEqual(
                TEXT("Renderer/Lumen mutation requires explicit R2 confirmation"),
                UThomasWorldToolset::PlanWorldPatch(RendererRequest).Code,
                FString(TEXT("confirmation_required")));
            RendererRequest.bConfirmDestructive = true;
            const FThomasWorldPatchPlanResult RendererPlan =
                UThomasWorldToolset::PlanWorldPatch(RendererRequest);
            TestTrue(TEXT("Renderer/Lumen guarded plan succeeds"), RendererPlan.bOk);
            TestEqual(TEXT("Renderer/Lumen guarded plan is R2"), RendererPlan.Risk, FString(TEXT("R2")));

            FThomasWorldPatchRequest MixedRendererWorldRequest = RendererRequest;
            FThomasWorldOperation MixedActorOperation;
            MixedActorOperation.Action = TEXT("set_actor_label");
            MixedActorOperation.ActorPath = PostProcessActorPath;
            MixedActorOperation.Label = TEXT("ThomasEditor mixed-batch refusal fixture");
            MixedRendererWorldRequest.Operations.Add(MixedActorOperation);
            TestEqual(
                TEXT("Renderer config and map mutations cannot share one plan"),
                UThomasWorldToolset::PlanWorldPatch(
                    MixedRendererWorldRequest).Code,
                FString(TEXT("mixed_renderer_world_batch_denied")));
        }

        const FThomasPropertyRecord* BoolRendererSetting =
            LumenInspection.RendererSettings.FindByPredicate(
                [](const FThomasPropertyRecord& Property)
                {
                    return Property.Name == TEXT("bGenerateMeshDistanceFields")
                        && Property.bEditable
                        && (Property.Value.Equals(TEXT("true"), ESearchCase::IgnoreCase)
                            || Property.Value.Equals(TEXT("false"), ESearchCase::IgnoreCase));
                });
        TestNotNull(
            TEXT("Renderer rollback regression finds the mutable distance-field setting"),
            BoolRendererSetting);
        if (BoolRendererSetting)
        {
            const FThomasWorldInspectionResult RendererWorld =
                UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
            FThomasWorldPatchRequest RollbackRequest;
            RollbackRequest.MapPath = RendererWorld.MapPath;
            RollbackRequest.ExpectedRevision = RendererWorld.Revision;
            RollbackRequest.bConfirmDestructive = true;
            FThomasWorldOperation ToggleRendererSetting;
            ToggleRendererSetting.Action = TEXT("set_renderer_property");
            ToggleRendererSetting.PropertyName = BoolRendererSetting->Name;
            ToggleRendererSetting.ExpectedValue = BoolRendererSetting->Value;
            ToggleRendererSetting.Value = BoolRendererSetting->Value.Equals(
                TEXT("true"), ESearchCase::IgnoreCase)
                ? TEXT("False") : TEXT("True");
            RollbackRequest.Operations.Add(ToggleRendererSetting);
            const FThomasWorldPatchPlanResult RollbackPlan =
                UThomasWorldToolset::PlanWorldPatch(RollbackRequest);
            TestTrue(
                TEXT("Renderer rollback regression plan succeeds"),
                RollbackPlan.bOk);
            IThomasEditorWorldProviderModule* WorldProvider =
                FModuleManager::GetModulePtr<IThomasEditorWorldProviderModule>(
                    TEXT("ThomasEditorWorld"));
            TestNotNull(
                TEXT("World provider exposes the renderer save-failure test seam"),
                WorldProvider);
            if (RollbackPlan.bOk && WorldProvider)
            {
                const FString RendererConfigFilename = FPaths::ConvertRelativePathToFull(
                    FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini"));
                TArray64<uint8> RendererConfigBeforeFailure;
                const bool bCapturedRendererConfig = FFileHelper::LoadFileToArray(
                    RendererConfigBeforeFailure,
                    *RendererConfigFilename,
                    FILEREAD_Silent);
                TestTrue(
                    TEXT("Renderer rollback regression captures DefaultEngine.ini before apply"),
                    bCapturedRendererConfig);
                WorldProvider->SetForceRendererConfigSaveFailureForTests(true);
                const FThomasWorldPatchApplyResult FailedRendererSave =
                    UThomasWorldToolset::ApplyWorldPlan(
                        RollbackPlan.PlanId, true);
                TestTrue(
                    TEXT("Renderer config save failure explicitly restores the previous value"),
                    !FailedRendererSave.bOk
                        && FailedRendererSave.Code == TEXT("save_failed")
                        && FailedRendererSave.bRolledBack);
                const FThomasLightingInspectionResult AfterRendererRollback =
                    UThomasWorldToolset::InspectLighting();
                const FThomasPropertyRecord* RestoredSetting =
                    AfterRendererRollback.RendererSettings.FindByPredicate(
                        [BoolRendererSetting](const FThomasPropertyRecord& Property)
                        {
                            return Property.Name == BoolRendererSetting->Name;
                        });
                TestTrue(
                    TEXT("Renderer rollback readback matches the exact pre-apply value"),
                    RestoredSetting
                        && RestoredSetting->Value == BoolRendererSetting->Value);
                TArray64<uint8> RendererConfigAfterFailure;
                const bool bReadRendererConfigAfterFailure = FFileHelper::LoadFileToArray(
                    RendererConfigAfterFailure,
                    *RendererConfigFilename,
                    FILEREAD_Silent);
                TestTrue(
                    TEXT("Renderer rollback restores DefaultEngine.ini byte-for-byte"),
                    bCapturedRendererConfig
                        && bReadRendererConfigAfterFailure
                        && RendererConfigAfterFailure == RendererConfigBeforeFailure);
            }
        }
    }

    const FThomasWorldInspectionResult WorldBeforePostProcessDelete =
        UThomasWorldToolset::InspectCurrentLevel(TEXT(""), false, 10);
    FThomasWorldPatchRequest DeletePostProcessRequest;
    DeletePostProcessRequest.MapPath = WorldBeforePostProcessDelete.MapPath;
    DeletePostProcessRequest.ExpectedRevision = WorldBeforePostProcessDelete.Revision;
    DeletePostProcessRequest.bConfirmDestructive = true;
    FThomasWorldOperation DeletePostProcess;
    DeletePostProcess.Action = TEXT("delete_actor");
    DeletePostProcess.ActorPath = PostProcessActorPath;
    DeletePostProcessRequest.Operations.Add(DeletePostProcess);
    const FThomasWorldPatchPlanResult DeletePostProcessPlan =
        UThomasWorldToolset::PlanWorldPatch(DeletePostProcessRequest);
    TestTrue(TEXT("Post Process proof actor delete plan succeeds"), DeletePostProcessPlan.bOk);
    const FThomasWorldPatchApplyResult DeletePostProcessApply =
        UThomasWorldToolset::ApplyWorldPlan(DeletePostProcessPlan.PlanId, false);
    TestTrue(TEXT("Post Process proof actor delete succeeds"), DeletePostProcessApply.bOk);

    const FString LevelAuditJson = UThomasCompatibilityToolset::LevelAudit(
        TEXT("/Game/PropHunt/Tests/L_PH_Intro"), {}, {}, {}, false, 5);
    TestTrue(
        TEXT("Compatibility level audit succeeds"),
        LevelAuditJson.Contains(TEXT("\"ok\":true")));

    const FString MessagesJson = UThomasCompatibilityToolset::RecentMessages(5, TEXT(""));
    TestTrue(
        TEXT("Compatibility recent messages succeeds"),
        MessagesJson.Contains(TEXT("\"ok\":true")));

    const FThomasAssetInventoryResult Inventory =
        UThomasAssetsToolset::GetAssetInventory(TEXT("/Game/PropHunt"), TEXT(""), 10);
    TestTrue(TEXT("Inventory succeeds"), Inventory.bOk);
    TestTrue(TEXT("Inventory is bounded"), Inventory.Items.Num() <= 10);
    TestTrue(
        TEXT("Assets provider loaded on first call"),
        FModuleManager::Get().IsModuleLoaded(TEXT("ThomasEditorAssets")));

    const FThomasAssetInventoryResult StaticMeshInventory =
        UThomasAssetsToolset::GetAssetInventory(
            TEXT("/Game/PropHunt"), TEXT("/Script/Engine.StaticMesh"), 10);
    TestTrue(TEXT("Project Static Mesh inventory succeeds"), StaticMeshInventory.bOk);
    TestTrue(TEXT("Project contains a Static Mesh for Nanite proof"), !StaticMeshInventory.Items.IsEmpty());
    if (!StaticMeshInventory.Items.IsEmpty())
    {
        const FThomasStaticMeshInspectionResult StaticMeshInspection =
            UThomasRenderToolset::InspectStaticMesh(StaticMeshInventory.Items[0].AssetPath);
        TestTrue(TEXT("Static Mesh/Nanite typed inspection succeeds"), StaticMeshInspection.bOk);
        if (StaticMeshInspection.bReadOnly)
        {
            TestEqual(TEXT("Nanite planning refuses a read-only production mesh"),
                UThomasRenderToolset::PlanNanite(
                    StaticMeshInventory.Items[0].AssetPath,
                    StaticMeshInspection.Revision,
                    !StaticMeshInspection.bNaniteEnabled).Code,
                FString(TEXT("read_only_asset")));
        }
        else
        {
            TestEqual(TEXT("Nanite planning rejects a stale revision"),
                UThomasRenderToolset::PlanNanite(
                    StaticMeshInventory.Items[0].AssetPath,
                    TEXT("stale-revision"),
                    !StaticMeshInspection.bNaniteEnabled).Code,
                FString(TEXT("revision_conflict")));
        }

        const FString SourceMeshObjectPath =
            StaticMeshInventory.Items[0].AssetPath.Contains(TEXT("."))
            ? StaticMeshInventory.Items[0].AssetPath
            : StaticMeshInventory.Items[0].AssetPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    StaticMeshInventory.Items[0].AssetPath);
        UStaticMesh* SourceMesh = LoadObject<UStaticMesh>(
            nullptr, *SourceMeshObjectPath);
        const FString NaniteFixturePath =
            TEXT("/Game/PropHunt/Tests/ThomasEditor/SM_TE_Nanite_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        UPackage* NaniteFixturePackage = CreatePackage(*NaniteFixturePath);
        UStaticMesh* NaniteFixture = SourceMesh && NaniteFixturePackage
            ? Cast<UStaticMesh>(StaticDuplicateObject(
                SourceMesh,
                NaniteFixturePackage,
                FName(*FPackageName::GetLongPackageAssetName(
                    NaniteFixturePath)),
                RF_AllFlags))
            : nullptr;
        if (NaniteFixture)
        {
            NaniteFixture->SetFlags(
                RF_Public | RF_Standalone | RF_Transactional);
            FAssetRegistryModule::AssetCreated(NaniteFixture);
            NaniteFixturePackage->MarkPackageDirty();
        }
        FSavePackageArgs NaniteFixtureSaveArgs;
        NaniteFixtureSaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        NaniteFixtureSaveArgs.SaveFlags = SAVE_NoError;
        NaniteFixtureSaveArgs.Error = GError;
        const FString NaniteFixtureFilename =
            FPackageName::LongPackageNameToFilename(
                NaniteFixturePath,
                FPackageName::GetAssetPackageExtension());
        TestTrue(
            TEXT("Nanite test mesh duplicates and saves through Unreal"),
            NaniteFixture
                && UPackage::SavePackage(
                    NaniteFixturePackage,
                    NaniteFixture,
                    *NaniteFixtureFilename,
                    NaniteFixtureSaveArgs));
        if (NaniteFixture)
        {
            FThomasStaticMeshInspectionResult NaniteBefore =
                UThomasRenderToolset::InspectStaticMesh(NaniteFixturePath);
            const bool bOriginalNaniteEnabled = NaniteBefore.bNaniteEnabled;
            const FThomasNanitePlanResult NanitePlan =
                UThomasRenderToolset::PlanNanite(
                    NaniteFixturePath,
                    NaniteBefore.Revision,
                    !bOriginalNaniteEnabled);
            TestTrue(
                TEXT("Nanite rebuild preflight succeeds on the temporary exact mesh revision"),
                NanitePlan.bOk && !NanitePlan.Warnings.IsEmpty());
            const FThomasNaniteApplyResult NaniteApply =
                UThomasRenderToolset::ApplyNanitePlan(
                    NanitePlan.PlanId, true);
            TestTrue(
                TEXT("Nanite apply rebuilds and saves the temporary mesh"),
                NaniteApply.bOk
                    && NaniteApply.bSaved
                    && NaniteApply.bNaniteEnabled == !bOriginalNaniteEnabled);
            NaniteBefore = UThomasRenderToolset::InspectStaticMesh(
                NaniteFixturePath);
            const FThomasNanitePlanResult NaniteRestorePlan =
                UThomasRenderToolset::PlanNanite(
                    NaniteFixturePath,
                    NaniteBefore.Revision,
                    bOriginalNaniteEnabled);
            TestTrue(
                TEXT("Nanite restoration plan succeeds"),
                NaniteRestorePlan.bOk);
            if (NaniteRestorePlan.bOk)
            {
                const FThomasNaniteApplyResult NaniteRestoreApply =
                    UThomasRenderToolset::ApplyNanitePlan(
                        NaniteRestorePlan.PlanId, true);
                TestTrue(
                    TEXT("Nanite restoration applies and saves the original state"),
                    NaniteRestoreApply.bOk
                        && NaniteRestoreApply.bSaved
                        && NaniteRestoreApply.bNaniteEnabled
                            == bOriginalNaniteEnabled);
            }
            TestEqual(
                TEXT("Temporary Nanite mesh is deleted through Unreal"),
                ObjectTools::DeleteObjectsUnchecked({NaniteFixture}),
                1);
            TestFalse(
                TEXT("Temporary Nanite mesh leaves no file"),
                IFileManager::Get().FileExists(*NaniteFixtureFilename));
        }
    }

    const FThomasPluginDetailsResult PluginDetails =
        UThomasProjectToolset::GetPluginDetails(TEXT("ThomasEditor"));
    TestTrue(TEXT("ThomasEditor plugin descriptor is inspectable"), PluginDetails.bOk);
    TestTrue(TEXT("ThomasEditor is Editor-only at runtime"), PluginDetails.bEnabled);
    TestTrue(
        TEXT("Self plugin mutation remains denied"),
        UThomasProjectToolset::PlanPluginEnabled(TEXT("ThomasEditor"), false, true).Code
            == TEXT("self_mutation_denied"));
    TestEqual(
        TEXT("An enabled required dependency cannot be disabled"),
        UThomasProjectToolset::PlanPluginEnabled(
            TEXT("ModelContextProtocol"), false, true).Code,
        FString(TEXT("required_plugin_disable_denied")));
    const FThomasPluginChangePlanResult ExpiringPluginPlan =
        UThomasProjectToolset::PlanPluginEnabled(
            TEXT("AndroidFileServer"), true, true);
    TestTrue(
        TEXT("An explicitly referenced disabled engine plugin can produce a guarded plan"),
        ExpiringPluginPlan.bOk);
    if (ExpiringPluginPlan.bOk)
    {
        TestTrue(
            TEXT("Plugin plan test seam expires the exact plan"),
            UThomasProjectToolset::ExpirePluginPlanForTests(
                ExpiringPluginPlan.PlanId));
        TestEqual(
            TEXT("Expired plugin plans are distinguished from missing plans"),
            UThomasProjectToolset::ApplyPluginPlan(
                ExpiringPluginPlan.PlanId).Code,
            FString(TEXT("plan_expired")));
    }

    const FThomasDomainInventoryResult AnimationInventory =
        UThomasDomainsToolset::InspectAnimationAssets(TEXT("/Game/PropHunt"), 200);
    const FThomasDomainInventoryResult GameplayInventory =
        UThomasDomainsToolset::InspectGameplayDataAssets(TEXT("/Game/PropHunt"), 20);
    const FThomasDomainInventoryResult FxInventory =
        UThomasDomainsToolset::InspectFxAssets(TEXT("/Game/PropHunt"), 20);
    const FThomasDomainInventoryResult AudioInventory =
        UThomasDomainsToolset::InspectAudioAssets(TEXT("/Game/PropHunt"), 20);
    TestTrue(TEXT("Animation/Character inventory succeeds"), AnimationInventory.bOk);
    TestTrue(TEXT("Enhanced Input/gameplay data inventory succeeds"), GameplayInventory.bOk);
    TestTrue(TEXT("FX inventory succeeds even when the project has no Niagara asset"), FxInventory.bOk);
    TestTrue(TEXT("Audio/MetaSound inventory succeeds"), AudioInventory.bOk);
    TestTrue(TEXT("Gameplay inventory finds project data assets"), GameplayInventory.MatchedCount > 0);
    const FThomasAssetRecord* SupportedAnimationAsset = AnimationInventory.Items.FindByPredicate(
        [](const FThomasAssetRecord& Item)
        {
            return Item.ClassPath.Contains(TEXT("AnimSequence"))
                || Item.ClassPath.Contains(TEXT("AnimMontage"))
                || Item.ClassPath.Contains(TEXT("AnimBlueprint"))
                || Item.ClassPath.Contains(TEXT("BlendSpace"))
                || Item.ClassPath.Contains(TEXT("Skeleton"))
                || Item.ClassPath.Contains(TEXT("SkeletalMesh"));
        });
    TestNotNull(TEXT("Animation inventory contains a typed-inspectable project asset"), SupportedAnimationAsset);
    if (SupportedAnimationAsset)
    {
        const FThomasSpecializedAssetInspectionResult AnimationInspection =
            UThomasDomainsToolset::InspectAnimationAsset(SupportedAnimationAsset->AssetPath, 50);
        TestTrue(TEXT("Typed Animation/Character inspection succeeds"), AnimationInspection.bOk);
        TestTrue(TEXT("Typed Animation/Character inspection reports metrics"), !AnimationInspection.Metrics.IsEmpty());
    }
    const FThomasAssetRecord* AnimationSkeletonContext = AnimationInventory.Items.FindByPredicate(
        [](const FThomasAssetRecord& Item)
        {
            return Item.ClassPath == TEXT("/Script/Engine.Skeleton")
                || Item.ClassPath == TEXT("/Script/Engine.SkeletalMesh");
        });
    TestNotNull(TEXT("Animation inventory contains an explicit Skeleton context"), AnimationSkeletonContext);
    const FThomasAssetRecord* AnimationSkeletalMeshContext =
        AnimationInventory.Items.FindByPredicate(
            [](const FThomasAssetRecord& Item)
            {
                return Item.ClassPath == TEXT("/Script/Engine.SkeletalMesh");
            });
    TestNotNull(TEXT("Animation inventory contains an explicit Skeletal Mesh context"),
        AnimationSkeletalMeshContext);
    if (AnimationSkeletonContext)
    {
        const FString SkeletonContextObjectPath =
            AnimationSkeletonContext->AssetPath + TEXT(".")
            + FPackageName::GetLongPackageAssetName(
                AnimationSkeletonContext->AssetPath);
        UObject* SkeletonContextObject = LoadObject<UObject>(
            nullptr, *SkeletonContextObjectPath);
        const USkeleton* LayeredBlendSkeleton = Cast<USkeleton>(
            SkeletonContextObject);
        if (const USkeletalMesh* ContextMesh = Cast<USkeletalMesh>(
                SkeletonContextObject))
        {
            LayeredBlendSkeleton = ContextMesh->GetSkeleton();
        }
        const FString LayeredBlendBoneName = LayeredBlendSkeleton
            && LayeredBlendSkeleton->GetReferenceSkeleton().GetNum() > 0
            ? LayeredBlendSkeleton->GetReferenceSkeleton()
                .GetBoneName(0).ToString()
            : FString();
        TestTrue(TEXT("Animation Skeleton context exposes a branch-filter bone"),
            !LayeredBlendBoneName.IsEmpty());

        const FString AnimationFixtureSkeletonPath =
            TEXT("/Game/PropHunt/Tests/ThomasEditor/SKEL_TE_R45_Context_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        USkeleton* AnimationFixtureSkeleton =
            CreateR45TemporarySkeletonFixture(
                *this,
                LayeredBlendSkeleton,
                AnimationFixtureSkeletonPath);
        TestNotNull(
            TEXT("R45 temporary Skeleton is available as animation context"),
            AnimationFixtureSkeleton);

        const FString IKRigPath =
            TEXT("/Game/PropHunt/Tests/ThomasEditor/IKR_TE_Native_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString IKRetargeterPath =
            TEXT("/Game/PropHunt/Tests/ThomasEditor/RTG_TE_Native_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString ControlRigPath =
            TEXT("/Game/PropHunt/Tests/ThomasEditor/CR_TE_Native_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString ExternalControlRigPath =
            TEXT("/Game/PropHunt/Tests/ThomasEditor/CR_TE_External_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        if (AnimationSkeletalMeshContext)
        {
            const FString IKRigMeshObjectPath =
                AnimationSkeletalMeshContext->AssetPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    AnimationSkeletalMeshContext->AssetPath);
            const USkeletalMesh* IKRigMesh = LoadObject<USkeletalMesh>(
                nullptr, *IKRigMeshObjectPath);
            const FString IKRigRootBone = IKRigMesh
                && IKRigMesh->GetRefSkeleton().GetNum() > 0
                ? IKRigMesh->GetRefSkeleton().GetBoneName(0).ToString()
                : FString();
            FString IKRigChainStartBone;
            FString IKRigChainEndBone;
            FString IKRigExcludedBone;
            if (IKRigMesh)
            {
                const FReferenceSkeleton& IKRigRefSkeleton =
                    IKRigMesh->GetRefSkeleton();
                for (int32 BoneIndex = IKRigRefSkeleton.GetNum() - 1;
                     BoneIndex > 0;
                     --BoneIndex)
                {
                    const int32 ParentIndex =
                        IKRigRefSkeleton.GetParentIndex(BoneIndex);
                    if (ParentIndex != INDEX_NONE)
                    {
                        IKRigChainStartBone =
                            IKRigRefSkeleton.GetBoneName(ParentIndex).ToString();
                        IKRigChainEndBone =
                            IKRigRefSkeleton.GetBoneName(BoneIndex).ToString();
                        break;
                    }
                }
                for (int32 BoneIndex = 1;
                     BoneIndex < IKRigRefSkeleton.GetNum();
                     ++BoneIndex)
                {
                    const FString Candidate =
                        IKRigRefSkeleton.GetBoneName(BoneIndex).ToString();
                    if (Candidate != IKRigChainStartBone
                        && Candidate != IKRigChainEndBone)
                    {
                        IKRigExcludedBone = Candidate;
                        break;
                    }
                }
            }
            TestTrue(TEXT("IK Rig mesh exposes root and parent/child chain bones"),
                !IKRigRootBone.IsEmpty()
                    && !IKRigChainStartBone.IsEmpty()
                    && !IKRigChainEndBone.IsEmpty()
                    && !IKRigExcludedBone.IsEmpty());
            FThomasDomainAssetCreateRequest IKRigRequest;
            IKRigRequest.AssetPath = IKRigPath;
            IKRigRequest.ExpectedRevision = TEXT("missing");
            IKRigRequest.AssetKind = TEXT("ik_rig");
            IKRigRequest.ContextAssetPath =
                AnimationSkeletalMeshContext->AssetPath;
            const FThomasDomainAssetCreatePlanResult IKRigPlan =
                UThomasDomainsToolset::PlanAnimationAssetCreate(
                    IKRigRequest);
            AddInfo(FString::Printf(
                TEXT("IK Rig create plan: ok=%d code=%s message=%s factory=%s context=%s"),
                IKRigPlan.bOk ? 1 : 0,
                *IKRigPlan.Code,
                *IKRigPlan.Message,
                *IKRigPlan.FactoryClassPath,
                *IKRigPlan.ContextAssetPath));
            TestTrue(TEXT("Native IK Rig create plan discovers its UE factory"),
                IKRigPlan.bOk && !IKRigPlan.FactoryClassPath.IsEmpty());
            const FThomasDomainAssetCreateApplyResult IKRigApply =
                UThomasDomainsToolset::ApplyAnimationAssetCreate(
                    IKRigPlan.PlanId, true);
            AddInfo(FString::Printf(
                TEXT("IK Rig create apply: ok=%d code=%s message=%s class=%s saved=%d"),
                IKRigApply.bOk ? 1 : 0,
                *IKRigApply.Code,
                *IKRigApply.Message,
                *IKRigApply.ClassPath,
                IKRigApply.bSaved ? 1 : 0));
            TestTrue(TEXT("Native IK Rig create/save succeeds"),
                IKRigApply.bOk && IKRigApply.bSaved
                    && IKRigApply.ClassPath
                        == TEXT("/Script/IKRig.IKRigDefinition"));
            UObject* SavedIKRig = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText IKRigReloadError;
            TestTrue(TEXT("Native IK Rig reloads from saved file"),
                SavedIKRig
                    && UPackageTools::ReloadPackages(
                        {SavedIKRig->GetOutermost()},
                        IKRigReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult IKRigInspection =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRigPath, 200);
            AddInfo(FString::Printf(
                TEXT("IK Rig inspection: metrics=%s items=%s"),
                *FString::Join(IKRigInspection.Metrics, TEXT(" | ")),
                *FString::Join(IKRigInspection.Items, TEXT(" | "))));
            const FString ExpectedIKRigPreviewMesh =
                TEXT("PreviewSkeletalMesh=")
                + (IKRigMesh ? IKRigMesh->GetPathName() : TEXT("None"));
            TestTrue(TEXT("Native IK Rig typed inspection succeeds"),
                IKRigInspection.bOk);
            TestEqual(TEXT("Native IK Rig inspection kind"),
                IKRigInspection.AssetKind, FString(TEXT("ik_rig")));
            TestTrue(TEXT("Native IK Rig inspection has loaded bones"),
                IKRigInspection.Metrics.ContainsByPredicate(
                    [](const FString& Metric)
                    {
                        return Metric.StartsWith(TEXT("BoneCount="))
                            && Metric != TEXT("BoneCount=0");
                    }));
            TestTrue(TEXT("Native IK Rig preview mesh persists after reload"),
                IKRigInspection.Metrics.Contains(ExpectedIKRigPreviewMesh));
            TestTrue(TEXT("Native IK Rig inspection exposes the root bone"),
                IKRigInspection.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.StartsWith(TEXT("IKRigBone Index=0 "));
                    }));

            FThomasAnimationPatchRequest InvalidIKRigRequest;
            InvalidIKRigRequest.AssetPath = IKRigPath;
            InvalidIKRigRequest.ExpectedRevision = IKRigInspection.Revision;
            FThomasAnimationOperation InvalidIKGoal;
            InvalidIKGoal.Action = TEXT("add_ik_goal");
            InvalidIKGoal.Name = TEXT("TE_InvalidGoal");
            InvalidIKGoal.BoneName = TEXT("TE_MissingBone");
            InvalidIKRigRequest.Operations.Add(InvalidIKGoal);
            TestEqual(TEXT("IK Rig preflight rejects an unknown bone"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    InvalidIKRigRequest).Code,
                FString(TEXT("invalid_or_duplicate_ik_goal")));

            FThomasAnimationPatchRequest IKRigPatchRequest;
            IKRigPatchRequest.AssetPath = IKRigPath;
            IKRigPatchRequest.ExpectedRevision = IKRigInspection.Revision;
            FThomasAnimationOperation SetIKRetargetRoot;
            SetIKRetargetRoot.Action = TEXT("set_ik_retarget_root");
            SetIKRetargetRoot.BoneName = IKRigRootBone;
            IKRigPatchRequest.Operations.Add(SetIKRetargetRoot);
            FThomasAnimationOperation AddIKGoal;
            AddIKGoal.Action = TEXT("add_ik_goal");
            AddIKGoal.Name = TEXT("TE_EndGoal");
            AddIKGoal.BoneName = IKRigChainEndBone;
            IKRigPatchRequest.Operations.Add(AddIKGoal);
            FThomasAnimationOperation AddIKChain;
            AddIKChain.Action = TEXT("add_ik_retarget_chain");
            AddIKChain.Name = TEXT("TE_FullChain");
            AddIKChain.StartBoneName = IKRigChainStartBone;
            AddIKChain.EndBoneName = IKRigChainEndBone;
            AddIKChain.GoalName = TEXT("TE_EndGoal");
            IKRigPatchRequest.Operations.Add(AddIKChain);
            const FThomasAnimationPlanResult IKRigPatchPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    IKRigPatchRequest);
            TestTrue(TEXT("IK Rig root, goal and retarget-chain plan succeeds"),
                IKRigPatchPlan.bOk);
            const FThomasAnimationApplyResult IKRigPatchApply =
                UThomasDomainsToolset::ApplyAnimationPlan(
                    IKRigPatchPlan.PlanId, true);
            AddInfo(FString::Printf(
                TEXT("IK Rig patch apply: ok=%d code=%s message=%s saved=%d"),
                IKRigPatchApply.bOk ? 1 : 0,
                *IKRigPatchApply.Code,
                *IKRigPatchApply.Message,
                IKRigPatchApply.bSaved ? 1 : 0));
            TestTrue(TEXT("IK Rig root, goal and retarget chain save"),
                IKRigPatchApply.bOk && IKRigPatchApply.bSaved);
            UObject* PatchedIKRig = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText IKRigPatchReloadError;
            TestTrue(TEXT("Patched IK Rig reloads from saved file"),
                PatchedIKRig
                    && UPackageTools::ReloadPackages(
                        {PatchedIKRig->GetOutermost()},
                        IKRigPatchReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult IKRigAfterPatch =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRigPath, 300);
            TestTrue(TEXT("IK Rig root, goal and chain are inspectable after reload"),
                IKRigAfterPatch.Metrics.Contains(
                    TEXT("GoalCount=1"))
                    && IKRigAfterPatch.Metrics.Contains(
                        TEXT("RetargetChainCount=1"))
                    && IKRigAfterPatch.Metrics.Contains(
                        TEXT("RetargetRoot=") + IKRigRootBone)
                    && IKRigAfterPatch.Items.ContainsByPredicate(
                        [&](const FString& Item)
                        {
                            return Item.Contains(
                                    TEXT("IKRigGoal Name=TE_EndGoal"))
                                && Item.Contains(
                                    TEXT("Bone=") + IKRigChainEndBone);
                        })
                    && IKRigAfterPatch.Items.ContainsByPredicate(
                        [&](const FString& Item)
                        {
                            return Item.Contains(
                                    TEXT("IKRigRetargetChain Name=TE_FullChain"))
                                && Item.Contains(
                                    TEXT("Start=") + IKRigChainStartBone)
                                && Item.Contains(
                                    TEXT("End=") + IKRigChainEndBone)
                                && Item.Contains(TEXT("Goal=TE_EndGoal"));
                        }));

            const FString* LimbSolverTypeItem =
                IKRigAfterPatch.Items.FindByPredicate(
                    [](const FString& Item)
                    {
                        return Item.StartsWith(
                                TEXT("IKRigSolverType Path="))
                            && Item.EndsWith(TEXT("LimbSolver"))
                            && !Item.Contains(TEXT("Stretch"));
                    });
            TestNotNull(TEXT("IK Rig exposes a native Limb solver type"),
                LimbSolverTypeItem);
            FString LimbSolverClassPath = LimbSolverTypeItem
                ? LimbSolverTypeItem->RightChop(
                    FString(TEXT("IKRigSolverType Path=")).Len())
                : FString();

            FThomasAnimationPatchRequest InvalidIKSolverRequest;
            InvalidIKSolverRequest.AssetPath = IKRigPath;
            InvalidIKSolverRequest.ExpectedRevision =
                IKRigAfterPatch.Revision;
            FThomasAnimationOperation InvalidIKSolver;
            InvalidIKSolver.Action = TEXT("add_ik_solver");
            InvalidIKSolver.ClassPath = TEXT("/Script/IKRig.NotASolver");
            InvalidIKSolverRequest.Operations.Add(InvalidIKSolver);
            TestEqual(TEXT("IK Rig rejects an unknown solver type"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    InvalidIKSolverRequest).Code,
                FString(TEXT("invalid_ik_solver_type")));

            FThomasAnimationPatchRequest AddIKSolverRequest;
            AddIKSolverRequest.AssetPath = IKRigPath;
            AddIKSolverRequest.ExpectedRevision =
                IKRigAfterPatch.Revision;
            FThomasAnimationOperation AddIKSolver;
            AddIKSolver.Action = TEXT("add_ik_solver");
            AddIKSolver.ClassPath = LimbSolverClassPath;
            AddIKSolverRequest.Operations.Add(AddIKSolver);
            const FThomasAnimationPlanResult AddIKSolverPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    AddIKSolverRequest);
            TestTrue(TEXT("IK Rig native Limb solver plan succeeds"),
                AddIKSolverPlan.bOk);
            TestTrue(TEXT("IK Rig native Limb solver save succeeds"),
                UThomasDomainsToolset::ApplyAnimationPlan(
                    AddIKSolverPlan.PlanId, true).bOk);
            UObject* IKRigWithSolver = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText IKRigSolverReloadError;
            TestTrue(TEXT("IK Rig native solver reloads from saved file"),
                IKRigWithSolver
                    && UPackageTools::ReloadPackages(
                        {IKRigWithSolver->GetOutermost()},
                        IKRigSolverReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult IKRigAfterSolver =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRigPath, 300);
            TestTrue(TEXT("IK Rig native solver is inspectable after reload"),
                IKRigAfterSolver.Metrics.Contains(TEXT("SolverCount=1"))
                    && IKRigAfterSolver.Items.ContainsByPredicate(
                        [&](const FString& Item)
                        {
                            return Item.StartsWith(
                                    TEXT("IKRigSolver Index=0 "))
                                && Item.Contains(
                                    TEXT("Type=") + LimbSolverClassPath)
                                && Item.Contains(TEXT("Enabled=true"));
                        }));

            FThomasAnimationPatchRequest ConfigureIKSolverRequest;
            ConfigureIKSolverRequest.AssetPath = IKRigPath;
            ConfigureIKSolverRequest.ExpectedRevision =
                IKRigAfterSolver.Revision;
            FThomasAnimationOperation SetIKSolverStartBone;
            SetIKSolverStartBone.Action =
                TEXT("set_ik_solver_start_bone");
            SetIKSolverStartBone.Index = 0;
            SetIKSolverStartBone.BoneName = IKRigChainStartBone;
            ConfigureIKSolverRequest.Operations.Add(SetIKSolverStartBone);
            FThomasAnimationOperation ConnectIKGoal;
            ConnectIKGoal.Action = TEXT("connect_ik_goal_to_solver");
            ConnectIKGoal.Index = 0;
            ConnectIKGoal.GoalName = TEXT("TE_EndGoal");
            ConfigureIKSolverRequest.Operations.Add(ConnectIKGoal);
            FThomasAnimationOperation DisableIKSolver;
            DisableIKSolver.Action = TEXT("set_ik_solver_enabled");
            DisableIKSolver.Index = 0;
            DisableIKSolver.bEnabled = false;
            ConfigureIKSolverRequest.Operations.Add(DisableIKSolver);
            const FThomasAnimationPlanResult ConfigureIKSolverPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    ConfigureIKSolverRequest);
            TestTrue(TEXT("IK Rig solver configuration plan succeeds"),
                ConfigureIKSolverPlan.bOk);
            const FThomasAnimationApplyResult ConfigureIKSolverApply =
                UThomasDomainsToolset::ApplyAnimationPlan(
                    ConfigureIKSolverPlan.PlanId, true);
            AddInfo(FString::Printf(
                TEXT("IK Rig solver apply: ok=%d code=%s message=%s saved=%d"),
                ConfigureIKSolverApply.bOk ? 1 : 0,
                *ConfigureIKSolverApply.Code,
                *ConfigureIKSolverApply.Message,
                ConfigureIKSolverApply.bSaved ? 1 : 0));
            TestTrue(TEXT("IK Rig solver configuration save succeeds"),
                ConfigureIKSolverApply.bOk
                    && ConfigureIKSolverApply.bSaved);
            UObject* ConfiguredIKRigSolver = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText IKRigSolverConfigReloadError;
            TestTrue(TEXT("Configured IK Rig solver reloads from saved file"),
                ConfiguredIKRigSolver
                    && UPackageTools::ReloadPackages(
                        {ConfiguredIKRigSolver->GetOutermost()},
                        IKRigSolverConfigReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult
                IKRigAfterSolverConfig =
                    UThomasDomainsToolset::InspectAnimationAsset(
                        IKRigPath, 300);
            TestTrue(TEXT("IK Rig solver settings persist after reload"),
                IKRigAfterSolverConfig.Items.ContainsByPredicate(
                    [&](const FString& Item)
                    {
                        return Item.StartsWith(
                                TEXT("IKRigSolver Index=0 "))
                            && Item.Contains(TEXT("Enabled=false"))
                            && Item.Contains(
                                TEXT("StartBone=")
                                    + IKRigChainStartBone)
                            && Item.Contains(TEXT("Goals=TE_EndGoal"));
                    }));

            FThomasAnimationPatchRequest InvalidIKLimbSettingsRequest;
            InvalidIKLimbSettingsRequest.AssetPath = IKRigPath;
            InvalidIKLimbSettingsRequest.ExpectedRevision =
                IKRigAfterSolverConfig.Revision;
            FThomasAnimationOperation InvalidIKLimbSettings;
            InvalidIKLimbSettings.Action =
                TEXT("set_ik_limb_solver_settings");
            InvalidIKLimbSettings.Index = 0;
            InvalidIKLimbSettings.IKReachPrecision = -1.0f;
            InvalidIKLimbSettingsRequest.Operations.Add(
                InvalidIKLimbSettings);
            TestEqual(TEXT("IK Rig rejects invalid Limb settings"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    InvalidIKLimbSettingsRequest).Code,
                FString(TEXT("invalid_ik_limb_solver_settings")));

            FThomasAnimationPatchRequest AdvancedIKSettingsRequest;
            AdvancedIKSettingsRequest.AssetPath = IKRigPath;
            AdvancedIKSettingsRequest.ExpectedRevision =
                IKRigAfterSolverConfig.Revision;
            FThomasAnimationOperation SetIKRootMotionBone;
            SetIKRootMotionBone.Action =
                TEXT("set_ik_root_motion_bone");
            SetIKRootMotionBone.BoneName = IKRigRootBone;
            AdvancedIKSettingsRequest.Operations.Add(
                SetIKRootMotionBone);
            FThomasAnimationOperation ExcludeIKBone;
            ExcludeIKBone.Action = TEXT("set_ik_bone_excluded");
            ExcludeIKBone.BoneName = IKRigExcludedBone;
            ExcludeIKBone.bIKBoneExcluded = true;
            AdvancedIKSettingsRequest.Operations.Add(ExcludeIKBone);
            FThomasAnimationOperation SetIKLimbSettings;
            SetIKLimbSettings.Action =
                TEXT("set_ik_limb_solver_settings");
            SetIKLimbSettings.Index = 0;
            SetIKLimbSettings.IKReachPrecision = 0.25f;
            SetIKLimbSettings.IKMaxIterations = 23;
            SetIKLimbSettings.bIKEnableRotationLimit = true;
            SetIKLimbSettings.IKMinRotationAngle = 17.0f;
            SetIKLimbSettings.bIKAveragePull = false;
            SetIKLimbSettings.IKPullDistribution = 0.35f;
            SetIKLimbSettings.IKReachStepAlpha = 0.8f;
            SetIKLimbSettings.bIKEnableTwistCorrection = true;
            SetIKLimbSettings.IKHingeRotationAxis = TEXT("y");
            SetIKLimbSettings.IKEndBoneForwardAxis = TEXT("z");
            AdvancedIKSettingsRequest.Operations.Add(SetIKLimbSettings);
            const FThomasAnimationPlanResult AdvancedIKSettingsPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    AdvancedIKSettingsRequest);
            TestTrue(TEXT("Advanced IK Rig settings plan succeeds"),
                AdvancedIKSettingsPlan.bOk);
            const FThomasAnimationApplyResult AdvancedIKSettingsApply =
                UThomasDomainsToolset::ApplyAnimationPlan(
                    AdvancedIKSettingsPlan.PlanId, true);
            AddInfo(FString::Printf(
                TEXT("IK Rig advanced settings apply: ok=%d code=%s message=%s saved=%d"),
                AdvancedIKSettingsApply.bOk ? 1 : 0,
                *AdvancedIKSettingsApply.Code,
                *AdvancedIKSettingsApply.Message,
                AdvancedIKSettingsApply.bSaved ? 1 : 0));
            TestTrue(TEXT("Advanced IK Rig settings save succeeds"),
                AdvancedIKSettingsApply.bOk
                    && AdvancedIKSettingsApply.bSaved);
            UObject* AdvancedIKRig = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText AdvancedIKRigReloadError;
            TestTrue(TEXT("Advanced IK Rig settings reload from disk"),
                AdvancedIKRig
                    && UPackageTools::ReloadPackages(
                        {AdvancedIKRig->GetOutermost()},
                        AdvancedIKRigReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult
                IKRigAfterAdvancedSettings =
                    UThomasDomainsToolset::InspectAnimationAsset(
                        IKRigPath, 300);
            TestTrue(TEXT("Advanced IK Rig settings persist after reload"),
                IKRigAfterAdvancedSettings.Metrics.Contains(
                    TEXT("RootMotionBone=") + IKRigRootBone)
                    && IKRigAfterAdvancedSettings.Metrics.Contains(
                        TEXT("ExcludedBoneCount=1"))
                    && IKRigAfterAdvancedSettings.Items.ContainsByPredicate(
                        [&](const FString& Item)
                        {
                            return Item.Contains(
                                    TEXT("Name=") + IKRigExcludedBone)
                                && Item.Contains(TEXT("Excluded=true"));
                        })
                    && IKRigAfterAdvancedSettings.Items.ContainsByPredicate(
                        [](const FString& Item)
                        {
                            return Item.StartsWith(
                                    TEXT("IKRigLimbSettings Index=0 "))
                                && Item.Contains(
                                    TEXT("ReachPrecision=0.250000"))
                                && Item.Contains(
                                    TEXT("MaxIterations=23"))
                                && Item.Contains(
                                    TEXT("EnableLimit=true"))
                                && Item.Contains(
                                    TEXT("MinRotationAngle=17.000000"))
                                && Item.Contains(
                                    TEXT("AveragePull=false"))
                                && Item.Contains(
                                    TEXT("PullDistribution=0.350000"))
                                && Item.Contains(
                                    TEXT("ReachStepAlpha=0.800000"))
                                && Item.Contains(
                                    TEXT("EnableTwistCorrection=true"))
                                && Item.Contains(TEXT("HingeAxis=y"))
                                && Item.Contains(TEXT("EndAxis=z"));
                        }));

            const FString* FullBodySolverTypeItem =
                IKRigAfterAdvancedSettings.Items.FindByPredicate(
                    [](const FString& Item)
                    {
                        return Item.StartsWith(
                                TEXT("IKRigSolverType Path="))
                            && Item.EndsWith(TEXT("FullBodyIKSolver"));
                    });
            TestNotNull(TEXT("IK Rig exposes the native Full Body IK solver"),
                FullBodySolverTypeItem);
            const FString FullBodySolverClassPath = FullBodySolverTypeItem
                ? FullBodySolverTypeItem->RightChop(
                    FString(TEXT("IKRigSolverType Path=")).Len())
                : FString();

            FThomasAnimationPatchRequest AddFBIKSolverRequest;
            AddFBIKSolverRequest.AssetPath = IKRigPath;
            AddFBIKSolverRequest.ExpectedRevision =
                IKRigAfterAdvancedSettings.Revision;
            FThomasAnimationOperation AddFBIKSolver;
            AddFBIKSolver.Action = TEXT("add_ik_solver");
            AddFBIKSolver.ClassPath = FullBodySolverClassPath;
            AddFBIKSolverRequest.Operations.Add(AddFBIKSolver);
            const FThomasAnimationPlanResult AddFBIKSolverPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    AddFBIKSolverRequest);
            TestTrue(TEXT("Full Body IK solver plan succeeds"),
                AddFBIKSolverPlan.bOk);
            TestTrue(TEXT("Full Body IK solver apply saves"),
                UThomasDomainsToolset::ApplyAnimationPlan(
                    AddFBIKSolverPlan.PlanId, true).bOk);
            UObject* IKRigWithFBIK = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText IKRigWithFBIKReloadError;
            TestTrue(TEXT("Full Body IK solver reloads from disk"),
                IKRigWithFBIK
                    && UPackageTools::ReloadPackages(
                        {IKRigWithFBIK->GetOutermost()},
                        IKRigWithFBIKReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult IKRigAfterFBIK =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRigPath, 600);
            TestTrue(TEXT("Full Body IK solver settings are inspectable"),
                IKRigAfterFBIK.Metrics.Contains(TEXT("SolverCount=2"))
                    && IKRigAfterFBIK.Items.ContainsByPredicate(
                        [](const FString& Item)
                        {
                            return Item.StartsWith(
                                    TEXT("IKRigSetting Scope=solver Index=1 "))
                                && Item.Contains(
                                    TEXT("Property=bAllowStretch "));
                        }));

            FThomasAnimationPatchRequest MoveFBIKToFrontRequest;
            MoveFBIKToFrontRequest.AssetPath = IKRigPath;
            MoveFBIKToFrontRequest.ExpectedRevision =
                IKRigAfterFBIK.Revision;
            FThomasAnimationOperation MoveFBIKToFront;
            MoveFBIKToFront.Action = TEXT("move_ik_solver");
            MoveFBIKToFront.Index = 1;
            MoveFBIKToFront.TargetIndex = 0;
            MoveFBIKToFrontRequest.Operations.Add(MoveFBIKToFront);
            const FThomasAnimationPlanResult MoveFBIKToFrontPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    MoveFBIKToFrontRequest);
            TestTrue(TEXT("IK Rig solver reorder plan succeeds"),
                MoveFBIKToFrontPlan.bOk);
            TestTrue(TEXT("IK Rig solver reorder apply saves"),
                UThomasDomainsToolset::ApplyAnimationPlan(
                    MoveFBIKToFrontPlan.PlanId, true).bOk);
            UObject* MovedFBIKToFront = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText MovedFBIKToFrontReloadError;
            TestTrue(TEXT("IK Rig solver reorder reloads from disk"),
                MovedFBIKToFront
                    && UPackageTools::ReloadPackages(
                        {MovedFBIKToFront->GetOutermost()},
                        MovedFBIKToFrontReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult
                IKRigAfterFBIKMove =
                    UThomasDomainsToolset::InspectAnimationAsset(
                        IKRigPath, 600);
            TestTrue(TEXT("IK Rig solver reorder persists after reload"),
                IKRigAfterFBIKMove.Items.ContainsByPredicate(
                    [&](const FString& Item)
                    {
                        return Item.StartsWith(
                                TEXT("IKRigSolver Index=0 "))
                            && Item.Contains(
                                TEXT("Type=")
                                + FullBodySolverClassPath);
                    }));

            FThomasAnimationPatchRequest MoveFBIKBackRequest;
            MoveFBIKBackRequest.AssetPath = IKRigPath;
            MoveFBIKBackRequest.ExpectedRevision =
                IKRigAfterFBIKMove.Revision;
            FThomasAnimationOperation MoveFBIKBack;
            MoveFBIKBack.Action = TEXT("move_ik_solver");
            MoveFBIKBack.Index = 0;
            MoveFBIKBack.TargetIndex = 1;
            MoveFBIKBackRequest.Operations.Add(MoveFBIKBack);
            const FThomasAnimationPlanResult MoveFBIKBackPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    MoveFBIKBackRequest);
            TestTrue(TEXT("IK Rig inverse solver reorder plan succeeds"),
                MoveFBIKBackPlan.bOk);
            TestTrue(TEXT("IK Rig inverse solver reorder apply saves"),
                UThomasDomainsToolset::ApplyAnimationPlan(
                    MoveFBIKBackPlan.PlanId, true).bOk);
            UObject* MovedFBIKBack = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText MovedFBIKBackReloadError;
            TestTrue(TEXT("IK Rig inverse solver reorder reloads from disk"),
                MovedFBIKBack
                    && UPackageTools::ReloadPackages(
                        {MovedFBIKBack->GetOutermost()},
                        MovedFBIKBackReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult
                IKRigAfterFBIKMoveBack =
                    UThomasDomainsToolset::InspectAnimationAsset(
                        IKRigPath, 600);
            TestTrue(TEXT("IK Rig inverse solver reorder persists after reload"),
                IKRigAfterFBIKMoveBack.Items.ContainsByPredicate(
                    [&](const FString& Item)
                    {
                        return Item.StartsWith(
                                TEXT("IKRigSolver Index=1 "))
                            && Item.Contains(
                                TEXT("Type=")
                                + FullBodySolverClassPath);
                    }));

            FThomasAnimationPatchRequest ConfigureFBIKRequest;
            ConfigureFBIKRequest.AssetPath = IKRigPath;
            ConfigureFBIKRequest.ExpectedRevision =
                IKRigAfterFBIKMoveBack.Revision;
            FThomasAnimationOperation SetFBIKRoot;
            SetFBIKRoot.Action = TEXT("set_ik_solver_start_bone");
            SetFBIKRoot.Index = 1;
            SetFBIKRoot.BoneName = IKRigRootBone;
            ConfigureFBIKRequest.Operations.Add(SetFBIKRoot);
            FThomasAnimationOperation ConnectFBIKGoal;
            ConnectFBIKGoal.Action = TEXT("connect_ik_goal_to_solver");
            ConnectFBIKGoal.Index = 1;
            ConnectFBIKGoal.GoalName = TEXT("TE_EndGoal");
            ConfigureFBIKRequest.Operations.Add(ConnectFBIKGoal);
            const FThomasAnimationPlanResult ConfigureFBIKPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    ConfigureFBIKRequest);
            TestTrue(TEXT("Full Body IK root and goal plan succeeds"),
                ConfigureFBIKPlan.bOk);
            TestTrue(TEXT("Full Body IK root and goal apply saves"),
                UThomasDomainsToolset::ApplyAnimationPlan(
                    ConfigureFBIKPlan.PlanId, true).bOk);
            UObject* ConfiguredFBIK = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText ConfiguredFBIKReloadError;
            TestTrue(TEXT("Full Body IK root and goal reload from disk"),
                ConfiguredFBIK
                    && UPackageTools::ReloadPackages(
                        {ConfiguredFBIK->GetOutermost()},
                        ConfiguredFBIKReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult
                IKRigAfterFBIKConfig =
                    UThomasDomainsToolset::InspectAnimationAsset(
                        IKRigPath, 600);

            FThomasAnimationPatchRequest AddFBIKBoneSettingRequest;
            AddFBIKBoneSettingRequest.AssetPath = IKRigPath;
            AddFBIKBoneSettingRequest.ExpectedRevision =
                IKRigAfterFBIKConfig.Revision;
            FThomasAnimationOperation AddFBIKBoneSetting;
            AddFBIKBoneSetting.Action = TEXT("add_ik_bone_setting");
            AddFBIKBoneSetting.Index = 1;
            AddFBIKBoneSetting.BoneName = IKRigChainStartBone;
            AddFBIKBoneSettingRequest.Operations.Add(AddFBIKBoneSetting);
            const FThomasAnimationPlanResult AddFBIKBoneSettingPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    AddFBIKBoneSettingRequest);
            TestTrue(TEXT("Full Body IK bone-setting plan succeeds"),
                AddFBIKBoneSettingPlan.bOk);
            TestTrue(TEXT("Full Body IK bone-setting apply saves"),
                UThomasDomainsToolset::ApplyAnimationPlan(
                    AddFBIKBoneSettingPlan.PlanId, true).bOk);
            UObject* FBIKWithBoneSetting = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText FBIKWithBoneSettingReloadError;
            TestTrue(TEXT("Full Body IK bone setting reloads from disk"),
                FBIKWithBoneSetting
                    && UPackageTools::ReloadPackages(
                        {FBIKWithBoneSetting->GetOutermost()},
                        FBIKWithBoneSettingReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult
                IKRigWithEditableFBIKSettings =
                    UThomasDomainsToolset::InspectAnimationAsset(
                        IKRigPath, 800);
            auto FindIKRigSettingValue = [&IKRigWithEditableFBIKSettings](
                const FString& Scope,
                const int32 SolverIndex,
                const FString& Subject,
                const FString& PropertyName)
            {
                const FString Prefix = FString::Printf(
                    TEXT("IKRigSetting Scope=%s Index=%d Subject=%s "),
                    *Scope, SolverIndex, *Subject);
                const FString PropertyToken =
                    TEXT(" Property=") + PropertyName + TEXT(" Value=");
                const FString* Item =
                    IKRigWithEditableFBIKSettings.Items.FindByPredicate(
                        [&](const FString& Candidate)
                        {
                            return Candidate.StartsWith(Prefix)
                                && Candidate.Contains(PropertyToken);
                        });
                FString Left;
                FString Value;
                if (Item)
                {
                    Item->Split(
                        PropertyToken,
                        &Left,
                        &Value,
                        ESearchCase::CaseSensitive,
                        ESearchDir::FromEnd);
                }
                return Value;
            };
            const FString FBIKAllowStretchValue = FindIKRigSettingValue(
                TEXT("solver"), 1, TEXT("-"), TEXT("bAllowStretch"));
            const FString FBIKStrengthValue = FindIKRigSettingValue(
                TEXT("goal"), 1, TEXT("TE_EndGoal"),
                TEXT("StrengthAlpha"));
            const FString FBIKStiffnessValue = FindIKRigSettingValue(
                TEXT("bone"), 1, IKRigChainStartBone,
                TEXT("RotationStiffness"));
            TestTrue(TEXT("Full Body IK solver, goal and bone fields are inspectable"),
                !FBIKAllowStretchValue.IsEmpty()
                    && !FBIKStrengthValue.IsEmpty()
                    && !FBIKStiffnessValue.IsEmpty());

            FThomasAnimationPatchRequest StaleFBIKSettingRequest;
            StaleFBIKSettingRequest.AssetPath = IKRigPath;
            StaleFBIKSettingRequest.ExpectedRevision =
                IKRigWithEditableFBIKSettings.Revision;
            FThomasAnimationOperation StaleFBIKSetting;
            StaleFBIKSetting.Action = TEXT("set_ik_solver_setting");
            StaleFBIKSetting.Index = 1;
            StaleFBIKSetting.PropertyName = TEXT("bAllowStretch");
            StaleFBIKSetting.ExpectedValue = TEXT("stale");
            StaleFBIKSetting.SettingValue = TEXT("True");
            StaleFBIKSettingRequest.Operations.Add(StaleFBIKSetting);
            TestEqual(TEXT("IK Rig setting edit rejects a stale precondition"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    StaleFBIKSettingRequest).Code,
                FString(TEXT("ik_setting_precondition_failed")));

            FThomasAnimationPatchRequest InvalidFBIKSettingRequest;
            InvalidFBIKSettingRequest.AssetPath = IKRigPath;
            InvalidFBIKSettingRequest.ExpectedRevision =
                IKRigWithEditableFBIKSettings.Revision;
            FThomasAnimationOperation InvalidFBIKSetting;
            InvalidFBIKSetting.Action = TEXT("set_ik_goal_setting");
            InvalidFBIKSetting.Index = 1;
            InvalidFBIKSetting.GoalName = TEXT("TE_EndGoal");
            InvalidFBIKSetting.PropertyName = TEXT("StrengthAlpha");
            InvalidFBIKSetting.ExpectedValue = FBIKStrengthValue;
            InvalidFBIKSetting.SettingValue = TEXT("2.0");
            InvalidFBIKSettingRequest.Operations.Add(InvalidFBIKSetting);
            TestEqual(TEXT("IK Rig setting edit enforces reflected clamps"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    InvalidFBIKSettingRequest).Code,
                FString(TEXT("invalid_ik_setting_value")));

            FThomasAnimationPatchRequest EditFBIKSettingsRequest;
            EditFBIKSettingsRequest.AssetPath = IKRigPath;
            EditFBIKSettingsRequest.ExpectedRevision =
                IKRigWithEditableFBIKSettings.Revision;
            FThomasAnimationOperation EditFBIKSolverSetting;
            EditFBIKSolverSetting.Action = TEXT("set_ik_solver_setting");
            EditFBIKSolverSetting.Index = 1;
            EditFBIKSolverSetting.PropertyName = TEXT("bAllowStretch");
            EditFBIKSolverSetting.ExpectedValue = FBIKAllowStretchValue;
            EditFBIKSolverSetting.SettingValue =
                FBIKAllowStretchValue == TEXT("True")
                    ? TEXT("False") : TEXT("True");
            EditFBIKSettingsRequest.Operations.Add(
                EditFBIKSolverSetting);
            FThomasAnimationOperation EditFBIKGoalSetting;
            EditFBIKGoalSetting.Action = TEXT("set_ik_goal_setting");
            EditFBIKGoalSetting.Index = 1;
            EditFBIKGoalSetting.GoalName = TEXT("TE_EndGoal");
            EditFBIKGoalSetting.PropertyName = TEXT("StrengthAlpha");
            EditFBIKGoalSetting.ExpectedValue = FBIKStrengthValue;
            EditFBIKGoalSetting.SettingValue = TEXT("0.25");
            EditFBIKSettingsRequest.Operations.Add(EditFBIKGoalSetting);
            FThomasAnimationOperation EditFBIKBoneSetting;
            EditFBIKBoneSetting.Action = TEXT("set_ik_bone_setting");
            EditFBIKBoneSetting.Index = 1;
            EditFBIKBoneSetting.BoneName = IKRigChainStartBone;
            EditFBIKBoneSetting.PropertyName = TEXT("RotationStiffness");
            EditFBIKBoneSetting.ExpectedValue = FBIKStiffnessValue;
            EditFBIKBoneSetting.SettingValue = TEXT("0.65");
            EditFBIKSettingsRequest.Operations.Add(EditFBIKBoneSetting);
            const FThomasAnimationPlanResult EditFBIKSettingsPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    EditFBIKSettingsRequest);
            TestTrue(TEXT("Generic Full Body IK settings plan succeeds"),
                EditFBIKSettingsPlan.bOk);
            const FThomasAnimationApplyResult EditFBIKSettingsApply =
                UThomasDomainsToolset::ApplyAnimationPlan(
                    EditFBIKSettingsPlan.PlanId, true);
            AddInfo(FString::Printf(
                TEXT("IK Rig generic settings apply: ok=%d code=%s message=%s saved=%d"),
                EditFBIKSettingsApply.bOk ? 1 : 0,
                *EditFBIKSettingsApply.Code,
                *EditFBIKSettingsApply.Message,
                EditFBIKSettingsApply.bSaved ? 1 : 0));
            TestTrue(TEXT("Generic Full Body IK settings apply saves"),
                EditFBIKSettingsApply.bOk
                    && EditFBIKSettingsApply.bSaved);
            UObject* EditedFBIKSettings = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText EditedFBIKSettingsReloadError;
            TestTrue(TEXT("Generic Full Body IK settings reload from disk"),
                EditedFBIKSettings
                    && UPackageTools::ReloadPackages(
                        {EditedFBIKSettings->GetOutermost()},
                        EditedFBIKSettingsReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult IKRigAfterGenericFBIK =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRigPath, 800);
            TestTrue(TEXT("Generic solver, goal and bone settings persist after reload"),
                IKRigAfterGenericFBIK.Items.ContainsByPredicate(
                    [&](const FString& Item)
                    {
                        return Item.StartsWith(
                                TEXT("IKRigSetting Scope=solver Index=1 "))
                            && Item.Contains(
                                TEXT(" Property=bAllowStretch Value=")
                                + EditFBIKSolverSetting.SettingValue);
                    })
                    && IKRigAfterGenericFBIK.Items.ContainsByPredicate(
                        [](const FString& Item)
                        {
                            return Item.StartsWith(
                                    TEXT("IKRigSetting Scope=goal Index=1 Subject=TE_EndGoal "))
                                && Item.Contains(
                                    TEXT(" Property=StrengthAlpha Value=0.250000"));
                        })
                    && IKRigAfterGenericFBIK.Items.ContainsByPredicate(
                        [&](const FString& Item)
                        {
                            return Item.StartsWith(
                                    TEXT("IKRigSetting Scope=bone Index=1 Subject=")
                                    + IKRigChainStartBone + TEXT(" "))
                                && Item.Contains(
                                    TEXT(" Property=RotationStiffness Value=0.650000"));
                        }));

            FThomasAnimationPatchRequest RemoveFBIKRequest;
            RemoveFBIKRequest.AssetPath = IKRigPath;
            RemoveFBIKRequest.ExpectedRevision =
                IKRigAfterGenericFBIK.Revision;
            FThomasAnimationOperation RemoveFBIKBoneSetting;
            RemoveFBIKBoneSetting.Action =
                TEXT("remove_ik_bone_setting");
            RemoveFBIKBoneSetting.Index = 1;
            RemoveFBIKBoneSetting.BoneName = IKRigChainStartBone;
            RemoveFBIKRequest.Operations.Add(RemoveFBIKBoneSetting);
            FThomasAnimationOperation DisconnectFBIKGoal;
            DisconnectFBIKGoal.Action =
                TEXT("disconnect_ik_goal_from_solver");
            DisconnectFBIKGoal.Index = 1;
            DisconnectFBIKGoal.GoalName = TEXT("TE_EndGoal");
            RemoveFBIKRequest.Operations.Add(DisconnectFBIKGoal);
            FThomasAnimationOperation RemoveFBIKSolver;
            RemoveFBIKSolver.Action = TEXT("remove_ik_solver");
            RemoveFBIKSolver.Index = 1;
            RemoveFBIKRequest.Operations.Add(RemoveFBIKSolver);
            TestEqual(TEXT("Full Body IK cleanup requires R2 confirmation"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    RemoveFBIKRequest).Code,
                FString(TEXT("confirmation_required")));
            RemoveFBIKRequest.bConfirmDestructive = true;
            const FThomasAnimationPlanResult RemoveFBIKPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    RemoveFBIKRequest);
            TestTrue(TEXT("Full Body IK cleanup plan succeeds"),
                RemoveFBIKPlan.bOk);
            TestTrue(TEXT("Full Body IK cleanup apply saves"),
                UThomasDomainsToolset::ApplyAnimationPlan(
                    RemoveFBIKPlan.PlanId, true).bOk);
            UObject* CleanedFBIK = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText CleanedFBIKReloadError;
            TestTrue(TEXT("Full Body IK cleanup reloads from disk"),
                CleanedFBIK
                    && UPackageTools::ReloadPackages(
                        {CleanedFBIK->GetOutermost()},
                        CleanedFBIKReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult IKRigAfterFBIKCleanup =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRigPath, 500);
            TestTrue(TEXT("Full Body IK solver and settings are absent after R2 reload"),
                IKRigAfterFBIKCleanup.Metrics.Contains(TEXT("SolverCount=1"))
                    && !IKRigAfterFBIKCleanup.Items.ContainsByPredicate(
                        [](const FString& Item)
                        {
                            return Item.StartsWith(
                                TEXT("IKRigSetting Scope=solver Index=1 "));
                        }));

            const FString* PoleSolverTypeItem =
                IKRigAfterFBIKCleanup.Items.FindByPredicate(
                    [](const FString& Item)
                    {
                        return Item.StartsWith(
                                TEXT("IKRigSolverType Path="))
                            && Item.EndsWith(TEXT("IKRigPoleSolver"));
                    });
            TestNotNull(TEXT("IK Rig exposes the native Pole solver"),
                PoleSolverTypeItem);
            const FString PoleSolverClassPath = PoleSolverTypeItem
                ? PoleSolverTypeItem->RightChop(
                    FString(TEXT("IKRigSolverType Path=")).Len())
                : FString();
            FThomasAnimationPatchRequest AddPoleSolverRequest;
            AddPoleSolverRequest.AssetPath = IKRigPath;
            AddPoleSolverRequest.ExpectedRevision =
                IKRigAfterFBIKCleanup.Revision;
            FThomasAnimationOperation AddPoleSolver;
            AddPoleSolver.Action = TEXT("add_ik_solver");
            AddPoleSolver.ClassPath = PoleSolverClassPath;
            AddPoleSolverRequest.Operations.Add(AddPoleSolver);
            const FThomasAnimationPlanResult AddPoleSolverPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    AddPoleSolverRequest);
            TestTrue(TEXT("IK Rig Pole solver plan succeeds"),
                AddPoleSolverPlan.bOk);
            TestTrue(TEXT("IK Rig Pole solver apply saves"),
                UThomasDomainsToolset::ApplyAnimationPlan(
                    AddPoleSolverPlan.PlanId, true).bOk);
            UObject* IKRigWithPoleSolver = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText IKRigWithPoleSolverReloadError;
            TestTrue(TEXT("IK Rig Pole solver reloads from disk"),
                IKRigWithPoleSolver
                    && UPackageTools::ReloadPackages(
                        {IKRigWithPoleSolver->GetOutermost()},
                        IKRigWithPoleSolverReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult IKRigAfterPoleAdd =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRigPath, 500);

            FThomasAnimationPatchRequest ConfigurePoleSolverRequest;
            ConfigurePoleSolverRequest.AssetPath = IKRigPath;
            ConfigurePoleSolverRequest.ExpectedRevision =
                IKRigAfterPoleAdd.Revision;
            FThomasAnimationOperation SetPoleStartBone;
            SetPoleStartBone.Action = TEXT("set_ik_solver_start_bone");
            SetPoleStartBone.Index = 1;
            SetPoleStartBone.BoneName = IKRigChainStartBone;
            ConfigurePoleSolverRequest.Operations.Add(SetPoleStartBone);
            FThomasAnimationOperation SetPoleEndBone;
            SetPoleEndBone.Action = TEXT("set_ik_solver_end_bone");
            SetPoleEndBone.Index = 1;
            SetPoleEndBone.BoneName = IKRigChainEndBone;
            ConfigurePoleSolverRequest.Operations.Add(SetPoleEndBone);
            const FThomasAnimationPlanResult ConfigurePoleSolverPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    ConfigurePoleSolverRequest);
            TestTrue(TEXT("IK Rig Pole start/end-bone plan succeeds"),
                ConfigurePoleSolverPlan.bOk);
            TestTrue(TEXT("IK Rig Pole start/end-bone apply saves"),
                UThomasDomainsToolset::ApplyAnimationPlan(
                    ConfigurePoleSolverPlan.PlanId, true).bOk);
            UObject* ConfiguredPoleSolver = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText ConfiguredPoleSolverReloadError;
            TestTrue(TEXT("IK Rig Pole start/end bones reload from disk"),
                ConfiguredPoleSolver
                    && UPackageTools::ReloadPackages(
                        {ConfiguredPoleSolver->GetOutermost()},
                        ConfiguredPoleSolverReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult
                IKRigAfterPoleConfig =
                    UThomasDomainsToolset::InspectAnimationAsset(
                        IKRigPath, 500);
            TestTrue(TEXT("IK Rig Pole start/end bones persist after reload"),
                IKRigAfterPoleConfig.Items.ContainsByPredicate(
                    [&](const FString& Item)
                    {
                        return Item.StartsWith(
                                TEXT("IKRigSolver Index=1 "))
                            && Item.Contains(
                                TEXT("StartBone=")
                                + IKRigChainStartBone)
                            && Item.Contains(
                                TEXT("EndBone=")
                                + IKRigChainEndBone);
                    }));

            FThomasAnimationPatchRequest RemovePoleSolverRequest;
            RemovePoleSolverRequest.AssetPath = IKRigPath;
            RemovePoleSolverRequest.ExpectedRevision =
                IKRigAfterPoleConfig.Revision;
            RemovePoleSolverRequest.bConfirmDestructive = true;
            FThomasAnimationOperation RemovePoleSolver;
            RemovePoleSolver.Action = TEXT("remove_ik_solver");
            RemovePoleSolver.Index = 1;
            RemovePoleSolverRequest.Operations.Add(RemovePoleSolver);
            const FThomasAnimationPlanResult RemovePoleSolverPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    RemovePoleSolverRequest);
            TestTrue(TEXT("IK Rig Pole cleanup plan succeeds"),
                RemovePoleSolverPlan.bOk);
            TestTrue(TEXT("IK Rig Pole cleanup apply saves"),
                UThomasDomainsToolset::ApplyAnimationPlan(
                    RemovePoleSolverPlan.PlanId, true).bOk);
            UObject* CleanedPoleSolver = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText CleanedPoleSolverReloadError;
            TestTrue(TEXT("IK Rig Pole cleanup reloads from disk"),
                CleanedPoleSolver
                    && UPackageTools::ReloadPackages(
                        {CleanedPoleSolver->GetOutermost()},
                        CleanedPoleSolverReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult
                IKRigAfterPoleCleanup =
                    UThomasDomainsToolset::InspectAnimationAsset(
                        IKRigPath, 500);

            FThomasAnimationPatchRequest IKRigRemovalRequest;
            IKRigRemovalRequest.AssetPath = IKRigPath;
            IKRigRemovalRequest.ExpectedRevision =
                IKRigAfterPoleCleanup.Revision;
            FThomasAnimationOperation DisconnectIKGoal;
            DisconnectIKGoal.Action =
                TEXT("disconnect_ik_goal_from_solver");
            DisconnectIKGoal.Index = 0;
            DisconnectIKGoal.GoalName = TEXT("TE_EndGoal");
            IKRigRemovalRequest.Operations.Add(DisconnectIKGoal);
            FThomasAnimationOperation RemoveIKSolver;
            RemoveIKSolver.Action = TEXT("remove_ik_solver");
            RemoveIKSolver.Index = 0;
            IKRigRemovalRequest.Operations.Add(RemoveIKSolver);
            FThomasAnimationOperation RemoveIKChain;
            RemoveIKChain.Action = TEXT("remove_ik_retarget_chain");
            RemoveIKChain.Name = TEXT("TE_FullChain");
            IKRigRemovalRequest.Operations.Add(RemoveIKChain);
            FThomasAnimationOperation RemoveIKGoal;
            RemoveIKGoal.Action = TEXT("remove_ik_goal");
            RemoveIKGoal.Name = TEXT("TE_EndGoal");
            IKRigRemovalRequest.Operations.Add(RemoveIKGoal);
            TestEqual(TEXT("IK Rig removals require R2 confirmation"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    IKRigRemovalRequest).Code,
                FString(TEXT("confirmation_required")));
            IKRigRemovalRequest.bConfirmDestructive = true;
            const FThomasAnimationPlanResult IKRigRemovalPlan =
                UThomasDomainsToolset::PlanAnimationPatch(
                    IKRigRemovalRequest);
            TestTrue(TEXT("Confirmed IK Rig removal plan succeeds"),
                IKRigRemovalPlan.bOk);
            TestTrue(TEXT("IK Rig chain and goal removal save"),
                UThomasDomainsToolset::ApplyAnimationPlan(
                    IKRigRemovalPlan.PlanId, true).bOk);
            UObject* RemovedIKRig = LoadObject<UObject>(
                nullptr,
                *(IKRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(IKRigPath)));
            FText IKRigRemovalReloadError;
            TestTrue(TEXT("Removed IK Rig content reloads from saved file"),
                RemovedIKRig
                    && UPackageTools::ReloadPackages(
                        {RemovedIKRig->GetOutermost()},
                        IKRigRemovalReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult IKRigAfterRemoval =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRigPath, 200);
            TestTrue(TEXT("IK Rig chain and goal are absent after R2 reload"),
                IKRigAfterRemoval.Metrics.Contains(TEXT("GoalCount=0"))
                    && IKRigAfterRemoval.Metrics.Contains(
                        TEXT("SolverCount=0"))
                    && IKRigAfterRemoval.Metrics.Contains(
                        TEXT("RetargetChainCount=0"))
                    && IKRigAfterRemoval.Metrics.Contains(
                        TEXT("RetargetRoot=") + IKRigRootBone));
        }

        FThomasDomainAssetCreateRequest IKRetargeterRequest;
        IKRetargeterRequest.AssetPath = IKRetargeterPath;
        IKRetargeterRequest.ExpectedRevision = TEXT("missing");
        IKRetargeterRequest.AssetKind = TEXT("ik_retargeter");
        IKRetargeterRequest.ContextAssetPath = IKRigPath;
        const FThomasDomainAssetCreatePlanResult IKRetargeterPlan =
            UThomasDomainsToolset::PlanAnimationAssetCreate(
                IKRetargeterRequest);
        TestTrue(TEXT("Native IK Retargeter create plan succeeds"),
            IKRetargeterPlan.bOk);
        const FThomasDomainAssetCreateApplyResult IKRetargeterApply =
            UThomasDomainsToolset::ApplyAnimationAssetCreate(
                IKRetargeterPlan.PlanId, true);
        TestTrue(TEXT("Native IK Retargeter create saves"),
            IKRetargeterApply.bOk && IKRetargeterApply.bSaved);

        auto ReloadIKRetargeter = [&](const TCHAR* TestLabel)
        {
            UObject* RetargeterAsset = LoadObject<UObject>(
                nullptr,
                *(IKRetargeterPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(
                        IKRetargeterPath)));
            FText ReloadError;
            const bool bReloaded = RetargeterAsset
                && UPackageTools::ReloadPackages(
                    {RetargeterAsset->GetOutermost()},
                    ReloadError,
                    EReloadPackagesInteractionMode::AssumePositive);
            TestTrue(TestLabel, bReloaded);
            return bReloaded;
        };
        auto MetricValue = [](const FThomasSpecializedAssetInspectionResult&
                                  Inspection,
                              const FString& Prefix)
        {
            const FString* Metric = Inspection.Metrics.FindByPredicate(
                [&](const FString& Candidate)
                {
                    return Candidate.StartsWith(Prefix);
                });
            return Metric ? Metric->RightChop(Prefix.Len()) : FString();
        };

        ReloadIKRetargeter(
            TEXT("Created IK Retargeter reloads from saved file"));
        const FThomasSpecializedAssetInspectionResult
            CreatedIKRetargeter =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRetargeterPath, 1000);
        TestTrue(TEXT("Created IK Retargeter typed inspection succeeds"),
            CreatedIKRetargeter.bOk
                && CreatedIKRetargeter.AssetKind
                    == TEXT("ik_retargeter")
                && CreatedIKRetargeter.Metrics.Contains(
                    TEXT("SourceIKRig=") + IKRigPath)
                && CreatedIKRetargeter.Metrics.Contains(
                    TEXT("TargetIKRig=None"))
                && CreatedIKRetargeter.Metrics.Contains(
                    TEXT("RetargetOpCount=0"))
                && CreatedIKRetargeter.Metrics.Contains(
                    TEXT("OverrideSetCount=0")));

        FThomasAnimationPatchRequest SetTargetRigRequest;
        SetTargetRigRequest.AssetPath = IKRetargeterPath;
        SetTargetRigRequest.ExpectedRevision =
            CreatedIKRetargeter.Revision;
        FThomasAnimationOperation SetTargetRig;
        SetTargetRig.Action = TEXT("set_ik_retargeter_rig");
        SetTargetRig.RetargetSide = TEXT("target");
        SetTargetRig.ReferencedAssetPath = IKRigPath;
        SetTargetRig.ExpectedValue = TEXT("None");
        SetTargetRigRequest.Operations.Add(SetTargetRig);
        const FThomasAnimationPlanResult SetTargetRigPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                SetTargetRigRequest);
        TestTrue(TEXT("IK Retargeter target-rig plan succeeds"),
            SetTargetRigPlan.bOk);
        const FThomasAnimationApplyResult SetTargetRigApply =
            UThomasDomainsToolset::ApplyAnimationPlan(
                SetTargetRigPlan.PlanId, true);
        TestTrue(TEXT("IK Retargeter target-rig edit saves"),
            SetTargetRigApply.bOk && SetTargetRigApply.bSaved);
        ReloadIKRetargeter(
            TEXT("IK Retargeter target rig reloads from saved file"));
        const FThomasSpecializedAssetInspectionResult
            IKRetargeterWithTarget =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRetargeterPath, 1000);
        TestTrue(TEXT("IK Retargeter source and target rigs persist"),
            IKRetargeterWithTarget.Metrics.Contains(
                TEXT("SourceIKRig=") + IKRigPath)
                && IKRetargeterWithTarget.Metrics.Contains(
                    TEXT("TargetIKRig=") + IKRigPath));

        FThomasAnimationPatchRequest AddDefaultRetargetOpsRequest;
        AddDefaultRetargetOpsRequest.AssetPath = IKRetargeterPath;
        AddDefaultRetargetOpsRequest.ExpectedRevision =
            IKRetargeterWithTarget.Revision;
        FThomasAnimationOperation AddDefaultRetargetOps;
        AddDefaultRetargetOps.Action =
            TEXT("add_default_ik_retarget_ops");
        AddDefaultRetargetOps.ExpectedValue = TEXT("0");
        AddDefaultRetargetOpsRequest.Operations.Add(
            AddDefaultRetargetOps);
        const FThomasAnimationPlanResult AddDefaultRetargetOpsPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                AddDefaultRetargetOpsRequest);
        TestTrue(TEXT("IK Retargeter default-op plan succeeds"),
            AddDefaultRetargetOpsPlan.bOk);
        const FThomasAnimationApplyResult AddDefaultRetargetOpsApply =
            UThomasDomainsToolset::ApplyAnimationPlan(
                AddDefaultRetargetOpsPlan.PlanId, true);
        TestTrue(TEXT("IK Retargeter default-op stack saves"),
            AddDefaultRetargetOpsApply.bOk
                && AddDefaultRetargetOpsApply.bSaved);
        ReloadIKRetargeter(
            TEXT("IK Retargeter default-op stack reloads from disk"));
        const FThomasSpecializedAssetInspectionResult
            IKRetargeterWithDefaultOps =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRetargeterPath, 2000);
        TestTrue(TEXT("Five UE 5.8 default retarget ops persist"),
            IKRetargeterWithDefaultOps.Metrics.Contains(
                TEXT("RetargetOpCount=5"))
                && IKRetargeterWithDefaultOps.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.StartsWith(
                            TEXT("IKRetargetOp Index=4 "));
                    }));

        const FString RetargetPoseOpType =
            TEXT("/Script/IKRig.IKRetargetPoseOp");
        TestTrue(TEXT("Retarget Pose op type is discoverable"),
            IKRetargeterWithDefaultOps.Items.Contains(
                TEXT("IKRetargetOpType Path=")
                    + RetargetPoseOpType));
        FThomasAnimationPatchRequest AddRetargetOpRequest;
        AddRetargetOpRequest.AssetPath = IKRetargeterPath;
        AddRetargetOpRequest.ExpectedRevision =
            IKRetargeterWithDefaultOps.Revision;
        FThomasAnimationOperation AddRetargetOp;
        AddRetargetOp.Action = TEXT("add_ik_retarget_op");
        AddRetargetOp.ClassPath = RetargetPoseOpType;
        AddRetargetOpRequest.Operations.Add(AddRetargetOp);
        const FThomasAnimationPlanResult AddRetargetOpPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                AddRetargetOpRequest);
        TestTrue(TEXT("Arbitrary retarget-op add plan succeeds"),
            AddRetargetOpPlan.bOk);
        TestTrue(TEXT("Arbitrary retarget op saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                AddRetargetOpPlan.PlanId, true).bOk);
        ReloadIKRetargeter(
            TEXT("Arbitrary retarget op reloads from disk"));
        const FThomasSpecializedAssetInspectionResult
            IKRetargeterWithExtraOp =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRetargeterPath, 2000);
        TestTrue(TEXT("Arbitrary Retarget Pose op persists"),
            IKRetargeterWithExtraOp.Metrics.Contains(
                TEXT("RetargetOpCount=6"))
                && IKRetargeterWithExtraOp.Items.ContainsByPredicate(
                    [&](const FString& Item)
                    {
                        return Item.StartsWith(
                                TEXT("IKRetargetOp Index=5 "))
                            && Item.Contains(
                                TEXT(" Type=") + RetargetPoseOpType);
                    }));

        FThomasAnimationPatchRequest RemoveExtraRetargetOpRequest;
        RemoveExtraRetargetOpRequest.AssetPath = IKRetargeterPath;
        RemoveExtraRetargetOpRequest.ExpectedRevision =
            IKRetargeterWithExtraOp.Revision;
        FThomasAnimationOperation RemoveExtraRetargetOp;
        RemoveExtraRetargetOp.Action = TEXT("remove_ik_retarget_op");
        RemoveExtraRetargetOp.Index = 5;
        RemoveExtraRetargetOpRequest.Operations.Add(
            RemoveExtraRetargetOp);
        TestEqual(TEXT("Retarget-op removal requires R2 confirmation"),
            UThomasDomainsToolset::PlanAnimationPatch(
                RemoveExtraRetargetOpRequest).Code,
            FString(TEXT("confirmation_required")));
        RemoveExtraRetargetOpRequest.bConfirmDestructive = true;
        const FThomasAnimationPlanResult RemoveExtraRetargetOpPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                RemoveExtraRetargetOpRequest);
        TestTrue(TEXT("Confirmed retarget-op removal plan succeeds"),
            RemoveExtraRetargetOpPlan.bOk);
        TestTrue(TEXT("Confirmed retarget-op removal saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                RemoveExtraRetargetOpPlan.PlanId, true).bOk);
        ReloadIKRetargeter(
            TEXT("Retarget-op removal reloads from disk"));
        const FThomasSpecializedAssetInspectionResult
            IKRetargeterAfterExtraOpRemoval =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRetargeterPath, 2000);
        TestTrue(TEXT("Removed retarget op is absent after reload"),
            IKRetargeterAfterExtraOpRemoval.Metrics.Contains(
                TEXT("RetargetOpCount=5")));

        FThomasAnimationPatchRequest ConfigureRetargetOpsRequest;
        ConfigureRetargetOpsRequest.AssetPath = IKRetargeterPath;
        ConfigureRetargetOpsRequest.ExpectedRevision =
            IKRetargeterAfterExtraOpRemoval.Revision;
        ConfigureRetargetOpsRequest.bConfirmDestructive = true;
        FThomasAnimationOperation RenameRootMotionOp;
        RenameRootMotionOp.Action = TEXT("rename_ik_retarget_op");
        RenameRootMotionOp.Index = 4;
        RenameRootMotionOp.NewName = TEXT("TE_ProfileOp");
        ConfigureRetargetOpsRequest.Operations.Add(RenameRootMotionOp);
        FThomasAnimationOperation DisableRootMotionOp;
        DisableRootMotionOp.Action =
            TEXT("set_ik_retarget_op_enabled");
        DisableRootMotionOp.Index = 4;
        DisableRootMotionOp.bEnabled = false;
        ConfigureRetargetOpsRequest.Operations.Add(DisableRootMotionOp);
        FThomasAnimationOperation MoveRootMotionOp;
        MoveRootMotionOp.Action = TEXT("move_ik_retarget_op");
        MoveRootMotionOp.Index = 4;
        MoveRootMotionOp.TargetIndex = 0;
        ConfigureRetargetOpsRequest.Operations.Add(MoveRootMotionOp);
        const FThomasAnimationPlanResult ConfigureRetargetOpsPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                ConfigureRetargetOpsRequest);
        TestTrue(TEXT("Retarget-op rename, enabled-state and move plan succeeds"),
            ConfigureRetargetOpsPlan.bOk);
        TestTrue(TEXT("Retarget-op rename, enabled-state and move save"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                ConfigureRetargetOpsPlan.PlanId, true).bOk);
        ReloadIKRetargeter(
            TEXT("Configured retarget-op stack reloads from disk"));
        const FThomasSpecializedAssetInspectionResult
            IKRetargeterWithConfiguredOps =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRetargeterPath, 2000);
        TestTrue(TEXT("Retarget-op name, order and disabled state persist"),
            IKRetargeterWithConfiguredOps.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.StartsWith(
                            TEXT("IKRetargetOp Index=0 Name=TE_ProfileOp "))
                        && Item.Contains(TEXT(" Enabled=false "));
                }));

        FThomasAnimationPatchRequest AddOverrideSetsRequest;
        AddOverrideSetsRequest.AssetPath = IKRetargeterPath;
        AddOverrideSetsRequest.ExpectedRevision =
            IKRetargeterWithConfiguredOps.Revision;
        FThomasAnimationOperation AddBaseOverrideSet;
        AddBaseOverrideSet.Action =
            TEXT("add_ik_retarget_override_set");
        AddBaseOverrideSet.Name = TEXT("TE_ProfileBase");
        AddOverrideSetsRequest.Operations.Add(AddBaseOverrideSet);
        FThomasAnimationOperation AddFastOverrideSet;
        AddFastOverrideSet.Action =
            TEXT("add_ik_retarget_override_set");
        AddFastOverrideSet.Name = TEXT("TE_ProfileFast");
        AddOverrideSetsRequest.Operations.Add(AddFastOverrideSet);
        const FThomasAnimationPlanResult AddOverrideSetsPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                AddOverrideSetsRequest);
        TestTrue(TEXT("IK Retargeter override-set add plan succeeds"),
            AddOverrideSetsPlan.bOk);
        TestTrue(TEXT("IK Retargeter override sets save"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                AddOverrideSetsPlan.PlanId, true).bOk);
        ReloadIKRetargeter(
            TEXT("IK Retargeter override sets reload from disk"));
        const FThomasSpecializedAssetInspectionResult
            IKRetargeterWithOverrideSets =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRetargeterPath, 2000);
        TestTrue(TEXT("Two IK Retargeter override sets persist"),
            IKRetargeterWithOverrideSets.Metrics.Contains(
                TEXT("OverrideSetCount=2")));

        const FString RootMotionProperty =
            TEXT("bCopyAllSourceCurves");
        const FString RootMotionSettingToken =
            TEXT(" Property=") + RootMotionProperty + TEXT(" Value=");
        const FString* RootMotionSettingItem =
            IKRetargeterWithOverrideSets.Items.FindByPredicate(
                [&](const FString& Item)
                {
                    return Item.StartsWith(
                            TEXT("IKRetargetOpSetting Index=0 Op=TE_ProfileOp "))
                        && Item.Contains(RootMotionSettingToken);
                });
        FString RootMotionSettingLeft;
        FString RootMotionSettingValue;
        if (RootMotionSettingItem)
        {
            RootMotionSettingItem->Split(
                RootMotionSettingToken,
                &RootMotionSettingLeft,
                &RootMotionSettingValue,
                ESearchCase::CaseSensitive,
                ESearchDir::FromEnd);
        }
        for (const FString& Item : IKRetargeterWithOverrideSets.Items)
        {
            if (Item.StartsWith(
                    TEXT("IKRetargetOpSetting Index=0 Op=TE_ProfileOp ")))
            {
                AddInfo(TEXT("IK Retarget profile-op setting: ") + Item);
            }
        }
        TestTrue(FString::Printf(
                TEXT("Profile op overrideable bool is inspectable (value=%s)"),
                *RootMotionSettingValue),
            RootMotionSettingValue == TEXT("True")
                || RootMotionSettingValue == TEXT("False"));
        const FString NewRootMotionSettingValue =
            RootMotionSettingValue == TEXT("True")
                ? TEXT("False") : TEXT("True");

        FThomasAnimationPatchRequest StaleOverrideRequest;
        StaleOverrideRequest.AssetPath = IKRetargeterPath;
        StaleOverrideRequest.ExpectedRevision =
            IKRetargeterWithOverrideSets.Revision;
        FThomasAnimationOperation StaleOverride;
        StaleOverride.Action =
            TEXT("set_ik_retarget_property_override");
        StaleOverride.Name = TEXT("TE_ProfileFast");
        StaleOverride.RetargetOpName = TEXT("TE_ProfileOp");
        StaleOverride.PropertyName = RootMotionProperty;
        StaleOverride.ExpectedValue = TEXT("stale");
        StaleOverride.SettingValue = NewRootMotionSettingValue;
        StaleOverrideRequest.Operations.Add(StaleOverride);
        TestEqual(TEXT("IK Retargeter property override rejects stale value"),
            UThomasDomainsToolset::PlanAnimationPatch(
                StaleOverrideRequest).Code,
            FString(TEXT("invalid_ik_retarget_property_override")));

        FThomasAnimationPatchRequest ConfigureOverrideRequest;
        ConfigureOverrideRequest.AssetPath = IKRetargeterPath;
        ConfigureOverrideRequest.ExpectedRevision =
            IKRetargeterWithOverrideSets.Revision;
        FThomasAnimationOperation ActivateFastOverride;
        ActivateFastOverride.Action =
            TEXT("set_ik_retarget_override_set_active");
        ActivateFastOverride.Name = TEXT("TE_ProfileFast");
        ActivateFastOverride.ExpectedValue = TEXT("false");
        ActivateFastOverride.bEnabled = true;
        ConfigureOverrideRequest.Operations.Add(ActivateFastOverride);
        FThomasAnimationOperation ParentFastOverride;
        ParentFastOverride.Action =
            TEXT("set_ik_retarget_override_set_parent");
        ParentFastOverride.Name = TEXT("TE_ProfileFast");
        ParentFastOverride.ParentName = TEXT("TE_ProfileBase");
        ParentFastOverride.ExpectedValue = TEXT("None");
        ConfigureOverrideRequest.Operations.Add(ParentFastOverride);
        FThomasAnimationOperation SetRootMotionOverride;
        SetRootMotionOverride.Action =
            TEXT("set_ik_retarget_property_override");
        SetRootMotionOverride.Name = TEXT("TE_ProfileFast");
        SetRootMotionOverride.RetargetOpName = TEXT("TE_ProfileOp");
        SetRootMotionOverride.PropertyName = RootMotionProperty;
        SetRootMotionOverride.ExpectedValue = RootMotionSettingValue;
        SetRootMotionOverride.SettingValue = NewRootMotionSettingValue;
        ConfigureOverrideRequest.Operations.Add(SetRootMotionOverride);
        const FThomasAnimationPlanResult ConfigureOverridePlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                ConfigureOverrideRequest);
        TestTrue(FString::Printf(
                TEXT("Hierarchical typed override plan succeeds (code=%s message=%s)"),
                *ConfigureOverridePlan.Code,
                *ConfigureOverridePlan.Message),
            ConfigureOverridePlan.bOk);
        const FThomasAnimationApplyResult ConfigureOverrideApply =
            UThomasDomainsToolset::ApplyAnimationPlan(
                ConfigureOverridePlan.PlanId, true);
        TestTrue(FString::Printf(
                TEXT("Hierarchical typed override saves (code=%s message=%s)"),
                *ConfigureOverrideApply.Code,
                *ConfigureOverrideApply.Message),
            ConfigureOverrideApply.bOk && ConfigureOverrideApply.bSaved);
        ReloadIKRetargeter(
            TEXT("Hierarchical typed override reloads from disk"));
        const FThomasSpecializedAssetInspectionResult
            IKRetargeterWithTypedOverride =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRetargeterPath, 2000);
        TestTrue(TEXT("Override parent, active flag and value persist"),
            IKRetargeterWithTypedOverride.Metrics.Contains(
                TEXT("ActiveOverrideSetCount=1"))
                && IKRetargeterWithTypedOverride.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.StartsWith(
                                TEXT("IKRetargetOverrideSet Name=TE_ProfileFast "))
                            && Item.Contains(
                                TEXT(" Parent=TE_ProfileBase Active=true "))
                            && Item.Contains(
                                TEXT("PropertyOverrideCount=1"));
                    })
                && IKRetargeterWithTypedOverride.Items.ContainsByPredicate(
                    [&](const FString& Item)
                    {
                        return Item.StartsWith(
                                TEXT("IKRetargetPropertyOverride Set=TE_ProfileFast Op=TE_ProfileOp "))
                            && Item.Contains(
                                TEXT(" Property=") + RootMotionProperty
                                    + TEXT(" Value=")
                                    + NewRootMotionSettingValue);
                    }));

        FThomasAnimationPatchRequest AddRetargetPosesRequest;
        AddRetargetPosesRequest.AssetPath = IKRetargeterPath;
        AddRetargetPosesRequest.ExpectedRevision =
            IKRetargeterWithTypedOverride.Revision;
        FThomasAnimationOperation AddSourcePose;
        AddSourcePose.Action = TEXT("add_ik_retarget_pose");
        AddSourcePose.RetargetSide = TEXT("source");
        AddSourcePose.Name = TEXT("TE_SourcePose");
        AddRetargetPosesRequest.Operations.Add(AddSourcePose);
        FThomasAnimationOperation AddTargetPose;
        AddTargetPose.Action = TEXT("add_ik_retarget_pose");
        AddTargetPose.RetargetSide = TEXT("target");
        AddTargetPose.Name = TEXT("TE_TargetPose");
        AddRetargetPosesRequest.Operations.Add(AddTargetPose);
        const FThomasAnimationPlanResult AddRetargetPosesPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                AddRetargetPosesRequest);
        TestTrue(TEXT("Source and target retarget-pose add plan succeeds"),
            AddRetargetPosesPlan.bOk);
        TestTrue(TEXT("Source and target retarget poses save"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                AddRetargetPosesPlan.PlanId, true).bOk);
        ReloadIKRetargeter(
            TEXT("Source and target retarget poses reload from disk"));
        const FThomasSpecializedAssetInspectionResult
            IKRetargeterWithPoses =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRetargeterPath, 2000);
        TestTrue(TEXT("Source and target retarget poses persist"),
            IKRetargeterWithPoses.Metrics.Contains(
                TEXT("SourcePoseCount=2"))
                && IKRetargeterWithPoses.Metrics.Contains(
                    TEXT("TargetPoseCount=2")));

        const FString SourceCurrentPose = MetricValue(
            IKRetargeterWithPoses, TEXT("SourceCurrentPose="));
        const FString TargetCurrentPose = MetricValue(
            IKRetargeterWithPoses, TEXT("TargetCurrentPose="));
        const FString SourceRootOffset = MetricValue(
            IKRetargeterWithPoses,
            TEXT("SourceCurrentPoseRootOffset="));
        FThomasAnimationPatchRequest InvalidRetargetRootOffsetRequest;
        InvalidRetargetRootOffsetRequest.AssetPath = IKRetargeterPath;
        InvalidRetargetRootOffsetRequest.ExpectedRevision =
            IKRetargeterWithPoses.Revision;
        FThomasAnimationOperation InvalidRetargetRootOffset;
        InvalidRetargetRootOffset.Action =
            TEXT("set_ik_retarget_pose_root_offset");
        InvalidRetargetRootOffset.RetargetSide = TEXT("source");
        InvalidRetargetRootOffset.ExpectedValue = SourceRootOffset;
        InvalidRetargetRootOffset.Location = FVector(1.0, 2.0, 3.0);
        InvalidRetargetRootOffsetRequest.Operations.Add(
            InvalidRetargetRootOffset);
        TestFalse(TEXT("IK Retarget root offset rejects non-persistent X/Y axes"),
            UThomasDomainsToolset::PlanAnimationPatch(
                InvalidRetargetRootOffsetRequest).bOk);

        FThomasAnimationPatchRequest ConfigureRetargetPosesRequest;
        ConfigureRetargetPosesRequest.AssetPath = IKRetargeterPath;
        ConfigureRetargetPosesRequest.ExpectedRevision =
            IKRetargeterWithPoses.Revision;
        FThomasAnimationOperation SetSourcePose;
        SetSourcePose.Action = TEXT("set_current_ik_retarget_pose");
        SetSourcePose.RetargetSide = TEXT("source");
        SetSourcePose.Name = TEXT("TE_SourcePose");
        SetSourcePose.ExpectedValue = SourceCurrentPose;
        ConfigureRetargetPosesRequest.Operations.Add(SetSourcePose);
        FThomasAnimationOperation SetTargetPose;
        SetTargetPose.Action = TEXT("set_current_ik_retarget_pose");
        SetTargetPose.RetargetSide = TEXT("target");
        SetTargetPose.Name = TEXT("TE_TargetPose");
        SetTargetPose.ExpectedValue = TargetCurrentPose;
        ConfigureRetargetPosesRequest.Operations.Add(SetTargetPose);
        FThomasAnimationOperation SetSourcePoseRootOffset;
        SetSourcePoseRootOffset.Action =
            TEXT("set_ik_retarget_pose_root_offset");
        SetSourcePoseRootOffset.RetargetSide = TEXT("source");
        SetSourcePoseRootOffset.ExpectedValue = SourceRootOffset;
        SetSourcePoseRootOffset.Location = FVector(0.0, 0.0, 3.0);
        ConfigureRetargetPosesRequest.Operations.Add(
            SetSourcePoseRootOffset);
        const FThomasAnimationPlanResult ConfigureRetargetPosesPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                ConfigureRetargetPosesRequest);
        TestTrue(TEXT("Current retarget poses and root offset plan succeeds"),
            ConfigureRetargetPosesPlan.bOk);
        const FThomasAnimationApplyResult ConfigureRetargetPosesApply =
            UThomasDomainsToolset::ApplyAnimationPlan(
                ConfigureRetargetPosesPlan.PlanId, true);
        TestTrue(FString::Printf(
                TEXT("Current retarget poses and root offset save (code=%s message=%s diagnostics=%s)"),
                *ConfigureRetargetPosesApply.Code,
                *ConfigureRetargetPosesApply.Message,
                *FString::Join(
                    ConfigureRetargetPosesApply.Diagnostics, TEXT(";"))),
            ConfigureRetargetPosesApply.bOk
                && ConfigureRetargetPosesApply.bSaved);
        ReloadIKRetargeter(
            TEXT("Current retarget poses and root offset reload from disk"));
        const FThomasSpecializedAssetInspectionResult
            IKRetargeterWithConfiguredPoses =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRetargeterPath, 2000);
        TestTrue(TEXT("Current retarget poses and root offset persist"),
            IKRetargeterWithConfiguredPoses.Metrics.Contains(
                TEXT("SourceCurrentPose=TE_SourcePose"))
                && IKRetargeterWithConfiguredPoses.Metrics.Contains(
                    TEXT("TargetCurrentPose=TE_TargetPose"))
                && IKRetargeterWithConfiguredPoses.Metrics.Contains(
                    TEXT("SourceCurrentPoseRootOffset=X=0.000 Y=0.000 Z=3.000")));

        FThomasAnimationPatchRequest CleanupIKRetargeterRequest;
        CleanupIKRetargeterRequest.AssetPath = IKRetargeterPath;
        CleanupIKRetargeterRequest.ExpectedRevision =
            IKRetargeterWithConfiguredPoses.Revision;
        FThomasAnimationOperation RemoveRootMotionOverride;
        RemoveRootMotionOverride.Action =
            TEXT("remove_ik_retarget_property_override");
        RemoveRootMotionOverride.Name = TEXT("TE_ProfileFast");
        RemoveRootMotionOverride.RetargetOpName = TEXT("TE_ProfileOp");
        RemoveRootMotionOverride.PropertyName = RootMotionProperty;
        RemoveRootMotionOverride.ExpectedValue =
            NewRootMotionSettingValue;
        CleanupIKRetargeterRequest.Operations.Add(
            RemoveRootMotionOverride);
        FThomasAnimationOperation RemoveFastOverrideSet;
        RemoveFastOverrideSet.Action =
            TEXT("remove_ik_retarget_override_set");
        RemoveFastOverrideSet.Name = TEXT("TE_ProfileFast");
        CleanupIKRetargeterRequest.Operations.Add(RemoveFastOverrideSet);
        FThomasAnimationOperation RemoveBaseOverrideSet;
        RemoveBaseOverrideSet.Action =
            TEXT("remove_ik_retarget_override_set");
        RemoveBaseOverrideSet.Name = TEXT("TE_ProfileBase");
        CleanupIKRetargeterRequest.Operations.Add(RemoveBaseOverrideSet);
        FThomasAnimationOperation RemoveSourcePose;
        RemoveSourcePose.Action = TEXT("remove_ik_retarget_pose");
        RemoveSourcePose.RetargetSide = TEXT("source");
        RemoveSourcePose.Name = TEXT("TE_SourcePose");
        CleanupIKRetargeterRequest.Operations.Add(RemoveSourcePose);
        FThomasAnimationOperation RemoveTargetPose;
        RemoveTargetPose.Action = TEXT("remove_ik_retarget_pose");
        RemoveTargetPose.RetargetSide = TEXT("target");
        RemoveTargetPose.Name = TEXT("TE_TargetPose");
        CleanupIKRetargeterRequest.Operations.Add(RemoveTargetPose);
        FThomasAnimationOperation RemoveConfiguredRootMotionOp;
        RemoveConfiguredRootMotionOp.Action =
            TEXT("remove_ik_retarget_op");
        RemoveConfiguredRootMotionOp.Index = 0;
        CleanupIKRetargeterRequest.Operations.Add(
            RemoveConfiguredRootMotionOp);
        TestEqual(TEXT("IK Retargeter cleanup requires R2 confirmation"),
            UThomasDomainsToolset::PlanAnimationPatch(
                CleanupIKRetargeterRequest).Code,
            FString(TEXT("confirmation_required")));
        CleanupIKRetargeterRequest.bConfirmDestructive = true;
        const FThomasAnimationPlanResult CleanupIKRetargeterPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                CleanupIKRetargeterRequest);
        TestTrue(FString::Printf(
                TEXT("Confirmed IK Retargeter cleanup plan succeeds (code=%s message=%s)"),
                *CleanupIKRetargeterPlan.Code,
                *CleanupIKRetargeterPlan.Message),
            CleanupIKRetargeterPlan.bOk);
        const FThomasAnimationApplyResult CleanupIKRetargeterApply =
            UThomasDomainsToolset::ApplyAnimationPlan(
                CleanupIKRetargeterPlan.PlanId, true);
        TestTrue(FString::Printf(
                TEXT("Confirmed IK Retargeter cleanup saves (code=%s message=%s)"),
                *CleanupIKRetargeterApply.Code,
                *CleanupIKRetargeterApply.Message),
            CleanupIKRetargeterApply.bOk
                && CleanupIKRetargeterApply.bSaved);
        ReloadIKRetargeter(
            TEXT("IK Retargeter cleanup reloads from disk"));
        const FThomasSpecializedAssetInspectionResult
            IKRetargeterAfterCleanup =
                UThomasDomainsToolset::InspectAnimationAsset(
                    IKRetargeterPath, 2000);
        TestTrue(TEXT("IK Retargeter R2 cleanup persists"),
            IKRetargeterAfterCleanup.Metrics.Contains(
                TEXT("RetargetOpCount=4"))
                && IKRetargeterAfterCleanup.Metrics.Contains(
                    TEXT("OverrideSetCount=0"))
                && IKRetargeterAfterCleanup.Metrics.Contains(
                    TEXT("ActiveOverrideSetCount=0"))
                && IKRetargeterAfterCleanup.Metrics.Contains(
                    TEXT("SourcePoseCount=1"))
                && IKRetargeterAfterCleanup.Metrics.Contains(
                    TEXT("TargetPoseCount=1"))
                && !IKRetargeterAfterCleanup.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(TEXT("TE_Profile"))
                            || Item.Contains(TEXT("TE_SourcePose"))
                            || Item.Contains(TEXT("TE_TargetPose"));
                    }));

        if (AnimationSkeletalMeshContext)
        {
            FThomasDomainAssetCreateRequest ControlRigRequest;
            ControlRigRequest.AssetPath = ControlRigPath;
            ControlRigRequest.ExpectedRevision = TEXT("missing");
            ControlRigRequest.AssetKind = TEXT("control_rig");
            ControlRigRequest.ContextAssetPath =
                AnimationSkeletalMeshContext->AssetPath;
            const FThomasDomainAssetCreatePlanResult ControlRigPlan =
                UThomasDomainsToolset::PlanAnimationAssetCreate(
                    ControlRigRequest);
            TestTrue(FString::Printf(
                    TEXT("Native Control Rig create plan succeeds (code=%s message=%s factory=%s)"),
                    *ControlRigPlan.Code,
                    *ControlRigPlan.Message,
                    *ControlRigPlan.FactoryClassPath),
                ControlRigPlan.bOk);
            const FThomasDomainAssetCreateApplyResult ControlRigApply =
                UThomasDomainsToolset::ApplyAnimationAssetCreate(
                    ControlRigPlan.PlanId, true);
            TestTrue(FString::Printf(
                    TEXT("Native Control Rig create/import saves (code=%s message=%s class=%s)"),
                    *ControlRigApply.Code,
                    *ControlRigApply.Message,
                    *ControlRigApply.ClassPath),
                ControlRigApply.bOk && ControlRigApply.bSaved);
            UObject* ControlRigAsset = LoadObject<UObject>(
                nullptr,
                *(ControlRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(
                        ControlRigPath)));
            FText ControlRigReloadError;
            TestTrue(TEXT("Created Control Rig reloads from saved file"),
                ControlRigAsset
                    && UPackageTools::ReloadPackages(
                        {ControlRigAsset->GetOutermost()},
                        ControlRigReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult
                ControlRigInspection =
                    UThomasDomainsToolset::InspectAnimationAsset(
                        ControlRigPath, 1000);
            const FString ControlRigMeshObjectPath =
                AnimationSkeletalMeshContext->AssetPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    AnimationSkeletalMeshContext->AssetPath);
            const USkeletalMesh* ControlRigMesh =
                LoadObject<USkeletalMesh>(
                    nullptr, *ControlRigMeshObjectPath);
            const int32 ExpectedControlRigBoneCount = ControlRigMesh
                ? ControlRigMesh->GetRefSkeleton().GetNum() : 0;
            const FString ExpectedControlRigMeshPath = ControlRigMesh
                ? ControlRigMesh->GetOutermost()->GetName()
                : AnimationSkeletalMeshContext->AssetPath;
            AddInfo(FString::Printf(
                TEXT("Control Rig reload: class=%s expected_bones=%d metrics=%s"),
                *ControlRigInspection.ClassPath,
                ExpectedControlRigBoneCount,
                *FString::Join(ControlRigInspection.Metrics, TEXT(";"))));
            TestTrue(TEXT("Reloaded Control Rig inspection succeeds"),
                ControlRigInspection.bOk);
            TestEqual(TEXT("Reloaded Control Rig kind"),
                ControlRigInspection.AssetKind,
                FString(TEXT("control_rig")));
            TestEqual(TEXT("Reloaded Control Rig class"),
                ControlRigInspection.ClassPath,
                FString(TEXT("/Script/ControlRig.ControlRigRuntimeAsset")));
            TestTrue(TEXT("Control Rig preview mesh persists"),
                ControlRigInspection.Metrics.Contains(
                    TEXT("PreviewMesh=")
                        + ExpectedControlRigMeshPath));
            TestTrue(TEXT("Control Rig imported bone count persists"),
                ControlRigInspection.Metrics.Contains(
                    FString::Printf(TEXT("BoneCount=%d"),
                        ExpectedControlRigBoneCount)));
            TestTrue(TEXT("Control Rig hierarchy element count persists"),
                ControlRigInspection.Metrics.Contains(
                    FString::Printf(TEXT("ElementCount=%d"),
                        ExpectedControlRigBoneCount)));

            FThomasDomainAssetCreateRequest ExternalControlRigRequest;
            ExternalControlRigRequest.AssetPath = ExternalControlRigPath;
            ExternalControlRigRequest.ExpectedRevision = TEXT("missing");
            ExternalControlRigRequest.AssetKind = TEXT("control_rig");
            ExternalControlRigRequest.ContextAssetPath =
                AnimationSkeletalMeshContext->AssetPath;
            const FThomasDomainAssetCreatePlanResult ExternalControlRigPlan =
                UThomasDomainsToolset::PlanAnimationAssetCreate(
                    ExternalControlRigRequest);
            TestTrue(TEXT("External-library Control Rig create plan succeeds"),
                ExternalControlRigPlan.bOk);
            const FThomasDomainAssetCreateApplyResult
                ExternalControlRigApply =
                    UThomasDomainsToolset::ApplyAnimationAssetCreate(
                        ExternalControlRigPlan.PlanId, true);
            TestTrue(TEXT("External-library Control Rig create/import saves"),
                ExternalControlRigApply.bOk
                    && ExternalControlRigApply.bSaved);
            UObject* ExternalControlRigAsset = LoadObject<UObject>(
                nullptr,
                *(ExternalControlRigPath + TEXT(".")
                    + FPackageName::GetLongPackageAssetName(
                        ExternalControlRigPath)));
            FText ExternalControlRigReloadError;
            TestTrue(TEXT("External-library Control Rig reloads from saved file"),
                ExternalControlRigAsset
                    && UPackageTools::ReloadPackages(
                        {ExternalControlRigAsset->GetOutermost()},
                        ExternalControlRigReloadError,
                        EReloadPackagesInteractionMode::AssumePositive));
            const FThomasSpecializedAssetInspectionResult
                ExternalControlRigInspection =
                    UThomasDomainsToolset::InspectAnimationAsset(
                        ExternalControlRigPath, 1000);
            TestTrue(TEXT("External-library Control Rig baseline persists"),
                ExternalControlRigInspection.bOk
                    && ExternalControlRigInspection.Metrics.Contains(
                        FString::Printf(TEXT("BoneCount=%d"),
                            ExpectedControlRigBoneCount))
                    && ExternalControlRigInspection.Metrics.Contains(
                        TEXT("RigVMLocalFunctionCount=0")));

            RunControlRigHierarchyMutationTests(
                *this,
                ControlRigPath,
                ExternalControlRigPath,
                ControlRigMesh,
                ExpectedControlRigBoneCount);
        }

        const FString AnimMontagePath =
            TEXT("/Game/PropHunt/Tests/ThomasEditor/AM_TE_Native_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        FThomasDomainAssetCreateRequest AnimMontageRequest;
        AnimMontageRequest.AssetPath = AnimMontagePath;
        AnimMontageRequest.ExpectedRevision = TEXT("missing");
        AnimMontageRequest.AssetKind = TEXT("anim_montage");
        AnimMontageRequest.ContextAssetPath = AnimationFixtureSkeletonPath;
        const FThomasDomainAssetCreatePlanResult AnimMontagePlan =
            UThomasDomainsToolset::PlanAnimationAssetCreate(AnimMontageRequest);
        TestTrue(TEXT("Native Anim Montage create plan succeeds"), AnimMontagePlan.bOk);
        const FThomasDomainAssetCreateApplyResult AnimMontageApply =
            UThomasDomainsToolset::ApplyAnimationAssetCreate(AnimMontagePlan.PlanId, true);
        TestTrue(TEXT("Native Anim Montage create/save succeeds"), AnimMontageApply.bOk);
        const FThomasSpecializedAssetInspectionResult AnimMontageInspection =
            UThomasDomainsToolset::InspectAnimationAsset(AnimMontagePath, 100);
        TestTrue(TEXT("Created Anim Montage typed inspection succeeds"), AnimMontageInspection.bOk);
        TestEqual(TEXT("Created Animation kind is montage"),
            AnimMontageInspection.AssetKind, FString(TEXT("anim_montage")));

        FThomasAnimationPatchRequest MontagePatchRequest;
        MontagePatchRequest.AssetPath = AnimMontagePath;
        MontagePatchRequest.ExpectedRevision = AnimMontageInspection.Revision;
        FThomasAnimationOperation AddIntroSection;
        AddIntroSection.Action = TEXT("add_montage_section");
        AddIntroSection.SectionName = TEXT("Intro");
        AddIntroSection.Time = 0.0f;
        MontagePatchRequest.Operations.Add(AddIntroSection);
        FThomasAnimationOperation AddLoopSection;
        AddLoopSection.Action = TEXT("add_montage_section");
        AddLoopSection.SectionName = TEXT("Loop");
        AddLoopSection.Time = 0.0f;
        MontagePatchRequest.Operations.Add(AddLoopSection);
        FThomasAnimationOperation SetNextSection;
        SetNextSection.Action = TEXT("set_next_montage_section");
        SetNextSection.SectionName = TEXT("Intro");
        SetNextSection.NextSectionName = TEXT("Loop");
        MontagePatchRequest.Operations.Add(SetNextSection);
        FThomasAnimationOperation AddMontageSlot;
        AddMontageSlot.Action = TEXT("add_montage_slot");
        AddMontageSlot.SlotName = TEXT("DefaultGroup.ThomasTestSlot");
        MontagePatchRequest.Operations.Add(AddMontageSlot);
        const FThomasAnimationPlanResult MontagePatchPlan =
            UThomasDomainsToolset::PlanAnimationPatch(MontagePatchRequest);
        TestTrue(TEXT("Guarded Montage section/slot plan succeeds"), MontagePatchPlan.bOk);
        const FThomasAnimationApplyResult MontagePatchApply =
            UThomasDomainsToolset::ApplyAnimationPlan(MontagePatchPlan.PlanId, true);
        TestTrue(TEXT("Guarded Montage section/slot apply saves"),
            MontagePatchApply.bOk && MontagePatchApply.bSaved);
        const FThomasSpecializedAssetInspectionResult MontageAfterPatch =
            UThomasDomainsToolset::InspectAnimationAsset(AnimMontagePath, 200);
        TestTrue(TEXT("Montage sections and next link are inspectable"),
            MontageAfterPatch.Items.ContainsByPredicate([](const FString& Item)
            {
                return Item.Contains(TEXT("Name=Intro")) && Item.Contains(TEXT("Next=Loop"));
            }));
        TestTrue(TEXT("Montage slot is inspectable"),
            MontageAfterPatch.Items.ContainsByPredicate([](const FString& Item)
            {
                return Item.Contains(TEXT("DefaultGroup.ThomasTestSlot"));
            }));

        FThomasAnimationPatchRequest MontageRemovalRequest;
        MontageRemovalRequest.AssetPath = AnimMontagePath;
        MontageRemovalRequest.ExpectedRevision = MontageAfterPatch.Revision;
        FThomasAnimationOperation RemoveIntroSection;
        RemoveIntroSection.Action = TEXT("remove_montage_section");
        RemoveIntroSection.SectionName = TEXT("Intro");
        MontageRemovalRequest.Operations.Add(RemoveIntroSection);
        TestEqual(TEXT("Montage removal requires explicit R2 confirmation"),
            UThomasDomainsToolset::PlanAnimationPatch(MontageRemovalRequest).Code,
            FString(TEXT("confirmation_required")));
        MontageRemovalRequest.bConfirmDestructive = true;
        FThomasAnimationOperation RemoveLoopSection;
        RemoveLoopSection.Action = TEXT("remove_montage_section");
        RemoveLoopSection.SectionName = TEXT("Loop");
        MontageRemovalRequest.Operations.Add(RemoveLoopSection);
        FThomasAnimationOperation RemoveMontageSlot;
        RemoveMontageSlot.Action = TEXT("remove_montage_slot");
        RemoveMontageSlot.SlotName = TEXT("DefaultGroup.ThomasTestSlot");
        MontageRemovalRequest.Operations.Add(RemoveMontageSlot);
        const FThomasAnimationPlanResult MontageRemovalPlan =
            UThomasDomainsToolset::PlanAnimationPatch(MontageRemovalRequest);
        TestTrue(TEXT("Confirmed Montage removal plan succeeds"), MontageRemovalPlan.bOk);
        TestTrue(TEXT("Confirmed Montage removals apply and save"),
            UThomasDomainsToolset::ApplyAnimationPlan(MontageRemovalPlan.PlanId, true).bOk);

        UAnimMontage* MontageCrashFixture = LoadObject<UAnimMontage>(
            nullptr,
            *(AnimMontagePath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimMontagePath)));
        TestNotNull(
            TEXT("Montage batch-removal regression fixture loads"),
            MontageCrashFixture);
        if (MontageCrashFixture)
        {
            MontageCrashFixture->Modify();
            FSlotAnimationTrack SegmentSlot;
            SegmentSlot.SlotName = FName(TEXT("DefaultGroup.ThomasSegmentRegression"));
            SegmentSlot.AnimTrack.AnimSegments.SetNum(2);
            MontageCrashFixture->SlotAnimTracks.Add(MoveTemp(SegmentSlot));
            MontageCrashFixture->MarkPackageDirty();
            FSavePackageArgs SegmentFixtureSaveArgs;
            SegmentFixtureSaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            SegmentFixtureSaveArgs.SaveFlags = SAVE_NoError;
            SegmentFixtureSaveArgs.Error = GError;
            const FString SegmentFixtureFilename =
                FPackageName::LongPackageNameToFilename(
                    AnimMontagePath,
                    FPackageName::GetAssetPackageExtension());
            TestTrue(
                TEXT("Montage batch-removal regression fixture saves"),
                UPackage::SavePackage(
                    MontageCrashFixture->GetOutermost(),
                    MontageCrashFixture,
                    *SegmentFixtureFilename,
                    SegmentFixtureSaveArgs));

            const FThomasSpecializedAssetInspectionResult SegmentFixtureInspection =
                UThomasDomainsToolset::InspectAnimationAsset(
                    AnimMontagePath, 200);
            FThomasAnimationPatchRequest InvalidSegmentBatch;
            InvalidSegmentBatch.AssetPath = AnimMontagePath;
            InvalidSegmentBatch.ExpectedRevision = SegmentFixtureInspection.Revision;
            InvalidSegmentBatch.bConfirmDestructive = true;
            FThomasAnimationOperation RemoveSegmentZero;
            RemoveSegmentZero.Action = TEXT("remove_montage_segment");
            RemoveSegmentZero.SlotName = TEXT("DefaultGroup.ThomasSegmentRegression");
            RemoveSegmentZero.Index = 0;
            InvalidSegmentBatch.Operations.Add(RemoveSegmentZero);
            FThomasAnimationOperation RemoveOriginalSegmentOne = RemoveSegmentZero;
            RemoveOriginalSegmentOne.Index = 1;
            InvalidSegmentBatch.Operations.Add(RemoveOriginalSegmentOne);
            TestEqual(
                TEXT("Montage preflight simulates earlier removals in the same batch"),
                UThomasDomainsToolset::PlanAnimationPatch(
                    InvalidSegmentBatch).Code,
                FString(TEXT("montage_segment_not_found")));

            FThomasAnimationPatchRequest ValidSegmentBatch = InvalidSegmentBatch;
            ValidSegmentBatch.Operations[1].Index = 0;
            const FThomasAnimationPlanResult ValidSegmentRemovalPlan =
                UThomasDomainsToolset::PlanAnimationPatch(ValidSegmentBatch);
            TestTrue(
                TEXT("Montage sequential segment-removal plan succeeds"),
                ValidSegmentRemovalPlan.bOk);
            if (ValidSegmentRemovalPlan.bOk)
            {
                const FThomasAnimationApplyResult ValidSegmentRemovalApply =
                    UThomasDomainsToolset::ApplyAnimationPlan(
                        ValidSegmentRemovalPlan.PlanId, true);
                TestTrue(
                    TEXT("Montage sequential segment removals apply without checkf"),
                    ValidSegmentRemovalApply.bOk
                        && ValidSegmentRemovalApply.bSaved
                        && ValidSegmentRemovalApply.AppliedOperationCount == 2);
            }
        }

        const FString AnimSequencePath =
            TEXT("/Game/PropHunt/Tests/ThomasEditor/AS_TE_Native_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        FThomasDomainAssetCreateRequest AnimSequenceRequest;
        AnimSequenceRequest.AssetPath = AnimSequencePath;
        AnimSequenceRequest.ExpectedRevision = TEXT("missing");
        AnimSequenceRequest.AssetKind = TEXT("anim_sequence");
        AnimSequenceRequest.ContextAssetPath = AnimationFixtureSkeletonPath;
        const FThomasDomainAssetCreatePlanResult AnimSequencePlan =
            UThomasDomainsToolset::PlanAnimationAssetCreate(AnimSequenceRequest);
        TestTrue(TEXT("Native Anim Sequence create plan succeeds"), AnimSequencePlan.bOk);
        const FThomasDomainAssetCreateApplyResult AnimSequenceApply =
            UThomasDomainsToolset::ApplyAnimationAssetCreate(AnimSequencePlan.PlanId, true);
        TestTrue(TEXT("Native Anim Sequence create/save succeeds"), AnimSequenceApply.bOk);
        const FThomasSpecializedAssetInspectionResult AnimSequenceInspection =
            UThomasDomainsToolset::InspectAnimationAsset(AnimSequencePath, 100);
        FThomasAnimationPatchRequest SequencePatchRequest;
        SequencePatchRequest.AssetPath = AnimSequencePath;
        SequencePatchRequest.ExpectedRevision = AnimSequenceInspection.Revision;
        FThomasAnimationOperation SetRootMotion;
        SetRootMotion.Action = TEXT("set_root_motion");
        SetRootMotion.bEnabled = true;
        SetRootMotion.bForceRootLock = true;
        SetRootMotion.RootMotionLock = TEXT("zero");
        SequencePatchRequest.Operations.Add(SetRootMotion);
        FThomasAnimationOperation AddNotifyTrack;
        AddNotifyTrack.Action = TEXT("add_notify_track");
        AddNotifyTrack.TrackName = TEXT("ThomasEvents");
        SequencePatchRequest.Operations.Add(AddNotifyTrack);
        FThomasAnimationOperation AddCurve;
        AddCurve.Action = TEXT("add_curve");
        AddCurve.Name = TEXT("ThomasCurve");
        SequencePatchRequest.Operations.Add(AddCurve);
        FThomasAnimationOperation SetCurveKey;
        SetCurveKey.Action = TEXT("set_curve_key");
        SetCurveKey.Name = TEXT("ThomasCurve");
        SetCurveKey.Time = 0.0f;
        SetCurveKey.Value = 0.75f;
        SequencePatchRequest.Operations.Add(SetCurveKey);
        const FThomasAnimationPlanResult SequencePatchPlan =
            UThomasDomainsToolset::PlanAnimationPatch(SequencePatchRequest);
        TestTrue(TEXT("Guarded Sequence root motion/notify track/curve plan succeeds"), SequencePatchPlan.bOk);
        const FThomasAnimationApplyResult SequencePatchApply =
            UThomasDomainsToolset::ApplyAnimationPlan(SequencePatchPlan.PlanId, true);
        TestTrue(TEXT("Guarded Sequence root motion/notify track/curve apply saves"), SequencePatchApply.bOk);
        const FThomasSpecializedAssetInspectionResult SequenceAfterPatch =
            UThomasDomainsToolset::InspectAnimationAsset(AnimSequencePath, 200);
        TestTrue(TEXT("Sequence root motion is inspectable"),
            SequenceAfterPatch.Metrics.Contains(TEXT("RootMotionEnabled=true"))
                && SequenceAfterPatch.Metrics.Contains(TEXT("ForceRootLock=true")));
        TestTrue(TEXT("Sequence notify track is inspectable"),
            SequenceAfterPatch.Items.ContainsByPredicate([](const FString& Item)
            {
                return Item.Contains(TEXT("NotifyTrack")) && Item.Contains(TEXT("ThomasEvents"));
            }));
        TestTrue(TEXT("Sequence float curve key is inspectable"),
            SequenceAfterPatch.Items.ContainsByPredicate([](const FString& Item)
            {
                return Item.Contains(TEXT("CurveKey Curve=ThomasCurve")) && Item.Contains(TEXT("Value=0.750000"));
            }));

        FThomasAnimationPatchRequest SequenceRemovalRequest;
        SequenceRemovalRequest.AssetPath = AnimSequencePath;
        SequenceRemovalRequest.ExpectedRevision = SequenceAfterPatch.Revision;
        SequenceRemovalRequest.bConfirmDestructive = true;
        FThomasAnimationOperation RemoveNotifyTrack;
        RemoveNotifyTrack.Action = TEXT("remove_notify_track");
        RemoveNotifyTrack.TrackName = TEXT("ThomasEvents");
        SequenceRemovalRequest.Operations.Add(RemoveNotifyTrack);
        FThomasAnimationOperation RemoveCurve;
        RemoveCurve.Action = TEXT("remove_curve");
        RemoveCurve.Name = TEXT("ThomasCurve");
        SequenceRemovalRequest.Operations.Add(RemoveCurve);
        const FThomasAnimationPlanResult SequenceRemovalPlan =
            UThomasDomainsToolset::PlanAnimationPatch(SequenceRemovalRequest);
        TestTrue(TEXT("Confirmed Sequence removal plan succeeds"), SequenceRemovalPlan.bOk);
        TestTrue(TEXT("Confirmed Sequence removals apply and save"),
            UThomasDomainsToolset::ApplyAnimationPlan(SequenceRemovalPlan.PlanId, true).bOk);

        const FString AdditiveSequencePath =
            TEXT("/Game/PropHunt/Tests/ThomasEditor/ASA_TE_Native_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        FThomasDomainAssetCreateRequest AdditiveSequenceRequest;
        AdditiveSequenceRequest.AssetPath = AdditiveSequencePath;
        AdditiveSequenceRequest.ExpectedRevision = TEXT("missing");
        AdditiveSequenceRequest.AssetKind = TEXT("anim_sequence");
        AdditiveSequenceRequest.ContextAssetPath = AnimationFixtureSkeletonPath;
        const FThomasDomainAssetCreatePlanResult AdditiveSequencePlan =
            UThomasDomainsToolset::PlanAnimationAssetCreate(
                AdditiveSequenceRequest);
        TestTrue(TEXT("Native additive Anim Sequence create plan succeeds"),
            AdditiveSequencePlan.bOk);
        const FThomasDomainAssetCreateApplyResult AdditiveSequenceApply =
            UThomasDomainsToolset::ApplyAnimationAssetCreate(
                AdditiveSequencePlan.PlanId, true);
        TestTrue(TEXT("Native additive Anim Sequence create/save succeeds"),
            AdditiveSequenceApply.bOk && AdditiveSequenceApply.bSaved);
        FThomasSpecializedAssetInspectionResult AdditiveSequenceInspection =
            UThomasDomainsToolset::InspectAnimationAsset(
                AdditiveSequencePath, 100);
        FThomasAnimationPatchRequest AdditiveSettingsRequest;
        AdditiveSettingsRequest.AssetPath = AdditiveSequencePath;
        AdditiveSettingsRequest.ExpectedRevision = AdditiveSequenceInspection.Revision;
        FThomasAnimationOperation SetAdditiveSettings;
        SetAdditiveSettings.Action = TEXT("set_additive_settings");
        SetAdditiveSettings.AdditiveType = TEXT("local_space");
        SetAdditiveSettings.AdditiveBasePoseType = TEXT("ref_pose");
        SetAdditiveSettings.AdditiveBaseFrame = 0;
        AdditiveSettingsRequest.Operations.Add(SetAdditiveSettings);
        const FThomasAnimationPlanResult AdditiveSettingsPlan =
            UThomasDomainsToolset::PlanAnimationPatch(AdditiveSettingsRequest);
        TestTrue(TEXT("Additive Anim Sequence settings plan succeeds"),
            AdditiveSettingsPlan.bOk);
        TestTrue(TEXT("Additive Anim Sequence settings apply and save"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                AdditiveSettingsPlan.PlanId, true).bOk);
        AdditiveSequenceInspection = UThomasDomainsToolset::InspectAnimationAsset(
            AdditiveSequencePath, 100);
        TestTrue(TEXT("Additive Anim Sequence settings are inspectable"),
            AdditiveSequenceInspection.Metrics.Contains(TEXT("AdditiveType=local_space"))
                && AdditiveSequenceInspection.Metrics.Contains(
                    TEXT("AdditiveBasePoseType=ref_pose"))
                && AdditiveSequenceInspection.Metrics.Contains(
                    TEXT("AdditiveBasePoseAsset=None")));

        const FString BlendSpacePath =
            TEXT("/Game/PropHunt/Tests/ThomasEditor/BS_TE_Native_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        FThomasDomainAssetCreateRequest BlendSpaceRequest;
        BlendSpaceRequest.AssetPath = BlendSpacePath;
        BlendSpaceRequest.ExpectedRevision = TEXT("missing");
        BlendSpaceRequest.AssetKind = TEXT("blend_space");
        BlendSpaceRequest.ContextAssetPath = AnimationFixtureSkeletonPath;
        const FThomasDomainAssetCreatePlanResult BlendSpacePlan =
            UThomasDomainsToolset::PlanAnimationAssetCreate(BlendSpaceRequest);
        TestTrue(TEXT("Native 2D Blend Space create plan succeeds"),
            BlendSpacePlan.bOk);
        const FThomasDomainAssetCreateApplyResult BlendSpaceApply =
            UThomasDomainsToolset::ApplyAnimationAssetCreate(
                BlendSpacePlan.PlanId, true);
        TestTrue(TEXT("Native 2D Blend Space create/save succeeds"),
            BlendSpaceApply.bOk && BlendSpaceApply.bSaved);
        const FThomasSpecializedAssetInspectionResult BlendSpaceInspection =
            UThomasDomainsToolset::InspectAnimationAsset(BlendSpacePath, 100);
        TestTrue(TEXT("Created 2D Blend Space typed inspection succeeds"),
            BlendSpaceInspection.bOk);
        TestEqual(TEXT("Created Animation kind is Blend Space"),
            BlendSpaceInspection.AssetKind, FString(TEXT("blend_space")));

        const FString AnimBlueprintPath =
            TEXT("/Game/PropHunt/Tests/ThomasEditor/ABP_TE_StateMachine_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        FThomasDomainAssetCreateRequest AnimBlueprintRequest;
        AnimBlueprintRequest.AssetPath = AnimBlueprintPath;
        AnimBlueprintRequest.ExpectedRevision = TEXT("missing");
        AnimBlueprintRequest.AssetKind = TEXT("anim_blueprint");
        AnimBlueprintRequest.ContextAssetPath = AnimationFixtureSkeletonPath;
        const FThomasDomainAssetCreatePlanResult AnimBlueprintPlan =
            UThomasDomainsToolset::PlanAnimationAssetCreate(AnimBlueprintRequest);
        TestTrue(TEXT("Native Anim Blueprint create plan succeeds"), AnimBlueprintPlan.bOk);
        const FThomasDomainAssetCreateApplyResult AnimBlueprintApply =
            UThomasDomainsToolset::ApplyAnimationAssetCreate(AnimBlueprintPlan.PlanId, true);
        TestTrue(TEXT("Native Anim Blueprint create/save succeeds"), AnimBlueprintApply.bOk);
        const FThomasBlueprintInspectionResult AnimBlueprintGenericBefore =
            UThomasBlueprintToolset::InspectBlueprint(AnimBlueprintPath, false, 100);
        TestTrue(TEXT("Created Anim Blueprint generic inspection succeeds"),
            AnimBlueprintGenericBefore.bOk);
        FThomasBlueprintBatchRequest AnimVariableRequest;
        AnimVariableRequest.AssetPath = AnimBlueprintPath;
        AnimVariableRequest.ExpectedRevision = AnimBlueprintGenericBefore.Revision;
        AnimVariableRequest.AssetKind = TEXT("normal");
        FThomasBlueprintOperation AddCanMoveVariable;
        AddCanMoveVariable.Action = TEXT("add_variable");
        AddCanMoveVariable.Name = TEXT("CanMove");
        AddCanMoveVariable.Type = TEXT("bool");
        AddCanMoveVariable.Value = TEXT("false");
        AnimVariableRequest.Operations.Add(AddCanMoveVariable);
        FThomasBlueprintOperation AddSpeedVariable;
        AddSpeedVariable.Action = TEXT("add_variable");
        AddSpeedVariable.Name = TEXT("Speed");
        AddSpeedVariable.Type = TEXT("float");
        AddSpeedVariable.Value = TEXT("0.0");
        AnimVariableRequest.Operations.Add(AddSpeedVariable);
        FThomasBlueprintOperation AddDirectionVariable = AddSpeedVariable;
        AddDirectionVariable.Name = TEXT("Direction");
        AnimVariableRequest.Operations.Add(AddDirectionVariable);
        FThomasBlueprintOperation AddPoseIndexVariable;
        AddPoseIndexVariable.Action = TEXT("add_variable");
        AddPoseIndexVariable.Name = TEXT("PoseIndex");
        AddPoseIndexVariable.Type = TEXT("int");
        AddPoseIndexVariable.Value = TEXT("0");
        AnimVariableRequest.Operations.Add(AddPoseIndexVariable);
        FThomasBlueprintOperation AddAdditiveAlphaVariable = AddSpeedVariable;
        AddAdditiveAlphaVariable.Name = TEXT("AdditiveAlpha");
        AddAdditiveAlphaVariable.Value = TEXT("0.5");
        AnimVariableRequest.Operations.Add(AddAdditiveAlphaVariable);
        const FThomasBlueprintPlanResult AnimVariablePlan =
            UThomasBlueprintToolset::PlanBlueprintBatch(AnimVariableRequest);
        TestTrue(TEXT("Anim Blueprint bool transition variable plan succeeds"),
            AnimVariablePlan.bOk);
        const FThomasBlueprintApplyResult AnimVariableApply =
            UThomasBlueprintToolset::ApplyBlueprintPlan(
                AnimVariablePlan.PlanId, true, true);
        TestTrue(TEXT("Anim Blueprint bool transition variable compiles and saves"),
            AnimVariableApply.bOk);
        const FThomasSpecializedAssetInspectionResult AnimBlueprintBefore =
            UThomasDomainsToolset::InspectAnimationAsset(AnimBlueprintPath, 200);
        TestTrue(TEXT("Created Anim Blueprint typed inspection succeeds"), AnimBlueprintBefore.bOk);
        TestEqual(TEXT("Created Animation kind is Anim Blueprint"),
            AnimBlueprintBefore.AssetKind, FString(TEXT("anim_blueprint")));

        FThomasAnimationPatchRequest StateMachineRequest;
        StateMachineRequest.AssetPath = AnimBlueprintPath;
        StateMachineRequest.ExpectedRevision = AnimBlueprintBefore.Revision;
        FThomasAnimationOperation AddStateMachine;
        AddStateMachine.Action = TEXT("add_state_machine");
        AddStateMachine.MachineName = TEXT("Locomotion");
        AddStateMachine.PositionX = -300;
        AddStateMachine.PositionY = 0;
        StateMachineRequest.Operations.Add(AddStateMachine);
        FThomasAnimationOperation AddIdleState;
        AddIdleState.Action = TEXT("add_state");
        AddIdleState.MachineName = TEXT("Locomotion");
        AddIdleState.StateName = TEXT("Idle");
        AddIdleState.PositionX = 0;
        AddIdleState.PositionY = 0;
        AddIdleState.bAlwaysResetOnEntry = true;
        StateMachineRequest.Operations.Add(AddIdleState);
        FThomasAnimationOperation AddRunState;
        AddRunState.Action = TEXT("add_state");
        AddRunState.MachineName = TEXT("Locomotion");
        AddRunState.StateName = TEXT("Run");
        AddRunState.PositionX = 350;
        AddRunState.PositionY = 0;
        StateMachineRequest.Operations.Add(AddRunState);
        FThomasAnimationOperation SetIdleSequencePlayer;
        SetIdleSequencePlayer.Action = TEXT("set_state_sequence_player");
        SetIdleSequencePlayer.MachineName = TEXT("Locomotion");
        SetIdleSequencePlayer.StateName = TEXT("Idle");
        SetIdleSequencePlayer.ReferencedAssetPath = AnimSequencePath;
        SetIdleSequencePlayer.PlayRate = 1.0f;
        SetIdleSequencePlayer.bLoopAnimation = true;
        SetIdleSequencePlayer.PositionX = -300;
        SetIdleSequencePlayer.PositionY = 0;
        StateMachineRequest.Operations.Add(SetIdleSequencePlayer);
        FThomasAnimationOperation SetRunSequencePlayer = SetIdleSequencePlayer;
        SetRunSequencePlayer.StateName = TEXT("Run");
        SetRunSequencePlayer.PlayRate = 1.25f;
        SetRunSequencePlayer.bLoopAnimation = false;
        StateMachineRequest.Operations.Add(SetRunSequencePlayer);
        FThomasAnimationOperation SetIdleEntry;
        SetIdleEntry.Action = TEXT("set_entry_state");
        SetIdleEntry.MachineName = TEXT("Locomotion");
        SetIdleEntry.StateName = TEXT("Idle");
        StateMachineRequest.Operations.Add(SetIdleEntry);
        FThomasAnimationOperation AddIdleToRun;
        AddIdleToRun.Action = TEXT("add_transition");
        AddIdleToRun.MachineName = TEXT("Locomotion");
        AddIdleToRun.FromStateName = TEXT("Idle");
        AddIdleToRun.ToStateName = TEXT("Run");
        AddIdleToRun.Duration = 0.2f;
        AddIdleToRun.PriorityOrder = 0;
        AddIdleToRun.bEnabled = true;
        AddIdleToRun.PositionX = 175;
        AddIdleToRun.PositionY = 0;
        StateMachineRequest.Operations.Add(AddIdleToRun);
        FThomasAnimationOperation SetIdleToRunRule;
        SetIdleToRunRule.Action = TEXT("set_transition_rule_constant");
        SetIdleToRunRule.MachineName = TEXT("Locomotion");
        SetIdleToRunRule.FromStateName = TEXT("Idle");
        SetIdleToRunRule.ToStateName = TEXT("Run");
        SetIdleToRunRule.bRuleValue = true;
        StateMachineRequest.Operations.Add(SetIdleToRunRule);
        const FThomasAnimationPlanResult StateMachinePlan =
            UThomasDomainsToolset::PlanAnimationPatch(StateMachineRequest);
        TestTrue(TEXT("Guarded Anim Blueprint state-machine plan succeeds"), StateMachinePlan.bOk);
        const FThomasAnimationApplyResult StateMachineApply =
            UThomasDomainsToolset::ApplyAnimationPlan(StateMachinePlan.PlanId, true);
        TestTrue(TEXT("Anim Blueprint state-machine apply compiles and saves"), StateMachineApply.bOk);
        UObject* SavedAnimBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText AnimBlueprintReloadError;
        TestTrue(
            TEXT("Anim Blueprint state graph/rule reloads from the saved file"),
            SavedAnimBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedAnimBlueprint->GetOutermost()},
                    AnimBlueprintReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StateMachineInspection =
            UThomasDomainsToolset::InspectAnimationAsset(AnimBlueprintPath, 500);
        TestTrue(TEXT("Anim Blueprint state machine and entry are inspectable"),
            StateMachineInspection.Items.ContainsByPredicate([](const FString& Item)
            {
                return Item.Contains(TEXT("StateMachine Name=Locomotion"))
                    && Item.Contains(TEXT("States=2"))
                    && Item.Contains(TEXT("Transitions=1"))
                    && Item.Contains(TEXT("Entry=Idle"));
            }));
        TestTrue(TEXT("Anim Blueprint transition is inspectable"),
            StateMachineInspection.Items.ContainsByPredicate([](const FString& Item)
            {
                return Item.Contains(TEXT("Transition Machine=Locomotion"))
                    && Item.Contains(TEXT("From=Idle"))
                    && Item.Contains(TEXT("To=Run"));
            }));
        TestTrue(TEXT("Anim Blueprint state Sequence Players are inspectable"),
            StateMachineInspection.Metrics.Contains(TEXT("StatePlayerCount=2"))
                && StateMachineInspection.Items.ContainsByPredicate(
                    [&AnimSequencePath](const FString& Item)
                    {
                        return Item.Contains(TEXT("StatePlayer Machine=Locomotion State=Idle"))
                            && Item.Contains(TEXT("Asset=") + AnimSequencePath)
                            && Item.Contains(TEXT("PlayRate=1.000000"))
                            && Item.Contains(TEXT("Loop=true"))
                            && Item.Contains(TEXT("PoseLinked=true"));
                    })
                && StateMachineInspection.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(TEXT("StatePlayer Machine=Locomotion State=Run"))
                            && Item.Contains(TEXT("PlayRate=1.250000"))
                            && Item.Contains(TEXT("Loop=false"))
                            && Item.Contains(TEXT("PoseLinked=true"));
                    }));
        TestTrue(TEXT("Anim Blueprint transition rule graph is inspectable"),
            StateMachineInspection.Metrics.Contains(TEXT("TransitionRuleCount=1"))
                && StateMachineInspection.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(TEXT("Transition Machine=Locomotion From=Idle To=Run"))
                            && Item.Contains(TEXT("RuleDefault=true"))
                            && Item.Contains(TEXT("RuleLinked=0"))
                            && Item.Contains(TEXT("RuleNodes=1"));
                    }));

        FThomasAnimationPatchRequest StateMachineUpdateRequest;
        StateMachineUpdateRequest.AssetPath = AnimBlueprintPath;
        StateMachineUpdateRequest.ExpectedRevision = StateMachineInspection.Revision;
        StateMachineUpdateRequest.bConfirmDestructive = true;
        FThomasAnimationOperation RenameRunState;
        RenameRunState.Action = TEXT("rename_state");
        RenameRunState.MachineName = TEXT("Locomotion");
        RenameRunState.StateName = TEXT("Run");
        RenameRunState.NewName = TEXT("Move");
        StateMachineUpdateRequest.Operations.Add(RenameRunState);
        FThomasAnimationOperation UpdateIdleToMove;
        UpdateIdleToMove.Action = TEXT("set_transition");
        UpdateIdleToMove.MachineName = TEXT("Locomotion");
        UpdateIdleToMove.FromStateName = TEXT("Idle");
        UpdateIdleToMove.ToStateName = TEXT("Move");
        UpdateIdleToMove.Duration = 0.35f;
        UpdateIdleToMove.PriorityOrder = 1;
        UpdateIdleToMove.bAutomaticTransitionRule = false;
        UpdateIdleToMove.bEnabled = true;
        StateMachineUpdateRequest.Operations.Add(UpdateIdleToMove);
        FThomasAnimationOperation SetIdleToMoveVariableRule;
        SetIdleToMoveVariableRule.Action = TEXT("set_transition_rule_variable");
        SetIdleToMoveVariableRule.MachineName = TEXT("Locomotion");
        SetIdleToMoveVariableRule.FromStateName = TEXT("Idle");
        SetIdleToMoveVariableRule.ToStateName = TEXT("Move");
        SetIdleToMoveVariableRule.Name = TEXT("CanMove");
        SetIdleToMoveVariableRule.PositionX = -250;
        SetIdleToMoveVariableRule.PositionY = 0;
        StateMachineUpdateRequest.Operations.Add(SetIdleToMoveVariableRule);
        const FThomasAnimationPlanResult StateMachineUpdatePlan =
            UThomasDomainsToolset::PlanAnimationPatch(StateMachineUpdateRequest);
        TestTrue(TEXT("Anim Blueprint state rename/transition update plan succeeds"),
            StateMachineUpdatePlan.bOk);
        TestTrue(TEXT("Anim Blueprint state rename/transition update applies"),
            UThomasDomainsToolset::ApplyAnimationPlan(StateMachineUpdatePlan.PlanId, true).bOk);
        UObject* SavedVariableRuleBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText VariableRuleReloadError;
        TestTrue(TEXT("Anim Blueprint bool variable rule reloads from the saved file"),
            SavedVariableRuleBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedVariableRuleBlueprint->GetOutermost()},
                    VariableRuleReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StateMachineAfterUpdate =
            UThomasDomainsToolset::InspectAnimationAsset(AnimBlueprintPath, 500);
        TestTrue(TEXT("Renamed Anim Blueprint state is inspectable"),
            StateMachineAfterUpdate.Items.ContainsByPredicate([](const FString& Item)
            {
                return Item.Contains(TEXT("State Machine=Locomotion Name=Move"));
            }));
        TestTrue(TEXT("Updated Anim Blueprint transition settings are inspectable"),
            StateMachineAfterUpdate.Items.ContainsByPredicate([](const FString& Item)
            {
                return Item.Contains(TEXT("From=Idle"))
                    && Item.Contains(TEXT("To=Move"))
                    && Item.Contains(TEXT("Duration=0.350000"))
                    && Item.Contains(TEXT("Priority=1"))
                    && Item.Contains(TEXT("Automatic=false"))
                    && Item.Contains(TEXT("RuleVariable=CanMove"))
                    && Item.Contains(TEXT("RuleLinked=1"));
            }));
        TestTrue(TEXT("Anim Blueprint bool variable transition rule is counted"),
            StateMachineAfterUpdate.Metrics.Contains(
                TEXT("TransitionVariableRuleCount=1")));
        TestTrue(TEXT("Renamed state preserves its Sequence Player content"),
            StateMachineAfterUpdate.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.Contains(TEXT("StatePlayer Machine=Locomotion State=Move"))
                        && Item.Contains(TEXT("PlayRate=1.250000"))
                        && Item.Contains(TEXT("PoseLinked=true"));
                }));

        FThomasAnimationPatchRequest VariableRuleRemovalRequest;
        VariableRuleRemovalRequest.AssetPath = AnimBlueprintPath;
        VariableRuleRemovalRequest.ExpectedRevision = StateMachineAfterUpdate.Revision;
        FThomasAnimationOperation RemoveIdleToMoveVariableRule;
        RemoveIdleToMoveVariableRule.Action = TEXT("remove_transition_rule_variable");
        RemoveIdleToMoveVariableRule.MachineName = TEXT("Locomotion");
        RemoveIdleToMoveVariableRule.FromStateName = TEXT("Idle");
        RemoveIdleToMoveVariableRule.ToStateName = TEXT("Move");
        VariableRuleRemovalRequest.Operations.Add(RemoveIdleToMoveVariableRule);
        FThomasAnimationOperation RestoreIdleToMoveConstantRule;
        RestoreIdleToMoveConstantRule.Action = TEXT("set_transition_rule_constant");
        RestoreIdleToMoveConstantRule.MachineName = TEXT("Locomotion");
        RestoreIdleToMoveConstantRule.FromStateName = TEXT("Idle");
        RestoreIdleToMoveConstantRule.ToStateName = TEXT("Move");
        RestoreIdleToMoveConstantRule.bRuleValue = false;
        VariableRuleRemovalRequest.Operations.Add(RestoreIdleToMoveConstantRule);
        TestEqual(TEXT("Anim Blueprint variable-rule removal requires R2 confirmation"),
            UThomasDomainsToolset::PlanAnimationPatch(VariableRuleRemovalRequest).Code,
            FString(TEXT("confirmation_required")));
        VariableRuleRemovalRequest.bConfirmDestructive = true;
        const FThomasAnimationPlanResult VariableRuleRemovalPlan =
            UThomasDomainsToolset::PlanAnimationPatch(VariableRuleRemovalRequest);
        TestTrue(TEXT("Confirmed variable-rule removal/constant plan succeeds"),
            VariableRuleRemovalPlan.bOk);
        TestTrue(TEXT("Variable-rule removal/constant apply compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                VariableRuleRemovalPlan.PlanId, true).bOk);
        UObject* SavedConstantRuleBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText ConstantRuleReloadError;
        TestTrue(TEXT("Anim Blueprint restored constant rule reloads from the saved file"),
            SavedConstantRuleBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedConstantRuleBlueprint->GetOutermost()},
                    ConstantRuleReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StateRuleAfterRemoval =
            UThomasDomainsToolset::InspectAnimationAsset(AnimBlueprintPath, 500);
        TestTrue(TEXT("R2 variable-rule removal restores an unlinked constant"),
            StateRuleAfterRemoval.Metrics.Contains(
                TEXT("TransitionVariableRuleCount=0"))
                && StateRuleAfterRemoval.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("Transition Machine=Locomotion From=Idle To=Move"))
                            && Item.Contains(TEXT("RuleDefault=false"))
                            && Item.Contains(TEXT("RuleVariable=None"))
                            && Item.Contains(TEXT("RuleLinked=0"));
                    }));

        FThomasAnimationPatchRequest StateBoolBlendRequest;
        StateBoolBlendRequest.AssetPath = AnimBlueprintPath;
        StateBoolBlendRequest.ExpectedRevision = StateRuleAfterRemoval.Revision;
        FThomasAnimationOperation SetMoveBoolBlend;
        SetMoveBoolBlend.Action = TEXT("set_state_blend_by_bool");
        SetMoveBoolBlend.MachineName = TEXT("Locomotion");
        SetMoveBoolBlend.StateName = TEXT("Move");
        SetMoveBoolBlend.Name = TEXT("CanMove");
        SetMoveBoolBlend.ReferencedAssetPath = AnimSequencePath;
        SetMoveBoolBlend.SecondaryAssetPath = AnimSequencePath;
        SetMoveBoolBlend.PlayRate = 0.75f;
        SetMoveBoolBlend.SecondaryPlayRate = 1.5f;
        SetMoveBoolBlend.Duration = 0.15f;
        SetMoveBoolBlend.bLoopAnimation = true;
        SetMoveBoolBlend.PositionX = 0;
        SetMoveBoolBlend.PositionY = 0;
        StateBoolBlendRequest.Operations.Add(SetMoveBoolBlend);
        const FThomasAnimationPlanResult StateBoolBlendPlan =
            UThomasDomainsToolset::PlanAnimationPatch(StateBoolBlendRequest);
        TestTrue(TEXT("Anim Blueprint state bool-blend plan succeeds"),
            StateBoolBlendPlan.bOk);
        TestTrue(TEXT("Anim Blueprint state bool-blend compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                StateBoolBlendPlan.PlanId, true).bOk);
        UObject* SavedStateBoolBlendBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText StateBoolBlendReloadError;
        TestTrue(TEXT("Anim Blueprint state bool-blend reloads from the saved file"),
            SavedStateBoolBlendBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedStateBoolBlendBlueprint->GetOutermost()},
                    StateBoolBlendReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StateBoolBlendInspection =
            UThomasDomainsToolset::InspectAnimationAsset(AnimBlueprintPath, 500);
        TestTrue(TEXT("Anim Blueprint state bool-blend is inspectable"),
            StateBoolBlendInspection.Metrics.Contains(TEXT("StatePlayerCount=3"))
                && StateBoolBlendInspection.Metrics.Contains(
                    TEXT("StateBoolBlendCount=1"))
                && StateBoolBlendInspection.Items.ContainsByPredicate(
                    [&AnimSequencePath](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("StateBoolBlend Machine=Locomotion State=Move"))
                            && Item.Contains(TEXT("Variable=CanMove"))
                            && Item.Contains(TEXT("FalseAsset=") + AnimSequencePath)
                            && Item.Contains(TEXT("FalseRate=0.750000"))
                            && Item.Contains(TEXT("TrueAsset=") + AnimSequencePath)
                            && Item.Contains(TEXT("TrueRate=1.500000"))
                            && Item.Contains(TEXT("BlendTime=0.150000"))
                            && Item.Contains(TEXT("PoseLinked=true"));
                    }));

        FThomasAnimationPatchRequest DirectPlayerOverBlendRequest;
        DirectPlayerOverBlendRequest.AssetPath = AnimBlueprintPath;
        DirectPlayerOverBlendRequest.ExpectedRevision = StateBoolBlendInspection.Revision;
        DirectPlayerOverBlendRequest.Operations.Add(SetRunSequencePlayer);
        DirectPlayerOverBlendRequest.Operations[0].StateName = TEXT("Move");
        TestEqual(TEXT("Direct player edit refuses to overwrite a bool-blend"),
            UThomasDomainsToolset::PlanAnimationPatch(
                DirectPlayerOverBlendRequest).Code,
            FString(TEXT("state_bool_blend_requires_remove")));

        FThomasAnimationPatchRequest StateBoolBlendRemovalRequest;
        StateBoolBlendRemovalRequest.AssetPath = AnimBlueprintPath;
        StateBoolBlendRemovalRequest.ExpectedRevision = StateBoolBlendInspection.Revision;
        FThomasAnimationOperation RemoveMoveBoolBlend;
        RemoveMoveBoolBlend.Action = TEXT("remove_state_blend_by_bool");
        RemoveMoveBoolBlend.MachineName = TEXT("Locomotion");
        RemoveMoveBoolBlend.StateName = TEXT("Move");
        StateBoolBlendRemovalRequest.Operations.Add(RemoveMoveBoolBlend);
        TestEqual(TEXT("Anim Blueprint state bool-blend removal requires R2 confirmation"),
            UThomasDomainsToolset::PlanAnimationPatch(
                StateBoolBlendRemovalRequest).Code,
            FString(TEXT("confirmation_required")));
        StateBoolBlendRemovalRequest.bConfirmDestructive = true;
        const FThomasAnimationPlanResult StateBoolBlendRemovalPlan =
            UThomasDomainsToolset::PlanAnimationPatch(StateBoolBlendRemovalRequest);
        TestTrue(TEXT("Confirmed state bool-blend removal plan succeeds"),
            StateBoolBlendRemovalPlan.bOk);
        TestTrue(TEXT("Confirmed state bool-blend removal compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                StateBoolBlendRemovalPlan.PlanId, true).bOk);
        UObject* SavedStateBoolBlendRemovalBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText StateBoolBlendRemovalReloadError;
        TestTrue(TEXT("Removed state bool-blend reloads from the saved file"),
            SavedStateBoolBlendRemovalBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedStateBoolBlendRemovalBlueprint->GetOutermost()},
                    StateBoolBlendRemovalReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StatePlayerAfterRemoval =
            UThomasDomainsToolset::InspectAnimationAsset(AnimBlueprintPath, 500);
        TestTrue(TEXT("Only the Idle state player remains after bool-blend R2 removal"),
            StatePlayerAfterRemoval.Metrics.Contains(TEXT("StatePlayerCount=1"))
                && StatePlayerAfterRemoval.Metrics.Contains(
                    TEXT("StateBoolBlendCount=0"))
                && !StatePlayerAfterRemoval.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(TEXT("StateBoolBlend Machine=Locomotion State=Move"))
                            || Item.Contains(
                                TEXT("StatePlayer Machine=Locomotion State=Move"));
                    }));

        FThomasAnimationPatchRequest StateBlendSpaceRequest;
        StateBlendSpaceRequest.AssetPath = AnimBlueprintPath;
        StateBlendSpaceRequest.ExpectedRevision = StatePlayerAfterRemoval.Revision;
        FThomasAnimationOperation SetMoveBlendSpacePlayer;
        SetMoveBlendSpacePlayer.Action = TEXT("set_state_blend_space_player");
        SetMoveBlendSpacePlayer.MachineName = TEXT("Locomotion");
        SetMoveBlendSpacePlayer.StateName = TEXT("Move");
        SetMoveBlendSpacePlayer.ReferencedAssetPath = BlendSpacePath;
        SetMoveBlendSpacePlayer.InputXVariableName = TEXT("Speed");
        SetMoveBlendSpacePlayer.InputYVariableName = TEXT("Direction");
        SetMoveBlendSpacePlayer.PlayRate = 0.85f;
        SetMoveBlendSpacePlayer.bLoopAnimation = true;
        SetMoveBlendSpacePlayer.PositionX = 0;
        SetMoveBlendSpacePlayer.PositionY = 0;
        StateBlendSpaceRequest.Operations.Add(SetMoveBlendSpacePlayer);
        const FThomasAnimationPlanResult StateBlendSpacePlan =
            UThomasDomainsToolset::PlanAnimationPatch(StateBlendSpaceRequest);
        TestTrue(TEXT("Anim Blueprint state Blend Space Player plan succeeds"),
            StateBlendSpacePlan.bOk);
        TestTrue(TEXT("Anim Blueprint state Blend Space Player compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                StateBlendSpacePlan.PlanId, true).bOk);
        UObject* SavedStateBlendSpaceBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText StateBlendSpaceReloadError;
        TestTrue(TEXT("Anim Blueprint state Blend Space Player reloads from saved file"),
            SavedStateBlendSpaceBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedStateBlendSpaceBlueprint->GetOutermost()},
                    StateBlendSpaceReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StateBlendSpaceInspection =
            UThomasDomainsToolset::InspectAnimationAsset(AnimBlueprintPath, 500);
        TestTrue(TEXT("Anim Blueprint state Blend Space Player is inspectable"),
            StateBlendSpaceInspection.Metrics.Contains(
                TEXT("StateBlendSpacePlayerCount=1"))
                && StateBlendSpaceInspection.Items.ContainsByPredicate(
                    [&BlendSpacePath](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("StateBlendSpacePlayer Machine=Locomotion State=Move"))
                            && Item.Contains(TEXT("Asset=") + BlendSpacePath)
                            && Item.Contains(TEXT("Dimension=2D"))
                            && Item.Contains(TEXT("XVariable=Speed"))
                            && Item.Contains(TEXT("YVariable=Direction"))
                            && Item.Contains(TEXT("PlayRate=0.850000"))
                            && Item.Contains(TEXT("Loop=true"))
                            && Item.Contains(TEXT("PoseLinked=true"));
                    }));

        FThomasAnimationPatchRequest DirectPlayerOverBlendSpaceRequest;
        DirectPlayerOverBlendSpaceRequest.AssetPath = AnimBlueprintPath;
        DirectPlayerOverBlendSpaceRequest.ExpectedRevision =
            StateBlendSpaceInspection.Revision;
        DirectPlayerOverBlendSpaceRequest.Operations.Add(SetRunSequencePlayer);
        DirectPlayerOverBlendSpaceRequest.Operations[0].StateName = TEXT("Move");
        TestEqual(TEXT("Direct player edit refuses to overwrite a Blend Space Player"),
            UThomasDomainsToolset::PlanAnimationPatch(
                DirectPlayerOverBlendSpaceRequest).Code,
            FString(TEXT("state_blend_space_requires_remove")));

        FThomasAnimationPatchRequest StateBlendSpaceRemovalRequest;
        StateBlendSpaceRemovalRequest.AssetPath = AnimBlueprintPath;
        StateBlendSpaceRemovalRequest.ExpectedRevision =
            StateBlendSpaceInspection.Revision;
        FThomasAnimationOperation RemoveMoveBlendSpacePlayer;
        RemoveMoveBlendSpacePlayer.Action = TEXT("remove_state_blend_space_player");
        RemoveMoveBlendSpacePlayer.MachineName = TEXT("Locomotion");
        RemoveMoveBlendSpacePlayer.StateName = TEXT("Move");
        StateBlendSpaceRemovalRequest.Operations.Add(RemoveMoveBlendSpacePlayer);
        TestEqual(TEXT("State Blend Space Player removal requires R2 confirmation"),
            UThomasDomainsToolset::PlanAnimationPatch(
                StateBlendSpaceRemovalRequest).Code,
            FString(TEXT("confirmation_required")));
        StateBlendSpaceRemovalRequest.bConfirmDestructive = true;
        const FThomasAnimationPlanResult StateBlendSpaceRemovalPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                StateBlendSpaceRemovalRequest);
        TestTrue(TEXT("Confirmed state Blend Space Player removal plan succeeds"),
            StateBlendSpaceRemovalPlan.bOk);
        TestTrue(TEXT("Confirmed state Blend Space Player removal compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                StateBlendSpaceRemovalPlan.PlanId, true).bOk);
        UObject* SavedStateBlendSpaceRemovalBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText StateBlendSpaceRemovalReloadError;
        TestTrue(TEXT("Removed state Blend Space Player reloads from saved file"),
            SavedStateBlendSpaceRemovalBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedStateBlendSpaceRemovalBlueprint->GetOutermost()},
                    StateBlendSpaceRemovalReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StateBlendSpaceAfterRemoval =
            UThomasDomainsToolset::InspectAnimationAsset(AnimBlueprintPath, 500);
        TestTrue(TEXT("State Blend Space Player is absent after R2 removal"),
            StateBlendSpaceAfterRemoval.Metrics.Contains(
                TEXT("StateBlendSpacePlayerCount=0"))
                && !StateBlendSpaceAfterRemoval.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(
                            TEXT("StateBlendSpacePlayer Machine=Locomotion State=Move"));
                    }));

        FThomasAnimationPatchRequest StateIntBlendRequest;
        StateIntBlendRequest.AssetPath = AnimBlueprintPath;
        StateIntBlendRequest.ExpectedRevision = StateBlendSpaceAfterRemoval.Revision;
        FThomasAnimationOperation SetMoveIntBlend;
        SetMoveIntBlend.Action = TEXT("set_state_blend_list_by_int");
        SetMoveIntBlend.MachineName = TEXT("Locomotion");
        SetMoveIntBlend.StateName = TEXT("Move");
        SetMoveIntBlend.InputIndexVariableName = TEXT("PoseIndex");
        SetMoveIntBlend.ReferencedAssetPaths = {
            AnimSequencePath, AnimSequencePath, AnimSequencePath};
        SetMoveIntBlend.PlayRates = {0.5f, 1.0f, 1.5f};
        SetMoveIntBlend.BlendTimes = {0.10f, 0.20f, 0.30f};
        SetMoveIntBlend.bLoopAnimation = true;
        SetMoveIntBlend.PositionX = 0;
        SetMoveIntBlend.PositionY = 0;
        StateIntBlendRequest.Operations.Add(SetMoveIntBlend);
        const FThomasAnimationPlanResult StateIntBlendPlan =
            UThomasDomainsToolset::PlanAnimationPatch(StateIntBlendRequest);
        TestTrue(TEXT("Anim Blueprint three-pose int blend plan succeeds"),
            StateIntBlendPlan.bOk);
        TestTrue(TEXT("Anim Blueprint three-pose int blend compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                StateIntBlendPlan.PlanId, true).bOk);
        UObject* SavedStateIntBlendBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText StateIntBlendReloadError;
        TestTrue(TEXT("Anim Blueprint three-pose int blend reloads from saved file"),
            SavedStateIntBlendBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedStateIntBlendBlueprint->GetOutermost()},
                    StateIntBlendReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StateIntBlendInspection =
            UThomasDomainsToolset::InspectAnimationAsset(AnimBlueprintPath, 700);
        TestTrue(TEXT("Anim Blueprint three-pose int blend is inspectable"),
            StateIntBlendInspection.Metrics.Contains(TEXT("StateIntBlendCount=1"))
                && StateIntBlendInspection.Metrics.Contains(TEXT("StatePlayerCount=4"))
                && StateIntBlendInspection.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("StateIntBlend Machine=Locomotion State=Move"))
                            && Item.Contains(TEXT("PoseCount=3"))
                            && Item.Contains(TEXT("IndexVariable=PoseIndex"))
                            && Item.Contains(TEXT("PoseLinked=true"));
                    })
                && StateIntBlendInspection.Items.ContainsByPredicate(
                    [&AnimSequencePath](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("StateIntBlendPose Machine=Locomotion State=Move Index=0"))
                            && Item.Contains(TEXT("Asset=") + AnimSequencePath)
                            && Item.Contains(TEXT("PlayRate=0.500000"))
                            && Item.Contains(TEXT("BlendTime=0.100000"))
                            && Item.Contains(TEXT("Linked=true"));
                    })
                && StateIntBlendInspection.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("StateIntBlendPose Machine=Locomotion State=Move Index=2"))
                            && Item.Contains(TEXT("PlayRate=1.500000"))
                            && Item.Contains(TEXT("BlendTime=0.300000"));
                    }));

        FThomasAnimationPatchRequest DirectPlayerOverIntBlendRequest;
        DirectPlayerOverIntBlendRequest.AssetPath = AnimBlueprintPath;
        DirectPlayerOverIntBlendRequest.ExpectedRevision = StateIntBlendInspection.Revision;
        DirectPlayerOverIntBlendRequest.Operations.Add(SetRunSequencePlayer);
        DirectPlayerOverIntBlendRequest.Operations[0].StateName = TEXT("Move");
        TestEqual(TEXT("Direct player edit refuses to overwrite an int blend"),
            UThomasDomainsToolset::PlanAnimationPatch(
                DirectPlayerOverIntBlendRequest).Code,
            FString(TEXT("state_int_blend_requires_remove")));

        FThomasAnimationPatchRequest StateIntBlendRemovalRequest;
        StateIntBlendRemovalRequest.AssetPath = AnimBlueprintPath;
        StateIntBlendRemovalRequest.ExpectedRevision = StateIntBlendInspection.Revision;
        FThomasAnimationOperation RemoveMoveIntBlend;
        RemoveMoveIntBlend.Action = TEXT("remove_state_blend_list_by_int");
        RemoveMoveIntBlend.MachineName = TEXT("Locomotion");
        RemoveMoveIntBlend.StateName = TEXT("Move");
        StateIntBlendRemovalRequest.Operations.Add(RemoveMoveIntBlend);
        TestEqual(TEXT("State int blend removal requires R2 confirmation"),
            UThomasDomainsToolset::PlanAnimationPatch(
                StateIntBlendRemovalRequest).Code,
            FString(TEXT("confirmation_required")));
        StateIntBlendRemovalRequest.bConfirmDestructive = true;
        const FThomasAnimationPlanResult StateIntBlendRemovalPlan =
            UThomasDomainsToolset::PlanAnimationPatch(StateIntBlendRemovalRequest);
        TestTrue(TEXT("Confirmed state int blend removal plan succeeds"),
            StateIntBlendRemovalPlan.bOk);
        TestTrue(TEXT("Confirmed state int blend removal compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                StateIntBlendRemovalPlan.PlanId, true).bOk);
        UObject* SavedStateIntBlendRemovalBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText StateIntBlendRemovalReloadError;
        TestTrue(TEXT("Removed state int blend reloads from saved file"),
            SavedStateIntBlendRemovalBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedStateIntBlendRemovalBlueprint->GetOutermost()},
                    StateIntBlendRemovalReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StateIntBlendAfterRemoval =
            UThomasDomainsToolset::InspectAnimationAsset(AnimBlueprintPath, 500);
        TestTrue(TEXT("State int blend is absent after R2 removal"),
            StateIntBlendAfterRemoval.Metrics.Contains(TEXT("StateIntBlendCount=0"))
                && StateIntBlendAfterRemoval.Metrics.Contains(TEXT("StatePlayerCount=1"))
                && !StateIntBlendAfterRemoval.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(
                            TEXT("StateIntBlend Machine=Locomotion State=Move"));
                    }));

        FThomasAnimationPatchRequest StateApplyAdditiveRequest;
        StateApplyAdditiveRequest.AssetPath = AnimBlueprintPath;
        StateApplyAdditiveRequest.ExpectedRevision = StateIntBlendAfterRemoval.Revision;
        FThomasAnimationOperation SetMoveApplyAdditive;
        SetMoveApplyAdditive.Action = TEXT("set_state_apply_additive");
        SetMoveApplyAdditive.MachineName = TEXT("Locomotion");
        SetMoveApplyAdditive.StateName = TEXT("Move");
        SetMoveApplyAdditive.ReferencedAssetPath = AnimSequencePath;
        SetMoveApplyAdditive.SecondaryAssetPath = AdditiveSequencePath;
        SetMoveApplyAdditive.InputAlphaVariableName = TEXT("AdditiveAlpha");
        SetMoveApplyAdditive.PlayRate = 0.9f;
        SetMoveApplyAdditive.SecondaryPlayRate = 1.1f;
        SetMoveApplyAdditive.bLoopAnimation = true;
        SetMoveApplyAdditive.PositionX = 0;
        SetMoveApplyAdditive.PositionY = 0;
        StateApplyAdditiveRequest.Operations.Add(SetMoveApplyAdditive);
        const FThomasAnimationPlanResult StateApplyAdditivePlan =
            UThomasDomainsToolset::PlanAnimationPatch(StateApplyAdditiveRequest);
        TestTrue(TEXT("Anim Blueprint Apply Additive state plan succeeds"),
            StateApplyAdditivePlan.bOk);
        TestTrue(TEXT("Anim Blueprint Apply Additive state compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                StateApplyAdditivePlan.PlanId, true).bOk);
        UObject* SavedStateApplyAdditiveBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText StateApplyAdditiveReloadError;
        TestTrue(TEXT("Anim Blueprint Apply Additive state reloads from saved file"),
            SavedStateApplyAdditiveBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedStateApplyAdditiveBlueprint->GetOutermost()},
                    StateApplyAdditiveReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StateApplyAdditiveInspection =
            UThomasDomainsToolset::InspectAnimationAsset(AnimBlueprintPath, 700);
        TestTrue(TEXT("Anim Blueprint Apply Additive state is inspectable"),
            StateApplyAdditiveInspection.Metrics.Contains(
                TEXT("StateApplyAdditiveCount=1"))
                && StateApplyAdditiveInspection.Metrics.Contains(
                    TEXT("StatePlayerCount=3"))
                && StateApplyAdditiveInspection.Items.ContainsByPredicate(
                    [&AnimSequencePath, &AdditiveSequencePath](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("StateApplyAdditive Machine=Locomotion State=Move"))
                            && Item.Contains(TEXT("BaseAsset=") + AnimSequencePath)
                            && Item.Contains(TEXT("BaseRate=0.900000"))
                            && Item.Contains(
                                TEXT("AdditiveAsset=") + AdditiveSequencePath)
                            && Item.Contains(TEXT("AdditiveRate=1.100000"))
                            && Item.Contains(TEXT("AlphaVariable=AdditiveAlpha"))
                            && Item.Contains(TEXT("Loop=true"))
                            && Item.Contains(TEXT("PoseLinked=true"));
                    }));

        FThomasAnimationPatchRequest DirectPlayerOverAdditiveRequest;
        DirectPlayerOverAdditiveRequest.AssetPath = AnimBlueprintPath;
        DirectPlayerOverAdditiveRequest.ExpectedRevision =
            StateApplyAdditiveInspection.Revision;
        DirectPlayerOverAdditiveRequest.Operations.Add(SetRunSequencePlayer);
        DirectPlayerOverAdditiveRequest.Operations[0].StateName = TEXT("Move");
        TestEqual(TEXT("Direct player edit refuses to overwrite Apply Additive"),
            UThomasDomainsToolset::PlanAnimationPatch(
                DirectPlayerOverAdditiveRequest).Code,
            FString(TEXT("state_apply_additive_requires_remove")));

        FThomasAnimationPatchRequest StateApplyAdditiveRemovalRequest;
        StateApplyAdditiveRemovalRequest.AssetPath = AnimBlueprintPath;
        StateApplyAdditiveRemovalRequest.ExpectedRevision =
            StateApplyAdditiveInspection.Revision;
        FThomasAnimationOperation RemoveMoveApplyAdditive;
        RemoveMoveApplyAdditive.Action = TEXT("remove_state_apply_additive");
        RemoveMoveApplyAdditive.MachineName = TEXT("Locomotion");
        RemoveMoveApplyAdditive.StateName = TEXT("Move");
        StateApplyAdditiveRemovalRequest.Operations.Add(RemoveMoveApplyAdditive);
        TestEqual(TEXT("Apply Additive state removal requires R2 confirmation"),
            UThomasDomainsToolset::PlanAnimationPatch(
                StateApplyAdditiveRemovalRequest).Code,
            FString(TEXT("confirmation_required")));
        StateApplyAdditiveRemovalRequest.bConfirmDestructive = true;
        const FThomasAnimationPlanResult StateApplyAdditiveRemovalPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                StateApplyAdditiveRemovalRequest);
        TestTrue(TEXT("Confirmed Apply Additive state removal plan succeeds"),
            StateApplyAdditiveRemovalPlan.bOk);
        TestTrue(TEXT("Confirmed Apply Additive removal compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                StateApplyAdditiveRemovalPlan.PlanId, true).bOk);
        UObject* SavedStateApplyAdditiveRemovalBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText StateApplyAdditiveRemovalReloadError;
        TestTrue(TEXT("Removed Apply Additive state reloads from saved file"),
            SavedStateApplyAdditiveRemovalBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedStateApplyAdditiveRemovalBlueprint->GetOutermost()},
                    StateApplyAdditiveRemovalReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StateApplyAdditiveAfterRemoval =
            UThomasDomainsToolset::InspectAnimationAsset(AnimBlueprintPath, 500);
        TestTrue(TEXT("Apply Additive state is absent after R2 removal"),
            StateApplyAdditiveAfterRemoval.Metrics.Contains(
                TEXT("StateApplyAdditiveCount=0"))
                && StateApplyAdditiveAfterRemoval.Metrics.Contains(
                    TEXT("StatePlayerCount=1"))
                && !StateApplyAdditiveAfterRemoval.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(
                            TEXT("StateApplyAdditive Machine=Locomotion State=Move"));
                    }));

        FThomasAnimationPatchRequest StateLayeredBlendRequest;
        StateLayeredBlendRequest.AssetPath = AnimBlueprintPath;
        StateLayeredBlendRequest.ExpectedRevision =
            StateApplyAdditiveAfterRemoval.Revision;
        FThomasAnimationOperation SetMoveLayeredBlend;
        SetMoveLayeredBlend.Action =
            TEXT("set_state_layered_blend_per_bone");
        SetMoveLayeredBlend.MachineName = TEXT("Locomotion");
        SetMoveLayeredBlend.StateName = TEXT("Move");
        SetMoveLayeredBlend.ReferencedAssetPath = AnimSequencePath;
        SetMoveLayeredBlend.ReferencedAssetPaths = {
            AnimSequencePath, AnimSequencePath};
        SetMoveLayeredBlend.PlayRate = 0.75f;
        SetMoveLayeredBlend.PlayRates = {1.10f, 0.80f};
        SetMoveLayeredBlend.LayerAlphaVariableNames = {
            TEXT("AdditiveAlpha"), TEXT("Speed")};
        SetMoveLayeredBlend.LayerBoneNames = {
            LayeredBlendBoneName, LayeredBlendBoneName};
        SetMoveLayeredBlend.LayerBlendDepths = {1, 2};
        SetMoveLayeredBlend.bMeshSpaceRotationBlend = true;
        SetMoveLayeredBlend.bMeshSpaceScaleBlend = false;
        SetMoveLayeredBlend.bBlendRootMotionBasedOnRootBone = true;
        SetMoveLayeredBlend.bLoopAnimation = true;
        SetMoveLayeredBlend.PositionX = 0;
        SetMoveLayeredBlend.PositionY = 0;
        StateLayeredBlendRequest.Operations.Add(SetMoveLayeredBlend);
        const FThomasAnimationPlanResult StateLayeredBlendPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                StateLayeredBlendRequest);
        TestTrue(TEXT("Anim Blueprint two-layer per-bone blend plan succeeds"),
            StateLayeredBlendPlan.bOk);
        TestTrue(TEXT("Anim Blueprint layered per-bone blend compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                StateLayeredBlendPlan.PlanId, true).bOk);
        UObject* SavedStateLayeredBlendBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText StateLayeredBlendReloadError;
        TestTrue(TEXT("Layered per-bone state reloads from saved file"),
            SavedStateLayeredBlendBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedStateLayeredBlendBlueprint->GetOutermost()},
                    StateLayeredBlendReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StateLayeredBlendInspection =
            UThomasDomainsToolset::InspectAnimationAsset(
                AnimBlueprintPath, 800);
        TestTrue(TEXT("Two-layer per-bone state is inspectable"),
            StateLayeredBlendInspection.Metrics.Contains(
                TEXT("StateLayeredBlendPerBoneCount=1"))
                && StateLayeredBlendInspection.Metrics.Contains(
                    TEXT("StatePlayerCount=4"))
                && StateLayeredBlendInspection.Items.ContainsByPredicate(
                    [&AnimSequencePath](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("StateLayeredBlendPerBone Machine=Locomotion State=Move"))
                            && Item.Contains(
                                TEXT("BaseAsset=") + AnimSequencePath)
                            && Item.Contains(TEXT("BaseRate=0.750000"))
                            && Item.Contains(TEXT("LayerCount=2"))
                            && Item.Contains(TEXT("MeshRotation=true"))
                            && Item.Contains(TEXT("MeshScale=false"))
                            && Item.Contains(TEXT("RootMotionRootBone=true"))
                            && Item.Contains(TEXT("PoseLinked=true"));
                    })
                && StateLayeredBlendInspection.Items.ContainsByPredicate(
                    [&AnimSequencePath, &LayeredBlendBoneName](
                        const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("StateLayeredBlendLayer Machine=Locomotion State=Move Index=0"))
                            && Item.Contains(TEXT("Asset=") + AnimSequencePath)
                            && Item.Contains(TEXT("PlayRate=1.100000"))
                            && Item.Contains(TEXT("AlphaVariable=AdditiveAlpha"))
                            && Item.Contains(
                                TEXT("Bone=") + LayeredBlendBoneName)
                            && Item.Contains(TEXT("BlendDepth=1"))
                            && Item.Contains(TEXT("Linked=true"));
                    })
                && StateLayeredBlendInspection.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("StateLayeredBlendLayer Machine=Locomotion State=Move Index=1"))
                            && Item.Contains(TEXT("PlayRate=0.800000"))
                            && Item.Contains(TEXT("AlphaVariable=Speed"))
                            && Item.Contains(TEXT("BlendDepth=2"));
                    }));

        FString R39ReferenceSequencePath;
        const FThomasSpecializedAssetInspectionResult
            StateLayeredBlendAfterR39 =
                RunR39ComplexStateGraphPropertyTests(
                    *this,
                    AnimBlueprintPath,
                    AnimationFixtureSkeletonPath,
                    StateLayeredBlendInspection,
                    R39ReferenceSequencePath);
        const FThomasSpecializedAssetInspectionResult
            StateLayeredBlendAfterR40 =
                RunR40StateGraphArrayLifecycleTests(
                    *this,
                    AnimBlueprintPath,
                    StateLayeredBlendAfterR39);

        FThomasAnimationPatchRequest DirectPlayerOverLayeredBlendRequest;
        DirectPlayerOverLayeredBlendRequest.AssetPath = AnimBlueprintPath;
        DirectPlayerOverLayeredBlendRequest.ExpectedRevision =
            StateLayeredBlendAfterR40.Revision;
        DirectPlayerOverLayeredBlendRequest.Operations.Add(
            SetRunSequencePlayer);
        DirectPlayerOverLayeredBlendRequest.Operations[0].StateName =
            TEXT("Move");
        TestEqual(TEXT("Direct player edit refuses to overwrite layered blend"),
            UThomasDomainsToolset::PlanAnimationPatch(
                DirectPlayerOverLayeredBlendRequest).Code,
            FString(TEXT("state_layered_blend_requires_remove")));

        FThomasAnimationPatchRequest StateLayeredBlendRemovalRequest;
        StateLayeredBlendRemovalRequest.AssetPath = AnimBlueprintPath;
        StateLayeredBlendRemovalRequest.ExpectedRevision =
            StateLayeredBlendAfterR40.Revision;
        FThomasAnimationOperation RemoveMoveLayeredBlend;
        RemoveMoveLayeredBlend.Action =
            TEXT("remove_state_layered_blend_per_bone");
        RemoveMoveLayeredBlend.MachineName = TEXT("Locomotion");
        RemoveMoveLayeredBlend.StateName = TEXT("Move");
        StateLayeredBlendRemovalRequest.Operations.Add(
            RemoveMoveLayeredBlend);
        TestEqual(TEXT("Layered per-bone removal requires R2 confirmation"),
            UThomasDomainsToolset::PlanAnimationPatch(
                StateLayeredBlendRemovalRequest).Code,
            FString(TEXT("confirmation_required")));
        StateLayeredBlendRemovalRequest.bConfirmDestructive = true;
        const FThomasAnimationPlanResult StateLayeredBlendRemovalPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                StateLayeredBlendRemovalRequest);
        TestTrue(TEXT("Confirmed layered per-bone removal plan succeeds"),
            StateLayeredBlendRemovalPlan.bOk);
        TestTrue(TEXT("Confirmed layered per-bone removal compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                StateLayeredBlendRemovalPlan.PlanId, true).bOk);
        UObject* SavedStateLayeredBlendRemovalBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText StateLayeredBlendRemovalReloadError;
        TestTrue(TEXT("Removed layered per-bone state reloads from saved file"),
            SavedStateLayeredBlendRemovalBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedStateLayeredBlendRemovalBlueprint->GetOutermost()},
                    StateLayeredBlendRemovalReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult StateLayeredBlendAfterRemoval =
            UThomasDomainsToolset::InspectAnimationAsset(
                AnimBlueprintPath, 500);
        TestTrue(TEXT("Layered per-bone state is absent after R2 removal"),
            StateLayeredBlendAfterRemoval.Metrics.Contains(
                TEXT("StateLayeredBlendPerBoneCount=0"))
                && StateLayeredBlendAfterRemoval.Metrics.Contains(
                    TEXT("StatePlayerCount=1"))
                && !StateLayeredBlendAfterRemoval.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(
                            TEXT("StateLayeredBlendPerBone Machine=Locomotion State=Move"));
                    }));

        FThomasAnimationPatchRequest TimeRemainingRuleRequest;
        TimeRemainingRuleRequest.AssetPath = AnimBlueprintPath;
        TimeRemainingRuleRequest.ExpectedRevision =
            StateLayeredBlendAfterRemoval.Revision;
        FThomasAnimationOperation SetIdleToMoveTimeRemainingRule;
        SetIdleToMoveTimeRemainingRule.Action =
            TEXT("set_transition_rule_time_remaining");
        SetIdleToMoveTimeRemainingRule.MachineName = TEXT("Locomotion");
        SetIdleToMoveTimeRemainingRule.FromStateName = TEXT("Idle");
        SetIdleToMoveTimeRemainingRule.ToStateName = TEXT("Move");
        SetIdleToMoveTimeRemainingRule.Name = TEXT("CanMove");
        SetIdleToMoveTimeRemainingRule.TransitionGetter =
            TEXT("time_remaining_fraction");
        SetIdleToMoveTimeRemainingRule.ComparisonOperator =
            TEXT("less_equal");
        SetIdleToMoveTimeRemainingRule.LogicalOperator = TEXT("and");
        SetIdleToMoveTimeRemainingRule.Value = 0.20f;
        SetIdleToMoveTimeRemainingRule.PositionX = -150;
        SetIdleToMoveTimeRemainingRule.PositionY = 0;
        TimeRemainingRuleRequest.Operations.Add(
            SetIdleToMoveTimeRemainingRule);
        const FThomasAnimationPlanResult TimeRemainingRulePlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                TimeRemainingRuleRequest);
        TestTrue(TEXT("Composed time-remaining transition rule plan succeeds"),
            TimeRemainingRulePlan.bOk);
        const FThomasAnimationApplyResult TimeRemainingRuleApply =
            UThomasDomainsToolset::ApplyAnimationPlan(
                TimeRemainingRulePlan.PlanId, true);
        AddInfo(FString::Printf(
            TEXT("Time remaining rule apply: ok=%d code=%s message=%s diagnostics=%s"),
            TimeRemainingRuleApply.bOk ? 1 : 0,
            *TimeRemainingRuleApply.Code,
            *TimeRemainingRuleApply.Message,
            *FString::Join(TimeRemainingRuleApply.Diagnostics, TEXT(" | "))));
        TestTrue(TEXT("Composed time-remaining rule compiles and saves"),
            TimeRemainingRuleApply.bOk);
        UObject* SavedTimeRemainingRuleBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText TimeRemainingRuleReloadError;
        TestTrue(TEXT("Composed time-remaining rule reloads from saved file"),
            SavedTimeRemainingRuleBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedTimeRemainingRuleBlueprint->GetOutermost()},
                    TimeRemainingRuleReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult TimeRemainingRuleInspection =
            UThomasDomainsToolset::InspectAnimationAsset(
                AnimBlueprintPath, 600);
        TestTrue(TEXT("Composed Transition Getter rule is inspectable"),
            TimeRemainingRuleInspection.Metrics.Contains(
                TEXT("TransitionTimeRemainingRuleCount=1"))
                && TimeRemainingRuleInspection.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("Transition Machine=Locomotion From=Idle To=Move"))
                            && Item.Contains(
                                TEXT("RuleGetter=time_remaining_fraction"))
                            && Item.Contains(
                                TEXT("RuleComparison=less_equal"))
                            && Item.Contains(TEXT("RuleThreshold=0.2"))
                            && Item.Contains(TEXT("RuleGuard=CanMove"))
                            && Item.Contains(TEXT("RuleLogical=and"))
                            && Item.Contains(TEXT("RuleLinked=1"))
                            && Item.Contains(TEXT("RuleNodes=5"));
                    }));

        FThomasAnimationPatchRequest ConstantOverTimeRuleRequest;
        ConstantOverTimeRuleRequest.AssetPath = AnimBlueprintPath;
        ConstantOverTimeRuleRequest.ExpectedRevision =
            TimeRemainingRuleInspection.Revision;
        ConstantOverTimeRuleRequest.Operations.Add(
            RestoreIdleToMoveConstantRule);
        TestEqual(TEXT("Constant refuses to overwrite composed rule"),
            UThomasDomainsToolset::PlanAnimationPatch(
                ConstantOverTimeRuleRequest).Code,
            FString(TEXT("linked_transition_rule_unsupported")));

        FThomasAnimationPatchRequest TimeRemainingRuleRemovalRequest;
        TimeRemainingRuleRemovalRequest.AssetPath = AnimBlueprintPath;
        TimeRemainingRuleRemovalRequest.ExpectedRevision =
            TimeRemainingRuleInspection.Revision;
        FThomasAnimationOperation RemoveIdleToMoveTimeRemainingRule;
        RemoveIdleToMoveTimeRemainingRule.Action =
            TEXT("remove_transition_rule_time_remaining");
        RemoveIdleToMoveTimeRemainingRule.MachineName = TEXT("Locomotion");
        RemoveIdleToMoveTimeRemainingRule.FromStateName = TEXT("Idle");
        RemoveIdleToMoveTimeRemainingRule.ToStateName = TEXT("Move");
        TimeRemainingRuleRemovalRequest.Operations.Add(
            RemoveIdleToMoveTimeRemainingRule);
        TestEqual(TEXT("Composed time rule removal requires R2 confirmation"),
            UThomasDomainsToolset::PlanAnimationPatch(
                TimeRemainingRuleRemovalRequest).Code,
            FString(TEXT("confirmation_required")));
        TimeRemainingRuleRemovalRequest.bConfirmDestructive = true;
        const FThomasAnimationPlanResult TimeRemainingRuleRemovalPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                TimeRemainingRuleRemovalRequest);
        TestTrue(TEXT("Confirmed composed time rule removal plan succeeds"),
            TimeRemainingRuleRemovalPlan.bOk);
        TestTrue(TEXT("Composed time rule removal compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                TimeRemainingRuleRemovalPlan.PlanId, true).bOk);
        UObject* SavedTimeRemainingRuleRemovalBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText TimeRemainingRuleRemovalReloadError;
        TestTrue(TEXT("Removed composed time rule reloads from saved file"),
            SavedTimeRemainingRuleRemovalBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedTimeRemainingRuleRemovalBlueprint->GetOutermost()},
                    TimeRemainingRuleRemovalReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult TimeRemainingRuleAfterRemoval =
            UThomasDomainsToolset::InspectAnimationAsset(
                AnimBlueprintPath, 500);
        TestTrue(TEXT("Composed time rule is absent after R2 removal"),
            TimeRemainingRuleAfterRemoval.Metrics.Contains(
                TEXT("TransitionTimeRemainingRuleCount=0"))
                && TimeRemainingRuleAfterRemoval.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("Transition Machine=Locomotion From=Idle To=Move"))
                            && Item.Contains(TEXT("RuleDefault=false"))
                            && Item.Contains(TEXT("RuleGetter=None"))
                            && Item.Contains(TEXT("RuleLinked=0"))
                            && Item.Contains(TEXT("RuleNodes=1"));
                    }));

        FThomasAnimationPatchRequest InvalidExpressionRequest;
        InvalidExpressionRequest.AssetPath = AnimBlueprintPath;
        InvalidExpressionRequest.ExpectedRevision =
            TimeRemainingRuleAfterRemoval.Revision;
        FThomasAnimationOperation InvalidExpression;
        InvalidExpression.Action = TEXT("set_transition_rule_expression");
        InvalidExpression.MachineName = TEXT("Locomotion");
        InvalidExpression.FromStateName = TEXT("Idle");
        InvalidExpression.ToStateName = TEXT("Move");
        InvalidExpression.TransitionExpressionTokens = {
            MakeTransitionExpressionToken(
                TEXT("bool_variable"), TEXT("CanMove")),
            MakeTransitionExpressionToken(TEXT("number"), FString(), FString(), 1.0),
            MakeTransitionExpressionToken(TEXT("and"))};
        InvalidExpressionRequest.Operations.Add(InvalidExpression);
        TestEqual(TEXT("Transition expression preflight rejects type mismatch"),
            UThomasDomainsToolset::PlanAnimationPatch(
                InvalidExpressionRequest).Code,
            FString(TEXT("invalid_transition_expression")));
        RunR38TransitionExpressionRefusalTests(
            *this, AnimBlueprintPath, TimeRemainingRuleAfterRemoval);

        FThomasAnimationPatchRequest ExpressionRequest;
        ExpressionRequest.AssetPath = AnimBlueprintPath;
        ExpressionRequest.ExpectedRevision =
            TimeRemainingRuleAfterRemoval.Revision;
        FThomasAnimationOperation SetIdleToMoveExpression;
        SetIdleToMoveExpression.Action =
            TEXT("set_transition_rule_expression");
        SetIdleToMoveExpression.MachineName = TEXT("Locomotion");
        SetIdleToMoveExpression.FromStateName = TEXT("Idle");
        SetIdleToMoveExpression.ToStateName = TEXT("Move");
        SetIdleToMoveExpression.PositionX = -100;
        SetIdleToMoveExpression.PositionY = 0;
        SetIdleToMoveExpression.TransitionExpressionTokens =
            MakeR38TransitionExpressionTokens();
        ExpressionRequest.Operations.Add(SetIdleToMoveExpression);
        const FThomasAnimationPlanResult ExpressionPlan =
            UThomasDomainsToolset::PlanAnimationPatch(ExpressionRequest);
        TestTrue(TEXT("Typed composed transition expression plan succeeds"),
            ExpressionPlan.bOk);
        const FThomasAnimationApplyResult ExpressionApply =
            UThomasDomainsToolset::ApplyAnimationPlan(
                ExpressionPlan.PlanId, true);
        AddInfo(FString::Printf(
            TEXT("Transition expression apply: ok=%d code=%s message=%s diagnostics=%s"),
            ExpressionApply.bOk ? 1 : 0,
            *ExpressionApply.Code,
            *ExpressionApply.Message,
            *FString::Join(ExpressionApply.Diagnostics, TEXT(" | "))));
        TestTrue(TEXT("Typed composed transition expression compiles and saves"),
            ExpressionApply.bOk);
        UObject* SavedExpressionBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText ExpressionReloadError;
        TestTrue(TEXT("Typed composed transition expression reloads from saved file"),
            SavedExpressionBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedExpressionBlueprint->GetOutermost()},
                    ExpressionReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult ExpressionInspection =
            UThomasDomainsToolset::InspectAnimationAsset(
                AnimBlueprintPath, 700);
        TestTrue(TEXT("Typed transition expression is reconstructed canonically"),
            ExpressionInspection.Metrics.Contains(
                TEXT("TransitionExpressionRuleCount=1"))
                && ExpressionInspection.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("Transition Machine=Locomotion From=Idle To=Move"))
                            && Item.Contains(
                                TEXT("RuleExpression=")
                                    + R38TransitionExpressionCanonical())
                            && Item.Contains(TEXT("RuleExpressionTokens=36"))
                            && Item.Contains(TEXT("RuleLinked=1"))
                            && Item.Contains(TEXT("RuleNodes=31"));
                    }));

        const FThomasSpecializedAssetInspectionResult
            ExpressionAfterR38Rename =
                RunR38ArbitraryStateContextRenameTests(
                    *this, AnimBlueprintPath, ExpressionInspection);

        FThomasAnimationPatchRequest ConstantOverExpressionRequest;
        ConstantOverExpressionRequest.AssetPath = AnimBlueprintPath;
        ConstantOverExpressionRequest.ExpectedRevision =
            ExpressionAfterR38Rename.Revision;
        ConstantOverExpressionRequest.Operations.Add(
            RestoreIdleToMoveConstantRule);
        TestEqual(TEXT("Constant refuses to overwrite typed expression"),
            UThomasDomainsToolset::PlanAnimationPatch(
                ConstantOverExpressionRequest).Code,
            FString(TEXT("linked_transition_rule_unsupported")));

        FThomasAnimationPatchRequest ExpressionRemovalRequest;
        ExpressionRemovalRequest.AssetPath = AnimBlueprintPath;
        ExpressionRemovalRequest.ExpectedRevision =
            ExpressionAfterR38Rename.Revision;
        FThomasAnimationOperation RemoveIdleToMoveExpression;
        RemoveIdleToMoveExpression.Action =
            TEXT("remove_transition_rule_expression");
        RemoveIdleToMoveExpression.MachineName = TEXT("Locomotion");
        RemoveIdleToMoveExpression.FromStateName = TEXT("Idle");
        RemoveIdleToMoveExpression.ToStateName = TEXT("Move");
        ExpressionRemovalRequest.Operations.Add(
            RemoveIdleToMoveExpression);
        TestEqual(TEXT("Transition expression removal requires R2 confirmation"),
            UThomasDomainsToolset::PlanAnimationPatch(
                ExpressionRemovalRequest).Code,
            FString(TEXT("confirmation_required")));
        ExpressionRemovalRequest.bConfirmDestructive = true;
        const FThomasAnimationPlanResult ExpressionRemovalPlan =
            UThomasDomainsToolset::PlanAnimationPatch(
                ExpressionRemovalRequest);
        TestTrue(TEXT("Confirmed transition expression removal plan succeeds"),
            ExpressionRemovalPlan.bOk);
        TestTrue(TEXT("Transition expression removal compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(
                ExpressionRemovalPlan.PlanId, true).bOk);
        UObject* SavedExpressionRemovalBlueprint = LoadObject<UObject>(
            nullptr,
            *(AnimBlueprintPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AnimBlueprintPath)));
        FText ExpressionRemovalReloadError;
        TestTrue(TEXT("Removed transition expression reloads from saved file"),
            SavedExpressionRemovalBlueprint
                && UPackageTools::ReloadPackages(
                    {SavedExpressionRemovalBlueprint->GetOutermost()},
                    ExpressionRemovalReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        const FThomasSpecializedAssetInspectionResult ExpressionAfterRemoval =
            UThomasDomainsToolset::InspectAnimationAsset(
                AnimBlueprintPath, 500);
        TestTrue(TEXT("Transition expression is absent after R2 removal"),
            ExpressionAfterRemoval.Metrics.Contains(
                TEXT("TransitionExpressionRuleCount=0"))
                && ExpressionAfterRemoval.Items.ContainsByPredicate(
                    [](const FString& Item)
                    {
                        return Item.Contains(
                                TEXT("Transition Machine=Locomotion From=Idle To=Move"))
                            && Item.Contains(TEXT("RuleDefault=false"))
                            && Item.Contains(TEXT("RuleExpression=None"))
                            && Item.Contains(TEXT("RuleExpressionTokens=0"))
                            && Item.Contains(TEXT("RuleLinked=0"))
                            && Item.Contains(TEXT("RuleNodes=1"));
                    }));

        FString R42LinkedTargetPath;
        FString R43CurveFloatPath;
        FString R44PhysicsAssetPath;
        FString R44MirrorDataTablePath;
        FString R45PhysicsControlAssetPath;
        const FThomasSpecializedAssetInspectionResult
            GenericStateGraphAfterCleanup = RunGenericStateGraphTests(
                *this,
                AnimBlueprintPath,
                AnimationFixtureSkeletonPath,
                AnimSequencePath,
                IKRigPath,
                IKRetargeterPath,
                ExpressionAfterRemoval,
                R42LinkedTargetPath,
                R43CurveFloatPath,
                R44PhysicsAssetPath,
                R44MirrorDataTablePath,
                R45PhysicsControlAssetPath);

        FThomasAnimationPatchRequest StateMachineRemovalRequest;
        StateMachineRemovalRequest.AssetPath = AnimBlueprintPath;
        StateMachineRemovalRequest.ExpectedRevision =
            GenericStateGraphAfterCleanup.Revision;
        FThomasAnimationOperation RemoveStateMachine;
        RemoveStateMachine.Action = TEXT("remove_state_machine");
        RemoveStateMachine.MachineName = TEXT("Locomotion");
        StateMachineRemovalRequest.Operations.Add(RemoveStateMachine);
        TestEqual(TEXT("Anim Blueprint structural removal requires R2 confirmation"),
            UThomasDomainsToolset::PlanAnimationPatch(StateMachineRemovalRequest).Code,
            FString(TEXT("confirmation_required")));
        StateMachineRemovalRequest.bConfirmDestructive = true;
        const FThomasAnimationPlanResult StateMachineRemovalPlan =
            UThomasDomainsToolset::PlanAnimationPatch(StateMachineRemovalRequest);
        TestTrue(TEXT("Confirmed Anim Blueprint state-machine removal plan succeeds"),
            StateMachineRemovalPlan.bOk);
        TestTrue(TEXT("Confirmed Anim Blueprint state-machine removal compiles and saves"),
            UThomasDomainsToolset::ApplyAnimationPlan(StateMachineRemovalPlan.PlanId, true).bOk);

        TArray<UObject*> AnimationAssetsToDelete;
        if (UObject* AnimSequenceAsset = LoadObject<UObject>(nullptr,
            *(AnimSequencePath + TEXT(".") + FPackageName::GetLongPackageAssetName(AnimSequencePath))))
        {
            AnimationAssetsToDelete.Add(AnimSequenceAsset);
        }
        if (UObject* AdditiveSequenceAsset = LoadObject<UObject>(nullptr,
            *(AdditiveSequencePath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(AdditiveSequencePath))))
        {
            AnimationAssetsToDelete.Add(AdditiveSequenceAsset);
        }
        if (UObject* AnimMontageAsset = LoadObject<UObject>(nullptr,
            *(AnimMontagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(AnimMontagePath))))
        {
            AnimationAssetsToDelete.Add(AnimMontageAsset);
        }
        if (UObject* BlendSpaceAsset = LoadObject<UObject>(nullptr,
            *(BlendSpacePath + TEXT(".") + FPackageName::GetLongPackageAssetName(BlendSpacePath))))
        {
            AnimationAssetsToDelete.Add(BlendSpaceAsset);
        }
        if (UObject* AnimBlueprintAsset = LoadObject<UObject>(nullptr,
            *(AnimBlueprintPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AnimBlueprintPath))))
        {
            AnimationAssetsToDelete.Add(AnimBlueprintAsset);
        }
        if (UObject* IKRetargeterAsset = LoadObject<UObject>(nullptr,
            *(IKRetargeterPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    IKRetargeterPath))))
        {
            AnimationAssetsToDelete.Add(IKRetargeterAsset);
        }
        if (UObject* ControlRigAsset = LoadObject<UObject>(nullptr,
            *(ControlRigPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    ControlRigPath))))
        {
            AnimationAssetsToDelete.Add(ControlRigAsset);
        }
        if (UObject* ExternalControlRigAsset = LoadObject<UObject>(nullptr,
            *(ExternalControlRigPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    ExternalControlRigPath))))
        {
            AnimationAssetsToDelete.Add(ExternalControlRigAsset);
        }
        if (UObject* IKRigAsset = LoadObject<UObject>(nullptr,
            *(IKRigPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(IKRigPath))))
        {
            AnimationAssetsToDelete.Add(IKRigAsset);
        }
        if (UObject* R39ReferenceSequenceAsset = LoadObject<UObject>(nullptr,
            *(R39ReferenceSequencePath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    R39ReferenceSequencePath))))
        {
            AnimationAssetsToDelete.Add(R39ReferenceSequenceAsset);
        }
        if (UObject* R42LinkedTargetAsset = LoadObject<UObject>(nullptr,
            *(R42LinkedTargetPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    R42LinkedTargetPath))))
        {
            AnimationAssetsToDelete.Add(R42LinkedTargetAsset);
        }
        if (UObject* R43CurveFloatAsset = LoadObject<UObject>(nullptr,
            *(R43CurveFloatPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    R43CurveFloatPath))))
        {
            AnimationAssetsToDelete.Add(R43CurveFloatAsset);
        }
        if (UObject* R44PhysicsAsset = LoadObject<UObject>(nullptr,
            *(R44PhysicsAssetPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    R44PhysicsAssetPath))))
        {
            AnimationAssetsToDelete.Add(R44PhysicsAsset);
        }
        if (UObject* R44MirrorDataTable = LoadObject<UObject>(nullptr,
            *(R44MirrorDataTablePath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    R44MirrorDataTablePath))))
        {
            AnimationAssetsToDelete.Add(R44MirrorDataTable);
        }
        TestEqual(TEXT("Temporary native Animation assets loaded for cleanup"),
            AnimationAssetsToDelete.Num(), 14);
        TestEqual(TEXT("Temporary native Animation assets deleted through Unreal API"),
            ObjectTools::DeleteObjectsUnchecked(AnimationAssetsToDelete), AnimationAssetsToDelete.Num());
        TestFalse(TEXT("Temporary native Anim Montage removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                AnimMontagePath, FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary native Anim Sequence removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                AnimSequencePath, FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary native additive Anim Sequence removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                AdditiveSequencePath, FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary R39 reference Anim Sequence removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                R39ReferenceSequencePath,
                FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary R42 linked Anim Blueprint removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                R42LinkedTargetPath,
                FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary R43 CurveFloat removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                R43CurveFloatPath,
                FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary R44 PhysicsAsset removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                R44PhysicsAssetPath,
                FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary R44 MirrorDataTable removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                R44MirrorDataTablePath,
                FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary native Anim Blueprint removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                AnimBlueprintPath, FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary native Blend Space removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                BlendSpacePath, FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary native IK Rig removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                IKRigPath, FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary native IK Retargeter removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                IKRetargeterPath,
                FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary native Control Rig removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                ControlRigPath,
                FPackageName::GetAssetPackageExtension())));
        TestFalse(TEXT("Temporary external-library Control Rig removed from disk"),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                ExternalControlRigPath,
                FPackageName::GetAssetPackageExtension())));
        CleanupR45TemporaryAnimationFixtures(
            *this,
            R45PhysicsControlAssetPath,
            AnimationFixtureSkeletonPath);
    }

    const FString NiagaraSystemPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/NS_TE_Native_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FThomasDomainAssetCreateRequest NiagaraSystemRequest;
    NiagaraSystemRequest.AssetPath = NiagaraSystemPath;
    NiagaraSystemRequest.ExpectedRevision = TEXT("missing");
    NiagaraSystemRequest.AssetKind = TEXT("niagara_system");
    const FThomasDomainAssetCreatePlanResult NiagaraSystemPlan =
        UThomasDomainsToolset::PlanFxAssetCreate(NiagaraSystemRequest);
    TestTrue(TEXT("Native Niagara System create plan succeeds"), NiagaraSystemPlan.bOk);
    const FThomasDomainAssetCreateApplyResult NiagaraSystemApply =
        UThomasDomainsToolset::ApplyFxAssetCreate(NiagaraSystemPlan.PlanId, true);
    TestTrue(TEXT("Native Niagara System create/save succeeds"), NiagaraSystemApply.bOk);
    const FThomasSpecializedAssetInspectionResult NiagaraSystemInspection =
        UThomasDomainsToolset::InspectFxAsset(NiagaraSystemPath, 100);
    TestTrue(TEXT("Typed Niagara System inspection succeeds"), NiagaraSystemInspection.bOk);
    TestEqual(TEXT("Typed Niagara inspection reports system kind"),
        NiagaraSystemInspection.AssetKind, FString(TEXT("niagara_system")));
    TestTrue(TEXT("Typed Niagara inspection reports emitter count"),
        NiagaraSystemInspection.Metrics.ContainsByPredicate([](const FString& Item)
        {
            return Item.StartsWith(TEXT("EmitterCount="));
        }));
    const FThomasSpecializedAssetInspectionResult NiagaraModuleSearch =
        UThomasDomainsToolset::SearchNiagaraModules(TEXT("Spawn"), TEXT("emitter_update"), 20);
    TestTrue(TEXT("Native Niagara module discovery succeeds"), NiagaraModuleSearch.bOk);

    FThomasNiagaraPatchRequest NiagaraParameterRequest;
    NiagaraParameterRequest.AssetPath = NiagaraSystemPath;
    NiagaraParameterRequest.ExpectedRevision = NiagaraSystemInspection.Revision;
    FThomasNiagaraOperation AddNiagaraParameter;
    AddNiagaraParameter.Action = TEXT("add_user_parameter");
    AddNiagaraParameter.Name = TEXT("User.TestStrength");
    AddNiagaraParameter.Type = TEXT("float");
    AddNiagaraParameter.Value = TEXT("2.5");
    NiagaraParameterRequest.Operations.Add(AddNiagaraParameter);
    FThomasNiagaraOperation CompileNiagara;
    CompileNiagara.Action = TEXT("compile");
    NiagaraParameterRequest.Operations.Add(CompileNiagara);
    const FThomasNiagaraPlanResult NiagaraParameterPlan =
        UThomasDomainsToolset::PlanNiagaraPatch(NiagaraParameterRequest);
    TestTrue(TEXT("Guarded Niagara user parameter plan succeeds"), NiagaraParameterPlan.bOk);
    const FThomasNiagaraApplyResult NiagaraParameterApply =
        UThomasDomainsToolset::ApplyNiagaraPlan(NiagaraParameterPlan.PlanId, true);
    TestTrue(TEXT("Guarded Niagara user parameter apply compiles and saves"),
        NiagaraParameterApply.bOk && NiagaraParameterApply.bCompiled && NiagaraParameterApply.bSaved);
    const FThomasSpecializedAssetInspectionResult NiagaraAfterParameter =
        UThomasDomainsToolset::InspectFxAsset(NiagaraSystemPath, 200);
    TestTrue(TEXT("Niagara user parameter is inspectable after save"),
        NiagaraAfterParameter.bOk && NiagaraAfterParameter.Items.ContainsByPredicate([](const FString& Item)
        {
            return Item.Contains(TEXT("Parameter Name=User.TestStrength"));
        }));

    FThomasNiagaraPatchRequest UnconfirmedNiagaraRemoval;
    UnconfirmedNiagaraRemoval.AssetPath = NiagaraSystemPath;
    UnconfirmedNiagaraRemoval.ExpectedRevision = NiagaraAfterParameter.Revision;
    FThomasNiagaraOperation RemoveNiagaraParameter;
    RemoveNiagaraParameter.Action = TEXT("remove_user_parameter");
    RemoveNiagaraParameter.Name = TEXT("User.TestStrength");
    RemoveNiagaraParameter.Type = TEXT("float");
    UnconfirmedNiagaraRemoval.Operations.Add(RemoveNiagaraParameter);
    TestEqual(TEXT("Niagara removal requires explicit R2 confirmation"),
        UThomasDomainsToolset::PlanNiagaraPatch(UnconfirmedNiagaraRemoval).Code,
        FString(TEXT("confirmation_required")));
    UnconfirmedNiagaraRemoval.bConfirmDestructive = true;
    const FThomasNiagaraPlanResult NiagaraRemovalPlan =
        UThomasDomainsToolset::PlanNiagaraPatch(UnconfirmedNiagaraRemoval);
    TestTrue(TEXT("Confirmed Niagara user parameter removal plan succeeds"), NiagaraRemovalPlan.bOk);
    TestTrue(TEXT("Confirmed Niagara user parameter removal applies and saves"),
        UThomasDomainsToolset::ApplyNiagaraPlan(NiagaraRemovalPlan.PlanId, true).bOk);
    const FThomasSpecializedAssetInspectionResult NiagaraAfterRemoval =
        UThomasDomainsToolset::InspectFxAsset(NiagaraSystemPath, 200);
    TestFalse(TEXT("Removed Niagara user parameter is absent"),
        NiagaraAfterRemoval.Items.ContainsByPredicate([](const FString& Item)
        {
            return Item.Contains(TEXT("Parameter Name=User.TestStrength"));
        }));
    if (UObject* NiagaraAsset = LoadObject<UObject>(nullptr,
        *(NiagaraSystemPath + TEXT(".") + FPackageName::GetLongPackageAssetName(NiagaraSystemPath))))
    {
        TestEqual(TEXT("Temporary native Niagara System deleted through Unreal API"),
            ObjectTools::DeleteObjectsUnchecked({NiagaraAsset}), 1);
    }
    else
    {
        AddError(TEXT("Temporary native Niagara System could not be loaded for cleanup."));
    }
    TestFalse(TEXT("Temporary Niagara System removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            NiagaraSystemPath, FPackageName::GetAssetPackageExtension())));

    const FString AudioTestSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString SoundAttenuationPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/SA_TE_Native_") + AudioTestSuffix;
    FThomasAudioCreateRequest SoundAttenuationRequest;
    SoundAttenuationRequest.AssetPath = SoundAttenuationPath;
    SoundAttenuationRequest.ExpectedRevision = TEXT("missing");
    SoundAttenuationRequest.AssetKind = TEXT("sound_attenuation");
    const FThomasAudioCreatePlanResult SoundAttenuationPlan =
        UThomasDomainsToolset::PlanAudioAssetCreate(SoundAttenuationRequest);
    TestTrue(TEXT("Native Sound Attenuation create plan succeeds"), SoundAttenuationPlan.bOk);
    const FThomasAudioCreateApplyResult SoundAttenuationApply =
        UThomasDomainsToolset::ApplyAudioAssetCreate(SoundAttenuationPlan.PlanId, true);
    TestTrue(TEXT("Native Sound Attenuation create/save succeeds"), SoundAttenuationApply.bOk);
    TestTrue(TEXT("Native Sound Attenuation was created"), SoundAttenuationApply.bCreated);
    TestTrue(TEXT("Native Sound Attenuation was saved"), SoundAttenuationApply.bSaved);
    const FThomasSpecializedAssetInspectionResult AttenuationTypedInspection =
        UThomasDomainsToolset::InspectAudioAsset(SoundAttenuationPath, 20);
    TestTrue(TEXT("Typed common audio inspection succeeds"), AttenuationTypedInspection.bOk);
    TestTrue(TEXT("Typed common audio inspection delegates configuration to Details"),
        AttenuationTypedInspection.Metrics.Contains(TEXT("ConfigurationSurface=InspectAssetDetails")));

    FThomasObjectDetailsResult AttenuationDetails = UThomasAssetsToolset::GetObjectDetails(
        SoundAttenuationPath,
        {TEXT("Attenuation.bAttenuate"), TEXT("Attenuation.bSpatialize"), TEXT("Attenuation.FalloffDistance")},
        10);
    TestTrue(TEXT("Nested Sound Attenuation Details inspection succeeds"), AttenuationDetails.bOk);
    TestEqual(TEXT("Requested nested attenuation field count is exact"), AttenuationDetails.Properties.Num(), 3);
    const FThomasPropertyRecord* AttenuateProperty = AttenuationDetails.Properties.FindByPredicate(
        [](const FThomasPropertyRecord& Property)
        {
            return Property.Name == TEXT("Attenuation.bAttenuate");
        });
    const FThomasPropertyRecord* FalloffProperty = AttenuationDetails.Properties.FindByPredicate(
        [](const FThomasPropertyRecord& Property)
        {
            return Property.Name == TEXT("Attenuation.FalloffDistance");
        });
    TestNotNull(TEXT("Nested attenuation enable property is present"), AttenuateProperty);
    TestNotNull(TEXT("Inherited nested attenuation falloff property is present"), FalloffProperty);
    if (AttenuateProperty && FalloffProperty)
    {
        FThomasObjectPatchRequest AttenuationPatchRequest;
        AttenuationPatchRequest.ObjectPath = SoundAttenuationPath;
        AttenuationPatchRequest.ExpectedRevision = AttenuationDetails.Revision;
        FThomasObjectPatchOperation ToggleAttenuation;
        ToggleAttenuation.Name = TEXT("Attenuation.bAttenuate");
        ToggleAttenuation.ExpectedValue = AttenuateProperty->Value;
        ToggleAttenuation.Value = AttenuateProperty->Value.Equals(TEXT("True"), ESearchCase::IgnoreCase)
            ? TEXT("False") : TEXT("True");
        AttenuationPatchRequest.Operations.Add(ToggleAttenuation);
        FThomasObjectPatchOperation SetFalloff;
        SetFalloff.Name = TEXT("Attenuation.FalloffDistance");
        SetFalloff.ExpectedValue = FalloffProperty->Value;
        SetFalloff.Value = TEXT("1234.0");
        AttenuationPatchRequest.Operations.Add(SetFalloff);
        const FThomasObjectPatchPlanResult AttenuationPatchPlan =
            UThomasAssetsToolset::PlanObjectPatch(AttenuationPatchRequest);
        TestTrue(TEXT("Nested Sound Attenuation Details patch plan succeeds"), AttenuationPatchPlan.bOk);
        const FThomasObjectPatchApplyResult AttenuationPatchApply =
            UThomasAssetsToolset::ApplyObjectPatch(AttenuationPatchPlan.PlanId, true);
        TestTrue(TEXT("Nested Sound Attenuation Details patch/save succeeds"), AttenuationPatchApply.bOk);
        AttenuationDetails = UThomasAssetsToolset::GetObjectDetails(
            SoundAttenuationPath,
            {TEXT("Attenuation.bAttenuate"), TEXT("Attenuation.FalloffDistance")},
            10);
        TestTrue(TEXT("Nested Sound Attenuation reinspection succeeds"), AttenuationDetails.bOk);
        TestEqual(TEXT("Nested Sound Attenuation reinspection count"), AttenuationDetails.Properties.Num(), 2);
        const FThomasPropertyRecord* AttenuateAfter = AttenuationDetails.Properties.FindByPredicate(
            [](const FThomasPropertyRecord& Property)
            {
                return Property.Name == TEXT("Attenuation.bAttenuate");
            });
        const FThomasPropertyRecord* FalloffAfter = AttenuationDetails.Properties.FindByPredicate(
            [](const FThomasPropertyRecord& Property)
            {
                return Property.Name == TEXT("Attenuation.FalloffDistance");
            });
        TestNotNull(TEXT("Nested attenuation enable property remains inspectable"), AttenuateAfter);
        TestNotNull(TEXT("Nested attenuation falloff property remains inspectable"), FalloffAfter);
        if (AttenuateAfter)
        {
            TestEqual(
                TEXT("Nested attenuation enable value changed exactly"),
                AttenuateAfter->Value.ToLower(),
                ToggleAttenuation.Value.ToLower());
        }
        if (FalloffAfter)
        {
            TestTrue(
                *FString::Printf(
                    TEXT("Nested attenuation falloff value is 1234 (observed %s)"),
                    *FalloffAfter->Value),
                FMath::IsNearlyEqual(FCString::Atof(*FalloffAfter->Value), 1234.0f));
        }
    }

    const FString SoundConcurrencyPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/SC_TE_Native_") + AudioTestSuffix;
    FThomasAudioCreateRequest SoundConcurrencyRequest;
    SoundConcurrencyRequest.AssetPath = SoundConcurrencyPath;
    SoundConcurrencyRequest.ExpectedRevision = TEXT("missing");
    SoundConcurrencyRequest.AssetKind = TEXT("sound_concurrency");
    const FThomasAudioCreatePlanResult SoundConcurrencyPlan =
        UThomasDomainsToolset::PlanAudioAssetCreate(SoundConcurrencyRequest);
    TestTrue(TEXT("Native Sound Concurrency create plan succeeds"), SoundConcurrencyPlan.bOk);
    const FThomasAudioCreateApplyResult SoundConcurrencyApply =
        UThomasDomainsToolset::ApplyAudioAssetCreate(SoundConcurrencyPlan.PlanId, true);
    TestTrue(TEXT("Native Sound Concurrency create/save succeeds"), SoundConcurrencyApply.bOk);
    const FThomasObjectDetailsResult ConcurrencyDetails = UThomasAssetsToolset::GetObjectDetails(
        SoundConcurrencyPath, {TEXT("Concurrency.MaxCount")}, 10);
    TestTrue(TEXT("Nested Sound Concurrency Details inspection succeeds"), ConcurrencyDetails.bOk);
    if (ConcurrencyDetails.bOk && ConcurrencyDetails.Properties.Num() == 1)
    {
        FThomasObjectPatchRequest ConcurrencyPatchRequest;
        ConcurrencyPatchRequest.ObjectPath = SoundConcurrencyPath;
        ConcurrencyPatchRequest.ExpectedRevision = ConcurrencyDetails.Revision;
        FThomasObjectPatchOperation SetMaxCount;
        SetMaxCount.Name = TEXT("Concurrency.MaxCount");
        SetMaxCount.ExpectedValue = ConcurrencyDetails.Properties[0].Value;
        SetMaxCount.Value = TEXT("7");
        ConcurrencyPatchRequest.Operations.Add(SetMaxCount);
        const FThomasObjectPatchPlanResult ConcurrencyPatchPlan =
            UThomasAssetsToolset::PlanObjectPatch(ConcurrencyPatchRequest);
        TestTrue(TEXT("Nested Sound Concurrency Details patch plan succeeds"), ConcurrencyPatchPlan.bOk);
        TestTrue(
            TEXT("Nested Sound Concurrency Details patch/save succeeds"),
            UThomasAssetsToolset::ApplyObjectPatch(ConcurrencyPatchPlan.PlanId, true).bOk);
        const FThomasObjectDetailsResult ConcurrencyAfter = UThomasAssetsToolset::GetObjectDetails(
            SoundConcurrencyPath, {TEXT("Concurrency.MaxCount")}, 10);
        TestTrue(
            TEXT("Nested Sound Concurrency value changed exactly"),
            ConcurrencyAfter.bOk
                && ConcurrencyAfter.Properties.Num() == 1
                && FCString::Atoi(*ConcurrencyAfter.Properties[0].Value) == 7);
    }

    const FString MetaSoundSourcePath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/MS_TE_Native_") + AudioTestSuffix;
    FThomasAudioCreateRequest MetaSoundSourceRequest;
    MetaSoundSourceRequest.AssetPath = MetaSoundSourcePath;
    MetaSoundSourceRequest.ExpectedRevision = TEXT("missing");
    MetaSoundSourceRequest.AssetKind = TEXT("meta_sound_source");
    const FThomasAudioCreatePlanResult MetaSoundSourcePlan =
        UThomasDomainsToolset::PlanAudioAssetCreate(MetaSoundSourceRequest);
    TestTrue(TEXT("Native MetaSound Source create plan succeeds"), MetaSoundSourcePlan.bOk);
    const FThomasAudioCreateApplyResult MetaSoundSourceApply =
        UThomasDomainsToolset::ApplyAudioAssetCreate(MetaSoundSourcePlan.PlanId, true);
    TestTrue(TEXT("Native MetaSound Source create/save succeeds"), MetaSoundSourceApply.bOk);
    const FThomasSpecializedAssetInspectionResult MetaSoundInspection =
        UThomasDomainsToolset::InspectAudioAsset(MetaSoundSourcePath, 100);
    TestTrue(TEXT("Native MetaSound graph inspection succeeds"), MetaSoundInspection.bOk);
    TestEqual(TEXT("Native MetaSound graph inspection reports source kind"),
        MetaSoundInspection.AssetKind, FString(TEXT("meta_sound_source")));
    TestTrue(TEXT("Native MetaSound graph inspection reports graph metrics"),
        MetaSoundInspection.Metrics.ContainsByPredicate([](const FString& Item)
        {
            return Item.StartsWith(TEXT("NodeCount="));
        }));
    const FThomasSpecializedAssetInspectionResult MetaSoundClassSearch =
        UThomasDomainsToolset::SearchMetaSoundNodeClasses(TEXT("Sine"), 20);
    TestTrue(TEXT("Native MetaSound node class discovery succeeds"), MetaSoundClassSearch.bOk);
    TestTrue(TEXT("Native MetaSound node class discovery finds a Sine node"),
        MetaSoundClassSearch.Items.ContainsByPredicate([](const FString& Item)
        {
            return Item.Contains(TEXT("Sine"), ESearchCase::IgnoreCase);
        }));

    FThomasMetaSoundPatchRequest MetaSoundPatchRequest;
    MetaSoundPatchRequest.AssetPath = MetaSoundSourcePath;
    MetaSoundPatchRequest.ExpectedRevision = MetaSoundInspection.Revision;
    FThomasMetaSoundOperation AddGainInput;
    AddGainInput.Action = TEXT("add_graph_input");
    AddGainInput.Name = TEXT("TestGain");
    AddGainInput.DataType = TEXT("Float");
    AddGainInput.LiteralType = TEXT("float");
    AddGainInput.Value = TEXT("0.5");
    AddGainInput.ResultId = TEXT("gain_input");
    MetaSoundPatchRequest.Operations.Add(AddGainInput);
    FThomasMetaSoundOperation AddGraphVariable;
    AddGraphVariable.Action = TEXT("add_graph_variable");
    AddGraphVariable.Name = TEXT("TestPitch");
    AddGraphVariable.DataType = TEXT("Float");
    AddGraphVariable.LiteralType = TEXT("float");
    AddGraphVariable.Value = TEXT("440.0");
    MetaSoundPatchRequest.Operations.Add(AddGraphVariable);
    FThomasMetaSoundOperation AddSineNode;
    AddSineNode.Action = TEXT("add_node_by_class");
    AddSineNode.ClassName = TEXT("UE.Sine.Audio");
    AddSineNode.MajorVersion = 1;
    AddSineNode.ResultId = TEXT("osc");
    MetaSoundPatchRequest.Operations.Add(AddSineNode);
    FThomasMetaSoundOperation SetFrequencyDefault;
    SetFrequencyDefault.Action = TEXT("set_node_input_default");
    SetFrequencyDefault.NodeId = TEXT("osc");
    SetFrequencyDefault.InputName = TEXT("Frequency");
    SetFrequencyDefault.LiteralType = TEXT("float");
    SetFrequencyDefault.Value = TEXT("220.0");
    MetaSoundPatchRequest.Operations.Add(SetFrequencyDefault);
    FThomasMetaSoundOperation SetSineLocation;
    SetSineLocation.Action = TEXT("set_node_location");
    SetSineLocation.NodeId = TEXT("osc");
    SetSineLocation.LocationX = 320.0f;
    SetSineLocation.LocationY = 180.0f;
    MetaSoundPatchRequest.Operations.Add(SetSineLocation);
    const FThomasMetaSoundPlanResult MetaSoundPatchPlan =
        UThomasDomainsToolset::PlanMetaSoundPatch(MetaSoundPatchRequest);
    TestTrue(TEXT("Guarded MetaSound graph patch plan succeeds"), MetaSoundPatchPlan.bOk);
    const FThomasMetaSoundApplyResult MetaSoundPatchApply =
        UThomasDomainsToolset::ApplyMetaSoundPlan(MetaSoundPatchPlan.PlanId, true);
    TestTrue(TEXT("Guarded MetaSound graph patch registers and saves"),
        MetaSoundPatchApply.bOk && MetaSoundPatchApply.bRegistered && MetaSoundPatchApply.bSaved);
    TestTrue(TEXT("MetaSound patch returns frontend node ids"),
        MetaSoundPatchApply.CreatedNodeIds.Num() >= 2);
    const FThomasSpecializedAssetInspectionResult MetaSoundAfterPatch =
        UThomasDomainsToolset::InspectAudioAsset(MetaSoundSourcePath, 300);
    TestTrue(TEXT("MetaSound graph input is inspectable after save"),
        MetaSoundAfterPatch.bOk && MetaSoundAfterPatch.Items.ContainsByPredicate([](const FString& Item)
        {
            return Item.Contains(TEXT("TestGain"));
        }));
    TestTrue(TEXT("MetaSound inspection exposes frontend node ids"),
        MetaSoundAfterPatch.Items.ContainsByPredicate([](const FString& Item)
        {
            return Item.StartsWith(TEXT("Node Id="));
        }));

    if (MetaSoundPatchApply.CreatedNodeIds.Num() >= 2)
    {
        FThomasMetaSoundPatchRequest MetaSoundRemovalRequest;
        MetaSoundRemovalRequest.AssetPath = MetaSoundSourcePath;
        MetaSoundRemovalRequest.ExpectedRevision = MetaSoundAfterPatch.Revision;
        FThomasMetaSoundOperation RemoveSineNode;
        RemoveSineNode.Action = TEXT("remove_node");
        RemoveSineNode.NodeId = MetaSoundPatchApply.CreatedNodeIds.Last();
        MetaSoundRemovalRequest.Operations.Add(RemoveSineNode);
        TestEqual(TEXT("MetaSound node removal requires explicit R2 confirmation"),
            UThomasDomainsToolset::PlanMetaSoundPatch(MetaSoundRemovalRequest).Code,
            FString(TEXT("confirmation_required")));
        MetaSoundRemovalRequest.bConfirmDestructive = true;
        FThomasMetaSoundOperation RemoveGainInput;
        RemoveGainInput.Action = TEXT("remove_graph_input");
        RemoveGainInput.Name = TEXT("TestGain");
        MetaSoundRemovalRequest.Operations.Add(RemoveGainInput);
        FThomasMetaSoundOperation RemovePitchVariable;
        RemovePitchVariable.Action = TEXT("remove_graph_variable");
        RemovePitchVariable.Name = TEXT("TestPitch");
        MetaSoundRemovalRequest.Operations.Add(RemovePitchVariable);
        const FThomasMetaSoundPlanResult MetaSoundRemovalPlan =
            UThomasDomainsToolset::PlanMetaSoundPatch(MetaSoundRemovalRequest);
        TestTrue(TEXT("Confirmed MetaSound graph removal plan succeeds"), MetaSoundRemovalPlan.bOk);
        TestTrue(TEXT("Confirmed MetaSound graph removals apply and save"),
            UThomasDomainsToolset::ApplyMetaSoundPlan(MetaSoundRemovalPlan.PlanId, true).bOk);
        const FThomasSpecializedAssetInspectionResult MetaSoundAfterRemoval =
            UThomasDomainsToolset::InspectAudioAsset(MetaSoundSourcePath, 300);
        TestFalse(TEXT("Removed MetaSound graph input is absent"),
            MetaSoundAfterRemoval.Items.ContainsByPredicate([](const FString& Item)
            {
                return Item.Contains(TEXT("TestGain"));
            }));
    }

    TArray<UObject*> AudioAssetsToDelete;
    for (const FString& AudioPath : {MetaSoundSourcePath, SoundConcurrencyPath, SoundAttenuationPath})
    {
        if (UObject* Asset = LoadObject<UObject>(
            nullptr,
            *(AudioPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AudioPath))))
        {
            AudioAssetsToDelete.Add(Asset);
        }
    }
    TestEqual(TEXT("All temporary native audio assets loaded for cleanup"), AudioAssetsToDelete.Num(), 3);
    TestEqual(
        TEXT("Temporary native audio assets deleted through Unreal API"),
        ObjectTools::DeleteObjectsUnchecked(AudioAssetsToDelete),
        AudioAssetsToDelete.Num());
    TestFalse(
        TEXT("Temporary Sound Attenuation removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            SoundAttenuationPath, FPackageName::GetAssetPackageExtension())));
    TestFalse(
        TEXT("Temporary Sound Concurrency removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            SoundConcurrencyPath, FPackageName::GetAssetPackageExtension())));
    TestFalse(
        TEXT("Temporary MetaSound Source removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            MetaSoundSourcePath, FPackageName::GetAssetPackageExtension())));

    const FThomasDomainInventoryResult CinematicInventory =
        UThomasDomainsToolset::InspectCinematicAssets(TEXT("/Game/PropHunt"), 20);
    TestTrue(TEXT("Cinematics inventory succeeds"), CinematicInventory.bOk);
    const FString LevelSequencePath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/LS_TE_Native_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString AudioTrackClass =
        TEXT("/Script/MovieSceneTracks.MovieSceneAudioTrack");
    FThomasCinematicPatchRequest CinematicRequest;
    CinematicRequest.AssetPath = LevelSequencePath;
    CinematicRequest.ExpectedRevision = TEXT("missing");
    CinematicRequest.bCreateIfMissing = true;
    FThomasCinematicOperation CreateSequence;
    CreateSequence.Action = TEXT("create_level_sequence");
    CinematicRequest.Operations.Add(CreateSequence);
    FThomasCinematicOperation SetDisplayRate;
    SetDisplayRate.Action = TEXT("set_display_rate");
    SetDisplayRate.Numerator = 24;
    SetDisplayRate.Denominator = 1;
    CinematicRequest.Operations.Add(SetDisplayRate);
    FThomasCinematicOperation SetTickResolution;
    SetTickResolution.Action = TEXT("set_tick_resolution");
    SetTickResolution.Numerator = 24000;
    SetTickResolution.Denominator = 1;
    CinematicRequest.Operations.Add(SetTickResolution);
    FThomasCinematicOperation SetPlaybackRange;
    SetPlaybackRange.Action = TEXT("set_playback_range");
    SetPlaybackRange.StartFrame = 0;
    SetPlaybackRange.EndFrame = 24000;
    CinematicRequest.Operations.Add(SetPlaybackRange);
    FThomasCinematicOperation AddAudioTrack;
    AddAudioTrack.Action = TEXT("add_root_track");
    AddAudioTrack.TrackClassPath = AudioTrackClass;
    CinematicRequest.Operations.Add(AddAudioTrack);
    FThomasCinematicOperation AddAudioSection;
    AddAudioSection.Action = TEXT("add_section");
    AddAudioSection.TrackClassPath = AudioTrackClass;
    AddAudioSection.TrackIndex = 0;
    AddAudioSection.StartFrame = 0;
    AddAudioSection.EndFrame = 12000;
    CinematicRequest.Operations.Add(AddAudioSection);
    const FThomasCinematicPlanResult CinematicPlan =
        UThomasDomainsToolset::PlanCinematicPatch(CinematicRequest);
    TestTrue(TEXT("Native Level Sequence structural create plan succeeds"), CinematicPlan.bOk);
    const FThomasCinematicApplyResult CinematicApply =
        UThomasDomainsToolset::ApplyCinematicPlan(CinematicPlan.PlanId, true);
    TestTrue(TEXT("Native Level Sequence create/track/section/save succeeds"), CinematicApply.bOk);
    TestTrue(TEXT("Native Level Sequence was created"), CinematicApply.bCreated);
    TestTrue(TEXT("Native Level Sequence was saved"), CinematicApply.bSaved);
    FThomasCinematicInspectionResult CinematicInspection =
        UThomasDomainsToolset::InspectLevelSequence(LevelSequencePath, 100);
    TestTrue(TEXT("Created native Level Sequence is inspectable"), CinematicInspection.bOk);
    TestEqual(TEXT("Level Sequence display rate numerator"), CinematicInspection.DisplayRateNumerator, 24);
    TestEqual(TEXT("Level Sequence display rate denominator"), CinematicInspection.DisplayRateDenominator, 1);
    TestEqual(TEXT("Level Sequence tick resolution numerator"), CinematicInspection.TickResolutionNumerator, 24000);
    TestEqual(TEXT("Level Sequence playback start"), CinematicInspection.PlaybackStartFrame, 0);
    TestEqual(TEXT("Level Sequence playback end"), CinematicInspection.PlaybackEndFrame, 24000);
    TestTrue(
        TEXT("Level Sequence root Audio track and section are inspectable"),
        CinematicInspection.Tracks.ContainsByPredicate(
            [&AudioTrackClass](const FThomasCinematicTrackRecord& Track)
            {
                return Track.BindingGuid.IsEmpty()
                    && Track.ClassPath == AudioTrackClass
                    && Track.Sections.Num() == 1
                    && Track.Sections[0].StartFrame == 0
                    && Track.Sections[0].EndFrame == 12000;
            }));

    const FString FloatTrackClass =
        TEXT("/Script/MovieSceneTracks.MovieSceneFloatTrack");
    FThomasCinematicPatchRequest BoundTrackRequest;
    BoundTrackRequest.AssetPath = LevelSequencePath;
    BoundTrackRequest.ExpectedRevision = CinematicInspection.Revision;
    FThomasCinematicOperation AddPossessable;
    AddPossessable.Action = TEXT("add_possessable");
    AddPossessable.ResultId = TEXT("actor_binding");
    AddPossessable.BindingName = TEXT("CinematicActor");
    AddPossessable.BindingClassPath = TEXT("/Script/Engine.Actor");
    BoundTrackRequest.Operations.Add(AddPossessable);
    FThomasCinematicOperation AddFloatTrack;
    AddFloatTrack.Action = TEXT("add_property_track");
    AddFloatTrack.BindingGuid = TEXT("actor_binding");
    AddFloatTrack.TrackClassPath = FloatTrackClass;
    AddFloatTrack.PropertyName = TEXT("CustomTimeDilation");
    AddFloatTrack.PropertyPath = TEXT("CustomTimeDilation");
    BoundTrackRequest.Operations.Add(AddFloatTrack);
    FThomasCinematicOperation AddFloatSection;
    AddFloatSection.Action = TEXT("add_section");
    AddFloatSection.BindingGuid = TEXT("actor_binding");
    AddFloatSection.TrackClassPath = FloatTrackClass;
    AddFloatSection.TrackIndex = 0;
    AddFloatSection.StartFrame = 100;
    AddFloatSection.EndFrame = 2000;
    BoundTrackRequest.Operations.Add(AddFloatSection);
    FThomasCinematicOperation AddFloatKey;
    AddFloatKey.Action = TEXT("set_channel_key");
    AddFloatKey.BindingGuid = TEXT("actor_binding");
    AddFloatKey.TrackClassPath = FloatTrackClass;
    AddFloatKey.TrackIndex = 0;
    AddFloatKey.SectionIndex = 0;
    AddFloatKey.ChannelType = TEXT("MovieSceneFloatChannel");
    AddFloatKey.ChannelIndex = 0;
    AddFloatKey.Frame = 240;
    AddFloatKey.Value = TEXT("0.5");
    AddFloatKey.Interpolation = TEXT("linear");
    BoundTrackRequest.Operations.Add(AddFloatKey);
    FThomasCinematicOperation SetFloatSectionState = AddFloatSection;
    SetFloatSectionState.Action = TEXT("set_section_state");
    SetFloatSectionState.SectionIndex = 0;
    SetFloatSectionState.RowIndex = 2;
    SetFloatSectionState.PreRollFrames = 12;
    SetFloatSectionState.PostRollFrames = 24;
    SetFloatSectionState.bEnabled = false;
    SetFloatSectionState.bLocked = true;
    BoundTrackRequest.Operations.Add(SetFloatSectionState);
    const FThomasCinematicPlanResult BoundTrackPlan =
        UThomasDomainsToolset::PlanCinematicPatch(BoundTrackRequest);
    TestTrue(TEXT("Possessable/property track/channel key plan succeeds"), BoundTrackPlan.bOk);
    const FThomasCinematicApplyResult BoundTrackApply =
        UThomasDomainsToolset::ApplyCinematicPlan(BoundTrackPlan.PlanId, true);
    TestTrue(TEXT("Possessable/property track/channel key saves"), BoundTrackApply.bOk);
    CinematicInspection =
        UThomasDomainsToolset::InspectLevelSequence(LevelSequencePath, 100);
    const FThomasCinematicBindingRecord* ActorBinding =
        CinematicInspection.Bindings.FindByPredicate(
            [](const FThomasCinematicBindingRecord& Binding)
            {
                return Binding.Name == TEXT("CinematicActor")
                    && Binding.Kind == TEXT("possessable");
            });
    TestNotNull(TEXT("Possessable binding is inspectable"), ActorBinding);
    const FString ActorBindingGuid = ActorBinding ? ActorBinding->Guid : FString();
    const FThomasCinematicTrackRecord* FloatTrack =
        CinematicInspection.Tracks.FindByPredicate(
            [&ActorBindingGuid, &FloatTrackClass](const FThomasCinematicTrackRecord& Track)
            {
                return Track.BindingGuid == ActorBindingGuid
                    && Track.ClassPath == FloatTrackClass;
            });
    TestNotNull(TEXT("Bound float property track is inspectable"), FloatTrack);
    TestTrue(
        TEXT("Section state and typed channel key persist"),
        FloatTrack && FloatTrack->Sections.Num() == 1
            && FloatTrack->Sections[0].RowIndex == 2
            && FloatTrack->Sections[0].PreRollFrames == 12
            && FloatTrack->Sections[0].PostRollFrames == 24
            && !FloatTrack->Sections[0].bActive
            && FloatTrack->Sections[0].bLocked
            && FloatTrack->Sections[0].Channels.ContainsByPredicate(
                [](const FThomasCinematicChannelRecord& Channel)
                {
                    return Channel.Type == TEXT("MovieSceneFloatChannel")
                        && Channel.Index == 0
                        && Channel.Keys.ContainsByPredicate(
                            [](const FThomasCinematicKeyRecord& Key)
                            {
                                return Key.Frame == 240 && Key.Value == TEXT("0.5");
                            });
                }));

    FThomasCinematicPatchRequest BoundCleanupRequest;
    BoundCleanupRequest.AssetPath = LevelSequencePath;
    BoundCleanupRequest.ExpectedRevision = CinematicInspection.Revision;
    BoundCleanupRequest.bConfirmDestructive = true;
    FThomasCinematicOperation RemoveFloatKey = AddFloatKey;
    RemoveFloatKey.Action = TEXT("remove_channel_key");
    RemoveFloatKey.BindingGuid = ActorBindingGuid;
    BoundCleanupRequest.Operations.Add(RemoveFloatKey);
    FThomasCinematicOperation RemoveFloatSection = AddFloatSection;
    RemoveFloatSection.Action = TEXT("remove_section");
    RemoveFloatSection.BindingGuid = ActorBindingGuid;
    RemoveFloatSection.SectionIndex = 0;
    BoundCleanupRequest.Operations.Add(RemoveFloatSection);
    FThomasCinematicOperation RemoveFloatTrack = AddFloatTrack;
    RemoveFloatTrack.Action = TEXT("remove_binding_track");
    RemoveFloatTrack.BindingGuid = ActorBindingGuid;
    RemoveFloatTrack.TrackIndex = 0;
    BoundCleanupRequest.Operations.Add(RemoveFloatTrack);
    FThomasCinematicOperation RemovePossessable;
    RemovePossessable.Action = TEXT("remove_binding");
    RemovePossessable.BindingGuid = ActorBindingGuid;
    BoundCleanupRequest.Operations.Add(RemovePossessable);
    const FThomasCinematicPlanResult BoundCleanupPlan =
        UThomasDomainsToolset::PlanCinematicPatch(BoundCleanupRequest);
    TestTrue(TEXT("Sequencer binding structural cleanup plan succeeds"), BoundCleanupPlan.bOk);
    TestEqual(TEXT("Sequencer binding structural cleanup is R2"), BoundCleanupPlan.Risk, FString(TEXT("R2")));
    TestTrue(
        TEXT("Sequencer binding structural cleanup saves"),
        UThomasDomainsToolset::ApplyCinematicPlan(BoundCleanupPlan.PlanId, true).bOk);
    CinematicInspection =
        UThomasDomainsToolset::InspectLevelSequence(LevelSequencePath, 100);
    TestFalse(
        TEXT("Possessable and bound property track were removed"),
        CinematicInspection.Bindings.ContainsByPredicate(
            [&ActorBindingGuid](const FThomasCinematicBindingRecord& Binding)
            {
                return Binding.Guid == ActorBindingGuid;
            }));

    const FString CameraCutTrackClass =
        TEXT("/Script/MovieSceneTracks.MovieSceneCameraCutTrack");
    const FString EventTrackClass =
        TEXT("/Script/MovieSceneTracks.MovieSceneEventTrack");
    FThomasCinematicPatchRequest InvalidSpecialTrackRequest;
    InvalidSpecialTrackRequest.AssetPath = LevelSequencePath;
    InvalidSpecialTrackRequest.ExpectedRevision = CinematicInspection.Revision;
    FThomasCinematicOperation InvalidGenericCameraTrack;
    InvalidGenericCameraTrack.Action = TEXT("add_root_track");
    InvalidGenericCameraTrack.TrackClassPath = CameraCutTrackClass;
    InvalidSpecialTrackRequest.Operations.Add(InvalidGenericCameraTrack);
    TestEqual(
        TEXT("Camera Cut tracks require the typed camera-cut operation"),
        UThomasDomainsToolset::PlanCinematicPatch(InvalidSpecialTrackRequest).Code,
        FString(TEXT("invalid_track_class")));

    FThomasCinematicPatchRequest AdvancedCinematicRequest;
    AdvancedCinematicRequest.AssetPath = LevelSequencePath;
    AdvancedCinematicRequest.ExpectedRevision = CinematicInspection.Revision;
    FThomasCinematicOperation AddCineCamera;
    AddCineCamera.Action = TEXT("add_spawnable");
    AddCineCamera.ResultId = TEXT("cine_camera");
    AddCineCamera.BindingName = TEXT("ThomasCineCamera");
    AddCineCamera.BindingClassPath =
        TEXT("/Script/CinematicCamera.CineCameraActor");
    AddCineCamera.LocationX = 125.0;
    AddCineCamera.LocationY = -250.0;
    AddCineCamera.LocationZ = 400.0;
    AddCineCamera.RotationPitch = -10.0;
    AddCineCamera.RotationYaw = 45.0;
    AddCineCamera.RotationRoll = 2.0;
    AddCineCamera.FocalLength = 50.0f;
    AddCineCamera.Aperture = 4.0f;
    AddCineCamera.FocusDistance = 2500.0f;
    AdvancedCinematicRequest.Operations.Add(AddCineCamera);
    FThomasCinematicOperation AddCameraCut;
    AddCameraCut.Action = TEXT("add_camera_cut");
    AddCameraCut.BindingGuid = TEXT("cine_camera");
    AddCameraCut.Frame = 1200;
    AddCameraCut.bLockPreviousCamera = true;
    AdvancedCinematicRequest.Operations.Add(AddCameraCut);
    FThomasCinematicOperation AddSequenceEvent;
    AddSequenceEvent.Action = TEXT("add_event");
    AddSequenceEvent.EventName = TEXT("TE_OnCameraCut");
    AddSequenceEvent.Frame = 1200;
    AdvancedCinematicRequest.Operations.Add(AddSequenceEvent);
    const FThomasCinematicPlanResult AdvancedCinematicPlan =
        UThomasDomainsToolset::PlanCinematicPatch(AdvancedCinematicRequest);
    TestTrue(
        TEXT("Spawnable Cine Camera, Camera Cut, and compiled event plan succeeds"),
        AdvancedCinematicPlan.bOk);
    const FThomasCinematicApplyResult AdvancedCinematicApply =
        UThomasDomainsToolset::ApplyCinematicPlan(
            AdvancedCinematicPlan.PlanId, true);
    TestTrue(
        TEXT("Spawnable Cine Camera, Camera Cut, and compiled event save succeeds"),
        AdvancedCinematicApply.bOk);
    CinematicInspection =
        UThomasDomainsToolset::InspectLevelSequence(LevelSequencePath, 200);
    TestTrue(
        TEXT("Sequence Director Blueprint is present and compiled"),
        !CinematicInspection.DirectorBlueprintPath.IsEmpty()
            && CinematicInspection.DirectorBlueprintStatus == TEXT("up_to_date"));
    const FThomasCinematicBindingRecord* CineCameraBinding =
        CinematicInspection.Bindings.FindByPredicate(
            [](const FThomasCinematicBindingRecord& Binding)
            {
                return Binding.Name == TEXT("ThomasCineCamera")
                    && Binding.Kind == TEXT("spawnable");
            });
    TestNotNull(TEXT("Spawnable Cine Camera binding is inspectable"), CineCameraBinding);
    TestTrue(
        TEXT("Spawnable Cine Camera template transform and lens settings persist"),
        CineCameraBinding
            && CineCameraBinding->ClassPath
                == TEXT("/Script/CinematicCamera.CineCameraActor")
            && !CineCameraBinding->TemplateObjectPath.IsEmpty()
            && CineCameraBinding->bCineCamera
            && FMath::IsNearlyEqual(CineCameraBinding->LocationX, 125.0)
            && FMath::IsNearlyEqual(CineCameraBinding->LocationY, -250.0)
            && FMath::IsNearlyEqual(CineCameraBinding->LocationZ, 400.0)
            && FMath::IsNearlyEqual(CineCameraBinding->RotationPitch, -10.0)
            && FMath::IsNearlyEqual(CineCameraBinding->RotationYaw, 45.0)
            && FMath::IsNearlyEqual(CineCameraBinding->RotationRoll, 2.0)
            && FMath::IsNearlyEqual(CineCameraBinding->CurrentFocalLength, 50.0f)
            && FMath::IsNearlyEqual(CineCameraBinding->CurrentAperture, 4.0f)
            && FMath::IsNearlyEqual(CineCameraBinding->ManualFocusDistance, 2500.0f));
    const FString CineCameraBindingGuid =
        CineCameraBinding ? CineCameraBinding->Guid : FString();
    TestTrue(
        TEXT("Camera Cut references the exact Cine Camera and persists its lock option"),
        CinematicInspection.Tracks.ContainsByPredicate(
            [&CameraCutTrackClass, &CineCameraBindingGuid](
                const FThomasCinematicTrackRecord& Track)
            {
                return Track.ClassPath == CameraCutTrackClass
                    && Track.Sections.ContainsByPredicate(
                        [&CineCameraBindingGuid](
                            const FThomasCinematicSectionRecord& Section)
                        {
                            return Section.StartFrame == 1200
                                && Section.CameraBindingGuid == CineCameraBindingGuid
                                && Section.bLockPreviousCamera;
                        });
            }));
    TestTrue(
        TEXT("Sequencer event key references the named Director Blueprint endpoint"),
        CinematicInspection.Tracks.ContainsByPredicate(
            [&EventTrackClass](const FThomasCinematicTrackRecord& Track)
            {
                return Track.ClassPath == EventTrackClass
                    && Track.Sections.ContainsByPredicate(
                        [](const FThomasCinematicSectionRecord& Section)
                        {
                            return Section.Channels.ContainsByPredicate(
                                [](const FThomasCinematicChannelRecord& Channel)
                                {
                                    return Channel.Type
                                            == TEXT("MovieSceneEventChannel")
                                        && Channel.Keys.ContainsByPredicate(
                                            [](const FThomasCinematicKeyRecord& Key)
                                            {
                                                return Key.Frame == 1200
                                                    && Key.Value
                                                        == TEXT("TE_OnCameraCut");
                                            });
                                });
                        });
            }));

    FThomasCinematicPatchRequest UnconfirmedCameraOverwrite;
    UnconfirmedCameraOverwrite.AssetPath = LevelSequencePath;
    UnconfirmedCameraOverwrite.ExpectedRevision = CinematicInspection.Revision;
    FThomasCinematicOperation OverwriteCameraCut = AddCameraCut;
    OverwriteCameraCut.BindingGuid = CineCameraBindingGuid;
    UnconfirmedCameraOverwrite.Operations.Add(OverwriteCameraCut);
    TestEqual(
        TEXT("Overwriting an existing Camera Cut requires confirmation"),
        UThomasDomainsToolset::PlanCinematicPatch(
            UnconfirmedCameraOverwrite).Code,
        FString(TEXT("confirmation_required")));

    FThomasCinematicPatchRequest AdvancedCinematicCleanup;
    AdvancedCinematicCleanup.AssetPath = LevelSequencePath;
    AdvancedCinematicCleanup.ExpectedRevision = CinematicInspection.Revision;
    AdvancedCinematicCleanup.bConfirmDestructive = true;
    FThomasCinematicOperation RemoveCameraCut;
    RemoveCameraCut.Action = TEXT("remove_camera_cut");
    RemoveCameraCut.Frame = 1200;
    AdvancedCinematicCleanup.Operations.Add(RemoveCameraCut);
    FThomasCinematicOperation RemoveEventTrack;
    RemoveEventTrack.Action = TEXT("remove_root_track");
    RemoveEventTrack.TrackClassPath = EventTrackClass;
    RemoveEventTrack.TrackIndex = 0;
    AdvancedCinematicCleanup.Operations.Add(RemoveEventTrack);
    FThomasCinematicOperation RemoveCineCamera;
    RemoveCineCamera.Action = TEXT("remove_binding");
    RemoveCineCamera.BindingGuid = CineCameraBindingGuid;
    AdvancedCinematicCleanup.Operations.Add(RemoveCineCamera);
    const FThomasCinematicPlanResult AdvancedCinematicCleanupPlan =
        UThomasDomainsToolset::PlanCinematicPatch(AdvancedCinematicCleanup);
    TestTrue(
        TEXT("Camera Cut, event endpoint/track, and spawnable cleanup plan succeeds"),
        AdvancedCinematicCleanupPlan.bOk);
    TestEqual(
        TEXT("Advanced Cinematic cleanup is R2"),
        AdvancedCinematicCleanupPlan.Risk,
        FString(TEXT("R2")));
    TestTrue(
        TEXT("Camera Cut, event endpoint/track, and spawnable cleanup saves"),
        UThomasDomainsToolset::ApplyCinematicPlan(
            AdvancedCinematicCleanupPlan.PlanId, true).bOk);
    CinematicInspection =
        UThomasDomainsToolset::InspectLevelSequence(LevelSequencePath, 200);
    TestFalse(
        TEXT("Advanced Cinematic binding and specialized tracks were removed"),
        CinematicInspection.Bindings.ContainsByPredicate(
            [&CineCameraBindingGuid](const FThomasCinematicBindingRecord& Binding)
            {
                return Binding.Guid == CineCameraBindingGuid;
            })
            || CinematicInspection.Tracks.ContainsByPredicate(
                [&CameraCutTrackClass, &EventTrackClass](
                    const FThomasCinematicTrackRecord& Track)
                {
                    return Track.ClassPath == CameraCutTrackClass
                        || Track.ClassPath == EventTrackClass;
                }));

    FThomasCinematicPatchRequest UnconfirmedTrackRemoval;
    UnconfirmedTrackRemoval.AssetPath = LevelSequencePath;
    UnconfirmedTrackRemoval.ExpectedRevision = CinematicInspection.Revision;
    FThomasCinematicOperation RemoveAudioTrack;
    RemoveAudioTrack.Action = TEXT("remove_root_track");
    RemoveAudioTrack.TrackClassPath = AudioTrackClass;
    RemoveAudioTrack.TrackIndex = 0;
    UnconfirmedTrackRemoval.Operations.Add(RemoveAudioTrack);
    TestEqual(
        TEXT("Cinematic track removal requires explicit confirmation"),
        UThomasDomainsToolset::PlanCinematicPatch(UnconfirmedTrackRemoval).Code,
        FString(TEXT("confirmation_required")));
    FThomasCinematicPatchRequest ConfirmedTrackRemoval = UnconfirmedTrackRemoval;
    ConfirmedTrackRemoval.bConfirmDestructive = true;
    const FThomasCinematicPlanResult ConfirmedTrackRemovalPlan =
        UThomasDomainsToolset::PlanCinematicPatch(ConfirmedTrackRemoval);
    TestTrue(TEXT("Confirmed Cinematic track removal plan succeeds"), ConfirmedTrackRemovalPlan.bOk);
    TestEqual(TEXT("Confirmed Cinematic track removal is R2"), ConfirmedTrackRemovalPlan.Risk, FString(TEXT("R2")));
    TestTrue(
        TEXT("Confirmed Cinematic track removal/save succeeds"),
        UThomasDomainsToolset::ApplyCinematicPlan(ConfirmedTrackRemovalPlan.PlanId, true).bOk);
    CinematicInspection = UThomasDomainsToolset::InspectLevelSequence(LevelSequencePath, 100);
    TestFalse(
        TEXT("Native Level Sequence root Audio track was removed"),
        CinematicInspection.Tracks.ContainsByPredicate(
            [&AudioTrackClass](const FThomasCinematicTrackRecord& Track)
            {
                return Track.BindingGuid.IsEmpty() && Track.ClassPath == AudioTrackClass;
            }));

    UObject* LevelSequenceAsset = LoadObject<UObject>(
        nullptr,
        *(LevelSequencePath + TEXT(".") + FPackageName::GetLongPackageAssetName(LevelSequencePath)));
    TestNotNull(TEXT("Temporary native Level Sequence loaded for cleanup"), LevelSequenceAsset);
    if (LevelSequenceAsset)
    {
        TestEqual(
            TEXT("Temporary native Level Sequence deleted through Unreal API"),
            ObjectTools::DeleteObjectsUnchecked({LevelSequenceAsset}),
            1);
    }
    TestFalse(
        TEXT("Temporary native Level Sequence removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            LevelSequencePath, FPackageName::GetAssetPackageExtension())));

    const FThomasDomainInventoryResult Paper2DInventory =
        UThomasDomainsToolset::InspectPaper2DAssets(TEXT("/Game/PropHunt"), 20);
    TestTrue(TEXT("Paper2D inventory succeeds"), Paper2DInventory.bOk);
    const FString Paper2DSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString SpritePath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/SPR_TE_Native_") + Paper2DSuffix;
    FThomasPaper2DPatchRequest SpriteRequest;
    SpriteRequest.AssetPath = SpritePath;
    SpriteRequest.ExpectedRevision = TEXT("missing");
    SpriteRequest.AssetKind = TEXT("sprite");
    SpriteRequest.bCreateIfMissing = true;
    FThomasPaper2DOperation CreateSprite;
    CreateSprite.Action = TEXT("create_paper_asset");
    SpriteRequest.Operations.Add(CreateSprite);
    const FThomasPaper2DPlanResult SpritePlan =
        UThomasDomainsToolset::PlanPaper2DPatch(SpriteRequest);
    TestTrue(TEXT("Native blank Paper Sprite create plan succeeds"), SpritePlan.bOk);
    const FThomasPaper2DApplyResult SpriteApply =
        UThomasDomainsToolset::ApplyPaper2DPlan(SpritePlan.PlanId, true);
    TestTrue(TEXT("Native blank Paper Sprite create/save succeeds"), SpriteApply.bOk);
    TestTrue(TEXT("Native Paper Sprite was created"), SpriteApply.bCreated);
    TestTrue(TEXT("Native Paper Sprite was saved"), SpriteApply.bSaved);
    const FThomasPaper2DInspectionResult SpriteInspection =
        UThomasDomainsToolset::InspectPaper2DAsset(SpritePath, 20);
    TestTrue(TEXT("Created native Paper Sprite is inspectable"), SpriteInspection.bOk);
    TestEqual(TEXT("Created Paper2D asset kind is sprite"), SpriteInspection.AssetKind, FString(TEXT("sprite")));

    const FString FlipbookPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/FB_TE_Native_") + Paper2DSuffix;
    FThomasPaper2DPatchRequest FlipbookRequest;
    FlipbookRequest.AssetPath = FlipbookPath;
    FlipbookRequest.ExpectedRevision = TEXT("missing");
    FlipbookRequest.AssetKind = TEXT("flipbook");
    FlipbookRequest.bCreateIfMissing = true;
    FThomasPaper2DOperation CreateFlipbook;
    CreateFlipbook.Action = TEXT("create_paper_asset");
    FlipbookRequest.Operations.Add(CreateFlipbook);
    FThomasPaper2DOperation SetFlipbookFps;
    SetFlipbookFps.Action = TEXT("set_flipbook_fps");
    SetFlipbookFps.FramesPerSecond = 12.0f;
    FlipbookRequest.Operations.Add(SetFlipbookFps);
    FThomasPaper2DOperation AddFlipbookFrame;
    AddFlipbookFrame.Action = TEXT("add_flipbook_frame");
    AddFlipbookFrame.ObjectPath = SpritePath;
    AddFlipbookFrame.FrameRun = 3;
    FlipbookRequest.Operations.Add(AddFlipbookFrame);
    const FThomasPaper2DPlanResult FlipbookPlan =
        UThomasDomainsToolset::PlanPaper2DPatch(FlipbookRequest);
    TestTrue(TEXT("Native Paper Flipbook create/frame plan succeeds"), FlipbookPlan.bOk);
    const FThomasPaper2DApplyResult FlipbookApply =
        UThomasDomainsToolset::ApplyPaper2DPlan(FlipbookPlan.PlanId, true);
    TestTrue(TEXT("Native Paper Flipbook create/frame/save succeeds"), FlipbookApply.bOk);
    FThomasPaper2DInspectionResult FlipbookInspection =
        UThomasDomainsToolset::InspectPaper2DAsset(FlipbookPath, 20);
    TestTrue(TEXT("Created native Paper Flipbook is inspectable"), FlipbookInspection.bOk);
    TestEqual(TEXT("Created Paper Flipbook FPS"), FlipbookInspection.FramesPerSecond, 12.0f);
    TestEqual(TEXT("Created Paper Flipbook total frames"), FlipbookInspection.TotalFrames, 3);
    TestTrue(
        TEXT("Created Paper Flipbook frame references the exact Sprite"),
        FlipbookInspection.Frames.Num() == 1
            && FlipbookInspection.Frames[0].SpritePath.Contains(SpritePath)
            && FlipbookInspection.Frames[0].FrameRun == 3);

    FThomasPaper2DPatchRequest UnconfirmedFrameRemoval;
    UnconfirmedFrameRemoval.AssetPath = FlipbookPath;
    UnconfirmedFrameRemoval.ExpectedRevision = FlipbookInspection.Revision;
    UnconfirmedFrameRemoval.AssetKind = TEXT("flipbook");
    FThomasPaper2DOperation RemoveFlipbookFrame;
    RemoveFlipbookFrame.Action = TEXT("remove_flipbook_frame");
    RemoveFlipbookFrame.FrameIndex = 0;
    UnconfirmedFrameRemoval.Operations.Add(RemoveFlipbookFrame);
    TestEqual(
        TEXT("Paper Flipbook frame removal requires explicit confirmation"),
        UThomasDomainsToolset::PlanPaper2DPatch(UnconfirmedFrameRemoval).Code,
        FString(TEXT("confirmation_required")));
    FThomasPaper2DPatchRequest ConfirmedFrameRemoval = UnconfirmedFrameRemoval;
    ConfirmedFrameRemoval.bConfirmDestructive = true;
    const FThomasPaper2DPlanResult ConfirmedFrameRemovalPlan =
        UThomasDomainsToolset::PlanPaper2DPatch(ConfirmedFrameRemoval);
    TestTrue(TEXT("Confirmed Paper Flipbook frame removal plan succeeds"), ConfirmedFrameRemovalPlan.bOk);
    TestEqual(TEXT("Confirmed Paper Flipbook frame removal is R2"), ConfirmedFrameRemovalPlan.Risk, FString(TEXT("R2")));
    TestTrue(
        TEXT("Confirmed Paper Flipbook frame removal/save succeeds"),
        UThomasDomainsToolset::ApplyPaper2DPlan(ConfirmedFrameRemovalPlan.PlanId, true).bOk);
    TestEqual(
        TEXT("Native Paper Flipbook frame was removed"),
        UThomasDomainsToolset::InspectPaper2DAsset(FlipbookPath, 20).Frames.Num(),
        0);

    TArray<UObject*> Paper2DAssetsToDelete;
    for (const FString& PaperPath : {FlipbookPath, SpritePath})
    {
        if (UObject* Asset = LoadObject<UObject>(
            nullptr,
            *(PaperPath + TEXT(".") + FPackageName::GetLongPackageAssetName(PaperPath))))
        {
            Paper2DAssetsToDelete.Add(Asset);
        }
    }
    TestEqual(TEXT("Both temporary Paper2D assets loaded for cleanup"), Paper2DAssetsToDelete.Num(), 2);
    TestEqual(
        TEXT("Temporary native Paper2D assets deleted through Unreal API"),
        ObjectTools::DeleteObjectsUnchecked(Paper2DAssetsToDelete),
        Paper2DAssetsToDelete.Num());
    TestFalse(
        TEXT("Temporary Paper Sprite removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            SpritePath, FPackageName::GetAssetPackageExtension())));
    TestFalse(
        TEXT("Temporary Paper Flipbook removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            FlipbookPath, FPackageName::GetAssetPackageExtension())));

    const FString DataAISuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString DataAssetPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/DA_TE_Native_") + DataAISuffix;
    FThomasDataAIPatchRequest DataAssetRequest;
    DataAssetRequest.AssetPath = DataAssetPath;
    DataAssetRequest.ExpectedRevision = TEXT("missing");
    DataAssetRequest.AssetKind = TEXT("data_asset");
    DataAssetRequest.bCreateIfMissing = true;
    FThomasDataAIOperation CreateDataAsset;
    CreateDataAsset.Action = TEXT("create_data_asset");
    CreateDataAsset.ClassPath = TEXT("/Script/PropHunt.PHMatchRulesDataAsset");
    DataAssetRequest.Operations.Add(CreateDataAsset);
    const FThomasDataAIPlanResult DataAssetPlan =
        UThomasDomainsToolset::PlanDataAIPatch(DataAssetRequest);
    TestTrue(TEXT("Native project Data Asset create plan succeeds"), DataAssetPlan.bOk);
    const FThomasDataAIApplyResult DataAssetApply =
        UThomasDomainsToolset::ApplyDataAIPlan(DataAssetPlan.PlanId, true);
    TestTrue(TEXT("Native project Data Asset create/save succeeds"), DataAssetApply.bOk);
    TestTrue(TEXT("Native project Data Asset was created"), DataAssetApply.bCreated);
    TestTrue(TEXT("Native project Data Asset was saved"), DataAssetApply.bSaved);
    const FThomasDataAssetInspectionResult DataAssetInspection =
        UThomasDomainsToolset::InspectDataAsset(DataAssetPath, 10);
    TestTrue(TEXT("Created project Data Asset is inspectable"), DataAssetInspection.bOk);
    TestEqual(
        TEXT("Created project Data Asset kind is exact"),
        DataAssetInspection.AssetKind,
        FString(TEXT("data_asset")));
    TestEqual(
        TEXT("Created project Data Asset class is exact"),
        DataAssetInspection.ClassPath,
        FString(TEXT("/Script/PropHunt.PHMatchRulesDataAsset")));

    const FString DataTablePath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/DT_TE_Native_") + DataAISuffix;
    FThomasDataAIPatchRequest DataTableRequest;
    DataTableRequest.AssetPath = DataTablePath;
    DataTableRequest.ExpectedRevision = TEXT("missing");
    DataTableRequest.AssetKind = TEXT("data_table");
    DataTableRequest.bCreateIfMissing = true;
    FThomasDataAIOperation CreateDataTable;
    CreateDataTable.Action = TEXT("create_data_table");
    CreateDataTable.RowStructPath = TEXT("/Script/GameplayTags.GameplayTagTableRow");
    DataTableRequest.Operations.Add(CreateDataTable);
    FThomasDataAIOperation SetDataTableRow;
    SetDataTableRow.Action = TEXT("set_table_row");
    SetDataTableRow.Name = TEXT("ThomasRow");
    SetDataTableRow.Value =
        TEXT("(Tag=\"ThomasEditor.Automation\",DevComment=\"Native Data Table proof\")");
    DataTableRequest.Operations.Add(SetDataTableRow);
    const FThomasDataAIPlanResult DataTablePlan =
        UThomasDomainsToolset::PlanDataAIPatch(DataTableRequest);
    TestTrue(TEXT("Native Data Table create/row plan succeeds"), DataTablePlan.bOk);
    const FThomasDataAIApplyResult DataTableApply =
        UThomasDomainsToolset::ApplyDataAIPlan(DataTablePlan.PlanId, true);
    TestTrue(TEXT("Native Data Table create/row/save succeeds"), DataTableApply.bOk);
    const FThomasDataAssetInspectionResult DataTableInspection =
        UThomasDomainsToolset::InspectDataAsset(DataTablePath, 10);
    TestTrue(TEXT("Created native Data Table is inspectable"), DataTableInspection.bOk);
    TestEqual(TEXT("Created native Data Table row count"), DataTableInspection.RowCount, 1);
    TestTrue(
        TEXT("Created native Data Table row exposes exact native fields"),
        DataTableInspection.Rows.Num() == 1
            && DataTableInspection.Rows[0].Name == TEXT("ThomasRow")
            && DataTableInspection.Rows[0].Value.Contains(TEXT("ThomasEditor.Automation"))
            && DataTableInspection.Rows[0].Value.Contains(TEXT("Native Data Table proof")));

    FThomasDataAIPatchRequest UnconfirmedRowRemoval;
    UnconfirmedRowRemoval.AssetPath = DataTablePath;
    UnconfirmedRowRemoval.ExpectedRevision = DataTableInspection.Revision;
    UnconfirmedRowRemoval.AssetKind = TEXT("data_table");
    FThomasDataAIOperation RemoveDataTableRow;
    RemoveDataTableRow.Action = TEXT("remove_table_row");
    RemoveDataTableRow.Name = TEXT("ThomasRow");
    UnconfirmedRowRemoval.Operations.Add(RemoveDataTableRow);
    const FThomasDataAIPlanResult UnconfirmedRowRemovalPlan =
        UThomasDomainsToolset::PlanDataAIPatch(UnconfirmedRowRemoval);
    TestFalse(TEXT("Data Table row removal requires R2 confirmation"), UnconfirmedRowRemovalPlan.bOk);
    TestEqual(
        TEXT("Data Table row removal reports confirmation gate"),
        UnconfirmedRowRemovalPlan.Code,
        FString(TEXT("confirmation_required")));
    FThomasDataAIPatchRequest ConfirmedRowRemoval = UnconfirmedRowRemoval;
    ConfirmedRowRemoval.bConfirmDestructive = true;
    const FThomasDataAIPlanResult ConfirmedRowRemovalPlan =
        UThomasDomainsToolset::PlanDataAIPatch(ConfirmedRowRemoval);
    TestTrue(TEXT("Confirmed Data Table row removal plan succeeds"), ConfirmedRowRemovalPlan.bOk);
    TestEqual(TEXT("Confirmed Data Table row removal is R2"), ConfirmedRowRemovalPlan.Risk, FString(TEXT("R2")));
    TestTrue(
        TEXT("Confirmed Data Table row removal/save succeeds"),
        UThomasDomainsToolset::ApplyDataAIPlan(ConfirmedRowRemovalPlan.PlanId, true).bOk);
    TestEqual(
        TEXT("Native Data Table row was removed"),
        UThomasDomainsToolset::InspectDataAsset(DataTablePath, 10).RowCount,
        0);

    const FString BlackboardPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/BB_TE_Native_") + DataAISuffix;
    FThomasDataAIPatchRequest BlackboardRequest;
    BlackboardRequest.AssetPath = BlackboardPath;
    BlackboardRequest.ExpectedRevision = TEXT("missing");
    BlackboardRequest.AssetKind = TEXT("blackboard");
    BlackboardRequest.bCreateIfMissing = true;
    FThomasDataAIOperation CreateBlackboard;
    CreateBlackboard.Action = TEXT("create_ai_asset");
    BlackboardRequest.Operations.Add(CreateBlackboard);
    FThomasDataAIOperation AddBlackboardKey;
    AddBlackboardKey.Action = TEXT("add_blackboard_key");
    AddBlackboardKey.Name = TEXT("CanSeeTarget");
    AddBlackboardKey.KeyTypeClassPath = TEXT("/Script/AIModule.BlackboardKeyType_Bool");
    AddBlackboardKey.Description = TEXT("ThomasEditor native Blackboard proof");
    AddBlackboardKey.Category = TEXT("Automation");
    AddBlackboardKey.bInstanceSynced = true;
    BlackboardRequest.Operations.Add(AddBlackboardKey);
    const FThomasDataAIPlanResult BlackboardPlan =
        UThomasDomainsToolset::PlanDataAIPatch(BlackboardRequest);
    TestTrue(TEXT("Native Blackboard create/key plan succeeds"), BlackboardPlan.bOk);
    const FThomasDataAIApplyResult BlackboardApply =
        UThomasDomainsToolset::ApplyDataAIPlan(BlackboardPlan.PlanId, true);
    TestTrue(TEXT("Native Blackboard create/key/save succeeds"), BlackboardApply.bOk);
    const FThomasAIAssetInspectionResult BlackboardInspection =
        UThomasDomainsToolset::InspectAIAsset(BlackboardPath, 20);
    TestTrue(TEXT("Created native Blackboard is inspectable"), BlackboardInspection.bOk);
    TestTrue(
        TEXT("Created native Blackboard key exposes typed metadata"),
        BlackboardInspection.BlackboardKeys.ContainsByPredicate(
            [](const FThomasBlackboardKeyRecord& Key)
            {
                return Key.Name == TEXT("CanSeeTarget")
                    && Key.TypeClassPath == TEXT("/Script/AIModule.BlackboardKeyType_Bool")
                    && Key.Category == TEXT("Automation")
                    && Key.bInstanceSynced
                    && !Key.bInherited;
            }));

    const FString BehaviorTreePath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/BT_TE_Native_") + DataAISuffix;
    FThomasDataAIPatchRequest BehaviorTreeRequest;
    BehaviorTreeRequest.AssetPath = BehaviorTreePath;
    BehaviorTreeRequest.ExpectedRevision = TEXT("missing");
    BehaviorTreeRequest.AssetKind = TEXT("behavior_tree");
    BehaviorTreeRequest.bCreateIfMissing = true;
    FThomasDataAIOperation CreateBehaviorTree;
    CreateBehaviorTree.Action = TEXT("create_ai_asset");
    BehaviorTreeRequest.Operations.Add(CreateBehaviorTree);
    FThomasDataAIOperation SetBehaviorTreeBlackboard;
    SetBehaviorTreeBlackboard.Action = TEXT("set_behavior_tree_blackboard");
    SetBehaviorTreeBlackboard.ObjectPath = BlackboardPath;
    BehaviorTreeRequest.Operations.Add(SetBehaviorTreeBlackboard);
    const FThomasDataAIPlanResult BehaviorTreePlan =
        UThomasDomainsToolset::PlanDataAIPatch(BehaviorTreeRequest);
    TestTrue(TEXT("Native Behavior Tree create/Blackboard plan succeeds"), BehaviorTreePlan.bOk);
    const FThomasDataAIApplyResult BehaviorTreeApply =
        UThomasDomainsToolset::ApplyDataAIPlan(BehaviorTreePlan.PlanId, true);
    TestTrue(TEXT("Native Behavior Tree create/Blackboard/save succeeds"), BehaviorTreeApply.bOk);
    const FThomasAIAssetInspectionResult BehaviorTreeInspection =
        UThomasDomainsToolset::InspectAIAsset(BehaviorTreePath, 20);
    TestTrue(TEXT("Created native Behavior Tree is inspectable"), BehaviorTreeInspection.bOk);
    TestEqual(
        TEXT("Created Behavior Tree references the exact Blackboard"),
        BehaviorTreeInspection.BlackboardPath,
        BlackboardPath);
    const FThomasAINodeRecord* BehaviorRoot = BehaviorTreeInspection.Nodes.FindByPredicate(
        [](const FThomasAINodeRecord& Node) { return Node.Role == TEXT("root"); });
    TestNotNull(TEXT("Behavior Tree factory root exposes a stable graph ID"), BehaviorRoot);
    FThomasDataAIPatchRequest BehaviorStructure;
    BehaviorStructure.AssetPath = BehaviorTreePath;
    BehaviorStructure.ExpectedRevision = BehaviorTreeInspection.Revision;
    BehaviorStructure.AssetKind = TEXT("behavior_tree");
    FThomasDataAIOperation AddSelector;
    AddSelector.Action = TEXT("add_behavior_tree_node");
    AddSelector.NodeRole = TEXT("composite");
    AddSelector.ClassPath = TEXT("/Script/AIModule.BTComposite_Selector");
    AddSelector.PositionX = 0;
    AddSelector.PositionY = 200;
    AddSelector.ResultId = TEXT("selector");
    BehaviorStructure.Operations.Add(AddSelector);
    FThomasDataAIOperation AddWaitTask;
    AddWaitTask.Action = TEXT("add_behavior_tree_node");
    AddWaitTask.NodeRole = TEXT("task");
    AddWaitTask.ClassPath = TEXT("/Script/AIModule.BTTask_Wait");
    AddWaitTask.PositionX = 0;
    AddWaitTask.PositionY = 400;
    AddWaitTask.ResultId = TEXT("wait");
    BehaviorStructure.Operations.Add(AddWaitTask);
    FThomasDataAIOperation SetWaitTime;
    SetWaitTime.Action = TEXT("set_ai_graph_node_property");
    SetWaitTime.ItemId = TEXT("wait");
    SetWaitTime.PropertyScope = TEXT("instance");
    SetWaitTime.KeyTypePropertyName = TEXT("WaitTime");
    SetWaitTime.KeyTypePropertyValue = TEXT("0.25");
    BehaviorStructure.Operations.Add(SetWaitTime);
    FThomasDataAIOperation AddForceSuccess;
    AddForceSuccess.Action = TEXT("add_behavior_tree_aux_node");
    AddForceSuccess.NodeRole = TEXT("decorator");
    AddForceSuccess.ParentId = TEXT("wait");
    AddForceSuccess.ClassPath = TEXT("/Script/AIModule.BTDecorator_ForceSuccess");
    AddForceSuccess.ResultId = TEXT("force_success");
    BehaviorStructure.Operations.Add(AddForceSuccess);
    FThomasDataAIOperation AddDefaultFocus;
    AddDefaultFocus.Action = TEXT("add_behavior_tree_aux_node");
    AddDefaultFocus.NodeRole = TEXT("service");
    AddDefaultFocus.ParentId = TEXT("selector");
    AddDefaultFocus.ClassPath = TEXT("/Script/AIModule.BTService_DefaultFocus");
    AddDefaultFocus.ResultId = TEXT("default_focus");
    BehaviorStructure.Operations.Add(AddDefaultFocus);
    if (BehaviorRoot)
    {
        FThomasDataAIOperation ConnectRoot;
        ConnectRoot.Action = TEXT("connect_ai_graph_nodes");
        ConnectRoot.SourceId = BehaviorRoot->Id;
        ConnectRoot.TargetId = TEXT("selector");
        BehaviorStructure.Operations.Add(ConnectRoot);
    }
    FThomasDataAIOperation ConnectSelector;
    ConnectSelector.Action = TEXT("connect_ai_graph_nodes");
    ConnectSelector.SourceId = TEXT("selector");
    ConnectSelector.TargetId = TEXT("wait");
    BehaviorStructure.Operations.Add(ConnectSelector);
    const FThomasDataAIPlanResult BehaviorStructurePlan =
        UThomasDomainsToolset::PlanDataAIPatch(BehaviorStructure);
    TestTrue(TEXT("Behavior Tree structural graph plan succeeds"), BehaviorStructurePlan.bOk);
    const FThomasDataAIApplyResult BehaviorStructureApply =
        UThomasDomainsToolset::ApplyDataAIPlan(BehaviorStructurePlan.PlanId, true);
    TestTrue(TEXT("Behavior Tree graph builds runtime tree and saves"), BehaviorStructureApply.bOk);
    const FThomasAIAssetInspectionResult AuthoredBehaviorTree =
        UThomasDomainsToolset::InspectAIAsset(BehaviorTreePath, 100);
    TestEqual(TEXT("Behavior Tree graph node and auxiliary count"), AuthoredBehaviorTree.GraphNodeCount, 5);
    TestEqual(TEXT("Behavior Tree graph edge count"), AuthoredBehaviorTree.Edges.Num(), 2);
    TestEqual(
        TEXT("Behavior Tree runtime root is the authored selector"),
        AuthoredBehaviorTree.RootNodeClassPath,
        FString(TEXT("/Script/AIModule.BTComposite_Selector")));
    TestTrue(
        TEXT("Behavior Tree exposes task, decorator, and service roles"),
        AuthoredBehaviorTree.Nodes.ContainsByPredicate(
            [](const FThomasAINodeRecord& Node) { return Node.Role == TEXT("task"); })
        && AuthoredBehaviorTree.Nodes.ContainsByPredicate(
            [](const FThomasAINodeRecord& Node) { return Node.Role == TEXT("decorator"); })
        && AuthoredBehaviorTree.Nodes.ContainsByPredicate(
            [](const FThomasAINodeRecord& Node) { return Node.Role == TEXT("service"); }));

    FThomasDataAIPatchRequest BehaviorRemoval;
    BehaviorRemoval.AssetPath = BehaviorTreePath;
    BehaviorRemoval.ExpectedRevision = AuthoredBehaviorTree.Revision;
    BehaviorRemoval.AssetKind = TEXT("behavior_tree");
    BehaviorRemoval.bConfirmDestructive = true;
    for (const FString& Role : {TEXT("decorator"), TEXT("service"), TEXT("task"), TEXT("composite")})
    {
        if (const FThomasAINodeRecord* Node = AuthoredBehaviorTree.Nodes.FindByPredicate(
                [&Role](const FThomasAINodeRecord& Candidate)
                {
                    return Candidate.Role == Role;
                }))
        {
            FThomasDataAIOperation RemoveNode;
            RemoveNode.Action = TEXT("remove_ai_graph_node");
            RemoveNode.ItemId = Node->Id;
            BehaviorRemoval.Operations.Add(RemoveNode);
        }
    }
    const FThomasDataAIPlanResult BehaviorRemovalPlan =
        UThomasDomainsToolset::PlanDataAIPatch(BehaviorRemoval);
    TestTrue(TEXT("Behavior Tree confirmed node removal plan succeeds"), BehaviorRemovalPlan.bOk);
    TestEqual(TEXT("Behavior Tree graph node removals are R2"), BehaviorRemovalPlan.Risk, FString(TEXT("R2")));
    TestTrue(
        TEXT("Behavior Tree node removals rebuild and save"),
        UThomasDomainsToolset::ApplyDataAIPlan(BehaviorRemovalPlan.PlanId, true).bOk);
    TestEqual(
        TEXT("Behavior Tree retains only its protected factory root"),
        UThomasDomainsToolset::InspectAIAsset(BehaviorTreePath, 20).GraphNodeCount,
        1);

    const FString EQSPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/EQS_TE_Native_") + DataAISuffix;
    FThomasDataAIPatchRequest EQSRequest;
    EQSRequest.AssetPath = EQSPath;
    EQSRequest.ExpectedRevision = TEXT("missing");
    EQSRequest.AssetKind = TEXT("eqs");
    EQSRequest.bCreateIfMissing = true;
    FThomasDataAIOperation CreateEQS;
    CreateEQS.Action = TEXT("create_ai_asset");
    EQSRequest.Operations.Add(CreateEQS);
    FThomasDataAIOperation AddGrid;
    AddGrid.Action = TEXT("add_eqs_option");
    AddGrid.ClassPath = TEXT("/Script/AIModule.EnvQueryGenerator_SimpleGrid");
    AddGrid.PositionX = 0;
    AddGrid.PositionY = 200;
    AddGrid.ResultId = TEXT("grid");
    EQSRequest.Operations.Add(AddGrid);
    FThomasDataAIOperation AddDistanceTest;
    AddDistanceTest.Action = TEXT("add_eqs_test");
    AddDistanceTest.ParentId = TEXT("grid");
    AddDistanceTest.ClassPath = TEXT("/Script/AIModule.EnvQueryTest_Distance");
    AddDistanceTest.ResultId = TEXT("distance");
    AddDistanceTest.bEnabled = true;
    EQSRequest.Operations.Add(AddDistanceTest);
    const FThomasDataAIPlanResult EQSPlan =
        UThomasDomainsToolset::PlanDataAIPatch(EQSRequest);
    TestTrue(TEXT("Native EQS generator/test plan succeeds"), EQSPlan.bOk);
    const FThomasDataAIApplyResult EQSApply =
        UThomasDomainsToolset::ApplyDataAIPlan(EQSPlan.PlanId, true);
    TestTrue(TEXT("Native EQS generator/test graph saves"), EQSApply.bOk);
    const FThomasAIAssetInspectionResult EQSInspection =
        UThomasDomainsToolset::InspectAIAsset(EQSPath, 100);
    TestTrue(TEXT("Created EQS graph is inspectable"), EQSInspection.bOk);
    TestEqual(TEXT("EQS runtime option count"), EQSInspection.OptionCount, 1);
    TestEqual(TEXT("EQS root/generator/test graph node count"), EQSInspection.GraphNodeCount, 3);
    TestEqual(TEXT("EQS root-to-option edge count"), EQSInspection.Edges.Num(), 1);
    const FThomasAINodeRecord* EQSTestNode = EQSInspection.Nodes.FindByPredicate(
        [](const FThomasAINodeRecord& Node) { return Node.Role == TEXT("test"); });
    TestNotNull(TEXT("EQS test exposes a stable graph ID"), EQSTestNode);
    TestTrue(TEXT("EQS test is enabled by default"), EQSTestNode && EQSTestNode->bEnabled);

    FThomasDataAIPatchRequest DisableEQSTest;
    DisableEQSTest.AssetPath = EQSPath;
    DisableEQSTest.ExpectedRevision = EQSInspection.Revision;
    DisableEQSTest.AssetKind = TEXT("eqs");
    if (EQSTestNode)
    {
        FThomasDataAIOperation DisableTest;
        DisableTest.Action = TEXT("set_eqs_test_enabled");
        DisableTest.ItemId = EQSTestNode->Id;
        DisableTest.bEnabled = false;
        DisableEQSTest.Operations.Add(DisableTest);
    }
    const FThomasDataAIPlanResult DisableEQSTestPlan =
        UThomasDomainsToolset::PlanDataAIPatch(DisableEQSTest);
    TestTrue(TEXT("EQS test disable plan succeeds"), DisableEQSTestPlan.bOk);
    TestTrue(
        TEXT("EQS test disable rebuilds runtime query and saves"),
        UThomasDomainsToolset::ApplyDataAIPlan(DisableEQSTestPlan.PlanId, true).bOk);
    const FThomasAIAssetInspectionResult DisabledEQS =
        UThomasDomainsToolset::InspectAIAsset(EQSPath, 100);
    const FThomasAINodeRecord* DisabledTest = DisabledEQS.Nodes.FindByPredicate(
        [](const FThomasAINodeRecord& Node) { return Node.Role == TEXT("test"); });
    TestTrue(TEXT("EQS test enabled state persisted"), DisabledTest && !DisabledTest->bEnabled);

    FThomasDataAIPatchRequest EQSRemoval;
    EQSRemoval.AssetPath = EQSPath;
    EQSRemoval.ExpectedRevision = DisabledEQS.Revision;
    EQSRemoval.AssetKind = TEXT("eqs");
    EQSRemoval.bConfirmDestructive = true;
    for (const FString& Role : {TEXT("test"), TEXT("generator")})
    {
        if (const FThomasAINodeRecord* Node = DisabledEQS.Nodes.FindByPredicate(
                [&Role](const FThomasAINodeRecord& Candidate)
                {
                    return Candidate.Role == Role;
                }))
        {
            FThomasDataAIOperation RemoveNode;
            RemoveNode.Action = TEXT("remove_ai_graph_node");
            RemoveNode.ItemId = Node->Id;
            EQSRemoval.Operations.Add(RemoveNode);
        }
    }
    const FThomasDataAIPlanResult EQSRemovalPlan =
        UThomasDomainsToolset::PlanDataAIPatch(EQSRemoval);
    TestTrue(TEXT("Confirmed EQS structural removal plan succeeds"), EQSRemovalPlan.bOk);
    TestEqual(TEXT("EQS structural removals are R2"), EQSRemovalPlan.Risk, FString(TEXT("R2")));
    TestTrue(
        TEXT("Confirmed EQS structural removals rebuild/save"),
        UThomasDomainsToolset::ApplyDataAIPlan(EQSRemovalPlan.PlanId, true).bOk);
    const FThomasAIAssetInspectionResult EmptyEQS =
        UThomasDomainsToolset::InspectAIAsset(EQSPath, 20);
    TestEqual(TEXT("EQS options were removed"), EmptyEQS.OptionCount, 0);
    TestEqual(TEXT("EQS retains only its protected root"), EmptyEQS.GraphNodeCount, 1);

    const FString StateTreePath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/ST_TE_Native_") + DataAISuffix;
    FThomasDataAIPatchRequest StateTreeRequest;
    StateTreeRequest.AssetPath = StateTreePath;
    StateTreeRequest.ExpectedRevision = TEXT("missing");
    StateTreeRequest.AssetKind = TEXT("state_tree");
    StateTreeRequest.bCreateIfMissing = true;
    FThomasDataAIOperation CreateStateTree;
    CreateStateTree.Action = TEXT("create_ai_asset");
    CreateStateTree.ClassPath =
        TEXT("/Script/GameplayStateTreeModule.StateTreeAIComponentSchema");
    StateTreeRequest.Operations.Add(CreateStateTree);
    const FThomasDataAIPlanResult StateTreeCreatePlan =
        UThomasDomainsToolset::PlanDataAIPatch(StateTreeRequest);
    TestTrue(TEXT("Native StateTree factory plan succeeds"), StateTreeCreatePlan.bOk);
    const FThomasDataAIApplyResult StateTreeCreateApply =
        UThomasDomainsToolset::ApplyDataAIPlan(StateTreeCreatePlan.PlanId, true);
    TestTrue(TEXT("Native StateTree factory compile/save succeeds"), StateTreeCreateApply.bOk);
    TestTrue(TEXT("Native StateTree was created"), StateTreeCreateApply.bCreated);
    TestTrue(TEXT("Native StateTree was saved"), StateTreeCreateApply.bSaved);
    const FThomasAIAssetInspectionResult InitialStateTreeInspection =
        UThomasDomainsToolset::InspectAIAsset(StateTreePath, 100);
    TestTrue(TEXT("Native StateTree factory root is inspectable"), InitialStateTreeInspection.bOk);
    TestEqual(TEXT("Native StateTree factory creates one root"), InitialStateTreeInspection.States.Num(), 1);
    const FString FactoryRootId = InitialStateTreeInspection.States.IsValidIndex(0)
        ? InitialStateTreeInspection.States[0].Id : FString();

    FThomasDataAIPatchRequest StateTreeStructure;
    StateTreeStructure.AssetPath = StateTreePath;
    StateTreeStructure.ExpectedRevision = InitialStateTreeInspection.Revision;
    StateTreeStructure.AssetKind = TEXT("state_tree");
    FThomasDataAIOperation AddPatrolState;
    AddPatrolState.Action = TEXT("add_state");
    AddPatrolState.Name = TEXT("Patrol");
    AddPatrolState.ParentId = FactoryRootId;
    AddPatrolState.StateType = TEXT("State");
    AddPatrolState.SelectionBehavior = TEXT("TryEnterState");
    AddPatrolState.ResultId = TEXT("patrol");
    StateTreeStructure.Operations.Add(AddPatrolState);
    FThomasDataAIOperation AddIdleState = AddPatrolState;
    AddIdleState.Name = TEXT("Idle");
    AddIdleState.ResultId = TEXT("idle");
    StateTreeStructure.Operations.Add(AddIdleState);
    FThomasDataAIOperation AddMoveTask;
    AddMoveTask.Action = TEXT("add_state_task");
    AddMoveTask.StateId = TEXT("patrol");
    AddMoveTask.ClassPath =
        TEXT("/Script/GameplayStateTreeModule.StateTreeMoveToTask");
    AddMoveTask.Name = TEXT("MoveToAutomationPoint");
    AddMoveTask.ResultId = TEXT("move_task");
    StateTreeStructure.Operations.Add(AddMoveTask);
    FThomasDataAIOperation SetMoveDestination;
    SetMoveDestination.Action = TEXT("set_state_tree_node_property");
    SetMoveDestination.ItemId = TEXT("move_task");
    SetMoveDestination.PropertyScope = TEXT("instance");
    SetMoveDestination.KeyTypePropertyName = TEXT("Destination");
    SetMoveDestination.KeyTypePropertyValue = TEXT("(X=100,Y=200,Z=0)");
    StateTreeStructure.Operations.Add(SetMoveDestination);
    FThomasDataAIOperation AddEnterCondition;
    AddEnterCondition.Action = TEXT("add_enter_condition");
    AddEnterCondition.StateId = TEXT("patrol");
    AddEnterCondition.ClassPath =
        TEXT("/Script/StateTreeModule.StateTreeRandomCondition");
    AddEnterCondition.Name = TEXT("RandomGate");
    AddEnterCondition.ResultId = TEXT("random_condition");
    StateTreeStructure.Operations.Add(AddEnterCondition);
    FThomasDataAIOperation AddTransition;
    AddTransition.Action = TEXT("add_state_transition");
    AddTransition.StateId = TEXT("patrol");
    AddTransition.TargetStateId = TEXT("idle");
    AddTransition.TransitionTrigger = TEXT("OnStateSucceeded");
    AddTransition.TransitionType = TEXT("GotoState");
    AddTransition.ResultId = TEXT("patrol_to_idle");
    StateTreeStructure.Operations.Add(AddTransition);
    const FThomasDataAIPlanResult StateTreePlan =
        UThomasDomainsToolset::PlanDataAIPatch(StateTreeStructure);
    TestTrue(TEXT("Native StateTree structural plan succeeds"), StateTreePlan.bOk);
    const FThomasDataAIApplyResult StateTreeApply =
        UThomasDomainsToolset::ApplyDataAIPlan(StateTreePlan.PlanId, true);
    TestTrue(TEXT("Native StateTree builder/compile/save succeeds"), StateTreeApply.bOk);
    TestFalse(TEXT("StateTree structural patch reuses the native asset"), StateTreeApply.bCreated);
    TestTrue(TEXT("Native StateTree structural patch was saved"), StateTreeApply.bSaved);
    const FThomasAIAssetInspectionResult StateTreeInspection =
        UThomasDomainsToolset::InspectAIAsset(StateTreePath, 100);
    TestTrue(TEXT("Created StateTree is structurally inspectable"), StateTreeInspection.bOk);
    TestEqual(TEXT("Created StateTree state count"), StateTreeInspection.States.Num(), 3);
    TestEqual(TEXT("Created StateTree node count"), StateTreeInspection.Nodes.Num(), 2);
    TestEqual(TEXT("Created StateTree transition count"), StateTreeInspection.Transitions.Num(), 1);
    TestTrue(
        TEXT("Created StateTree exposes typed task structure"),
        StateTreeInspection.Nodes.ContainsByPredicate(
            [](const FThomasAINodeRecord& Node)
            {
                return Node.Role == TEXT("state_task")
                    && Node.Name == TEXT("MoveToAutomationPoint")
                    && Node.StructPath ==
                        TEXT("/Script/GameplayStateTreeModule.StateTreeMoveToTask");
            }));

    FThomasDataAIPatchRequest StateTreeRemoval;
    StateTreeRemoval.AssetPath = StateTreePath;
    StateTreeRemoval.ExpectedRevision = StateTreeInspection.Revision;
    StateTreeRemoval.AssetKind = TEXT("state_tree");
    StateTreeRemoval.bConfirmDestructive = true;
    if (StateTreeInspection.Transitions.IsValidIndex(0))
    {
        FThomasDataAIOperation RemoveTransition;
        RemoveTransition.Action = TEXT("remove_state_transition");
        RemoveTransition.ItemId = StateTreeInspection.Transitions[0].Id;
        StateTreeRemoval.Operations.Add(RemoveTransition);
    }
    for (const FThomasAINodeRecord& Node : StateTreeInspection.Nodes)
    {
        FThomasDataAIOperation RemoveNode;
        RemoveNode.Action = TEXT("remove_state_tree_node");
        RemoveNode.ItemId = Node.Id;
        StateTreeRemoval.Operations.Add(RemoveNode);
    }
    const FThomasAIStateRecord* IdleState = StateTreeInspection.States.FindByPredicate(
        [](const FThomasAIStateRecord& State) { return State.Name == TEXT("Idle"); });
    TestNotNull(TEXT("Created StateTree Idle state has a stable ID"), IdleState);
    if (IdleState)
    {
        FThomasDataAIOperation RemoveIdleState;
        RemoveIdleState.Action = TEXT("remove_state");
        RemoveIdleState.StateId = IdleState->Id;
        StateTreeRemoval.Operations.Add(RemoveIdleState);
    }
    const FThomasDataAIPlanResult StateTreeRemovalPlan =
        UThomasDomainsToolset::PlanDataAIPatch(StateTreeRemoval);
    TestTrue(TEXT("Confirmed StateTree structural removal plan succeeds"), StateTreeRemovalPlan.bOk);
    TestEqual(TEXT("StateTree structural removals are R2"), StateTreeRemovalPlan.Risk, FString(TEXT("R2")));
    TestTrue(
        TEXT("Confirmed StateTree structural removals compile/save"),
        UThomasDomainsToolset::ApplyDataAIPlan(StateTreeRemovalPlan.PlanId, true).bOk);
    const FThomasAIAssetInspectionResult StateTreeAfterRemoval =
        UThomasDomainsToolset::InspectAIAsset(StateTreePath, 100);
    TestEqual(TEXT("StateTree transition was removed"), StateTreeAfterRemoval.Transitions.Num(), 0);
    TestEqual(TEXT("StateTree nodes were removed"), StateTreeAfterRemoval.Nodes.Num(), 0);
    TestEqual(TEXT("StateTree child state was removed"), StateTreeAfterRemoval.States.Num(), 2);

    FThomasDataAIPatchRequest UnconfirmedBlackboardRemoval;
    UnconfirmedBlackboardRemoval.AssetPath = BlackboardPath;
    UnconfirmedBlackboardRemoval.ExpectedRevision = BlackboardInspection.Revision;
    UnconfirmedBlackboardRemoval.AssetKind = TEXT("blackboard");
    FThomasDataAIOperation RemoveBlackboardKey;
    RemoveBlackboardKey.Action = TEXT("remove_blackboard_key");
    RemoveBlackboardKey.Name = TEXT("CanSeeTarget");
    UnconfirmedBlackboardRemoval.Operations.Add(RemoveBlackboardKey);
    TestEqual(
        TEXT("Blackboard key removal requires explicit confirmation"),
        UThomasDomainsToolset::PlanDataAIPatch(UnconfirmedBlackboardRemoval).Code,
        FString(TEXT("confirmation_required")));
    FThomasDataAIPatchRequest ConfirmedBlackboardRemoval = UnconfirmedBlackboardRemoval;
    ConfirmedBlackboardRemoval.bConfirmDestructive = true;
    const FThomasDataAIPlanResult ConfirmedBlackboardRemovalPlan =
        UThomasDomainsToolset::PlanDataAIPatch(ConfirmedBlackboardRemoval);
    TestTrue(TEXT("Confirmed Blackboard key removal plan succeeds"), ConfirmedBlackboardRemovalPlan.bOk);
    TestTrue(
        TEXT("Confirmed Blackboard key removal/save succeeds"),
        UThomasDomainsToolset::ApplyDataAIPlan(ConfirmedBlackboardRemovalPlan.PlanId, true).bOk);
    TestFalse(
        TEXT("Native Blackboard key was removed while factory defaults are preserved"),
        UThomasDomainsToolset::InspectAIAsset(BlackboardPath, 20).BlackboardKeys.ContainsByPredicate(
            [](const FThomasBlackboardKeyRecord& Key)
            {
                return Key.Name == TEXT("CanSeeTarget");
            }));

    TArray<UObject*> DataAIAssetsToDelete;
    const TArray<FString> DataAIPathsToDelete = {
        StateTreePath, EQSPath, BehaviorTreePath, BlackboardPath, DataTablePath, DataAssetPath};
    for (const FString& DataAIPath : DataAIPathsToDelete)
    {
        if (UObject* Asset = LoadObject<UObject>(
            nullptr,
            *(DataAIPath + TEXT(".") + FPackageName::GetLongPackageAssetName(DataAIPath))))
        {
            DataAIAssetsToDelete.Add(Asset);
        }
    }
    TestEqual(TEXT("All temporary Data/AI assets loaded for cleanup"), DataAIAssetsToDelete.Num(), 6);
    TestEqual(
        TEXT("Temporary native Data/AI assets deleted through Unreal API"),
        ObjectTools::DeleteObjectsUnchecked(DataAIAssetsToDelete),
        DataAIAssetsToDelete.Num());
    for (const FString& DataAIPath : DataAIPathsToDelete)
    {
        TestFalse(
            *FString::Printf(TEXT("Temporary Data/AI asset removed from disk: %s"), *DataAIPath),
            IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
                DataAIPath, FPackageName::GetAssetPackageExtension())));
    }

    const FString InputTestSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString InputActionPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/IA_TE_Native_") + InputTestSuffix;
    const FString InputActionObjectPath = InputActionPath + TEXT(".")
        + FPackageName::GetLongPackageAssetName(InputActionPath);
    FThomasEnhancedInputPatchRequest InputActionRequest;
    InputActionRequest.AssetPath = InputActionPath;
    InputActionRequest.ExpectedRevision = TEXT("missing");
    InputActionRequest.AssetKind = TEXT("InputAction");
    InputActionRequest.bCreateIfMissing = true;
    FThomasEnhancedInputOperation SetValueType;
    SetValueType.Action = TEXT("set_value_type");
    SetValueType.ValueType = TEXT("Axis2D");
    InputActionRequest.Operations.Add(SetValueType);
    const FThomasEnhancedInputPlanResult InputActionPlan =
        UThomasDomainsToolset::PlanEnhancedInputPatch(InputActionRequest);
    TestTrue(TEXT("Native Input Action create plan succeeds"), InputActionPlan.bOk);
    const FThomasEnhancedInputApplyResult InputActionApply =
        UThomasDomainsToolset::ApplyEnhancedInputPlan(InputActionPlan.PlanId, true);
    TestTrue(TEXT("Native Input Action create/save succeeds"), InputActionApply.bOk);
    TestTrue(TEXT("Native Input Action was created"), InputActionApply.bCreated);
    TestTrue(TEXT("Native Input Action was saved"), InputActionApply.bSaved);
    const FThomasEnhancedInputInspectionResult InputActionInspection =
        UThomasDomainsToolset::InspectEnhancedInputAsset(InputActionPath);
    TestTrue(TEXT("Created native Input Action is inspectable"), InputActionInspection.bOk);
    TestEqual(TEXT("Created native Input Action value type"), InputActionInspection.ValueType, FString(TEXT("Axis2D")));

    const FString InputContextPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/IMC_TE_Native_") + InputTestSuffix;
    FThomasEnhancedInputPatchRequest InputContextRequest;
    InputContextRequest.AssetPath = InputContextPath;
    InputContextRequest.ExpectedRevision = TEXT("missing");
    InputContextRequest.AssetKind = TEXT("InputMappingContext");
    InputContextRequest.bCreateIfMissing = true;
    FThomasEnhancedInputOperation AddMapping;
    AddMapping.Action = TEXT("add_mapping");
    AddMapping.InputActionPath = InputActionObjectPath;
    AddMapping.Key = TEXT("W");
    InputContextRequest.Operations.Add(AddMapping);
    const FThomasEnhancedInputPlanResult InputContextPlan =
        UThomasDomainsToolset::PlanEnhancedInputPatch(InputContextRequest);
    TestTrue(TEXT("Native Input Mapping Context create plan succeeds"), InputContextPlan.bOk);
    const FThomasEnhancedInputApplyResult InputContextApply =
        UThomasDomainsToolset::ApplyEnhancedInputPlan(InputContextPlan.PlanId, true);
    TestTrue(TEXT("Native Input Mapping Context create/save succeeds"), InputContextApply.bOk);
    TestTrue(TEXT("Native Input Mapping Context was created"), InputContextApply.bCreated);
    TestTrue(TEXT("Native Input Mapping Context was saved"), InputContextApply.bSaved);
    const FThomasEnhancedInputInspectionResult InputContextInspection =
        UThomasDomainsToolset::InspectEnhancedInputAsset(InputContextPath);
    TestTrue(TEXT("Created native Input Mapping Context is inspectable"), InputContextInspection.bOk);
    TestEqual(TEXT("Created native Input Mapping Context mapping count"), InputContextInspection.MappingCount, 1);
    TestTrue(
        TEXT("Created native mapping exposes its action and key"),
        InputContextInspection.Mappings.Num() == 1
            && InputContextInspection.Mappings[0].InputActionPath == InputActionObjectPath
            && InputContextInspection.Mappings[0].Key == TEXT("W"));

    FThomasEnhancedInputPatchRequest RemoveMappingRequest;
    RemoveMappingRequest.AssetPath = InputContextPath;
    RemoveMappingRequest.ExpectedRevision = InputContextInspection.Revision;
    RemoveMappingRequest.bConfirmDestructive = true;
    FThomasEnhancedInputOperation RemoveMapping;
    RemoveMapping.Action = TEXT("remove_mapping");
    RemoveMapping.InputActionPath = InputActionObjectPath;
    RemoveMapping.Key = TEXT("W");
    RemoveMappingRequest.Operations.Add(RemoveMapping);
    const FThomasEnhancedInputPlanResult RemoveMappingPlan =
        UThomasDomainsToolset::PlanEnhancedInputPatch(RemoveMappingRequest);
    TestTrue(TEXT("Native mapping removal plan succeeds with confirmation"), RemoveMappingPlan.bOk);
    TestEqual(TEXT("Native mapping removal is classified R2"), RemoveMappingPlan.Risk, FString(TEXT("R2")));
    const FThomasEnhancedInputApplyResult RemoveMappingApply =
        UThomasDomainsToolset::ApplyEnhancedInputPlan(RemoveMappingPlan.PlanId, true);
    TestTrue(TEXT("Native mapping removal/save succeeds"), RemoveMappingApply.bOk);
    TestEqual(
        TEXT("Native mapping was removed"),
        UThomasDomainsToolset::InspectEnhancedInputAsset(InputContextPath).MappingCount,
        0);

    UObject* InputContextAsset = LoadObject<UObject>(
        nullptr,
        *(InputContextPath + TEXT(".") + FPackageName::GetLongPackageAssetName(InputContextPath)));
    UObject* InputActionAsset = LoadObject<UObject>(nullptr, *InputActionObjectPath);
    TestNotNull(TEXT("Temporary native Input Mapping Context loaded for cleanup"), InputContextAsset);
    TestNotNull(TEXT("Temporary native Input Action loaded for cleanup"), InputActionAsset);
    TArray<UObject*> InputAssetsToDelete;
    if (InputContextAsset) InputAssetsToDelete.Add(InputContextAsset);
    if (InputActionAsset) InputAssetsToDelete.Add(InputActionAsset);
    if (!InputAssetsToDelete.IsEmpty())
    {
        TestEqual(
            TEXT("Temporary native Enhanced Input assets deleted through Unreal API"),
            ObjectTools::DeleteObjectsUnchecked(InputAssetsToDelete),
            InputAssetsToDelete.Num());
    }
    TestFalse(
        TEXT("Temporary native Input Action removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            InputActionPath, FPackageName::GetAssetPackageExtension())));
    TestFalse(
        TEXT("Temporary native Input Mapping Context removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            InputContextPath, FPackageName::GetAssetPackageExtension())));

    const FThomasDomainInventoryResult PCGInventory =
        UThomasDomainsToolset::InspectPCGAssets(TEXT("/Game/PropHunt"), 20);
    TestTrue(TEXT("Native PCG inventory succeeds"), PCGInventory.bOk);
    const FThomasSpecializedAssetInspectionResult PCGClassSearch =
        UThomasDomainsToolset::SearchPCGSettingsClasses(TEXT("TransformPoints"), 20);
    TestTrue(TEXT("Native PCG settings class discovery succeeds"), PCGClassSearch.bOk);
    TestTrue(
        TEXT("Native PCG settings class discovery finds Transform Points"),
        PCGClassSearch.Items.ContainsByPredicate([](const FString& Item)
        {
            return Item.Contains(TEXT("/Script/PCG.PCGTransformPointsSettings"));
        }));

    const FString PCGGraphPath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/PCG_TE_Native_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FThomasDomainAssetCreateRequest PCGCreateRequest;
    PCGCreateRequest.AssetPath = PCGGraphPath;
    PCGCreateRequest.ExpectedRevision = TEXT("missing");
    PCGCreateRequest.AssetKind = TEXT("pcg_graph");
    const FThomasDomainAssetCreatePlanResult PCGCreatePlan =
        UThomasDomainsToolset::PlanPCGAssetCreate(PCGCreateRequest);
    TestTrue(TEXT("Native PCG Graph creation plan succeeds"), PCGCreatePlan.bOk);
    const FThomasDomainAssetCreateApplyResult PCGCreateApply =
        UThomasDomainsToolset::ApplyPCGAssetCreate(PCGCreatePlan.PlanId, true);
    TestTrue(TEXT("Native PCG Graph creation/save succeeds"), PCGCreateApply.bOk);
    TestTrue(TEXT("Native PCG Graph creation reports save"), PCGCreateApply.bSaved);

    FThomasSpecializedAssetInspectionResult PCGInspection =
        UThomasDomainsToolset::InspectPCGGraph(PCGGraphPath, 500);
    TestTrue(TEXT("Native PCG Graph inspection succeeds"), PCGInspection.bOk);
    TestTrue(
        TEXT("New native PCG Graph contains the input/output nodes"),
        PCGInspection.Metrics.Contains(TEXT("NodeCount=2")));
    TestTrue(
        TEXT("New native PCG Graph has no user nodes"),
        PCGInspection.Metrics.Contains(TEXT("UserNodeCount=0")));

    FThomasPCGPatchRequest PCGParameterSchemaRequest;
    PCGParameterSchemaRequest.AssetPath = PCGGraphPath;
    PCGParameterSchemaRequest.ExpectedRevision = PCGInspection.Revision;
    FThomasPCGOperation PCGAddParameter;
    PCGAddParameter.Action = TEXT("add_graph_parameter");
    PCGAddParameter.ParameterName = TEXT("Density");
    PCGAddParameter.ParameterType = TEXT("double");
    PCGAddParameter.Value = TEXT("0.25");
    PCGParameterSchemaRequest.Operations.Add(PCGAddParameter);
    FThomasPCGOperation PCGRenameParameter;
    PCGRenameParameter.Action = TEXT("rename_graph_parameter");
    PCGRenameParameter.ParameterName = TEXT("Density");
    PCGRenameParameter.NewName = TEXT("SpawnDensity");
    PCGParameterSchemaRequest.Operations.Add(PCGRenameParameter);
    FThomasPCGOperation PCGSetAssetUsage;
    PCGSetAssetUsage.Action = TEXT("set_graph_usage");
    PCGSetAssetUsage.GraphUsage = TEXT("asset");
    PCGParameterSchemaRequest.Operations.Add(PCGSetAssetUsage);
    const FThomasPCGPlanResult PCGUnconfirmedParameterSchemaPlan =
        UThomasDomainsToolset::PlanPCGPatch(PCGParameterSchemaRequest);
    TestFalse(
        TEXT("Native PCG parameter schema/usage plan requires confirmation"),
        PCGUnconfirmedParameterSchemaPlan.bOk);
    TestEqual(
        TEXT("Native PCG parameter schema/usage plan reports confirmation gate"),
        PCGUnconfirmedParameterSchemaPlan.Code,
        FString(TEXT("confirmation_required")));
    PCGParameterSchemaRequest.bConfirmDestructive = true;
    const FThomasPCGPlanResult PCGParameterSchemaPlan =
        UThomasDomainsToolset::PlanPCGPatch(PCGParameterSchemaRequest);
    TestTrue(
        TEXT("Native PCG parameter add/rename and Asset usage plan succeeds"),
        PCGParameterSchemaPlan.bOk);
    TestEqual(
        TEXT("Native PCG parameter schema/usage plan is R2"),
        PCGParameterSchemaPlan.Risk,
        FString(TEXT("R2")));
    const FThomasPCGApplyResult PCGParameterSchemaApply =
        UThomasDomainsToolset::ApplyPCGPlan(PCGParameterSchemaPlan.PlanId, true);
    TestTrue(
        TEXT("Native PCG parameter add/rename and Asset usage apply/save succeeds"),
        PCGParameterSchemaApply.bOk);
    TestEqual(
        TEXT("Native PCG parameter schema/usage applies three operations"),
        PCGParameterSchemaApply.AppliedOperationCount,
        3);
    PCGInspection = UThomasDomainsToolset::InspectPCGGraph(PCGGraphPath, 500);
    TestTrue(
        TEXT("Native PCG Graph inspection reports one graph parameter"),
        PCGInspection.Metrics.Contains(TEXT("ParameterCount=1")));
    TestTrue(
        TEXT("Native PCG Graph inspection reports Asset usage"),
        PCGInspection.Metrics.ContainsByPredicate([](const FString& Metric)
        {
            return Metric.StartsWith(TEXT("GraphUsage="))
                && Metric.Contains(TEXT("Asset"));
        }));
    TestTrue(
        TEXT("Native PCG Graph inspection reports the renamed typed parameter"),
        PCGInspection.Items.ContainsByPredicate([](const FString& Item)
        {
            return Item.StartsWith(TEXT("GraphParameter Name=SpawnDensity Type=Double "))
                && Item.Contains(TEXT("Overridden=false"));
        }));

    const auto GetPCGParameterValue = [](const FThomasSpecializedAssetInspectionResult& Inspection)
    {
        for (const FString& Item : Inspection.Items)
        {
            if (!Item.StartsWith(TEXT("GraphParameter Name=SpawnDensity ")))
            {
                continue;
            }
            const int32 ValueIndex = Item.Find(TEXT(" Value="));
            const int32 OverrideIndex = Item.Find(TEXT(" Overridden="));
            if (ValueIndex != INDEX_NONE && OverrideIndex > ValueIndex)
            {
                return Item.Mid(ValueIndex + 7, OverrideIndex - ValueIndex - 7);
            }
        }
        return FString();
    };
    const FString PCGBaseParameterValue = GetPCGParameterValue(PCGInspection);
    TestFalse(
        TEXT("Native PCG Graph inspection exposes the parameter default value"),
        PCGBaseParameterValue.IsEmpty());

    FThomasPCGPatchRequest PCGAddRequest;
    PCGAddRequest.AssetPath = PCGGraphPath;
    PCGAddRequest.ExpectedRevision = PCGInspection.Revision;
    FThomasPCGOperation PCGAddNode;
    PCGAddNode.Action = TEXT("add_node");
    PCGAddNode.Handle = TEXT("transform_points");
    PCGAddNode.SettingsClassPath = TEXT("/Script/PCG.PCGTransformPointsSettings");
    PCGAddNode.Title = TEXT("Thomas Transform Points");
    PCGAddNode.PositionX = 320;
    PCGAddNode.PositionY = 160;
    PCGAddRequest.Operations.Add(PCGAddNode);
    FThomasPCGOperation PCGConnectInput;
    PCGConnectInput.Action = TEXT("connect");
    PCGConnectInput.NodePath = TEXT("input");
    PCGConnectInput.OtherHandle = TEXT("transform_points");
    PCGConnectInput.FromPin = TEXT("In");
    PCGConnectInput.ToPin = TEXT("In");
    PCGAddRequest.Operations.Add(PCGConnectInput);
    FThomasPCGOperation PCGConnectOutput;
    PCGConnectOutput.Action = TEXT("connect");
    PCGConnectOutput.Handle = TEXT("transform_points");
    PCGConnectOutput.OtherNodePath = TEXT("output");
    PCGConnectOutput.FromPin = TEXT("Out");
    PCGConnectOutput.ToPin = TEXT("Out");
    PCGAddRequest.Operations.Add(PCGConnectOutput);
    const FThomasPCGPlanResult PCGAddPlan =
        UThomasDomainsToolset::PlanPCGPatch(PCGAddRequest);
    TestTrue(TEXT("Native PCG add/connect plan succeeds"), PCGAddPlan.bOk);
    TestEqual(TEXT("Native PCG add/connect plan is R1"), PCGAddPlan.Risk, FString(TEXT("R1")));
    const FThomasPCGApplyResult PCGAddApply =
        UThomasDomainsToolset::ApplyPCGPlan(PCGAddPlan.PlanId, true);
    TestTrue(TEXT("Native PCG add/connect apply/save succeeds"), PCGAddApply.bOk);
    TestEqual(TEXT("Native PCG add/connect applies three operations"), PCGAddApply.AppliedOperationCount, 3);
    TestEqual(TEXT("Native PCG add returns one created node"), PCGAddApply.CreatedNodePaths.Num(), 1);

    FString PCGNodePath;
    if (PCGAddApply.CreatedNodePaths.Num() == 1)
    {
        PCGNodePath = PCGAddApply.CreatedNodePaths[0];
    }
    PCGInspection = UThomasDomainsToolset::InspectPCGGraph(PCGGraphPath, 500);
    TestTrue(TEXT("Mutated native PCG Graph inspection succeeds"), PCGInspection.bOk);
    TestTrue(
        TEXT("Mutated native PCG Graph reports one user node"),
        PCGInspection.Metrics.Contains(TEXT("UserNodeCount=1")));
    TestTrue(
        TEXT("Mutated native PCG Graph reports two edges"),
        PCGInspection.Metrics.Contains(TEXT("EdgeCount=2")));
    TestTrue(
        TEXT("Mutated native PCG Graph preserves the authored title"),
        PCGInspection.Items.ContainsByPredicate([](const FString& Item)
        {
            return Item.Contains(TEXT("Title=Thomas Transform Points"));
        }));

    FString PCGSeedBefore;
    const FString PCGSeedPrefix = FString::Printf(
        TEXT("SettingProperty Node=%s Name=Seed "), *PCGNodePath);
    for (const FString& Item : PCGInspection.Items)
    {
        if (Item.StartsWith(PCGSeedPrefix))
        {
            const int32 ValueIndex = Item.Find(TEXT(" Value="));
            if (ValueIndex != INDEX_NONE)
            {
                PCGSeedBefore = Item.Mid(ValueIndex + 7);
            }
            break;
        }
    }
    TestFalse(TEXT("Native PCG inspection exposes the node Seed property"), PCGSeedBefore.IsEmpty());
    const FString PCGSeedAfter = PCGSeedBefore == TEXT("12345") ? TEXT("54321") : TEXT("12345");
    FThomasPCGPatchRequest PCGPropertyRequest;
    PCGPropertyRequest.AssetPath = PCGGraphPath;
    PCGPropertyRequest.ExpectedRevision = PCGInspection.Revision;
    FThomasPCGOperation PCGSetSeed;
    PCGSetSeed.Action = TEXT("set_node_property");
    PCGSetSeed.NodePath = PCGNodePath;
    PCGSetSeed.PropertyName = TEXT("Seed");
    PCGSetSeed.ExpectedValue = PCGSeedBefore;
    PCGSetSeed.Value = PCGSeedAfter;
    PCGPropertyRequest.Operations.Add(PCGSetSeed);
    const FThomasPCGPlanResult PCGPropertyPlan =
        UThomasDomainsToolset::PlanPCGPatch(PCGPropertyRequest);
    TestTrue(TEXT("Native PCG settings property plan succeeds"), PCGPropertyPlan.bOk);
    const FThomasPCGApplyResult PCGPropertyApply =
        UThomasDomainsToolset::ApplyPCGPlan(PCGPropertyPlan.PlanId, true);
    TestTrue(TEXT("Native PCG settings property apply/save succeeds"), PCGPropertyApply.bOk);

    PCGInspection = UThomasDomainsToolset::InspectPCGGraph(PCGGraphPath, 500);
    TestTrue(
        TEXT("Native PCG inspection reports the updated Seed"),
        PCGInspection.Items.ContainsByPredicate([PCGNodePath, PCGSeedAfter](const FString& Item)
        {
            return Item.StartsWith(FString::Printf(
                TEXT("SettingProperty Node=%s Name=Seed "), *PCGNodePath))
                && Item.EndsWith(TEXT("Value=") + PCGSeedAfter);
        }));

    FThomasPCGPatchRequest PCGRemoveRequest;
    PCGRemoveRequest.AssetPath = PCGGraphPath;
    PCGRemoveRequest.ExpectedRevision = PCGInspection.Revision;
    FThomasPCGOperation PCGDisconnectInput = PCGConnectInput;
    PCGDisconnectInput.Action = TEXT("disconnect");
    PCGDisconnectInput.OtherHandle.Reset();
    PCGDisconnectInput.OtherNodePath = PCGNodePath;
    PCGRemoveRequest.Operations.Add(PCGDisconnectInput);
    FThomasPCGOperation PCGDisconnectOutput = PCGConnectOutput;
    PCGDisconnectOutput.Action = TEXT("disconnect");
    PCGDisconnectOutput.Handle.Reset();
    PCGDisconnectOutput.NodePath = PCGNodePath;
    PCGRemoveRequest.Operations.Add(PCGDisconnectOutput);
    FThomasPCGOperation PCGRemoveNode;
    PCGRemoveNode.Action = TEXT("remove_node");
    PCGRemoveNode.NodePath = PCGNodePath;
    PCGRemoveRequest.Operations.Add(PCGRemoveNode);
    const FThomasPCGPlanResult PCGUnconfirmedRemovePlan =
        UThomasDomainsToolset::PlanPCGPatch(PCGRemoveRequest);
    TestFalse(TEXT("Native PCG destructive plan refuses missing confirmation"), PCGUnconfirmedRemovePlan.bOk);
    TestEqual(
        TEXT("Native PCG destructive plan reports confirmation gate"),
        PCGUnconfirmedRemovePlan.Code,
        FString(TEXT("confirmation_required")));
    PCGRemoveRequest.bConfirmDestructive = true;
    const FThomasPCGPlanResult PCGRemovePlan =
        UThomasDomainsToolset::PlanPCGPatch(PCGRemoveRequest);
    TestTrue(TEXT("Native PCG destructive plan succeeds with confirmation"), PCGRemovePlan.bOk);
    TestEqual(TEXT("Native PCG destructive plan is R2"), PCGRemovePlan.Risk, FString(TEXT("R2")));
    const FThomasPCGApplyResult PCGRemoveApply =
        UThomasDomainsToolset::ApplyPCGPlan(PCGRemovePlan.PlanId, true);
    TestTrue(TEXT("Native PCG disconnect/remove apply/save succeeds"), PCGRemoveApply.bOk);
    TestEqual(TEXT("Native PCG disconnect/remove applies three operations"), PCGRemoveApply.AppliedOperationCount, 3);
    PCGInspection = UThomasDomainsToolset::InspectPCGGraph(PCGGraphPath, 500);
    TestTrue(
        TEXT("Native PCG cleanup leaves no user node"),
        PCGInspection.Metrics.Contains(TEXT("UserNodeCount=0")));
    TestTrue(
        TEXT("Native PCG cleanup leaves no edge"),
        PCGInspection.Metrics.Contains(TEXT("EdgeCount=0")));

    FThomasPCGPatchRequest PCGExecutionNodeRequest;
    PCGExecutionNodeRequest.AssetPath = PCGGraphPath;
    PCGExecutionNodeRequest.ExpectedRevision = PCGInspection.Revision;
    FThomasPCGOperation PCGAddExecutionNode;
    PCGAddExecutionNode.Action = TEXT("add_node");
    PCGAddExecutionNode.Handle = TEXT("create_attribute");
    PCGAddExecutionNode.SettingsClassPath =
        TEXT("/Script/PCG.PCGCreateAttributeSetSettings");
    PCGAddExecutionNode.Title = TEXT("Thomas Execution Output");
    PCGAddExecutionNode.PositionX = 320;
    PCGAddExecutionNode.PositionY = 0;
    PCGExecutionNodeRequest.Operations.Add(PCGAddExecutionNode);
    FThomasPCGOperation PCGConnectExecutionOutput;
    PCGConnectExecutionOutput.Action = TEXT("connect");
    PCGConnectExecutionOutput.Handle = TEXT("create_attribute");
    PCGConnectExecutionOutput.OtherNodePath = TEXT("output");
    PCGConnectExecutionOutput.FromPin = TEXT("Out");
    PCGConnectExecutionOutput.ToPin = TEXT("Out");
    PCGExecutionNodeRequest.Operations.Add(PCGConnectExecutionOutput);
    const FThomasPCGPlanResult PCGExecutionNodePlan =
        UThomasDomainsToolset::PlanPCGPatch(PCGExecutionNodeRequest);
    TestTrue(
        TEXT("Native PCG standalone output node plan succeeds"),
        PCGExecutionNodePlan.bOk);
    const FThomasPCGApplyResult PCGExecutionNodeApply =
        UThomasDomainsToolset::ApplyPCGPlan(PCGExecutionNodePlan.PlanId, true);
    TestTrue(
        TEXT("Native PCG standalone output node apply/save succeeds"),
        PCGExecutionNodeApply.bOk);

    const FString PCGInstancePath =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/PCGI_TE_Native_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FThomasDomainAssetCreateRequest PCGInstanceCreateRequest;
    PCGInstanceCreateRequest.AssetPath = PCGInstancePath;
    PCGInstanceCreateRequest.ExpectedRevision = TEXT("missing");
    PCGInstanceCreateRequest.AssetKind = TEXT("pcg_graph_instance");
    PCGInstanceCreateRequest.ContextAssetPath = PCGGraphPath;
    const FThomasDomainAssetCreatePlanResult PCGInstanceCreatePlan =
        UThomasDomainsToolset::PlanPCGAssetCreate(PCGInstanceCreateRequest);
    TestTrue(
        TEXT("Native PCG Graph Instance creation plan succeeds"),
        PCGInstanceCreatePlan.bOk);
    const FThomasDomainAssetCreateApplyResult PCGInstanceCreateApply =
        UThomasDomainsToolset::ApplyPCGAssetCreate(PCGInstanceCreatePlan.PlanId, true);
    TestTrue(
        TEXT("Native PCG Graph Instance creation/save succeeds"),
        PCGInstanceCreateApply.bOk);
    TestTrue(
        TEXT("Native PCG Graph Instance creation reports save"),
        PCGInstanceCreateApply.bSaved);

    FThomasSpecializedAssetInspectionResult PCGInstanceInspection =
        UThomasDomainsToolset::InspectPCGGraph(PCGInstancePath, 500);
    TestTrue(
        TEXT("Native PCG Graph Instance inspection succeeds"),
        PCGInstanceInspection.bOk);
    TestEqual(
        TEXT("Native PCG Graph Instance inspection reports its kind"),
        PCGInstanceInspection.AssetKind,
        FString(TEXT("pcg_graph_instance")));
    TestTrue(
        TEXT("Native PCG Graph Instance inspection reports its base interface"),
        PCGInstanceInspection.Metrics.ContainsByPredicate([PCGGraphPath](const FString& Metric)
        {
            return Metric.StartsWith(TEXT("BaseInterface="))
                && Metric.Contains(PCGGraphPath);
        }));
    TestTrue(
        TEXT("Native PCG Graph Instance initially inherits its parameter"),
        PCGInstanceInspection.Metrics.Contains(TEXT("OverrideCount=0")));

    FThomasPCGPatchRequest PCGInstanceNodeEditRequest;
    PCGInstanceNodeEditRequest.AssetPath = PCGInstancePath;
    PCGInstanceNodeEditRequest.ExpectedRevision = PCGInstanceInspection.Revision;
    PCGInstanceNodeEditRequest.Operations.Add(PCGAddExecutionNode);
    const FThomasPCGPlanResult PCGInstanceNodeEditPlan =
        UThomasDomainsToolset::PlanPCGPatch(PCGInstanceNodeEditRequest);
    TestFalse(
        TEXT("Native PCG Graph Instance refuses base node edits"),
        PCGInstanceNodeEditPlan.bOk);
    TestEqual(
        TEXT("Native PCG Graph Instance reports the node ownership gate"),
        PCGInstanceNodeEditPlan.Code,
        FString(TEXT("graph_instance_node_edit_denied")));

    FThomasPCGPatchRequest PCGOverrideRequest;
    PCGOverrideRequest.AssetPath = PCGInstancePath;
    PCGOverrideRequest.ExpectedRevision = PCGInstanceInspection.Revision;
    FThomasPCGOperation PCGSetOverride;
    PCGSetOverride.Action = TEXT("set_graph_parameter");
    PCGSetOverride.ParameterName = TEXT("SpawnDensity");
    PCGSetOverride.ExpectedValue = GetPCGParameterValue(PCGInstanceInspection);
    PCGSetOverride.Value = TEXT("0.75");
    PCGOverrideRequest.Operations.Add(PCGSetOverride);
    const FThomasPCGPlanResult PCGOverridePlan =
        UThomasDomainsToolset::PlanPCGPatch(PCGOverrideRequest);
    TestTrue(
        TEXT("Native PCG Graph Instance parameter override plan succeeds"),
        PCGOverridePlan.bOk);
    const FThomasPCGApplyResult PCGOverrideApply =
        UThomasDomainsToolset::ApplyPCGPlan(PCGOverridePlan.PlanId, true);
    TestTrue(
        TEXT("Native PCG Graph Instance parameter override apply/save succeeds"),
        PCGOverrideApply.bOk);
    PCGInstanceInspection = UThomasDomainsToolset::InspectPCGGraph(PCGInstancePath, 500);
    TestTrue(
        TEXT("Native PCG Graph Instance inspection reports one override"),
        PCGInstanceInspection.Metrics.Contains(TEXT("OverrideCount=1")));
    const FString PCGOverrideValue = GetPCGParameterValue(PCGInstanceInspection);
    TestFalse(
        TEXT("Native PCG Graph Instance inspection exposes the override value"),
        PCGOverrideValue.IsEmpty());

    FThomasPCGPatchRequest PCGResetOverrideRequest;
    PCGResetOverrideRequest.AssetPath = PCGInstancePath;
    PCGResetOverrideRequest.ExpectedRevision = PCGInstanceInspection.Revision;
    FThomasPCGOperation PCGResetOverride;
    PCGResetOverride.Action = TEXT("reset_graph_parameter_override");
    PCGResetOverride.ParameterName = TEXT("SpawnDensity");
    PCGResetOverride.ExpectedValue = PCGOverrideValue;
    PCGResetOverrideRequest.Operations.Add(PCGResetOverride);
    const FThomasPCGPlanResult PCGResetOverridePlan =
        UThomasDomainsToolset::PlanPCGPatch(PCGResetOverrideRequest);
    TestTrue(
        TEXT("Native PCG Graph Instance override reset plan succeeds"),
        PCGResetOverridePlan.bOk);
    const FThomasPCGApplyResult PCGResetOverrideApply =
        UThomasDomainsToolset::ApplyPCGPlan(PCGResetOverridePlan.PlanId, true);
    TestTrue(
        TEXT("Native PCG Graph Instance override reset apply/save succeeds"),
        PCGResetOverrideApply.bOk);
    PCGInstanceInspection = UThomasDomainsToolset::InspectPCGGraph(PCGInstancePath, 500);
    TestTrue(
        TEXT("Native PCG Graph Instance override reset restores inheritance"),
        PCGInstanceInspection.Metrics.Contains(TEXT("OverrideCount=0")));
    TestEqual(
        TEXT("Native PCG Graph Instance override reset restores the base value"),
        GetPCGParameterValue(PCGInstanceInspection),
        PCGBaseParameterValue);

    FThomasPCGExecutionPlanRequest PCGExecutionRequest;
    PCGExecutionRequest.AssetPath = PCGInstancePath;
    PCGExecutionRequest.ExpectedRevision = PCGInstanceInspection.Revision;
    PCGExecutionRequest.Seed = 31415;
    const FThomasPCGExecutionPlanResult PCGUnconfirmedExecutionPlan =
        UThomasDomainsToolset::PlanPCGExecution(PCGExecutionRequest);
    TestFalse(
        TEXT("Native standalone PCG execution refuses missing confirmation"),
        PCGUnconfirmedExecutionPlan.bOk);
    TestEqual(
        TEXT("Native standalone PCG execution reports confirmation gate"),
        PCGUnconfirmedExecutionPlan.Code,
        FString(TEXT("confirmation_required")));
    PCGExecutionRequest.bConfirmExecution = true;
    const FThomasPCGExecutionPlanResult PCGExecutionPlan =
        UThomasDomainsToolset::PlanPCGExecution(PCGExecutionRequest);
    TestTrue(
        TEXT("Native standalone PCG execution plan succeeds"),
        PCGExecutionPlan.bOk);
    TestEqual(
        TEXT("Native standalone PCG execution plan is R2"),
        PCGExecutionPlan.Risk,
        FString(TEXT("R2")));
    const FThomasPCGExecutionStartResult PCGExecutionStart =
        UThomasDomainsToolset::StartPCGExecution(PCGExecutionPlan.PlanId);
    TestTrue(
        TEXT("Native standalone PCG execution starts"),
        PCGExecutionStart.bOk);
    FThomasPCGExecutionStatusResult PCGExecutionStatus;
    for (int32 PollIndex = 0; PollIndex < 1000; ++PollIndex)
    {
        PCGExecutionStatus =
            UThomasDomainsToolset::GetPCGExecutionStatus(PCGExecutionStart.JobId);
        if (!PCGExecutionStatus.bOk || !PCGExecutionStatus.bRunning)
        {
            break;
        }
        FPlatformProcess::SleepNoStats(0.001f);
    }
    TestTrue(
        TEXT("Native standalone PCG execution status succeeds"),
        PCGExecutionStatus.bOk);
    TestEqual(
        TEXT("Native standalone PCG execution completes"),
        PCGExecutionStatus.Status,
        FString(TEXT("completed")));
    TestEqual(
        TEXT("Native standalone PCG execution preserves the requested seed"),
        PCGExecutionStatus.Seed,
        31415);
    TestTrue(
        TEXT("Native standalone PCG execution reports output data"),
        PCGExecutionStatus.TaggedDataCount >= 1);
    TestTrue(
        TEXT("Native standalone PCG execution reports bounded class/pin output summaries"),
        PCGExecutionStatus.Outputs.ContainsByPredicate([](const FString& Output)
        {
            return Output.Contains(TEXT("/Script/PCG.PCGParamData"))
                && Output.Contains(TEXT("Pin=Out"));
        }));
    const FThomasPCGExecutionCancelResult PCGCompletedCancel =
        UThomasDomainsToolset::CancelPCGExecution(PCGExecutionStart.JobId);
    TestFalse(
        TEXT("Native standalone PCG execution refuses cancellation after completion"),
        PCGCompletedCancel.bOk);
    TestEqual(
        TEXT("Native standalone PCG completed cancellation reports job_not_running"),
        PCGCompletedCancel.Code,
        FString(TEXT("job_not_running")));

    UObject* PCGInstanceAsset = LoadObject<UObject>(
        nullptr,
        *(PCGInstancePath + TEXT(".")
            + FPackageName::GetLongPackageAssetName(PCGInstancePath)));
    TestNotNull(
        TEXT("Temporary native PCG Graph Instance loaded for cleanup"),
        PCGInstanceAsset);
    if (PCGInstanceAsset)
    {
        TestEqual(
            TEXT("Temporary native PCG Graph Instance deleted through Unreal API"),
            ObjectTools::DeleteObjectsUnchecked({PCGInstanceAsset}),
            1);
    }
    TestFalse(
        TEXT("Temporary native PCG Graph Instance removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            PCGInstancePath, FPackageName::GetAssetPackageExtension())));

    PCGInspection = UThomasDomainsToolset::InspectPCGGraph(PCGGraphPath, 500);
    FThomasPCGPatchRequest PCGRemoveParameterRequest;
    PCGRemoveParameterRequest.AssetPath = PCGGraphPath;
    PCGRemoveParameterRequest.ExpectedRevision = PCGInspection.Revision;
    PCGRemoveParameterRequest.bConfirmDestructive = true;
    FThomasPCGOperation PCGRemoveParameter;
    PCGRemoveParameter.Action = TEXT("remove_graph_parameter");
    PCGRemoveParameter.ParameterName = TEXT("SpawnDensity");
    PCGRemoveParameterRequest.Operations.Add(PCGRemoveParameter);
    const FThomasPCGPlanResult PCGRemoveParameterPlan =
        UThomasDomainsToolset::PlanPCGPatch(PCGRemoveParameterRequest);
    TestTrue(
        TEXT("Native PCG graph parameter removal plan succeeds"),
        PCGRemoveParameterPlan.bOk);
    const FThomasPCGApplyResult PCGRemoveParameterApply =
        UThomasDomainsToolset::ApplyPCGPlan(PCGRemoveParameterPlan.PlanId, true);
    TestTrue(
        TEXT("Native PCG graph parameter removal apply/save succeeds"),
        PCGRemoveParameterApply.bOk);
    TestTrue(
        TEXT("Native PCG graph parameter removal leaves no parameter"),
        UThomasDomainsToolset::InspectPCGGraph(PCGGraphPath, 500)
            .Metrics.Contains(TEXT("ParameterCount=0")));

    UObject* PCGGraphAsset = LoadObject<UObject>(
        nullptr,
        *(PCGGraphPath + TEXT(".") + FPackageName::GetLongPackageAssetName(PCGGraphPath)));
    TestNotNull(TEXT("Temporary native PCG Graph loaded for cleanup"), PCGGraphAsset);
    if (PCGGraphAsset)
    {
        TestEqual(
            TEXT("Temporary native PCG Graph deleted through Unreal API"),
            ObjectTools::DeleteObjectsUnchecked({PCGGraphAsset}),
            1);
    }
    TestFalse(
        TEXT("Temporary native PCG Graph removed from disk"),
        IFileManager::Get().FileExists(*FPackageName::LongPackageNameToFilename(
            PCGGraphPath, FPackageName::GetAssetPackageExtension())));

    const FString PatchPackageName =
        TEXT("/Game/PropHunt/Tests/ThomasEditor/DT_TE_Details_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString PatchAssetName = FPackageName::GetLongPackageAssetName(PatchPackageName);
    UPackage* PatchPackage = CreatePackage(*PatchPackageName);
    UDataTable* PatchAsset = NewObject<UDataTable>(
        PatchPackage,
        *PatchAssetName,
        RF_Public | RF_Standalone | RF_Transactional);
    TestNotNull(TEXT("Temporary Details test asset created"), PatchAsset);
    if (PatchAsset)
    {
        TMap<FName, const uint8*> EmptyRows;
        PatchAsset->CreateTableFromRawData(EmptyRows, FTableRowBase::StaticStruct());
        FAssetRegistryModule::AssetCreated(PatchAsset);
        PatchPackage->MarkPackageDirty();
        const FString PatchFilename = FPackageName::LongPackageNameToFilename(
            PatchPackageName, FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        SaveArgs.Error = GError;
        TestTrue(
            TEXT("Temporary Details asset saved"),
            UPackage::SavePackage(PatchPackage, PatchAsset, *PatchFilename, SaveArgs));

        const FThomasObjectDetailsResult BeforePatch =
            UThomasAssetsToolset::GetObjectDetails(
                PatchAsset->GetPathName(), {TEXT("bStripFromClientBuilds")}, 10);
        TestTrue(TEXT("Details preflight inspection succeeds"), BeforePatch.bOk);
        FThomasObjectPatchRequest PatchRequest;
        PatchRequest.ObjectPath = PatchAsset->GetPathName();
        PatchRequest.ExpectedRevision = BeforePatch.Revision;
        FThomasObjectPatchOperation PatchOperation;
        PatchOperation.Name = TEXT("bStripFromClientBuilds");
        PatchOperation.ExpectedValue = BeforePatch.Properties[0].Value;
        PatchOperation.Value = BeforePatch.Properties[0].Value == TEXT("True")
            ? TEXT("False")
            : TEXT("True");
        PatchRequest.Operations.Add(PatchOperation);
        const FThomasObjectPatchPlanResult PatchPlan =
            UThomasAssetsToolset::PlanObjectPatch(PatchRequest);
        TestTrue(TEXT("Details patch plan succeeds"), PatchPlan.bOk);
        const FThomasObjectPatchApplyResult PatchApply =
            UThomasAssetsToolset::ApplyObjectPatch(PatchPlan.PlanId, false);
        TestTrue(TEXT("Details patch apply without implicit save succeeds"), PatchApply.bOk);
        TestFalse(TEXT("Details patch correctly reports no implicit save"), PatchApply.bSaved);
        TestTrue(TEXT("Details patch leaves the package dirty for explicit save"), PatchPackage->IsDirty());

        FThomasAssetSaveRequest ExplicitSaveRequest;
        FThomasAssetSaveItem ExplicitSaveItem;
        ExplicitSaveItem.AssetPath = PatchAsset->GetPathName();
        ExplicitSaveItem.ExpectedRevision = PatchApply.RevisionAfter;
        ExplicitSaveRequest.Items.Add(ExplicitSaveItem);
        const FThomasAssetSaveResult ExplicitSave =
            UThomasAssetsToolset::SaveAssets(ExplicitSaveRequest);
        TestTrue(TEXT("Explicit in-Editor save succeeds"), ExplicitSave.bOk);
        TestEqual(TEXT("Explicit in-Editor save reports one saved package"), ExplicitSave.SavedCount, 1);
        TestFalse(TEXT("Explicit in-Editor save clears the package dirty flag"), PatchPackage->IsDirty());

        const FThomasObjectDetailsResult AfterExplicitSave =
            UThomasAssetsToolset::GetObjectDetails(
                PatchAsset->GetPathName(), {TEXT("bStripFromClientBuilds")}, 10);
        PatchRequest.ExpectedRevision = AfterExplicitSave.Revision;
        PatchOperation.ExpectedValue = AfterExplicitSave.Properties[0].Value;
        PatchOperation.Value = BeforePatch.Properties[0].Value;
        PatchRequest.Operations = {PatchOperation};
        const FThomasObjectPatchPlanResult SecondPatchPlan =
            UThomasAssetsToolset::PlanObjectPatch(PatchRequest);
        TestTrue(TEXT("A fresh mutation can be planned after explicit save"), SecondPatchPlan.bOk);
        const FThomasObjectPatchApplyResult SecondPatchApply =
            UThomasAssetsToolset::ApplyObjectPatch(SecondPatchPlan.PlanId, false);
        TestTrue(TEXT("A second mutation applies after explicit save"), SecondPatchApply.bOk);
        ExplicitSaveRequest.Items[0].ExpectedRevision = SecondPatchApply.RevisionAfter;
        const FThomasAssetSaveResult SecondExplicitSave =
            UThomasAssetsToolset::SaveAssets(ExplicitSaveRequest);
        TestTrue(TEXT("The second explicit save succeeds without a consumed plan"), SecondExplicitSave.bOk);
        TestFalse(TEXT("The second explicit save clears dirty state"), PatchPackage->IsDirty());

        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({PatchFilename});

        TestEqual(
            TEXT("Temporary Details asset deleted through Unreal API"),
            ObjectTools::DeleteObjectsUnchecked({PatchAsset}),
            1);
        TestFalse(
            TEXT("Temporary Details asset removed from disk"),
            IFileManager::Get().FileExists(*PatchFilename));
    }

    return !HasAnyErrors();
}
#if PLATFORM_WINDOWS
#pragma warning(pop)
#endif

static void RunControlRigHierarchyMutationTests(
    FThomasEditorNativeCoreTest& Test,
    const FString& ControlRigPath,
    const FString& ExternalControlRigPath,
    const USkeletalMesh* ControlRigMesh,
    const int32 ExpectedControlRigBoneCount)
{
    auto ApplyControlRigOperationAndReload = [&Test, &ControlRigPath](
        const FString& Label,
        const FThomasAnimationOperation& Operation,
        const bool bConfirmDestructive)
    {
        const FThomasSpecializedAssetInspectionResult Before =
            UThomasDomainsToolset::InspectAnimationAsset(
                ControlRigPath, 1000);
        FThomasAnimationPatchRequest Request;
        Request.AssetPath = ControlRigPath;
        Request.ExpectedRevision = Before.Revision;
        Request.bConfirmDestructive = bConfirmDestructive;
        Request.Operations.Add(Operation);
        const FThomasAnimationPlanResult Plan =
            UThomasDomainsToolset::PlanAnimationPatch(Request);
        Test.TestTrue(Label + TEXT(" plan succeeds (code=")
                + Plan.Code + TEXT(" message=")
                + Plan.Message + TEXT(")"),
            Plan.bOk);
        if (!Plan.bOk)
        {
            return FThomasSpecializedAssetInspectionResult();
        }
        const FThomasAnimationApplyResult Apply =
            UThomasDomainsToolset::ApplyAnimationPlan(
                Plan.PlanId, true);
        Test.TestTrue(Label + TEXT(" apply saves (code=")
                + Apply.Code + TEXT(" message=")
                + Apply.Message + TEXT(")"),
            Apply.bOk && Apply.bSaved);
        if (!Apply.bOk || !Apply.bSaved)
        {
            return FThomasSpecializedAssetInspectionResult();
        }
        UObject* ReloadAsset = LoadObject<UObject>(
            nullptr,
            *(ControlRigPath + TEXT(".")
                + FPackageName::GetLongPackageAssetName(
                    ControlRigPath)));
        FText ReloadError;
        Test.TestTrue(Label + TEXT(" reloads from saved file"),
            ReloadAsset
                && UPackageTools::ReloadPackages(
                    {ReloadAsset->GetOutermost()},
                    ReloadError,
                    EReloadPackagesInteractionMode::AssumePositive));
        FThomasSpecializedAssetInspectionResult Inspection =
            UThomasDomainsToolset::InspectAnimationAsset(
                ControlRigPath, 1000);
        TArray<FString> TestElements;
        for (const FString& Item : Inspection.Items)
        {
            if (Item.Contains(TEXT("TE_")))
            {
                TestElements.Add(Item);
            }
        }
        Test.AddInfo(Label + TEXT(" readback metrics=")
            + FString::Join(Inspection.Metrics, TEXT(";"))
            + TEXT(" elements=")
            + FString::Join(TestElements, TEXT(";")));
        return Inspection;
    };

    const FString ControlRigRootBone =
        ControlRigMesh && ExpectedControlRigBoneCount > 0
            ? ControlRigMesh->GetRefSkeleton()
                .GetBoneName(0).ToString()
            : FString();
    const FString TestNullName = TEXT("TE_RootNull");
    const FString RenamedNullName =
        TEXT("TE_RootNullRenamed");
    auto GetNullInitialLocal = [](
        const FThomasSpecializedAssetInspectionResult& Inspection,
        const FString& NullName)
    {
        for (const FString& Item : Inspection.Items)
        {
            if (Item.Contains(TEXT("Name=") + NullName
                    + TEXT(" Type=Null")))
            {
                FString Prefix;
                FString Value;
                if (Item.Split(
                        TEXT(" InitialLocal="),
                        &Prefix,
                        &Value,
                        ESearchCase::CaseSensitive))
                {
                    return Value;
                }
            }
        }
        return FString();
    };
    const FTransform InitialNullTransform(
        FRotator(10.0, 20.0, 30.0),
        FVector(1.0, 2.0, 3.0),
        FVector(1.1, 1.2, 1.3));
    FThomasAnimationOperation AddNull;
    AddNull.Action = TEXT("add_control_rig_null");
    AddNull.Name = TestNullName;
    AddNull.ParentName = ControlRigRootBone;
    AddNull.Location = InitialNullTransform.GetLocation();
    AddNull.Rotation = InitialNullTransform.Rotator();
    AddNull.Scale = InitialNullTransform.GetScale3D();
    FThomasSpecializedAssetInspectionResult AfterNullAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig null add"), AddNull, false);
    const FString InitialNullReadback =
        GetNullInitialLocal(AfterNullAdd, TestNullName);
    Test.TestTrue(TEXT("Control Rig null survives save and reload"),
        AfterNullAdd.bOk
            && AfterNullAdd.Metrics.Contains(TEXT("NullCount=1"))
            && AfterNullAdd.Metrics.Contains(
                FString::Printf(TEXT("ElementCount=%d"),
                    ExpectedControlRigBoneCount + 1))
            && AfterNullAdd.Items.ContainsByPredicate(
                [&](const FString& Item)
                {
                    return Item.Contains(
                            TEXT("Name=") + TestNullName
                                + TEXT(" Type=Null"))
                        && Item.Contains(
                            TEXT("Parent=")
                                + ControlRigRootBone)
                        && Item.Contains(
                            TEXT("InitialLocal=1.000000,2.000000,3.000000"))
                        && Item.Contains(
                            TEXT("|1.100000,1.200000,1.300000"));
                }));
    Test.TestTrue(TEXT("Control Rig null exposes canonical saved transform"),
        !InitialNullReadback.IsEmpty());

    const FString TestCurveName = TEXT("TE_Curve");
    const FString RenamedCurveName =
        TEXT("TE_CurveRenamed");
    FThomasAnimationOperation AddCurve;
    AddCurve.Action = TEXT("add_control_rig_curve");
    AddCurve.Name = TestCurveName;
    AddCurve.Value = 0.25f;
    FThomasSpecializedAssetInspectionResult AfterCurveAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig curve add"), AddCurve, false);
    Test.TestTrue(TEXT("Control Rig curve survives save and reload"),
        AfterCurveAdd.bOk
            && AfterCurveAdd.Metrics.Contains(TEXT("NullCount=1"))
            && AfterCurveAdd.Metrics.Contains(TEXT("CurveCount=1"))
            && AfterCurveAdd.Items.ContainsByPredicate(
                [&](const FString& Item)
                {
                    return Item.Contains(
                            TEXT("Name=") + TestCurveName
                                + TEXT(" Type=Curve"))
                        && Item.Contains(TEXT("Value=0.25"));
                }));

    FThomasAnimationOperation RenameNull;
    RenameNull.Action = TEXT("rename_control_rig_null");
    RenameNull.Name = TestNullName;
    RenameNull.NewName = RenamedNullName;
    RenameNull.ExpectedValue = InitialNullReadback;
    FThomasAnimationPatchRequest UnconfirmedRenameRequest;
    UnconfirmedRenameRequest.AssetPath = ControlRigPath;
    UnconfirmedRenameRequest.ExpectedRevision =
        AfterCurveAdd.Revision;
    UnconfirmedRenameRequest.Operations.Add(RenameNull);
    Test.TestEqual(TEXT("Control Rig rename requires R2 confirmation"),
        UThomasDomainsToolset::PlanAnimationPatch(
            UnconfirmedRenameRequest).Code,
        FString(TEXT("confirmation_required")));
    FThomasSpecializedAssetInspectionResult AfterNullRename =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig null rename"), RenameNull, true);
    Test.TestTrue(TEXT("Control Rig null rename survives reload"),
        AfterNullRename.bOk
            && !AfterNullRename.Items.ContainsByPredicate(
                [&](const FString& Item)
                {
                    return Item.Contains(
                        TEXT("Name=") + TestNullName
                            + TEXT(" Type=Null"));
                })
            && AfterNullRename.Items.ContainsByPredicate(
                [&](const FString& Item)
                {
                    return Item.Contains(
                            TEXT("Name=") + RenamedNullName
                                + TEXT(" Type=Null"))
                        && Item.Contains(
                            TEXT("InitialLocal=")
                                + InitialNullReadback);
                }));

    FThomasAnimationOperation RenameCurve;
    RenameCurve.Action = TEXT("rename_control_rig_curve");
    RenameCurve.Name = TestCurveName;
    RenameCurve.NewName = RenamedCurveName;
    RenameCurve.ExpectedValue = TEXT("0.25");
    FThomasSpecializedAssetInspectionResult AfterCurveRename =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig curve rename"), RenameCurve, true);
    Test.TestTrue(TEXT("Control Rig curve rename survives reload"),
        AfterCurveRename.bOk
            && AfterCurveRename.Items.ContainsByPredicate(
                [&](const FString& Item)
                {
                    return Item.Contains(
                            TEXT("Name=") + RenamedCurveName
                                + TEXT(" Type=Curve"))
                        && Item.Contains(TEXT("Value=0.25"));
                }));

    FThomasAnimationOperation SetNullTransform;
    SetNullTransform.Action =
        TEXT("set_control_rig_null_transform");
    SetNullTransform.Name = RenamedNullName;
    SetNullTransform.ExpectedValue = TEXT("stale");
    SetNullTransform.Location = FVector(4.0, 5.0, 6.0);
    SetNullTransform.Rotation = FRotator(40.0, 50.0, 60.0);
    SetNullTransform.Scale = FVector(0.9, 0.8, 0.7);
    FThomasAnimationPatchRequest StaleNullRequest;
    StaleNullRequest.AssetPath = ControlRigPath;
    StaleNullRequest.ExpectedRevision = AfterCurveRename.Revision;
    StaleNullRequest.Operations.Add(SetNullTransform);
    Test.TestEqual(TEXT("Control Rig null rejects stale expected value"),
        UThomasDomainsToolset::PlanAnimationPatch(
            StaleNullRequest).Code,
        FString(TEXT("control_rig_value_conflict")));
    SetNullTransform.ExpectedValue =
        InitialNullReadback;
    FThomasSpecializedAssetInspectionResult AfterNullSet =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig null transform"),
            SetNullTransform,
            false);
    const FString UpdatedNullReadback =
        GetNullInitialLocal(AfterNullSet, RenamedNullName);
    Test.TestTrue(TEXT("Control Rig null transform survives reload"),
        AfterNullSet.bOk
            && AfterNullSet.Items.ContainsByPredicate(
                [&](const FString& Item)
                {
                        return Item.Contains(
                                TEXT("Name=") + RenamedNullName
                                    + TEXT(" Type=Null"))
                            && Item.Contains(
                                TEXT("InitialLocal=4.000000,5.000000,6.000000"))
                            && Item.Contains(
                                TEXT("|0.900000,0.800000,0.700000"));
                }));
    Test.TestTrue(TEXT("Control Rig null update exposes canonical saved transform"),
        !UpdatedNullReadback.IsEmpty());

    FThomasAnimationOperation SetCurveValue;
    SetCurveValue.Action = TEXT("set_control_rig_curve_value");
    SetCurveValue.Name = RenamedCurveName;
    SetCurveValue.ExpectedValue = TEXT("stale");
    SetCurveValue.Value = 0.75f;
    FThomasAnimationPatchRequest StaleCurveRequest;
    StaleCurveRequest.AssetPath = ControlRigPath;
    StaleCurveRequest.ExpectedRevision = AfterNullSet.Revision;
    StaleCurveRequest.Operations.Add(SetCurveValue);
    Test.TestEqual(TEXT("Control Rig curve rejects stale expected value"),
        UThomasDomainsToolset::PlanAnimationPatch(
            StaleCurveRequest).Code,
        FString(TEXT("control_rig_value_conflict")));
    SetCurveValue.ExpectedValue = TEXT("0.25");
    FThomasSpecializedAssetInspectionResult AfterCurveSet =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig curve value"),
            SetCurveValue,
            false);
    Test.TestTrue(TEXT("Control Rig curve value survives reload"),
        AfterCurveSet.bOk
            && AfterCurveSet.Items.ContainsByPredicate(
                [&](const FString& Item)
                {
                    return Item.Contains(
                            TEXT("Name=") + RenamedCurveName
                                + TEXT(" Type=Curve"))
                        && Item.Contains(TEXT("Value=0.75"));
                }));

    FThomasAnimationOperation RemoveCurve;
    RemoveCurve.Action = TEXT("remove_control_rig_curve");
    RemoveCurve.Name = RenamedCurveName;
    RemoveCurve.ExpectedValue = TEXT("0.75");
    FThomasSpecializedAssetInspectionResult AfterCurveRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig curve R2 remove"),
            RemoveCurve,
            true);
    Test.TestTrue(TEXT("Control Rig curve removal survives reload"),
        AfterCurveRemove.bOk
            && AfterCurveRemove.Metrics.Contains(
                TEXT("CurveCount=0")));

    FThomasAnimationOperation RemoveNull;
    RemoveNull.Action = TEXT("remove_control_rig_null");
    RemoveNull.Name = RenamedNullName;
    RemoveNull.ExpectedValue = UpdatedNullReadback;
    FThomasSpecializedAssetInspectionResult AfterNullRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig null R2 remove"),
            RemoveNull,
            true);
    Test.TestTrue(TEXT("Control Rig hierarchy cleanup survives reload"),
        AfterNullRemove.bOk
            && AfterNullRemove.Metrics.Contains(
                TEXT("NullCount=0"))
            && AfterNullRemove.Metrics.Contains(
                TEXT("CurveCount=0"))
            && AfterNullRemove.Metrics.Contains(
                FString::Printf(TEXT("ElementCount=%d"),
                    ExpectedControlRigBoneCount))
            && !AfterNullRemove.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.Contains(TEXT("TE_RootNull"))
                        || Item.Contains(TEXT("TE_Curve"));
                }));

    const FString FloatControlName = TEXT("TE_FloatControl");
    const FString RenamedFloatControlName =
        TEXT("TE_FloatControlRenamed");
    FThomasAnimationOperation AddFloatControl;
    AddFloatControl.Action =
        TEXT("add_control_rig_float_control");
    AddFloatControl.Name = FloatControlName;
    AddFloatControl.ParentName = ControlRigRootBone;
    AddFloatControl.Value = 0.5f;
    AddFloatControl.Color = FLinearColor(0.1f, 0.4f, 0.9f, 1.0f);
    FThomasSpecializedAssetInspectionResult AfterFloatControlAdd =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig float control add"),
            AddFloatControl,
            false);
    Test.TestTrue(TEXT("Control Rig float control survives reload"),
        AfterFloatControlAdd.bOk
            && AfterFloatControlAdd.Metrics.Contains(
                TEXT("ControlCount=1"))
            && AfterFloatControlAdd.Metrics.Contains(
                FString::Printf(TEXT("ElementCount=%d"),
                    ExpectedControlRigBoneCount + 1))
            && AfterFloatControlAdd.Items.ContainsByPredicate(
                [&](const FString& Item)
                {
                    return Item.Contains(
                            TEXT("Name=") + FloatControlName
                                + TEXT(" Type=Control"))
                        && Item.Contains(
                            TEXT("Parent=") + ControlRigRootBone)
                        && Item.Contains(TEXT("ControlType=Float"))
                        && Item.Contains(TEXT("InitialValue=0.5"))
                        && Item.Contains(TEXT("CurrentValue=0.5"));
                }));

    FThomasAnimationOperation RenameFloatControl;
    RenameFloatControl.Action =
        TEXT("rename_control_rig_float_control");
    RenameFloatControl.Name = FloatControlName;
    RenameFloatControl.NewName = RenamedFloatControlName;
    RenameFloatControl.ExpectedValue = TEXT("0.5");
    FThomasSpecializedAssetInspectionResult AfterFloatControlRename =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig float control rename"),
            RenameFloatControl,
            true);
    Test.TestTrue(TEXT("Control Rig float control rename survives reload"),
        AfterFloatControlRename.bOk
            && AfterFloatControlRename.Items.ContainsByPredicate(
                [&](const FString& Item)
                {
                    return Item.Contains(
                            TEXT("Name=") + RenamedFloatControlName
                                + TEXT(" Type=Control"))
                        && Item.Contains(TEXT("ControlType=Float"))
                        && Item.Contains(TEXT("InitialValue=0.5"));
                }));

    FThomasAnimationOperation SetFloatControlValue;
    SetFloatControlValue.Action =
        TEXT("set_control_rig_float_value");
    SetFloatControlValue.Name = RenamedFloatControlName;
    SetFloatControlValue.ExpectedValue = TEXT("stale");
    SetFloatControlValue.Value = 0.8f;
    FThomasAnimationPatchRequest StaleFloatControlRequest;
    StaleFloatControlRequest.AssetPath = ControlRigPath;
    StaleFloatControlRequest.ExpectedRevision =
        AfterFloatControlRename.Revision;
    StaleFloatControlRequest.Operations.Add(SetFloatControlValue);
    Test.TestEqual(
        TEXT("Control Rig float control rejects stale expected value"),
        UThomasDomainsToolset::PlanAnimationPatch(
            StaleFloatControlRequest).Code,
        FString(TEXT("control_rig_value_conflict")));
    SetFloatControlValue.ExpectedValue = TEXT("0.5");
    FThomasSpecializedAssetInspectionResult AfterFloatControlSet =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig float control value"),
            SetFloatControlValue,
            false);
    Test.TestTrue(TEXT("Control Rig float control value survives reload"),
        AfterFloatControlSet.bOk
            && AfterFloatControlSet.Items.ContainsByPredicate(
                [&](const FString& Item)
                {
                    return Item.Contains(
                            TEXT("Name=") + RenamedFloatControlName
                                + TEXT(" Type=Control"))
                        && Item.Contains(TEXT("InitialValue=0.8"))
                        && Item.Contains(TEXT("CurrentValue=0.8"));
                }));

    FThomasAnimationOperation RemoveFloatControl;
    RemoveFloatControl.Action =
        TEXT("remove_control_rig_float_control");
    RemoveFloatControl.Name = RenamedFloatControlName;
    RemoveFloatControl.ExpectedValue = TEXT("0.8");
    FThomasSpecializedAssetInspectionResult AfterFloatControlRemove =
        ApplyControlRigOperationAndReload(
            TEXT("Control Rig float control R2 remove"),
            RemoveFloatControl,
            true);
    Test.TestTrue(TEXT("Control Rig float control cleanup survives reload"),
        AfterFloatControlRemove.bOk
            && AfterFloatControlRemove.Metrics.Contains(
                TEXT("ControlCount=0"))
            && AfterFloatControlRemove.Metrics.Contains(
                FString::Printf(TEXT("ElementCount=%d"),
                    ExpectedControlRigBoneCount))
            && !AfterFloatControlRemove.Items.ContainsByPredicate(
                [](const FString& Item)
                {
                    return Item.Contains(TEXT("TE_FloatControl"));
                }));

    RunControlRigTypedControlMutationTests(
        Test,
        ControlRigPath,
        ControlRigRootBone,
        ExpectedControlRigBoneCount,
        ApplyControlRigOperationAndReload);
    RunControlRigSocketAndSpaceMutationTests(
        Test,
        ControlRigRootBone,
        ExpectedControlRigBoneCount,
        ApplyControlRigOperationAndReload);
    RunControlRigMetadataMutationTests(
        Test,
        ControlRigPath,
        ControlRigRootBone,
        ApplyControlRigOperationAndReload);
    RunControlRigGraphMutationTests(
        Test,
        ControlRigPath,
        ExternalControlRigPath,
        ApplyControlRigOperationAndReload);
}

#endif
