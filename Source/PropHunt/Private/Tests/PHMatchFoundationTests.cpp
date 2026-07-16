#include "Misc/AutomationTest.h"

#include "Characters/PHHunterCharacter.h"
#include "Characters/PHPropCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Game/PHMatchRulesDataAsset.h"
#include "Game/PHMatchTypes.h"
#include "Gameplay/Transformation/PHPropFormDataAsset.h"
#include "Gameplay/Capture/PHCaptureTypes.h"
#include "Gameplay/Capture/PHRetentionPoint.h"
#include "Gameplay/Characters/PHHumanPrototypeSelector.h"
#include "Gameplay/Physics/PHPhysicsPropDataAsset.h"
#include "Gameplay/Physics/PHPhysicsPropPrototype.h"
#include "Gameplay/Objectives/PHObjectiveActor.h"
#include "Gameplay/PHCollisionChannels.h"
#include "PhysicsEngine/BodySetup.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerStart.h"
#include "InputCoreTypes.h"
#include "UI/PHHumanStaminaWidget.h"
#include "UObject/SoftObjectPath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
bool HasAxisMapping(const UInputSettings& InputSettings, const FName AxisName, const FKey Key, const float Scale)
{
	return InputSettings.GetAxisMappings().ContainsByPredicate(
		[AxisName, Key, Scale](const FInputAxisKeyMapping& Mapping)
		{
			return Mapping.AxisName == AxisName
				&& Mapping.Key == Key
				&& FMath::IsNearlyEqual(Mapping.Scale, Scale);
		});
}

bool HasActionMapping(const UInputSettings& InputSettings, const FName ActionName, const FKey Key)
{
	return InputSettings.GetActionMappings().ContainsByPredicate(
		[ActionName, Key](const FInputActionKeyMapping& Mapping)
		{
			return Mapping.ActionName == ActionName && Mapping.Key == Key;
		});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHKeyboardLayoutMappingsTest,
	"PropHunt.Character.Input.KeyboardLayouts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHKeyboardLayoutMappingsTest::RunTest(const FString& Parameters)
{
	const UInputSettings* InputSettings = GetDefault<UInputSettings>();
	TestNotNull(TEXT("Input settings exist"), InputSettings);
	if (InputSettings == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("QWERTY forward uses W"), HasAxisMapping(*InputSettings, TEXT("MoveForward"), EKeys::W, 1.0f));
	TestTrue(TEXT("AZERTY forward uses Z"), HasAxisMapping(*InputSettings, TEXT("MoveForward"), EKeys::Z, 1.0f));
	TestTrue(TEXT("Backward uses S"), HasAxisMapping(*InputSettings, TEXT("MoveForward"), EKeys::S, -1.0f));
	TestTrue(TEXT("Right uses D"), HasAxisMapping(*InputSettings, TEXT("MoveRight"), EKeys::D, 1.0f));
	TestTrue(TEXT("QWERTY left uses A"), HasAxisMapping(*InputSettings, TEXT("MoveRight"), EKeys::A, -1.0f));
	TestTrue(TEXT("AZERTY left uses Q"), HasAxisMapping(*InputSettings, TEXT("MoveRight"), EKeys::Q, -1.0f));
	TestTrue(TEXT("Gamepad forward uses the left stick"), HasAxisMapping(*InputSettings, TEXT("MoveForward"), EKeys::Gamepad_LeftY, 1.0f));
	TestTrue(TEXT("Gamepad lateral movement uses the left stick"), HasAxisMapping(*InputSettings, TEXT("MoveRight"), EKeys::Gamepad_LeftX, 1.0f));
	TestTrue(TEXT("Survivor contextual interaction uses left click"),
		HasActionMapping(*InputSettings, TEXT("SurvivorInteract"), EKeys::LeftMouseButton));
	TestTrue(TEXT("Survivor contextual interaction has a gamepad trigger"),
		HasActionMapping(*InputSettings, TEXT("SurvivorInteract"), EKeys::Gamepad_RightTrigger));
	TestTrue(TEXT("Hunter contextual interaction uses E"),
		HasActionMapping(*InputSettings, TEXT("HunterInteract"), EKeys::E));
	TestTrue(TEXT("Human sprint uses left shift"), HasActionMapping(*InputSettings, TEXT("SprintHuman"), EKeys::LeftShift));
	TestTrue(TEXT("Human prototype selector uses T"), HasActionMapping(*InputSettings, TEXT("SwitchHumanPrototype"), EKeys::T));
	TestTrue(TEXT("Human prototype selector has a gamepad mapping"),
		HasActionMapping(*InputSettings, TEXT("SwitchHumanPrototype"), EKeys::Gamepad_FaceButton_Top));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHCaptureFlowDefaultsTest,
	"PropHunt.Capture.Graybox.Flow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHCaptureFlowDefaultsTest::RunTest(const FString& Parameters)
{
	const APHPropCharacter* PropDefaults = GetDefault<APHPropCharacter>();
	const APHHunterCharacter* HunterDefaults = GetDefault<APHHunterCharacter>();
	const APHRetentionPoint* PointDefaults = GetDefault<APHRetentionPoint>();
	const APHHumanPrototypeSelector* SelectorDefaults = GetDefault<APHHumanPrototypeSelector>();
	const UPHHumanStaminaWidget* StaminaWidgetDefaults = GetDefault<UPHHumanStaminaWidget>();
	TestNotNull(TEXT("Prop defaults exist"), PropDefaults);
	TestNotNull(TEXT("Hunter defaults exist"), HunterDefaults);
	TestNotNull(TEXT("Retention point defaults exist"), PointDefaults);
	TestNotNull(TEXT("Human prototype selector defaults exist"), SelectorDefaults);
	TestNotNull(TEXT("Native human stamina widget exists"), StaminaWidgetDefaults);
	if (PropDefaults == nullptr || HunterDefaults == nullptr || PointDefaults == nullptr
		|| SelectorDefaults == nullptr || StaminaWidgetDefaults == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Prop starts free"), PropDefaults->GetCaptureState(), EPHPropCaptureState::Free);
	TestEqual(TEXT("Prop starts with no retention"), PropDefaults->GetRetentionCount(), 0);
	TestEqual(TEXT("First retention graybox duration is exposed"), PropDefaults->GetFirstRetentionDuration(), 30.0f);
	TestEqual(TEXT("Second retention graybox duration is shorter"), PropDefaults->GetSecondRetentionDuration(), 20.0f);
	TestEqual(TEXT("Grace duration is exposed"), PropDefaults->GetGraceDuration(), 5.0f);
	TestEqual(TEXT("Downed crawl speed is deliberately slow"), PropDefaults->GetDownedCrawlSpeed(), 150.0f);
	TestEqual(TEXT("Downed self recovery starts at thirty seconds"), PropDefaults->GetDownedRecoveryDuration(), 30.0f);
	TestEqual(TEXT("Carry struggle requires deliberate alternations"), PropDefaults->GetCarryStruggleRequiredAlternations(), 24);
	TestEqual(TEXT("Carry struggle produces a visible lateral step"), PropDefaults->GetCarryStruggleHunterShoveDistance(), 90.0f);
	TestEqual(TEXT("Lateral struggle step is spread over a short readable duration"), HunterDefaults->GetCarryStruggleShoveDuration(), 0.35f);
	TestEqual(TEXT("Hunter capture interaction range is exposed"), HunterDefaults->GetCaptureInteractionDistance(), 225.0f);
	TestEqual(TEXT("Character capsules do not collapse third-person cameras"),
		HunterDefaults->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Camera), ECR_Ignore);
	TestEqual(TEXT("Survivor capsules do not collapse third-person cameras"),
		PropDefaults->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Camera), ECR_Ignore);
	TestEqual(TEXT("Synchronized carry pair shares the Hunter root origin"),
		HunterDefaults->GetCarryAnchorOffset(), FVector::ZeroVector);
	TestTrue(TEXT("Hunter starts in FPS when not carrying"), !HunterDefaults->IsUsingCarryThirdPersonCamera());
	TestTrue(TEXT("Hunter carry camera is a real shoulder TPS view"), HunterDefaults->GetCarryCameraArmLength() >= 250.0f);
	TestTrue(TEXT("Survivor starts in human FPS"), !PropDefaults->IsUsingPropThirdPersonCamera());
	TestEqual(TEXT("Human stamina starts with a readable full-scale maximum"), PropDefaults->GetMaximumHumanStamina(), 100.0f);
	TestTrue(TEXT("Human sprint is faster than normal movement"), PropDefaults->GetHumanSprintSpeed() > 500.0f);
	TestTrue(TEXT("Human sprint drains faster than it recharges"),
		PropDefaults->GetHumanSprintDrainPerSecond() > PropDefaults->GetHumanStaminaRechargePerSecond());
	TestEqual(TEXT("Human jump consumes part of the stamina bar"), PropDefaults->GetHumanJumpStaminaCost(), 20.0f);
	TestEqual(TEXT("Retention point interaction range is exposed"), PointDefaults->GetInteractionDistance(), 225.0f);
	TestEqual(TEXT("Retention rescue requires a short hold"), PointDefaults->GetReleaseDuration(), 2.0f);
	TestEqual(TEXT("Retention rescue starts empty"), PointDefaults->GetReleaseProgressNormalized(), 0.0f);
	TestNull(TEXT("Retention rescue starts without a rescuer"), PointDefaults->GetReleaseRescuer());
	TestTrue(TEXT("Human prototype selector replicates"), SelectorDefaults->GetIsReplicated());
	TestEqual(TEXT("Human prototype selector is reachable near the authored starts"), SelectorDefaults->GetInteractionDistance(), 350.0f);
	TestTrue(TEXT("A valid melee hit downs a free Prop"),
		PHCaptureFlow::CanMeleeHitDown(EPHPropCaptureState::Free));
	TestFalse(TEXT("Grace prevents an immediate melee redown"),
		PHCaptureFlow::CanMeleeHitDown(EPHPropCaptureState::Grace));
	TestFalse(TEXT("An already downed Prop cannot be downed twice"),
		PHCaptureFlow::CanMeleeHitDown(EPHPropCaptureState::Downed));
	TestFalse(TEXT("Second retention is not immediate elimination"),
		PHCaptureFlow::ShouldEliminateOnRetention(2, 3));
	TestTrue(TEXT("Third retention eliminates in the graybox rule"),
		PHCaptureFlow::ShouldEliminateOnRetention(3, 3));
	TestEqual(TEXT("First retention uses the normal timer"),
		PHCaptureFlow::GetRetentionDuration(1, 30.0f, 20.0f), 30.0f);
	TestEqual(TEXT("Second retention uses the shorter timer"),
		PHCaptureFlow::GetRetentionDuration(2, 30.0f, 20.0f), 20.0f);
	const float PickupMinimumAimDot = FMath::Cos(FMath::DegreesToRadians(35.0f));
	TestTrue(TEXT("A downed target below the camera remains selectable when horizontally in front"),
		PHCaptureFlow::IsDownedPickupDirectionAllowed(
			FVector::ForwardVector,
			FVector(75.0f, 0.0f, -130.0f),
			PickupMinimumAimDot));
	TestFalse(TEXT("A close downed target behind the Hunter is not selected"),
		PHCaptureFlow::IsDownedPickupDirectionAllowed(
			FVector::ForwardVector,
			FVector(-35.0f, 0.0f, -130.0f),
			PickupMinimumAimDot));
	TestTrue(TEXT("A downed target directly beneath the Hunter remains selectable"),
		PHCaptureFlow::IsDownedPickupDirectionAllowed(
			FVector::ForwardVector,
			FVector(0.0f, 0.0f, -130.0f),
			PickupMinimumAimDot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHPropnightReferenceAssetsTest,
	"PropHunt.Character.PropnightReferenceAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHPropnightReferenceAssetsTest::RunTest(const FString& Parameters)
{
	const USkeletalMesh* IsaacMesh = Cast<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/PropHunt/Characters/Survivor/Prototype/PropnightReference/Isaac/SK_PH_Survivor_Isaac_Ref.SK_PH_Survivor_Isaac_Ref")).TryLoad());
	const USkeletalMesh* JunMesh = Cast<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/PropHunt/Characters/Survivor/Prototype/PropnightReference/Jun/SK_PH_Survivor_Jun_Ref.SK_PH_Survivor_Jun_Ref")).TryLoad());
	const USkeletalMesh* SamuraiMesh = Cast<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/PropHunt/Characters/Hunter/Prototype/PropnightReference/Samurai/SK_PH_Hunter_Samurai_Ref.SK_PH_Hunter_Samurai_Ref")).TryLoad());
	const USkeletalMesh* MaddyMesh = Cast<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/PropHunt/Characters/Hunter/Prototype/PropnightReference/Maddy/SK_PH_Hunter_Maddy_Ref.SK_PH_Hunter_Maddy_Ref")).TryLoad());
	const UAnimSequence* JunCrawl = Cast<UAnimSequence>(FSoftObjectPath(
		TEXT("/Game/PropHunt/Characters/Survivor/Prototype/PropnightReference/Jun/A_PH_Jun_Crawl_Forward.A_PH_Jun_Crawl_Forward")).TryLoad());
	const UAnimSequence* MaddyCarry = Cast<UAnimSequence>(FSoftObjectPath(
		TEXT("/Game/PropHunt/Characters/Hunter/Prototype/PropnightReference/Maddy/A_PH_Maddy_Carry_Walk.A_PH_Maddy_Carry_Walk")).TryLoad());
	const UClass* PropBlueprintClass = FSoftClassPath(
		TEXT("/Game/PropHunt/Characters/Props/BP_PH_PropCharacter.BP_PH_PropCharacter_C"))
		.TryLoadClass<APHPropCharacter>();
	const APHPropCharacter* ConfiguredPropDefaults = PropBlueprintClass != nullptr
		? Cast<APHPropCharacter>(PropBlueprintClass->GetDefaultObject())
		: nullptr;

	TestNotNull(TEXT("Isaac reference mesh exists"), IsaacMesh);
	TestNotNull(TEXT("Jun reference mesh exists"), JunMesh);
	TestNotNull(TEXT("Samurai reference mesh exists"), SamuraiMesh);
	TestNotNull(TEXT("Maddy reference mesh exists"), MaddyMesh);
	TestNotNull(TEXT("Jun crawl animation exists"), JunCrawl);
	TestNotNull(TEXT("Maddy carry animation exists"), MaddyCarry);
	TestNotNull(TEXT("Configured Prop Blueprint class exists"), PropBlueprintClass);
	TestNotNull(TEXT("Configured Prop Blueprint defaults exist"), ConfiguredPropDefaults);
	if (IsaacMesh == nullptr || JunMesh == nullptr || SamuraiMesh == nullptr || MaddyMesh == nullptr)
	{
		return false;
	}

	TestNotEqual(TEXT("Jun and Isaac retain distinct source Skeletons"),
		JunMesh->GetSkeleton(), IsaacMesh->GetSkeleton());
	TestNotEqual(TEXT("Maddy and Samurai retain distinct source Skeletons"),
		MaddyMesh->GetSkeleton(), SamuraiMesh->GetSkeleton());
	if (JunCrawl != nullptr)
	{
		TestTrue(TEXT("Jun crawl uses the Jun Skeleton"),
			JunCrawl->GetSkeleton() == JunMesh->GetSkeleton());
	}
	if (MaddyCarry != nullptr)
	{
		TestTrue(TEXT("Maddy carry uses the Maddy Skeleton"),
			MaddyCarry->GetSkeleton() == MaddyMesh->GetSkeleton());
	}
	if (ConfiguredPropDefaults != nullptr)
	{
		TestTrue(TEXT("Configured Prop exposes the alternative Jun prototype"),
			ConfiguredPropDefaults->HasAlternativeHumanPrototype());
		TestEqual(TEXT("Configured Prop starts on Isaac"),
			ConfiguredPropDefaults->GetActiveHumanPrototypeName(), FName(TEXT("Isaac")));
		TestNotNull(TEXT("Isaac has a synchronized carried move animation"),
			ConfiguredPropDefaults->GetPrimaryHumanCarriedMoveAnimation());
		TestNotNull(TEXT("Jun has a carried move animation ready for its future paired Hunter"),
			ConfiguredPropDefaults->GetAlternativeHumanCarriedMoveAnimation());
		TestEqual(TEXT("Isaac synchronized pair shares the Hunter actor origin"),
			ConfiguredPropDefaults->GetPrimaryCarryAttachmentOffset(), FVector::ZeroVector);
		TestEqual(TEXT("Jun paired animation also starts from the shared actor origin"),
			ConfiguredPropDefaults->GetAlternativeCarryAttachmentOffset(), FVector::ZeroVector);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHCaptureAuthoredMapTest,
	"PropHunt.Capture.Graybox.AuthoredMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHCaptureAuthoredMapTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath MapPath(TEXT("/Game/PropHunt/Tests/L_PH_CaptureTest.L_PH_CaptureTest"));
	const UWorld* CaptureWorld = Cast<UWorld>(MapPath.TryLoad());
	TestNotNull(TEXT("Capture test map exists"), CaptureWorld);
	if (CaptureWorld == nullptr || CaptureWorld->PersistentLevel == nullptr)
	{
		return false;
	}

	int32 ObjectiveCount = 0;
	int32 RetentionPointCount = 0;
	int32 PlayerStartCount = 0;
	int32 HumanPrototypeSelectorCount = 0;
	FVector HumanPrototypeSelectorLocation = FVector::ZeroVector;
	TArray<FVector> PlayerStartLocations;
	for (const AActor* Actor : CaptureWorld->PersistentLevel->Actors)
	{
		ObjectiveCount += IsValid(Actor) && Actor->IsA<APHObjectiveActor>() ? 1 : 0;
		RetentionPointCount += IsValid(Actor) && Actor->IsA<APHRetentionPoint>() ? 1 : 0;
		PlayerStartCount += IsValid(Actor) && Actor->IsA<APlayerStart>() ? 1 : 0;
		HumanPrototypeSelectorCount += IsValid(Actor) && Actor->IsA<APHHumanPrototypeSelector>() ? 1 : 0;
		if (IsValid(Actor) && Actor->IsA<APlayerStart>())
		{
			PlayerStartLocations.Add(Actor->GetActorLocation());
		}
		if (IsValid(Actor) && Actor->IsA<APHHumanPrototypeSelector>())
		{
			HumanPrototypeSelectorLocation = Actor->GetActorLocation();
		}
	}

	TestEqual(TEXT("Five authored objectives keep normal match rules valid"), ObjectiveCount, 5);
	TestEqual(TEXT("One authored retention point is present"), RetentionPointCount, 1);
	TestEqual(TEXT("Hunter, victim and rescuer have authored starts"), PlayerStartCount, 3);
	TestEqual(TEXT("One authored Isaac and Jun selector is present"), HumanPrototypeSelectorCount, 1);
	if (HumanPrototypeSelectorCount == 1 && PlayerStartLocations.Num() > 0)
	{
		float ClosestStartDistanceSquared = MAX_flt;
		for (const FVector& PlayerStartLocation : PlayerStartLocations)
		{
			ClosestStartDistanceSquared = FMath::Min(
				ClosestStartDistanceSquared,
				FVector::DistSquared(PlayerStartLocation, HumanPrototypeSelectorLocation));
		}
		TestTrue(TEXT("Isaac and Jun selector is within interaction range of a player start"),
			ClosestStartDistanceSquared <= FMath::Square(350.0f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHObjectiveDefaultsTest,
	"PropHunt.Objective.Graybox.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHObjectiveDefaultsTest::RunTest(const FString& Parameters)
{
	const APHObjectiveActor* ObjectiveDefaults = GetDefault<APHObjectiveActor>();
	TestNotNull(TEXT("Objective class defaults exist"), ObjectiveDefaults);
	if (ObjectiveDefaults == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Objective actor replicates"), ObjectiveDefaults->GetIsReplicated());
	TestEqual(TEXT("Graybox objective duration is exposed"), ObjectiveDefaults->GetInteractionDurationSeconds(), 8.0f);
	TestEqual(TEXT("Graybox objective range is exposed"), ObjectiveDefaults->GetInteractionDistance(), 225.0f);
	TestEqual(TEXT("First objective preserves partial progress"), ObjectiveDefaults->GetInterruptionPolicy(), EPHObjectiveInterruptionPolicy::Preserve);
	TestEqual(TEXT("Objective starts with no progress"), ObjectiveDefaults->GetObjectiveProgress(), 0.0f);
	TestFalse(TEXT("Objective CDO is not completed"), ObjectiveDefaults->IsObjectiveCompleted());
	TestEqual(TEXT("Two Props contribute one and a half workers by default"),
		PHObjectiveFlow::GetContributionMultiplier(2, 0.5f), 1.5f);
	TestEqual(TEXT("One Prop advances one eighth per second"),
		PHObjectiveFlow::AdvanceProgress(0.0f, 1.0f, 8.0f, 1, 0.5f), 0.125f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHObjectiveAuthoredMapTest,
	"PropHunt.Objective.Graybox.AuthoredMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHObjectiveAuthoredMapTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath MapPath(TEXT("/Game/PropHunt/Tests/L_PH_ObjectiveTest.L_PH_ObjectiveTest"));
	const UWorld* ObjectiveWorld = Cast<UWorld>(MapPath.TryLoad());
	TestNotNull(TEXT("Objective test map exists"), ObjectiveWorld);
	if (ObjectiveWorld == nullptr || ObjectiveWorld->PersistentLevel == nullptr)
	{
		return false;
	}

	int32 ObjectiveCount = 0;
	int32 PlayerStartCount = 0;
	for (const AActor* Actor : ObjectiveWorld->PersistentLevel->Actors)
	{
		ObjectiveCount += IsValid(Actor) && Actor->IsA<APHObjectiveActor>() ? 1 : 0;
		PlayerStartCount += IsValid(Actor) && Actor->IsA<APlayerStart>() ? 1 : 0;
	}

	TestEqual(TEXT("Five authored objective anchors are present"), ObjectiveCount, 5);
	TestEqual(TEXT("Hunter and Prop have separate authored starts"), PlayerStartCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHHunterMeleeDefaultsTest,
	"PropHunt.Character.Melee.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHHunterMeleeDefaultsTest::RunTest(const FString& Parameters)
{
	const APHHunterCharacter* HunterDefaults = GetDefault<APHHunterCharacter>();
	TestNotNull(TEXT("Hunter class defaults exist"), HunterDefaults);
	if (HunterDefaults == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Melee range starts at the graybox value"), HunterDefaults->GetMeleeRange(), 175.0f);
	TestEqual(TEXT("Melee box width starts at the tolerant graybox value"), HunterDefaults->GetMeleeWidth(), 90.0f);
	TestEqual(TEXT("Melee vertical tolerance starts at the graybox value"), HunterDefaults->GetMeleeVerticalTolerance(), 70.0f);
	TestEqual(TEXT("Melee recovery starts at the graybox value"), HunterDefaults->GetMeleeRecoverySeconds(), 0.8f);
	TestEqual(TEXT("Hunter movement capsule ignores other Pawns"),
		HunterDefaults->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
	TestEqual(TEXT("Hunter movement capsule ignores Prop query hitboxes"),
		HunterDefaults->GetCapsuleComponent()->GetCollisionResponseToChannel(PHCollision::PropHitbox), ECR_Ignore);
	TestEqual(TEXT("Hunter movement capsule cannot become an invisible step"),
		HunterDefaults->GetCapsuleComponent()->CanCharacterStepUpOn, ECB_No);
	TestEqual(TEXT("No attack has been confirmed on the CDO"), HunterDefaults->GetMeleeAttackSequence(), 0);
	TestFalse(TEXT("The CDO does not report a hit"), HunterDefaults->DidLastMeleeAttackHitProp());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHPropTransformationDefaultsTest,
	"PropHunt.Character.Transformation.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHPropTransformationDefaultsTest::RunTest(const FString& Parameters)
{
	const APHPropCharacter* PropDefaults = GetDefault<APHPropCharacter>();
	TestNotNull(TEXT("Prop class defaults exist"), PropDefaults);
	if (PropDefaults == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Transformation range starts at the graybox value"), PropDefaults->GetTransformationDistance(), 400.0f);
	TestEqual(TEXT("Transformation cooldown is exposed with its graybox value"), PropDefaults->GetTransformationCooldownSeconds(), 1.0f);
	TestEqual(TEXT("Transformation targeting cone starts at the TPS graybox value"), PropDefaults->GetTransformationTargetingHalfAngleDegrees(), 12.0f);
	TestEqual(TEXT("Server placement clearance is bounded to 25 cm by default"), PropDefaults->GetMaximumPlacementAdjustment(), 25.0f);
	TestEqual(TEXT("Prop movement capsule ignores other Pawns"),
		PropDefaults->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
	TestEqual(TEXT("Prop movement capsule no longer answers melee visibility traces"),
		PropDefaults->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Visibility), ECR_Ignore);
	TestEqual(TEXT("Prop movement capsule cannot become an invisible step"),
		PropDefaults->GetCapsuleComponent()->CanCharacterStepUpOn, ECB_No);
	TestEqual(TEXT("Native CDO does not silently authorize forms"), PropDefaults->GetAllowedPropFormCount(), 0);
	TestEqual(TEXT("No transformation result exists on the CDO"), PropDefaults->GetTransformationSequence(), 0);
	TestEqual(TEXT("No form is active on the CDO"), PropDefaults->GetActivePropForm(), static_cast<const UPHPropFormDataAsset*>(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHPropTransformationAssetsTest,
	"PropHunt.Character.Transformation.AuthoredForms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHPropTransformationAssetsTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath CratePath(TEXT("/Game/PropHunt/Data/Transformation/DA_PH_PropForm_Crate_Test.DA_PH_PropForm_Crate_Test"));
	const FSoftObjectPath BarrelPath(TEXT("/Game/PropHunt/Data/Transformation/DA_PH_PropForm_Barrel_Test.DA_PH_PropForm_Barrel_Test"));
	const UPHPropFormDataAsset* Crate = Cast<UPHPropFormDataAsset>(CratePath.TryLoad());
	const UPHPropFormDataAsset* Barrel = Cast<UPHPropFormDataAsset>(BarrelPath.TryLoad());
	TestNotNull(TEXT("Authored crate form exists"), Crate);
	TestNotNull(TEXT("Authored barrel form exists"), Barrel);
	if (Crate == nullptr || Barrel == nullptr)
	{
		return false;
	}

	FText ValidationError;
	TestTrue(TEXT("Crate form is valid and references a production cooked mesh"), Crate->HasValidDefinition(&ValidationError));
	if (!ValidationError.IsEmpty())
	{
		AddError(ValidationError.ToString());
	}
	ValidationError = FText::GetEmpty();
	TestTrue(TEXT("Barrel form is valid and references a production cooked mesh"), Barrel->HasValidDefinition(&ValidationError));
	if (!ValidationError.IsEmpty())
	{
		AddError(ValidationError.ToString());
	}
	TestNotEqual(TEXT("The two test forms have distinct stable identifiers"), Crate->FormId, Barrel->FormId);
	TestEqual(TEXT("Crate uses its authored box hitbox"), Crate->HitboxShape, EPHPropHitboxShape::Box);
	TestTrue(TEXT("Crate box hitbox has positive authored dimensions"), Crate->HitboxBoxHalfExtents.GetMin() > 0.0f);
	TestEqual(TEXT("Barrel uses its authored capsule hitbox"), Barrel->HitboxShape, EPHPropHitboxShape::Capsule);
	TestTrue(TEXT("Barrel capsule hitbox has valid authored dimensions"),
		Barrel->HitboxCapsuleRadius > 0.0f && Barrel->HitboxCapsuleHalfHeight >= Barrel->HitboxCapsuleRadius);

	const FSoftClassPath PropBlueprintClassPath(TEXT("/Game/PropHunt/Characters/Props/BP_PH_PropCharacter.BP_PH_PropCharacter_C"));
	UClass* PropBlueprintClass = PropBlueprintClassPath.TryLoadClass<APHPropCharacter>();
	TestNotNull(TEXT("Prop Blueprint class exists"), PropBlueprintClass);
	const APHPropCharacter* BlueprintDefaults = PropBlueprintClass != nullptr
		? Cast<APHPropCharacter>(PropBlueprintClass->GetDefaultObject())
		: nullptr;
	TestNotNull(TEXT("Prop Blueprint defaults exist"), BlueprintDefaults);
	if (BlueprintDefaults != nullptr)
	{
		TestEqual(TEXT("Exactly two authored forms are explicitly allowlisted"), BlueprintDefaults->GetAllowedPropFormCount(), 2);
		TestTrue(TEXT("Crate is explicitly allowed"), BlueprintDefaults->IsPropFormAllowed(Crate));
		TestTrue(TEXT("Barrel is explicitly allowed"), BlueprintDefaults->IsPropFormAllowed(Barrel));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHPhysicsPropPrototypeDefaultsTest,
	"PropHunt.PhysicsProp.Prototype.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHPhysicsPropPrototypeDefaultsTest::RunTest(const FString& Parameters)
{
	const APHPhysicsPropPrototype* PrototypeDefaults = GetDefault<APHPhysicsPropPrototype>();
	TestNotNull(TEXT("Physics Prop prototype class defaults exist"), PrototypeDefaults);
	if (PrototypeDefaults == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Physics Prop prototype replicates"), PrototypeDefaults->GetIsReplicated());
	TestTrue(TEXT("Physics Prop prototype replicates movement"), PrototypeDefaults->IsReplicatingMovement());
	TestEqual(TEXT("Physics Prop prototype starts at the reconstructed 30 Hz"), PrototypeDefaults->GetNetUpdateFrequency(), 30.0f);
	TestNotNull(TEXT("Physics Prop prototype has a physical mesh"), PrototypeDefaults->GetPhysicsMesh());
	if (PrototypeDefaults->GetPhysicsMesh() != nullptr)
	{
		TestEqual(TEXT("Physics mesh ignores Pawn movement capsules"),
			PrototypeDefaults->GetPhysicsMesh()->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
		TestEqual(TEXT("Physics mesh cannot become a character step"),
			PrototypeDefaults->GetPhysicsMesh()->CanCharacterStepUpOn, ECB_No);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHScreamingChickenAssetTest,
	"PropHunt.PhysicsProp.ScreamingChicken.Asset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHScreamingChickenAssetTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath DefinitionPath(TEXT("/Game/PropHunt/Data/Physics/DA_PH_PhysicsProp_ScreamingChicken.DA_PH_PhysicsProp_ScreamingChicken"));
	const UPHPhysicsPropDataAsset* Definition = Cast<UPHPhysicsPropDataAsset>(DefinitionPath.TryLoad());
	TestNotNull(TEXT("Screaming Chicken definition exists"), Definition);
	if (Definition == nullptr)
	{
		return false;
	}

	FText ValidationError;
	TestTrue(TEXT("Screaming Chicken definition is valid"), Definition->HasValidDefinition(&ValidationError));
	if (!ValidationError.IsEmpty())
	{
		AddError(ValidationError.ToString());
	}
	TestEqual(TEXT("Screaming Chicken is the first stable Physics Prop id"), Definition->PropId, FName(TEXT("ScreamingChicken")));
	TestTrue(TEXT("Reconstructed mass is preserved"), FMath::IsNearlyEqual(Definition->MassOverrideKilograms, 8.3912f, 0.001f));
	TestEqual(TEXT("Reconstructed impulse-per-mass threshold is preserved"), Definition->ImpactImpulsePerMassThreshold, 50.0f);
	TestEqual(TEXT("Reconstructed network frequency is preserved"), Definition->NetworkUpdateFrequency, 30.0f);
	TestEqual(TEXT("Reconstructed free linear damping is preserved"), Definition->FreeLinearDamping, 0.01f);
	TestTrue(TEXT("Chaos angular assists are enabled after the faithful locomotion slid excessively"), Definition->bUseExperimentalAngularAssists);
	TestEqual(TEXT("Playable prototype uses the selected assisted torque"), Definition->MovementTorqueDegrees, 900000.0f);
	TestEqual(TEXT("Measured Physics Prop horizontal cap is preserved"), Definition->MaximumHorizontalSpeed, 628.0f);
	TestEqual(TEXT("Playable prototype interpolates its arcade velocity"), Definition->MovementVelocityInterpSpeed, 8.0f);
	TestEqual(TEXT("Playable prototype brakes horizontal drift"), Definition->MovementStopInterpSpeed, 12.0f);
	TestEqual(TEXT("Measured Physics Prop angular velocity cap is preserved"), Definition->MaximumAngularVelocityDegrees, 550.0f);
	TestEqual(TEXT("No speculative movement hop is enabled"), Definition->MovementHopImpulse, 0.0f);
	TestEqual(TEXT("Measured Physics Prop jump velocity is preserved"), Definition->JumpVelocity, 480.0f);
	TestEqual(TEXT("Jump adds a forward burst"), Definition->JumpHorizontalBoostVelocity, 225.0f);
	TestEqual(TEXT("Jump burst respects the measured horizontal cap"), Definition->MaximumJumpHorizontalSpeed, 628.0f);
	TestEqual(TEXT("Playable prototype exposes a double jump"), Definition->MaximumJumpCount, 2);
	TestEqual(TEXT("Playable prototype has a responsive jump cooldown"), Definition->JumpCooldownSeconds, 0.18f);
	TestEqual(TEXT("Playable prototype validates ground proximity"), Definition->GroundProbeDistance, 25.0f);
	TestEqual(TEXT("Reconstructed straighten damping is preserved"), Definition->StraightenAngularDamping, 10000.0f);
	TestEqual(TEXT("Reconstructed straighten target speed is preserved"), Definition->StraightenTargetInterpSpeed, 5.0f);
	TestEqual(TEXT("Reconstructed blocked rotation multiplier is preserved"), Definition->BlockedRotationMultiplier, 0.02f);
	TestTrue(TEXT("Measured high-speed Physics Prop uses CCD"), Definition->bUseContinuousCollisionDetection);

	TestNotNull(TEXT("Screaming Chicken Static Mesh exists"), Definition->StaticMesh.Get());
	if (Definition->StaticMesh != nullptr)
	{
		TestEqual(TEXT("Screaming Chicken imports all four visual LODs"), Definition->StaticMesh->GetNumLODs(), 4);
		const UBodySetup* BodySetup = Definition->StaticMesh->GetBodySetup();
		TestNotNull(TEXT("Screaming Chicken has a simple collision BodySetup"), BodySetup);
		if (BodySetup != nullptr)
		{
			TestEqual(TEXT("Screaming Chicken retains exactly two authored convex hulls"), BodySetup->AggGeom.ConvexElems.Num(), 2);
			TestTrue(TEXT("Screaming Chicken does not use complex-as-simple collision"),
				BodySetup->CollisionTraceFlag != CTF_UseComplexAsSimple);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHDefaultMatchRulesTest,
	"PropHunt.Match.Foundation.DefaultRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHDefaultMatchRulesTest::RunTest(const FString& Parameters)
{
	const UPHMatchRulesDataAsset* Rules = GetDefault<UPHMatchRulesDataAsset>();
	TestNotNull(TEXT("Default match rules exist"), Rules);
	if (Rules == nullptr)
	{
		return false;
	}

	FText ValidationError;
	TestTrue(TEXT("Default match rules are internally valid"), Rules->HasValidRules(&ValidationError));
	if (!ValidationError.IsEmpty())
	{
		AddError(ValidationError.ToString());
	}

	TestEqual(TEXT("Reference active objective count"), Rules->ActiveObjectiveCount, 5);
	TestEqual(TEXT("Reference required objective count"), Rules->RequiredObjectiveCount, 4);
	TestEqual(TEXT("MVP supports at most four Props"), Rules->MaximumPropPlayers, 4);
	TestEqual(TEXT("Reference Hunt lasts fifteen minutes"), Rules->HuntDuration, 900.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHMatchRulesAssetTest,
	"PropHunt.Match.Foundation.ConfiguredRulesAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHMatchRulesAssetTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath RulesAssetPath(TEXT("/Game/PropHunt/Data/DA_PH_MatchRules_Default.DA_PH_MatchRules_Default"));
	const UPHMatchRulesDataAsset* Rules = Cast<UPHMatchRulesDataAsset>(RulesAssetPath.TryLoad());
	TestNotNull(TEXT("Configured match rules Data Asset exists"), Rules);
	if (Rules == nullptr)
	{
		return false;
	}

	FText ValidationError;
	TestTrue(TEXT("Configured match rules Data Asset is valid"), Rules->HasValidRules(&ValidationError));
	if (!ValidationError.IsEmpty())
	{
		AddError(ValidationError.ToString());
	}

	TestEqual(TEXT("Configured minimum Prop count"), Rules->MinimumPropPlayers, 1);
	TestEqual(TEXT("Configured maximum Prop count"), Rules->MaximumPropPlayers, 4);
	TestEqual(TEXT("Configured preparation duration"), Rules->PreparationDuration, 10.0f);
	TestEqual(TEXT("Configured hunt duration"), Rules->HuntDuration, 900.0f);
	TestEqual(TEXT("Configured results duration"), Rules->ResultsDuration, 10.0f);
	TestEqual(TEXT("Configured active objective count"), Rules->ActiveObjectiveCount, 5);
	TestEqual(TEXT("Configured required objective count"), Rules->RequiredObjectiveCount, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHMatchPhaseFlowTest,
	"PropHunt.Match.Foundation.TimedPhaseFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHMatchPhaseFlowTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Preparation advances to Hunt"), PHMatchFlow::GetNextTimedPhase(EPHMatchPhase::Preparation), EPHMatchPhase::Hunt);
	TestEqual(TEXT("Hunt timeout advances to Results"), PHMatchFlow::GetNextTimedPhase(EPHMatchPhase::Hunt), EPHMatchPhase::Results);
	TestEqual(TEXT("Results return to Lobby"), PHMatchFlow::GetNextTimedPhase(EPHMatchPhase::Results), EPHMatchPhase::Lobby);
	TestFalse(TEXT("Three objectives do not open a four-objective exit"), PHMatchFlow::ShouldOpenEscape(3, 4));
	TestTrue(TEXT("Four objectives open a four-objective exit"), PHMatchFlow::ShouldOpenEscape(4, 4));
	TestEqual(TEXT("Departing last Prop leaves no Props"),
		PHMatchFlow::GetRemainingRoleCountAfterDeparture(1, EPHPlayerRole::Prop, EPHPlayerRole::Prop), 0);
	TestEqual(TEXT("Departing Hunter does not change Prop count"),
		PHMatchFlow::GetRemainingRoleCountAfterDeparture(2, EPHPlayerRole::Hunter, EPHPlayerRole::Prop), 2);
	return true;
}

#endif
