#include "Characters/PHPropCharacter.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Characters/PHHunterCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Game/PHGameMode.h"
#include "Game/PHGameState.h"
#include "Game/PHPlayerController.h"
#include "Game/PHPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Gameplay/PHCollisionChannels.h"
#include "Gameplay/Capture/PHRetentionPoint.h"
#include "Gameplay/Characters/PHHumanPrototypeSelector.h"
#include "Gameplay/Escape/PHExitGate.h"
#include "Gameplay/Objectives/PHObjectiveActor.h"
#include "Gameplay/Physics/PHPhysicsPropDataAsset.h"
#include "Gameplay/Transformation/PHPropFormDataAsset.h"
#include "Gameplay/Transformation/PHPropTransformTarget.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodyInstance.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHTransformation, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogPHCapture, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogPHPhysicsProp, Log, All);

APHPropCharacter::APHPropCharacter()
	: PrimaryHumanPrototypeName(TEXT("Isaac"))
	, AlternativeHumanPrototypeName(TEXT("Jun"))
	, PrimaryCarryAttachmentOffset(FVector::ZeroVector)
	, PrimaryCarryAttachmentRotation(FRotator::ZeroRotator)
	, AlternativeCarryAttachmentOffset(FVector::ZeroVector)
	, AlternativeCarryAttachmentRotation(FRotator::ZeroRotator)
	, HumanWalkAnimationThreshold(10.0f)
	, CameraArmLength(300.0f)
	, CameraCollisionProbeSize(12.0f)
	, CameraFieldOfView(90.0f)
	, CameraTargetHeight(95.0f)
	, MinimumCameraGroundClearance(30.0f)
	, CameraGroundTraceDepth(500.0f)
	, CameraSocketOffset(0.0f, 45.0f, 0.0f)
	, HumanFirstPersonCameraOffset(0.0f, 0.0f, 64.0f)
	, MaximumHumanStamina(100.0f)
	, HumanSprintSpeed(700.0f)
	, HumanSprintDrainPerSecond(40.0f)
	, HumanStaminaRechargePerSecond(15.0f)
	, HumanStaminaRechargeDelay(0.75f)
	, HumanJumpStaminaCost(20.0f)
	, TransformationDistance(250.0f)
	, TransformationCooldownSeconds(1.0f)
	, TransformationSearchDistance(3000.0f)
	, TransformationTargetingHalfAngleDegrees(12.0f)
	, MaximumPlacementAdjustment(25.0f)
	, PlacementAdjustmentStep(5.0f)
	, ObjectiveSearchDistance(1500.0f)
	, ObjectiveTargetingHalfAngleDegrees(15.0f)
	, CaptureInteractionDistance(225.0f)
	, FirstRetentionDuration(60.0f)
	, SecondRetentionDuration(60.0f)
	, MaximumRetentionCount(3)
	, GraceDuration(5.0f)
	, GraceSpeedMultiplier(1.25f)
	, DownedCrawlSpeed(150.0f)
	, DownedRecoveryDuration(30.0f)
	, AdditionalRecoveryContributionPerHelper(0.75f)
	, MaximumRecoveryHelpers(3)
	, CarryDropRecoveryBonus(0.05f)
	, CarryStruggleRequiredAlternations(72)
	, CarryStruggleMinimumInputInterval(0.08f)
	, CarryStruggleShoveProgressInterval(0.20f)
	, CarryStruggleHunterShoveDistance(90.0f)
	, MeleeHitSequence(0)
	, ReplicatedSpectatorViewRotation(FRotator::ZeroRotator)
	, ReplicatedPhysicsBodyRotation(FRotator::ZeroRotator)
	, ReplicatedPhysicsBodyLinearVelocity(FVector::ZeroVector)
	, ReplicatedPhysicsBodyAngularVelocity(FVector::ZeroVector)
	, bReplicatedPhysicsBodyGrounded(false)
	, CurrentHumanStamina(100.0f)
	, CaptureState(EPHPropCaptureState::Free)
	, RetentionCount(0)
	, CaptureStateEndServerTime(0.0f)
	, Carrier(nullptr)
	, RetentionPoint(nullptr)
	, bMementoInProgress(false)
	, MementoImpactPoint(FVector::ZeroVector)
	, DownedRecoveryProgress(0.0f)
	, RecoveryHelperCount(0)
	, CarryStruggleProgress(0.0f)
	, bUseAlternativeHumanPrototype(false)
	, InitialMeshRelativeLocation(FVector::ZeroVector)
	, InitialMeshRelativeRotation(FRotator::ZeroRotator)
	, InitialMeshRelativeScale(FVector::OneVector)
	, InitialCapsuleRadius(40.0f)
	, InitialCapsuleHalfHeight(60.0f)
	, LastLocalTransformationRequestTime(-DBL_MAX)
	, LastServerTransformationRequestTime(-DBL_MAX)
	, LastHumanStaminaUseServerTime(-DBL_MAX)
	, LastLocalStruggleInputTime(-DBL_MAX)
	, LastServerStruggleInputTime(-DBL_MAX)
	, LastServerPhysicsPropInputTime(-DBL_MAX)
	, LastLocalSpectatorViewPublishTime(-DBL_MAX)
	, LastServerSpectatorViewUpdateTime(-DBL_MAX)
	, LastPhysicsPropJumpTime(-DBL_MAX)
	, LastPhysicsPropImpactSoundTime(-DBL_MAX)
	, NormalWalkSpeed(500.0f)
	, LocalPhysicsForwardInput(0.0f)
	, LocalPhysicsRightInput(0.0f)
	, ServerPhysicsForwardInput(0.0f)
	, ServerPhysicsRightInput(0.0f)
	, ServerPhysicsViewYaw(0.0f)
	, LastPublishedSpectatorViewRotation(FRotator::ZeroRotator)
	, CurrentPhysicsStraightenInterpSpeed(0.0f)
	, bWantsHumanSprint(false)
	, bLocalPhysicsStraightenHeld(false)
	, bServerPhysicsStraightenHeld(false)
	, bPhysicsStraightenWasActive(false)
	, bPhysicsMovementWasActive(false)
	, bPhysicsWasGrounded(false)
	, PhysicsJumpsUsed(0)
	, bPlayingWalkAnimation(false)
	, bCurrentHumanAnimationFrozen(false)
	, CurrentHumanAnimation(nullptr)
	, LastLocalStruggleDirection(0)
	, LastServerStruggleDirection(0)
	, NextCarryStruggleShoveProgress(0.20f)
	, LastPublishedDownedRecoveryProgress(0.0f)
	, AssistedDownedProp(nullptr)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -96.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	GetMesh()->SetHiddenInGame(true);
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HumanFirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("HumanFirstPersonCamera"));
	HumanFirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	HumanFirstPersonCamera->bUsePawnControlRotation = true;
	HumanFirstPersonCamera->SetAutoActivate(true);

	PropPresentationRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PropPresentationRoot"));
	PropPresentationRoot->SetupAttachment(GetCapsuleComponent());

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(PropPresentationRoot);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeChannel = ECC_Camera;
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->bInheritPitch = true;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = false;

	ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdPersonCamera"));
	ThirdPersonCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	ThirdPersonCamera->bUsePawnControlRotation = false;
	ThirdPersonCamera->SetAutoActivate(false);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> InteractionOutlineMaterial(
		TEXT("/Game/PropHunt/UI/Materials/M_PH_InteractableOutline_PP_V2.M_PH_InteractableOutline_PP_V2"));
	if (InteractionOutlineMaterial.Succeeded())
	{
		HumanFirstPersonCamera->PostProcessSettings.AddBlendable(InteractionOutlineMaterial.Object, 1.0f);
		ThirdPersonCamera->PostProcessSettings.AddBlendable(InteractionOutlineMaterial.Object, 1.0f);
	}

	GrayboxPropBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GrayboxPropBody"));
	GrayboxPropBody->SetupAttachment(PropPresentationRoot);
	GrayboxPropBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrayboxPropBody->SetNotifyRigidBodyCollision(true);
	GrayboxPropBody->SetCanEverAffectNavigation(false);
	GrayboxPropBody->OnComponentHit.AddDynamic(this, &APHPropCharacter::OnPhysicsPropHit);
	GrayboxPropBody->SetRelativeLocation(FVector(0.0f, 0.0f, -8.0f));
	GrayboxPropBody->SetRelativeScale3D(FVector(0.65f, 0.65f, 0.90f));

	PropBoxHitbox = CreateDefaultSubobject<UBoxComponent>(TEXT("PropBoxHitbox"));
	PropBoxHitbox->SetupAttachment(PropPresentationRoot);
	PropBoxHitbox->InitBoxExtent(FVector(40.0f, 40.0f, 60.0f));
	PropBoxHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PropBoxHitbox->SetCollisionObjectType(PHCollision::PropHitbox);
	PropBoxHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
	PropBoxHitbox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PropBoxHitbox->SetGenerateOverlapEvents(false);
	PropBoxHitbox->SetCanEverAffectNavigation(false);
	PropBoxHitbox->CanCharacterStepUpOn = ECB_No;

	PropCapsuleHitbox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("PropCapsuleHitbox"));
	PropCapsuleHitbox->SetupAttachment(PropPresentationRoot);
	PropCapsuleHitbox->InitCapsuleSize(40.0f, 60.0f);
	PropCapsuleHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PropCapsuleHitbox->SetCollisionObjectType(PHCollision::PropHitbox);
	PropCapsuleHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
	PropCapsuleHitbox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PropCapsuleHitbox->SetGenerateOverlapEvents(false);
	PropCapsuleHitbox->SetCanEverAffectNavigation(false);
	PropCapsuleHitbox->CanCharacterStepUpOn = ECB_No;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		GrayboxPropBody->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UPHPhysicsPropDataAsset> DefaultPhysicsAsset(
		TEXT("/Game/PropHunt/Data/Physics/DA_PH_PhysicsProp_ScreamingChicken.DA_PH_PhysicsProp_ScreamingChicken"));
	if (DefaultPhysicsAsset.Succeeded())
	{
		DefaultPhysicsPropDefinition = DefaultPhysicsAsset.Object;
	}
}

void APHPropCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APHPropCharacter, MeleeHitSequence);
	DOREPLIFETIME(APHPropCharacter, TransformationState);
	DOREPLIFETIME_CONDITION(APHPropCharacter, ReplicatedSpectatorViewRotation, COND_SkipOwner);
	DOREPLIFETIME(APHPropCharacter, ReplicatedPhysicsBodyRotation);
	DOREPLIFETIME(APHPropCharacter, ReplicatedPhysicsBodyLinearVelocity);
	DOREPLIFETIME(APHPropCharacter, ReplicatedPhysicsBodyAngularVelocity);
	DOREPLIFETIME(APHPropCharacter, bReplicatedPhysicsBodyGrounded);
	DOREPLIFETIME_CONDITION(APHPropCharacter, ActiveObjective, COND_OwnerOnly);
	DOREPLIFETIME(APHPropCharacter, ActiveExitGate);
	DOREPLIFETIME(APHPropCharacter, ActiveRetentionRescuePoint);
	DOREPLIFETIME_CONDITION(APHPropCharacter, CurrentHumanStamina, COND_OwnerOnly);
	DOREPLIFETIME(APHPropCharacter, CaptureState);
	DOREPLIFETIME(APHPropCharacter, RetentionCount);
	DOREPLIFETIME(APHPropCharacter, CaptureStateEndServerTime);
	DOREPLIFETIME(APHPropCharacter, Carrier);
	DOREPLIFETIME(APHPropCharacter, RetentionPoint);
	DOREPLIFETIME(APHPropCharacter, bMementoInProgress);
	DOREPLIFETIME(APHPropCharacter, MementoImpactPoint);
	DOREPLIFETIME(APHPropCharacter, DownedRecoveryProgress);
	DOREPLIFETIME(APHPropCharacter, RecoveryHelperCount);
	DOREPLIFETIME_CONDITION(APHPropCharacter, AssistedDownedProp, COND_OwnerOnly);
	DOREPLIFETIME(APHPropCharacter, CarryStruggleProgress);
	DOREPLIFETIME(APHPropCharacter, bUseAlternativeHumanPrototype);
}

void APHPropCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ConfigureHumanPresentation();
}

#if WITH_EDITOR
void APHPropCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ConfigureHumanPresentation();
}
#endif

void APHPropCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	InitialPresentationMesh = GrayboxPropBody->GetStaticMesh();
	InitialMeshRelativeLocation = GrayboxPropBody->GetRelativeLocation();
	InitialMeshRelativeRotation = GrayboxPropBody->GetRelativeRotation();
	InitialMeshRelativeScale = GrayboxPropBody->GetRelativeScale3D();
	InitialCapsuleRadius = GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	InitialCapsuleHalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();

	CameraBoom->TargetArmLength = CameraArmLength;
	CameraBoom->ProbeSize = CameraCollisionProbeSize;
	CameraBoom->TargetOffset = FVector(0.0f, 0.0f, CameraTargetHeight);
	CameraBoom->SocketOffset = CameraSocketOffset;
	HumanFirstPersonCamera->SetRelativeLocation(HumanFirstPersonCameraOffset);
	HumanFirstPersonCamera->SetFieldOfView(CameraFieldOfView);
	ThirdPersonCamera->SetFieldOfView(CameraFieldOfView);
	NormalWalkSpeed = GetCharacterMovement() != nullptr ? GetCharacterMovement()->MaxWalkSpeed : 500.0f;
	ConfigureHumanPresentation();
	ApplyCaptureState();
	ApplyLocalViewMode();
}

void APHPropCharacter::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(true);

	if (HasAuthority())
	{
		CurrentHumanStamina = FMath::Max(1.0f, MaximumHumanStamina);
		OnRep_HumanStamina();
	}

	if (HasAuthority() || TransformationState.CapsuleHalfHeight <= 0.0f)
	{
		TransformationState.CapsuleRadius = InitialCapsuleRadius;
		TransformationState.CapsuleHalfHeight = InitialCapsuleHalfHeight;
		const FPHResolvedPropHitbox InitialHitbox = ResolveHitbox(nullptr);
		TransformationState.HitboxShape = InitialHitbox.Shape;
		TransformationState.HitboxRelativeLocation = InitialHitbox.RelativeLocation;
		TransformationState.HitboxRelativeRotation = InitialHitbox.RelativeRotation;
		TransformationState.HitboxBoxHalfExtents = InitialHitbox.BoxHalfExtents;
		TransformationState.HitboxCapsuleRadius = InitialHitbox.CapsuleRadius;
		TransformationState.HitboxCapsuleHalfHeight = InitialHitbox.CapsuleHalfHeight;
	}
	ApplyTransformationState();
}

void APHPropCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsLocallyControlled() && Controller != nullptr)
	{
		const FRotator CurrentViewRotation = Controller->GetControlRotation().GetNormalized();
		if (HasAuthority())
		{
			ReplicatedSpectatorViewRotation = CurrentViewRotation;
		}
		else if (GetWorld() != nullptr)
		{
			const double Now = GetWorld()->GetTimeSeconds();
			const bool bPublishIntervalElapsed = Now - LastLocalSpectatorViewPublishTime >= 0.05;
			const bool bViewChanged = !LastPublishedSpectatorViewRotation.Equals(CurrentViewRotation, 0.25f);
			const bool bKeepAliveElapsed = Now - LastLocalSpectatorViewPublishTime >= 0.25;
			if (bPublishIntervalElapsed && (bViewChanged || bKeepAliveElapsed))
			{
				ServerSetSpectatorViewRotation(
					FRotator::CompressAxisToShort(CurrentViewRotation.Pitch),
					FRotator::CompressAxisToShort(CurrentViewRotation.Yaw));
				LastPublishedSpectatorViewRotation = CurrentViewRotation;
				LastLocalSpectatorViewPublishTime = Now;
			}
		}
	}
	else if (bLocallySpectated)
	{
		SmoothedSpectatorViewRotation = FMath::RInterpTo(
			SmoothedSpectatorViewRotation,
			ReplicatedSpectatorViewRotation,
			DeltaSeconds,
			20.0f);
	}
	if (IsLocallyControlled())
	{
		RefreshLocalInteractionPresentation();
	}
	if (IsLocallyControlled() && IsUsingPhysicsPropMovement())
	{
		const float ViewYaw = Controller != nullptr ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw;
		if (HasAuthority())
		{
			SetServerPhysicsPropInput(LocalPhysicsForwardInput, LocalPhysicsRightInput, ViewYaw, bLocalPhysicsStraightenHeld);
		}
		else
		{
			ServerSetPhysicsPropInput(LocalPhysicsForwardInput, LocalPhysicsRightInput, ViewYaw, bLocalPhysicsStraightenHeld);
		}
	}
	if (HasAuthority())
	{
		if (IsUsingPhysicsPropMovement())
		{
			if (!IsLocallyControlled() && GetWorld() != nullptr
				&& GetWorld()->GetTimeSeconds() - LastServerPhysicsPropInputTime > 0.25)
			{
				ServerPhysicsForwardInput = 0.0f;
				ServerPhysicsRightInput = 0.0f;
				bServerPhysicsStraightenHeld = false;
			}
			ApplyPhysicsPropControl(DeltaSeconds);
			SynchronizeActorToPhysicsProp();
		}
		UpdateHumanStamina(DeltaSeconds);
		UpdateDownedRecovery(DeltaSeconds);
	}
	else if (IsUsingPhysicsPropMovement())
	{
		ApplyReplicatedPhysicsPose(DeltaSeconds);
	}
	UpdateLocomotionPresentation();
}

void APHPropCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HighlightedTransformTarget.IsValid())
	{
		HighlightedTransformTarget->SetLocallyHighlighted(false);
		HighlightedTransformTarget.Reset();
	}
	if (HasAuthority())
	{
		PerformAuthoritativeStopObjectiveInteraction();
		StopAssistingDownedTarget();
		StopRetentionRescue();
		ClearCaptureRelationships();
	}
	Super::EndPlay(EndPlayReason);
}

void APHPropCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);

	PlayerInputComponent->BindAction(TEXT("TransformProp"), IE_Pressed, this, &APHPropCharacter::RequestPropTransformation);
	PlayerInputComponent->BindAction(TEXT("ResetPropForm"), IE_Pressed, this, &APHPropCharacter::RequestReturnToInitialForm);
	PlayerInputComponent->BindAction(TEXT("SurvivorInteract"), IE_Pressed, this, &APHPropCharacter::RequestSurvivorInteraction);
	PlayerInputComponent->BindAction(TEXT("SurvivorInteract"), IE_Released, this, &APHPropCharacter::StopSurvivorInteraction);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &APHPropCharacter::HandleCaptureStruggleAxis);
	PlayerInputComponent->BindAction(TEXT("SprintHuman"), IE_Pressed, this, &APHPropCharacter::StartHumanSprint);
	PlayerInputComponent->BindAction(TEXT("SprintHuman"), IE_Released, this, &APHPropCharacter::StopHumanSprint);
	PlayerInputComponent->BindAction(TEXT("SwitchHumanPrototype"), IE_Pressed, this, &APHPropCharacter::RequestSwitchHumanPrototype);
	PlayerInputComponent->BindAction(TEXT("StraightenProp"), IE_Pressed, this, &APHPropCharacter::StartPhysicsPropStraighten);
	PlayerInputComponent->BindAction(TEXT("StraightenProp"), IE_Released, this, &APHPropCharacter::StopPhysicsPropStraighten);
}

bool APHPropCharacter::IsMoveInputIgnored() const
{
	return Super::IsMoveInputIgnored()
		|| bMementoInProgress
		|| (CaptureState != EPHPropCaptureState::Free
			&& CaptureState != EPHPropCaptureState::Grace
			&& CaptureState != EPHPropCaptureState::Downed);
}

void APHPropCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (CaptureState == EPHPropCaptureState::Downed)
	{
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->SetMovementMode(MOVE_Walking);
			ApplyMovementSpeed();
		}
	}
}

FRotator APHPropCharacter::GetViewRotation() const
{
	if (bLocallySpectated && !IsLocallyControlled())
	{
		return SmoothedSpectatorViewRotation;
	}

	return Super::GetViewRotation();
}

void APHPropCharacter::CalcCamera(const float DeltaTime, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	if (bLocallySpectated)
	{
		// A dead client follows the survivor's replicated aim instead of forcing
		// an unrelated third-person fallback camera. This changes presentation
		// only: the spectator controller remains unpossessed and input-locked.
		OutResult.Rotation = GetViewRotation();
	}

	UWorld* World = GetWorld();
	if (World == nullptr || !IsUsingPropThirdPersonCamera())
	{
		return;
	}

	const float SafeClearance = FMath::Clamp(MinimumCameraGroundClearance, 5.0f, 100.0f);
	const float SafeTraceDepth = FMath::Clamp(CameraGroundTraceDepth, 100.0f, 1000.0f);
	const FVector ActorLocation = GetActorLocation();
	const float TraceStartZ = FMath::Max(
		ActorLocation.Z + FMath::Max(CameraTargetHeight, SafeClearance * 2.0f),
		OutResult.Location.Z + SafeClearance * 2.0f);
	const FVector TraceStart(OutResult.Location.X, OutResult.Location.Y, TraceStartZ);
	const FVector TraceEnd(OutResult.Location.X, OutResult.Location.Y, ActorLocation.Z - SafeTraceDepth);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHPropCameraGroundGuard), false, this);
	FHitResult GroundHit;
	if (World->SweepSingleByChannel(
		GroundHit,
		TraceStart,
		TraceEnd,
		FQuat::Identity,
		ECC_Camera,
		FCollisionShape::MakeSphere(FMath::Max(2.0f, CameraCollisionProbeSize * 0.5f)),
		QueryParams)
		&& GroundHit.ImpactNormal.Z >= 0.25f)
	{
		OutResult.Location.Z = FMath::Max(
			OutResult.Location.Z,
			GroundHit.ImpactPoint.Z + SafeClearance);
	}
}

void APHPropCharacter::ReceiveAuthoritativeMeleeHit(APHHunterCharacter& Attacker)
{
	if (!HasAuthority() || !Attacker.HasAuthority())
	{
		return;
	}

	MeleeHitSequence = MeleeHitSequence == MAX_int32 ? 1 : MeleeHitSequence + 1;
	OnRep_MeleeHitSequence();
	ForceNetUpdate();

	if (PHCaptureFlow::CanMeleeHitDown(CaptureState))
	{
		PerformAuthoritativeStopObjectiveInteraction();
		StopAssistingDownedTarget();
		StopRetentionRescue();
		if (TransformationState.ActiveForm != nullptr)
		{
			PublishTransformationResult(EPHPropTransformationResult::ReturnedToInitial, nullptr);
		}
		ResetCaptureProgress();
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		SetCaptureState(EPHPropCaptureState::Downed);
		if (APHPlayerState* AttackerState = Attacker.GetPlayerState<APHPlayerState>())
		{
			AttackerState->RecordHunterDown();
		}
		UE_LOG(LogPHTransformation, Log, TEXT("%s was downed by a melee hit from %s."),
			*GetName(), *Attacker.GetName());
	}
}

void APHPropCharacter::RequestSurvivorInteraction()
{
	if (!IsLocallyControlled() || !CanPerformCaptureRescue())
	{
		return;
	}

	APHRetentionPoint* RequestedPoint = nullptr;
	if (TryResolveRetentionPoint(RequestedPoint) && RequestedPoint != nullptr
		&& RequestedPoint->GetRetainedProp() != nullptr)
	{
		RequestCaptureRescue();
		return;
	}

	APHPropCharacter* RequestedTarget = nullptr;
	if (TryResolveDownedRecoveryTarget(RequestedTarget) && RequestedTarget != nullptr)
	{
		RequestCaptureRescue();
		return;
	}

	APHObjectiveActor* RequestedObjective = nullptr;
	APHExitGate* RequestedExitGate = nullptr;
	if (TryResolveExitGateTarget(RequestedExitGate) && RequestedExitGate != nullptr)
	{
		if (HasAuthority())
		{
			PerformAuthoritativeStartExitGateInteraction(RequestedExitGate);
		}
		else
		{
			ServerRequestStartExitGateInteraction(RequestedExitGate);
		}
		return;
	}

	if (TryResolveObjectiveTarget(RequestedObjective) && RequestedObjective != nullptr)
	{
		RequestStartObjectiveInteraction();
		return;
	}

	RequestPropTransformation();
}

void APHPropCharacter::StopSurvivorInteraction()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	RequestStopCaptureAssist();
	RequestStopObjectiveInteraction();
	if (HasAuthority())
	{
		PerformAuthoritativeStopExitGateInteraction();
	}
	else if (ActiveExitGate != nullptr)
	{
		ServerRequestStopExitGateInteraction();
	}
}

void APHPropCharacter::RequestCaptureRescue()
{
	if (!IsLocallyControlled() || !CanPerformCaptureRescue())
	{
		return;
	}

	APHRetentionPoint* RequestedPoint = nullptr;
	if (TryResolveRetentionPoint(RequestedPoint) && RequestedPoint != nullptr
		&& RequestedPoint->GetRetainedProp() != nullptr)
	{
		UE_LOG(LogPHTransformation, Log, TEXT("Capture rescue request on %s targets %s."),
			*GetName(), *RequestedPoint->GetName());
		if (HasAuthority())
		{
			StopAssistingDownedTarget();
			if (PrepareHumanFormForSupport(TEXT("retention rescue")))
			{
				RequestedPoint->ServerTryBeginRelease(*this);
			}
		}
		else
		{
			ServerRequestCaptureRescue(RequestedPoint);
		}
		return;
	}

	APHPropCharacter* RequestedTarget = nullptr;
	if (!TryResolveDownedRecoveryTarget(RequestedTarget))
	{
		return;
	}
	if (HasAuthority())
	{
		BeginAssistingDownedTarget(*RequestedTarget);
	}
	else
	{
		ServerRequestCaptureAssist(RequestedTarget);
	}
}

#if !UE_BUILD_SHIPPING
void APHPropCharacter::ForceFinalRetentionForSmoke(APHRetentionPoint& SmokeRetentionPoint)
{
	if (!HasAuthority())
	{
		return;
	}

	RetentionCount = FMath::Max(0, MaximumRetentionCount - 1);
	Carrier = nullptr;
	SetCaptureState(EPHPropCaptureState::Carried);
	ServerRetainAt(SmokeRetentionPoint);
}

bool APHPropCharacter::ForcePropFormForSmoke(UPHPropFormDataAsset& SmokeForm)
{
	if (!HasAuthority() || !IsPropFormAllowed(&SmokeForm))
	{
		return false;
	}

	EPHPropTransformationResult Failure = EPHPropTransformationResult::None;
	if (!TryApplyForm(&SmokeForm, nullptr, Failure))
	{
		PublishTransformationResult(Failure, TransformationState.ActiveForm);
		return false;
	}
	return true;
}

void APHPropCharacter::PrepareMementoForSmoke(const int32 PriorRetentionCount)
{
	if (!HasAuthority())
	{
		return;
	}

	ClearCaptureRelationships();
	RetentionCount = FMath::Clamp(PriorRetentionCount, 0, MaximumRetentionCount);
	ResetCaptureProgress();
	SetCaptureState(EPHPropCaptureState::Downed);
}
#endif

bool APHPropCharacter::CanPerformCaptureRescue() const
{
	return CaptureState == EPHPropCaptureState::Free || CaptureState == EPHPropCaptureState::Grace;
}

float APHPropCharacter::GetRetentionRescueProgressNormalized() const
{
	return ActiveRetentionRescuePoint != nullptr
		? ActiveRetentionRescuePoint->GetReleaseProgressNormalized()
		: 0.0f;
}

void APHPropCharacter::RequestStopCaptureAssist()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	if (HasAuthority())
	{
		StopAssistingDownedTarget();
		StopRetentionRescue();
	}
	else
	{
		ServerRequestStopCaptureAssist();
	}
}

void APHPropCharacter::HandleCaptureStruggleAxis(const float Value)
{
	if (!IsLocallyControlled() || CaptureState != EPHPropCaptureState::Carried || GetWorld() == nullptr)
	{
		return;
	}

	const int8 Direction = Value <= -0.75f ? -1 : (Value >= 0.75f ? 1 : 0);
	if (Direction == 0 || Direction == LastLocalStruggleDirection)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastLocalStruggleInputTime < FMath::Clamp(
		static_cast<double>(CarryStruggleMinimumInputInterval), 0.05, 0.5))
	{
		return;
	}

	LastLocalStruggleDirection = Direction;
	LastLocalStruggleInputTime = Now;
	if (HasAuthority())
	{
		SubmitCarryStruggleInput(Direction);
	}
	else
	{
		ServerSubmitCarryStruggleInput(Direction);
	}
}

bool APHPropCharacter::IsUsingPhysicsPropMovement() const
{
	return TransformationState.ActiveForm != nullptr
		&& DefaultPhysicsPropDefinition != nullptr
		&& (CaptureState == EPHPropCaptureState::Free || CaptureState == EPHPropCaptureState::Grace);
}

void APHPropCharacter::MoveForward(const float Value)
{
	if (IsUsingPhysicsPropMovement())
	{
		LocalPhysicsForwardInput = FMath::Clamp(Value, -1.0f, 1.0f);
		return;
	}
	LocalPhysicsForwardInput = 0.0f;
	Super::MoveForward(Value);
}

void APHPropCharacter::MoveRight(const float Value)
{
	if (IsUsingPhysicsPropMovement())
	{
		LocalPhysicsRightInput = FMath::Clamp(Value, -1.0f, 1.0f);
		return;
	}
	LocalPhysicsRightInput = 0.0f;
	Super::MoveRight(Value);
}

void APHPropCharacter::RequestJump()
{
	if (!IsUsingPhysicsPropMovement())
	{
		Super::RequestJump();
		return;
	}
	if (HasAuthority())
	{
		TryPhysicsPropJump();
	}
	else
	{
		ServerRequestPhysicsPropJump();
	}
}

void APHPropCharacter::RequestStopJump()
{
	if (!IsUsingPhysicsPropMovement())
	{
		Super::RequestStopJump();
	}
}

bool APHPropCharacter::CanUseSoundEmote() const
{
	return !bMementoInProgress
		&& (CaptureState == EPHPropCaptureState::Free || CaptureState == EPHPropCaptureState::Grace);
}

void APHPropCharacter::StartPhysicsPropStraighten()
{
	bLocalPhysicsStraightenHeld = true;
}

void APHPropCharacter::StopPhysicsPropStraighten()
{
	bLocalPhysicsStraightenHeld = false;
}

void APHPropCharacter::ServerSetPhysicsPropInput_Implementation(
	const float Forward,
	const float Right,
	const float ViewYaw,
	const bool bStraighten)
{
	SetServerPhysicsPropInput(Forward, Right, ViewYaw, bStraighten);
}

void APHPropCharacter::ServerSetSpectatorViewRotation_Implementation(
	const uint16 CompressedPitch,
	const uint16 CompressedYaw)
{
	if (!HasAuthority() || GetWorld() == nullptr)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastServerSpectatorViewUpdateTime < (1.0 / 60.0))
	{
		return;
	}

	const float Pitch = FMath::Clamp(
		FRotator::NormalizeAxis(FRotator::DecompressAxisFromShort(CompressedPitch)),
		-89.0f,
		89.0f);
	const float Yaw = FRotator::NormalizeAxis(FRotator::DecompressAxisFromShort(CompressedYaw));
	ReplicatedSpectatorViewRotation = FRotator(Pitch, Yaw, 0.0f);
	LastServerSpectatorViewUpdateTime = Now;
}

void APHPropCharacter::ServerRequestPhysicsPropJump_Implementation()
{
	TryPhysicsPropJump();
}

void APHPropCharacter::SetServerPhysicsPropInput(
	const float Forward,
	const float Right,
	const float ViewYaw,
	const bool bStraighten)
{
	if (!HasAuthority() || !IsUsingPhysicsPropMovement())
	{
		return;
	}
	ServerPhysicsForwardInput = FMath::IsFinite(Forward) ? FMath::Clamp(Forward, -1.0f, 1.0f) : 0.0f;
	ServerPhysicsRightInput = FMath::IsFinite(Right) ? FMath::Clamp(Right, -1.0f, 1.0f) : 0.0f;
	ServerPhysicsViewYaw = FMath::IsFinite(ViewYaw) ? FRotator::NormalizeAxis(ViewYaw) : GetActorRotation().Yaw;
	bServerPhysicsStraightenHeld = bStraighten;
	LastServerPhysicsPropInputTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
}

void APHPropCharacter::RefreshPhysicsPropMovement()
{
	if (GrayboxPropBody == nullptr || GetCapsuleComponent() == nullptr || GetCharacterMovement() == nullptr)
	{
		return;
	}

	if (!IsUsingPhysicsPropMovement())
	{
		StopPhysicsPropSimulation();
		return;
	}

	FText DefinitionError;
	if (!DefaultPhysicsPropDefinition->HasValidDefinition(&DefinitionError))
	{
		UE_LOG(LogPHTransformation, Error, TEXT("Physics movement disabled for %s: %s"), *GetName(), *DefinitionError.ToString());
		StopPhysicsPropSimulation();
		return;
	}

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrayboxPropBody->SetLinearDamping(FMath::Max(0.0f, DefaultPhysicsPropDefinition->FreeLinearDamping));
	GrayboxPropBody->SetAngularDamping(FMath::Max(0.0f, DefaultPhysicsPropDefinition->FreeAngularDamping));
	GrayboxPropBody->SetPhysMaterialOverride(DefaultPhysicsPropDefinition->FreePhysicalMaterial);
	GrayboxPropBody->BodyInstance.bUseCCD = DefaultPhysicsPropDefinition->bUseContinuousCollisionDetection;
	GrayboxPropBody->SetPhysicsMaxAngularVelocityInDegrees(
		FMath::Clamp(DefaultPhysicsPropDefinition->MaximumAngularVelocityDegrees, 1.0f, 2000.0f),
		false);
	SetNetUpdateFrequency(FMath::Clamp(DefaultPhysicsPropDefinition->NetworkUpdateFrequency, 1.0f, 120.0f));
	SetMinNetUpdateFrequency(FMath::Min(10.0f, GetNetUpdateFrequency()));

	if (HasAuthority())
	{
		GrayboxPropBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		GrayboxPropBody->SetCollisionObjectType(ECC_PhysicsBody);
		GrayboxPropBody->SetCollisionResponseToAllChannels(ECR_Block);
		GrayboxPropBody->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		GrayboxPropBody->SetCollisionResponseToChannel(PHCollision::PropHitbox, ECR_Ignore);
		if (!GrayboxPropBody->IsSimulatingPhysics())
		{
			GrayboxPropBody->SetSimulatePhysics(true);
			GrayboxPropBody->SetMassOverrideInKg(
				NAME_None,
				FMath::Clamp(DefaultPhysicsPropDefinition->MassOverrideKilograms, 0.01f, 500.0f),
				true);
			GrayboxPropBody->WakeAllRigidBodies();
			ReplicatedPhysicsBodyRotation = GrayboxPropBody->GetComponentRotation();
			ReplicatedPhysicsBodyLinearVelocity = GrayboxPropBody->GetPhysicsLinearVelocity();
			ReplicatedPhysicsBodyAngularVelocity = GrayboxPropBody->GetPhysicsAngularVelocityInDegrees();
			bPhysicsWasGrounded = IsPhysicsPropGrounded();
			bReplicatedPhysicsBodyGrounded = bPhysicsWasGrounded;
			PhysicsJumpsUsed = 0;
		}
	}
	else
	{
		GrayboxPropBody->SetSimulatePhysics(false);
		GrayboxPropBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ApplyReplicatedPhysicsPose();
	}
}

void APHPropCharacter::StopPhysicsPropSimulation()
{
	if (GrayboxPropBody == nullptr || GetCapsuleComponent() == nullptr || PropPresentationRoot == nullptr)
	{
		return;
	}

	FTransform LastBodyTransform = GrayboxPropBody->GetComponentTransform();
	if (GrayboxPropBody->IsSimulatingPhysics())
	{
		GrayboxPropBody->SetSimulatePhysics(false);
		SetActorLocation(LastBodyTransform.GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (HasAuthority())
	{
		ReplicatedPhysicsBodyLinearVelocity = FVector::ZeroVector;
		ReplicatedPhysicsBodyAngularVelocity = FVector::ZeroVector;
		bReplicatedPhysicsBodyGrounded = false;
	}
	GrayboxPropBody->AttachToComponent(PropPresentationRoot, FAttachmentTransformRules::KeepWorldTransform);
	ResetPhysicsPropPresentation();
	GrayboxPropBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ServerPhysicsForwardInput = 0.0f;
	ServerPhysicsRightInput = 0.0f;
	bServerPhysicsStraightenHeld = false;
	bPhysicsStraightenWasActive = false;
	bPhysicsMovementWasActive = false;
	CurrentPhysicsStraightenInterpSpeed = 0.0f;
	PhysicsJumpsUsed = 0;

	const bool bCapsuleShouldCollide = CaptureState != EPHPropCaptureState::Carried
		&& CaptureState != EPHPropCaptureState::Retained
		&& !PHCaptureFlow::IsTerminal(CaptureState);
	GetCapsuleComponent()->SetCollisionEnabled(
		bCapsuleShouldCollide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	// Physics Prop control disables CharacterMovement on both the authority and
	// the owning client. Stopping Chaos must restore the Character movement mode
	// as well as the capsule, otherwise a successful return to the human form
	// leaves the replicated Pawn in MOVE_None until another capture-state change.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		if (bCapsuleShouldCollide && Movement->MovementMode == MOVE_None)
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
		ApplyMovementSpeed();
	}
}

void APHPropCharacter::ApplyPhysicsPropControl(const float DeltaSeconds)
{
	if (!HasAuthority() || !IsUsingPhysicsPropMovement() || GrayboxPropBody == nullptr
		|| DefaultPhysicsPropDefinition == nullptr || !GrayboxPropBody->IsSimulatingPhysics())
	{
		return;
	}

	const bool bGroundedNow = IsPhysicsPropGrounded();
	if (bGroundedNow && !bPhysicsWasGrounded)
	{
		PhysicsJumpsUsed = 0;
	}
	bPhysicsWasGrounded = bGroundedNow;
	bReplicatedPhysicsBodyGrounded = bGroundedNow;

	const FVector2D RawInput(ServerPhysicsForwardInput, ServerPhysicsRightInput);
	const bool bHasMovementInput = RawInput.SizeSquared() > FMath::Square(0.05f);
	if (bHasMovementInput)
	{
		const FVector2D ClampedInput = RawInput.GetClampedToMaxSize(1.0f);
		const FRotator YawRotation(0.0f, ServerPhysicsViewYaw, 0.0f);
		const FVector DesiredDirection = (
			FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X) * ClampedInput.X
			+ FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y) * ClampedInput.Y).GetSafeNormal();
		const FVector LinearVelocity = GrayboxPropBody->GetPhysicsLinearVelocity();
		const FVector HorizontalVelocity(LinearVelocity.X, LinearVelocity.Y, 0.0f);
		const float NormalMaximumSpeed = FMath::Clamp(DefaultPhysicsPropDefinition->MaximumHorizontalSpeed, 1.0f, 2000.0f);
		const float MaximumSpeed = !bGroundedNow && PhysicsJumpsUsed > 0
			? FMath::Clamp(DefaultPhysicsPropDefinition->MaximumJumpHorizontalSpeed, NormalMaximumSpeed, 2500.0f)
			: NormalMaximumSpeed;

		const FVector TorqueAxis = FVector::CrossProduct(FVector::UpVector, DesiredDirection).GetSafeNormal();
		const float SafeTorque = FMath::Clamp(DefaultPhysicsPropDefinition->MovementTorqueDegrees, 0.0f, 5000000.0f);
		GrayboxPropBody->AddTorqueInDegrees(TorqueAxis * SafeTorque * ClampedInput.Size(), NAME_None, false);
		if (DefaultPhysicsPropDefinition->bUseExperimentalAngularAssists)
		{
			FVector AngularVelocity = GrayboxPropBody->GetPhysicsAngularVelocityInDegrees();
			const float TargetSpeed = FMath::Clamp(
				DefaultPhysicsPropDefinition->MaximumAngularVelocityDegrees,
				1.0f,
				2000.0f) * ClampedInput.Size();
			const float CurrentSpeed = FVector::DotProduct(AngularVelocity, TorqueAxis);
			const float NewSpeed = FMath::FInterpConstantTo(
				CurrentSpeed,
				TargetSpeed,
				DeltaSeconds,
				FMath::Clamp(DefaultPhysicsPropDefinition->MovementAngularAccelerationDegrees, 0.0f, 5000.0f));
			AngularVelocity += TorqueAxis * (NewSpeed - CurrentSpeed);
			GrayboxPropBody->SetPhysicsAngularVelocityInDegrees(AngularVelocity, false);
		}

		const FVector DesiredHorizontalVelocity = DesiredDirection * MaximumSpeed * ClampedInput.Size();
		const FVector NewHorizontalVelocity = FMath::VInterpTo(
			HorizontalVelocity,
			DesiredHorizontalVelocity,
			DeltaSeconds,
			FMath::Clamp(DefaultPhysicsPropDefinition->MovementVelocityInterpSpeed, 0.1f, 50.0f));
		GrayboxPropBody->SetPhysicsLinearVelocity(
			FVector(NewHorizontalVelocity.X, NewHorizontalVelocity.Y, LinearVelocity.Z),
			false);
		if (!bPhysicsMovementWasActive && DefaultPhysicsPropDefinition->MovementHopImpulse > 0.0f)
		{
			const float HopVelocity = FMath::Clamp(DefaultPhysicsPropDefinition->MovementHopImpulse, 0.0f, 250.0f);
			GrayboxPropBody->AddImpulse(FVector::UpVector * GrayboxPropBody->GetMass() * HopVelocity);
		}
	}
	else
	{
		const FVector LinearVelocity = GrayboxPropBody->GetPhysicsLinearVelocity();
		const FVector HorizontalVelocity(LinearVelocity.X, LinearVelocity.Y, 0.0f);
		const FVector NewHorizontalVelocity = FMath::VInterpTo(
			HorizontalVelocity,
			FVector::ZeroVector,
			DeltaSeconds,
			FMath::Clamp(DefaultPhysicsPropDefinition->MovementStopInterpSpeed, 0.1f, 100.0f));
		GrayboxPropBody->SetPhysicsLinearVelocity(
			FVector(NewHorizontalVelocity.X, NewHorizontalVelocity.Y, LinearVelocity.Z),
			false);
	}
	bPhysicsMovementWasActive = bHasMovementInput;
	ApplyPhysicsPropStraighten(DeltaSeconds);
}

void APHPropCharacter::ApplyPhysicsPropStraighten(const float DeltaSeconds)
{
	if (GrayboxPropBody == nullptr || DefaultPhysicsPropDefinition == nullptr)
	{
		return;
	}
	const bool bStraightenActive = bServerPhysicsStraightenHeld && IsUsingPhysicsPropMovement();
	if (bStraightenActive != bPhysicsStraightenWasActive)
	{
		GrayboxPropBody->SetAngularDamping(bStraightenActive
			? FMath::Max(0.0f, DefaultPhysicsPropDefinition->StraightenAngularDamping)
			: FMath::Max(0.0f, DefaultPhysicsPropDefinition->FreeAngularDamping));
		GrayboxPropBody->SetPhysMaterialOverride(bStraightenActive
			? DefaultPhysicsPropDefinition->StraightenPhysicalMaterial
			: DefaultPhysicsPropDefinition->FreePhysicalMaterial);
		if (!bStraightenActive)
		{
			CurrentPhysicsStraightenInterpSpeed = 0.0f;
		}
		bPhysicsStraightenWasActive = bStraightenActive;
	}
	if (!bStraightenActive)
	{
		return;
	}

	CurrentPhysicsStraightenInterpSpeed = FMath::FInterpTo(
		CurrentPhysicsStraightenInterpSpeed,
		FMath::Max(0.0f, DefaultPhysicsPropDefinition->StraightenTargetInterpSpeed),
		DeltaSeconds,
		FMath::Max(0.0f, DefaultPhysicsPropDefinition->StraightenInterpSpeedAcceleration));
	const FRotator TargetRotation(0.0f, ServerPhysicsViewYaw, 0.0f);
	const FRotator CandidateRotation = FMath::RInterpTo(
		GrayboxPropBody->GetComponentRotation(),
		TargetRotation,
		DeltaSeconds,
		CurrentPhysicsStraightenInterpSpeed);
	FHitResult RotationHit;
	GrayboxPropBody->MoveComponent(
		FVector::ZeroVector,
		CandidateRotation.Quaternion(),
		true,
		&RotationHit,
		MOVECOMP_NoFlags,
		ETeleportType::TeleportPhysics);
}

void APHPropCharacter::TryPhysicsPropJump()
{
	if (!HasAuthority() || !IsUsingPhysicsPropMovement() || GrayboxPropBody == nullptr
		|| DefaultPhysicsPropDefinition == nullptr || !GrayboxPropBody->IsSimulatingPhysics() || GetWorld() == nullptr)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const double Cooldown = FMath::Clamp(static_cast<double>(DefaultPhysicsPropDefinition->JumpCooldownSeconds), 0.05, 2.0);
	if (Now - LastPhysicsPropJumpTime < Cooldown)
	{
		return;
	}
	const bool bGrounded = IsPhysicsPropGrounded();
	const int32 MaximumJumpCount = FMath::Clamp(DefaultPhysicsPropDefinition->MaximumJumpCount, 1, 3);
	if (bGrounded && !bPhysicsWasGrounded)
	{
		PhysicsJumpsUsed = 0;
	}
	if ((!bGrounded && PhysicsJumpsUsed == 0) || PhysicsJumpsUsed >= MaximumJumpCount)
	{
		return;
	}
	const float PhysicsJumpVelocity = FMath::Clamp(DefaultPhysicsPropDefinition->JumpVelocity, 0.0f, 1000.0f);
	if (PhysicsJumpVelocity <= 0.0f)
	{
		return;
	}

	const FVector CurrentVelocity = GrayboxPropBody->GetPhysicsLinearVelocity();
	FVector BoostDirection = FVector::ZeroVector;
	const FVector2D RawInput(ServerPhysicsForwardInput, ServerPhysicsRightInput);
	if (RawInput.SizeSquared() > FMath::Square(0.05f))
	{
		const FVector2D ClampedInput = RawInput.GetClampedToMaxSize(1.0f);
		const FRotator YawRotation(0.0f, ServerPhysicsViewYaw, 0.0f);
		BoostDirection = (
			FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X) * ClampedInput.X
			+ FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y) * ClampedInput.Y).GetSafeNormal();
	}
	const float MaximumJumpHorizontalSpeed = FMath::Clamp(
		DefaultPhysicsPropDefinition->MaximumJumpHorizontalSpeed,
		FMath::Max(1.0f, DefaultPhysicsPropDefinition->MaximumHorizontalSpeed),
		2500.0f);
	const FVector TargetVelocity = PHPhysicsPropPresentation::ComputeJumpTargetVelocity(
		CurrentVelocity,
		BoostDirection,
		PhysicsJumpVelocity,
		DefaultPhysicsPropDefinition->JumpHorizontalBoostVelocity,
		MaximumJumpHorizontalSpeed);
	LastPhysicsPropJumpTime = Now;
	++PhysicsJumpsUsed;
	const FVector JumpImpulse = (TargetVelocity - CurrentVelocity) * GrayboxPropBody->GetMass();
	GrayboxPropBody->AddImpulse(JumpImpulse);
	GrayboxPropBody->WakeAllRigidBodies();
	ReplicatedPhysicsBodyLinearVelocity = TargetVelocity;
	bReplicatedPhysicsBodyGrounded = false;
	ForceNetUpdate();
	UE_LOG(LogPHPhysicsProp, Log,
		TEXT("Authoritative playable Prop jump %d/%d accepted for %s: impulse=%.1f vertical=%.1f horizontal=%.1f grounded=%s."),
		PhysicsJumpsUsed,
		MaximumJumpCount,
		*GetName(),
		JumpImpulse.Size(),
		TargetVelocity.Z,
		TargetVelocity.Size2D(),
		bGrounded ? TEXT("yes") : TEXT("no"));
}

bool APHPropCharacter::IsPhysicsPropGrounded() const
{
	FHitResult GroundHit;
	return FindPhysicsPropGround(GroundHit);
}

bool APHPropCharacter::FindPhysicsPropGround(FHitResult& OutGroundHit) const
{
	OutGroundHit = FHitResult();
	const UWorld* World = GetWorld();
	if (World == nullptr || GrayboxPropBody == nullptr || DefaultPhysicsPropDefinition == nullptr)
	{
		return false;
	}
	const FBox CollisionBounds = GrayboxPropBody->BodyInstance.GetBodyBounds();
	const bool bHasCollisionBounds = CollisionBounds.IsValid != 0;
	const FBoxSphereBounds RenderBounds = GrayboxPropBody->Bounds;
	const float ProbeDistance = FMath::Clamp(DefaultPhysicsPropDefinition->GroundProbeDistance, 1.0f, 100.0f);
	const FVector Start = bHasCollisionBounds ? CollisionBounds.GetCenter() : RenderBounds.Origin;
	const float BodyHalfHeight = bHasCollisionBounds ? CollisionBounds.GetExtent().Z : RenderBounds.BoxExtent.Z;
	const FVector End = Start - FVector::UpVector * (BodyHalfHeight + ProbeDistance);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHPlayablePhysicsPropGround), false, this);
	return World->SweepSingleByChannel(
		OutGroundHit,
		Start,
		End,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(8.0f),
		QueryParams)
		&& OutGroundHit.ImpactNormal.Z >= 0.25f;
}

void APHPropCharacter::SynchronizeActorToPhysicsProp()
{
	if (!HasAuthority() || !IsUsingPhysicsPropMovement() || GrayboxPropBody == nullptr
		|| !GrayboxPropBody->IsSimulatingPhysics())
	{
		return;
	}
	const FTransform BodyTransform = GrayboxPropBody->GetComponentTransform();
	SetActorLocation(BodyTransform.GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	GrayboxPropBody->SetWorldTransform(BodyTransform, false, nullptr, ETeleportType::TeleportPhysics);
	ReplicatedPhysicsBodyRotation = BodyTransform.Rotator();
	ReplicatedPhysicsBodyLinearVelocity = GrayboxPropBody->GetPhysicsLinearVelocity();
	ReplicatedPhysicsBodyAngularVelocity = GrayboxPropBody->GetPhysicsAngularVelocityInDegrees();
	bReplicatedPhysicsBodyGrounded = bPhysicsWasGrounded;
	ApplyReplicatedPhysicsPose();
}

FPHPhysicsPropPresentationSettings APHPropCharacter::GetPhysicsPropPresentationSettings() const
{
	FPHPhysicsPropPresentationSettings Settings;
	if (DefaultPhysicsPropDefinition != nullptr)
	{
		Settings.LocationSmoothingSpeed = DefaultPhysicsPropDefinition->NetworkVisualLocationSmoothingSpeed;
		Settings.RotationSmoothingSpeed = DefaultPhysicsPropDefinition->NetworkVisualRotationSmoothingSpeed;
		Settings.MaximumExtrapolationSeconds = DefaultPhysicsPropDefinition->NetworkVisualMaximumExtrapolationSeconds;
		Settings.TeleportDistance = DefaultPhysicsPropDefinition->NetworkVisualTeleportDistance;
	}
	return Settings;
}

void APHPropCharacter::ResetPhysicsPropPresentation()
{
	PhysicsPropPresentationState = FPHPhysicsPropPresentationState();
	if (PropPresentationRoot != nullptr)
	{
		PropPresentationRoot->SetRelativeTransform(FTransform::Identity);
	}
}

void APHPropCharacter::ApplyReplicatedPhysicsPose(const float DeltaSeconds)
{
	if (!IsUsingPhysicsPropMovement() || GrayboxPropBody == nullptr || PropPresentationRoot == nullptr)
	{
		return;
	}

	FQuat VisualBodyRotation = ReplicatedPhysicsBodyRotation.Quaternion();
	if (HasAuthority())
	{
		PropPresentationRoot->SetRelativeTransform(FTransform::Identity);
	}
	else
	{
		const UWorld* World = GetWorld();
		PHPhysicsPropPresentation::Advance(
			PhysicsPropPresentationState,
			GetActorLocation(),
			VisualBodyRotation,
			FVector(ReplicatedPhysicsBodyLinearVelocity),
			FVector(ReplicatedPhysicsBodyAngularVelocity),
			World != nullptr ? World->GetTimeSeconds() : 0.0,
			DeltaSeconds,
			GetPhysicsPropPresentationSettings());
		PropPresentationRoot->SetWorldLocation(
			PhysicsPropPresentationState.Location,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		VisualBodyRotation = PhysicsPropPresentationState.Rotation;
	}

	const FQuat RelativeBodyRotation = PropPresentationRoot->GetComponentQuat().Inverse() * VisualBodyRotation;
	if (!HasAuthority() || !GrayboxPropBody->IsSimulatingPhysics())
	{
		GrayboxPropBody->SetRelativeLocation(FVector::ZeroVector);
		GrayboxPropBody->SetRelativeRotation(RelativeBodyRotation);
	}
	if (!HasAuthority() && bReplicatedPhysicsBodyGrounded && DefaultPhysicsPropDefinition != nullptr)
	{
		FHitResult GroundHit;
		if (FindPhysicsPropGround(GroundHit))
		{
			const FBoxSphereBounds VisualBounds = GrayboxPropBody->Bounds;
			const float MaximumCorrection = FMath::Clamp(
				DefaultPhysicsPropDefinition->GroundProbeDistance,
				1.0f,
				100.0f);
			const float GroundCorrection = PHPhysicsPropPresentation::ComputeGroundContactCorrection(
				true,
				VisualBounds.Origin.Z - VisualBounds.BoxExtent.Z,
				GroundHit.ImpactPoint.Z,
				MaximumCorrection);
			if (!FMath::IsNearlyZero(GroundCorrection, 0.05f))
			{
				PropPresentationRoot->AddWorldOffset(
					FVector::UpVector * GroundCorrection,
					false,
					nullptr,
					ETeleportType::TeleportPhysics);
			}
		}
	}
	const FPHResolvedPropHitbox BaseHitbox = ResolveHitbox(TransformationState.ActiveForm);
	FPHResolvedPropHitbox RotatedHitbox = BaseHitbox;
	RotatedHitbox.RelativeLocation = RelativeBodyRotation.RotateVector(BaseHitbox.RelativeLocation);
	RotatedHitbox.RelativeRotation = (RelativeBodyRotation * BaseHitbox.RelativeRotation.Quaternion()).Rotator();
	ApplyHitbox(RotatedHitbox);
}

void APHPropCharacter::OnRep_PhysicsBodyRotation()
{
	ApplyReplicatedPhysicsPose();
}

void APHPropCharacter::OnPhysicsPropHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	const FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (!HasAuthority() || HitComponent != GrayboxPropBody || OtherComponent == nullptr
		|| !IsUsingPhysicsPropMovement() || TransformationState.ActiveForm == nullptr
		|| DefaultPhysicsPropDefinition == nullptr || DefaultPhysicsPropDefinition->ImpactSound == nullptr
		|| TransformationState.ActiveForm->FormId != DefaultPhysicsPropDefinition->PropId)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const float Threshold = DefaultPhysicsPropDefinition->ImpactImpulsePerMassThreshold
		* GrayboxPropBody->GetMass();
	if (Threshold <= 0.0f || NormalImpulse.SizeSquared() <= FMath::Square(Threshold))
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const double Cooldown = FMath::Clamp(
		static_cast<double>(DefaultPhysicsPropDefinition->ImpactSoundCooldownSeconds),
		0.05,
		2.0);
	if (Now - LastPhysicsPropImpactSoundTime < Cooldown)
	{
		return;
	}

	LastPhysicsPropImpactSoundTime = Now;
	const FVector ImpactLocation = Hit.ImpactPoint.IsNearlyZero()
		? GrayboxPropBody->GetComponentLocation()
		: FVector(Hit.ImpactPoint);
	MulticastPlayPhysicsPropImpactSound(ImpactLocation);
	UE_LOG(LogPHTransformation, Log,
		TEXT("Authoritative playable chicken impact for %s: impulse %.2f, threshold %.2f, other %s."),
		*GetName(), NormalImpulse.Size(), Threshold,
		OtherActor != nullptr ? *OtherActor->GetName() : TEXT("none"));
}

void APHPropCharacter::MulticastPlayPhysicsPropImpactSound_Implementation(
	const FVector_NetQuantize ImpactLocation)
{
	UWorld* World = GetWorld();
	if (World == nullptr || World->IsNetMode(NM_DedicatedServer)
		|| DefaultPhysicsPropDefinition == nullptr
		|| DefaultPhysicsPropDefinition->ImpactSound == nullptr)
	{
		return;
	}

	// The authority already validated the ScreamingChicken FormId. Re-checking
	// replicated form state here can drop a valid sound when an RPC overtakes a
	// property update on a remote dedicated-server client.
	UAudioComponent* AudioComponent = NewObject<UAudioComponent>(this);
	if (AudioComponent != nullptr)
	{
		AudioComponent->bAutoActivate = false;
		AudioComponent->bAutoDestroy = true;
		DefaultPhysicsPropDefinition->ConfigureImpactAudioComponent(*AudioComponent);
		AudioComponent->SetWorldLocation(ImpactLocation);
		AudioComponent->RegisterComponent();
		AudioComponent->Play();
		UE_LOG(LogPHTransformation, Log,
			TEXT("Playable chicken impact multicast received for %s on local role %d at %s."),
			*GetName(), static_cast<int32>(GetLocalRole()), *FVector(ImpactLocation).ToCompactString());
	}
}

bool APHPropCharacter::ServerTryBeCarriedBy(APHHunterCharacter& Hunter)
{
	if (!HasAuthority() || CaptureState != EPHPropCaptureState::Downed || Carrier != nullptr)
	{
		return false;
	}

	ClearRecoveryHelpers();
	StopAssistingDownedTarget();
	ResetCarryStruggleInputWindow();
	Carrier = &Hunter;
	RetentionPoint = nullptr;
	SetCaptureState(EPHPropCaptureState::Carried);
	AttachToComponent(Hunter.GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(GetActiveCarryAttachmentOffset());
	SetActorRelativeRotation(GetActiveCarryAttachmentRotation());
	return true;
}

void APHPropCharacter::RequestSwitchHumanPrototype()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	APHHumanPrototypeSelector* RequestedSelector = nullptr;
	if (!TryResolveHumanPrototypeSelector(RequestedSelector) || RequestedSelector == nullptr)
	{
		return;
	}

	if (HasAuthority())
	{
		RequestedSelector->ServerTryUse(*this);
	}
	else
	{
		ServerRequestSwitchHumanPrototype(RequestedSelector);
	}
}

bool APHPropCharacter::HasAlternativeHumanPrototype() const
{
	return AlternativeHumanSkeletalMesh != nullptr && AlternativeHumanWalkAnimation != nullptr;
}

bool APHPropCharacter::IsPlayingCarriedMoveAnimation() const
{
	return CaptureState == EPHPropCaptureState::Carried
		&& CurrentHumanAnimation != nullptr
		&& CurrentHumanAnimation == GetActiveHumanCarriedMoveAnimation();
}

FName APHPropCharacter::GetActiveHumanPrototypeName() const
{
	return bUseAlternativeHumanPrototype ? AlternativeHumanPrototypeName : PrimaryHumanPrototypeName;
}

bool APHPropCharacter::ServerToggleHumanPrototype()
{
	const APHPlayerState* PHPlayerState = GetPlayerState<APHPlayerState>();
	if (!HasAuthority() || PHPlayerState == nullptr || PHPlayerState->GetPlayerRole() != EPHPlayerRole::Prop
		|| (CaptureState != EPHPropCaptureState::Free && CaptureState != EPHPropCaptureState::Grace)
		|| TransformationState.ActiveForm != nullptr || !HasAlternativeHumanPrototype())
	{
		return false;
	}

	bUseAlternativeHumanPrototype = !bUseAlternativeHumanPrototype;
	OnRep_HumanPrototype();
	ForceNetUpdate();
	return true;
}

void APHPropCharacter::ServerDropFromCarrier()
{
	if (!HasAuthority() || CaptureState != EPHPropCaptureState::Carried)
	{
		return;
	}

	if (Carrier != nullptr)
	{
		Carrier->ClearCarriedProp(this);
	}
	Carrier = nullptr;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	const FRotator DropRotation = GetActorRotation();
	SetActorRotation(FRotator(0.0f, DropRotation.Yaw, 0.0f), ETeleportType::TeleportPhysics);
	ResetCarryStruggleInputWindow();
	SetCaptureState(EPHPropCaptureState::Downed);
	DownedRecoveryProgress = PHCaptureFlow::GetRecoveryProgressAfterHunterDrop(
		DownedRecoveryProgress,
		CarryDropRecoveryBonus);
	LastPublishedDownedRecoveryProgress = DownedRecoveryProgress;
	OnRep_CaptureProgress();
	SetActorLocation(GetActorLocation() + FVector::UpVector * 20.0f, true);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Falling);
	}
	ForceNetUpdate();
}

void APHPropCharacter::ServerRetainAt(APHRetentionPoint& NewRetentionPoint)
{
	if (!HasAuthority() || CaptureState != EPHPropCaptureState::Carried)
	{
		return;
	}

	if (Carrier != nullptr)
	{
		Carrier->ClearCarriedProp(this);
	}
	Carrier = nullptr;
	RetentionPoint = &NewRetentionPoint;
	ResetCaptureProgress();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	AttachToComponent(NewRetentionPoint.GetRetentionAnchor(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	++RetentionCount;

	if (PHCaptureFlow::ShouldEliminateOnRetention(RetentionCount, MaximumRetentionCount))
	{
		SetCaptureState(EPHPropCaptureState::Eliminated);
		NewRetentionPoint.ClearRetainedProp(this);
		return;
	}

	const float Duration = PHCaptureFlow::GetRetentionDuration(
		RetentionCount, FirstRetentionDuration, SecondRetentionDuration);
	SetCaptureState(EPHPropCaptureState::Retained, Duration);
	if (APHGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<APHGameMode>() : nullptr)
	{
		GameMode->NotifyPropRetained(*this);
	}
}

void APHPropCharacter::ServerClampRetainedEliminationDelay(const float MaximumRemainingSeconds)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || World == nullptr || CaptureState != EPHPropCaptureState::Retained)
	{
		return;
	}

	const float SafeMaximumRemaining = FMath::Clamp(MaximumRemainingSeconds, 0.1f, 5.0f);
	const float RemainingSeconds = CaptureStateEndServerTime - World->GetTimeSeconds();
	if (RemainingSeconds > 0.0f && RemainingSeconds <= SafeMaximumRemaining + KINDA_SMALL_NUMBER)
	{
		return;
	}

	CaptureStateEndServerTime = World->GetTimeSeconds() + SafeMaximumRemaining;
	GetWorldTimerManager().ClearTimer(CaptureStateTimerHandle);
	GetWorldTimerManager().SetTimer(
		CaptureStateTimerHandle,
		this,
		&APHPropCharacter::FinishRetention,
		SafeMaximumRemaining,
		false);
	ForceNetUpdate();
	UE_LOG(LogPHCapture, Log,
		TEXT("All remaining Props are retained; clamped %s elimination delay to %.1f seconds."),
		*GetName(), SafeMaximumRemaining);
}

void APHPropCharacter::ServerReleaseWithGrace()
{
	if (!HasAuthority() || (CaptureState != EPHPropCaptureState::Retained
		&& CaptureState != EPHPropCaptureState::Downed))
	{
		return;
	}

	APHRetentionPoint* PreviousPoint = RetentionPoint;
	RetentionPoint = nullptr;
	Carrier = nullptr;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (PreviousPoint != nullptr)
	{
		SetActorLocation(PreviousPoint->GetActorLocation() + PreviousPoint->GetActorRightVector() * 160.0f
			+ FVector::UpVector * InitialCapsuleHalfHeight, false);
		PreviousPoint->ClearRetainedProp(this);
	}
	ResetCaptureProgress();
	SetCaptureState(EPHPropCaptureState::Grace, GraceDuration);
}

bool APHPropCharacter::ServerStartMemento(
	const FVector ImpactPoint,
	const FVector LaunchVelocity,
	const float DurationSeconds)
{
	if (!HasAuthority() || CaptureState != EPHPropCaptureState::Downed
		|| bMementoInProgress || ImpactPoint.ContainsNaN() || LaunchVelocity.ContainsNaN())
	{
		return false;
	}

	ClearRecoveryHelpers();
	StopAssistingDownedTarget();
	StopRetentionRescue();
	bMementoInProgress = true;
	MementoImpactPoint = ImpactPoint;
	const FVector HorizontalDirection(LaunchVelocity.X, LaunchVelocity.Y, 0.0f);
	if (!HorizontalDirection.IsNearlyZero())
	{
		SetActorRotation(HorizontalDirection.Rotation(), ETeleportType::TeleportPhysics);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Falling);
	}
	LaunchCharacter(LaunchVelocity, true, true);
	GetWorldTimerManager().ClearTimer(MementoTimerHandle);
	GetWorldTimerManager().SetTimer(
		MementoTimerHandle,
		this,
		&APHPropCharacter::FinishMemento,
		FMath::Clamp(DurationSeconds, 0.35f, 3.0f),
		false);
	OnRep_MementoState();
	ForceNetUpdate();
	return true;
}

void APHPropCharacter::ResetCaptureForMatch()
{
	if (!HasAuthority())
	{
		return;
	}

	ClearCaptureRelationships();
	GetWorldTimerManager().ClearTimer(MementoTimerHandle);
	bMementoInProgress = false;
	MementoImpactPoint = FVector::ZeroVector;
	RetentionCount = 0;
	ResetCaptureProgress();
	SetActorHiddenInGame(false);
	SetCaptureState(EPHPropCaptureState::Free);
}

void APHPropCharacter::RequestPropTransformation()
{
	UWorld* World = GetWorld();
	if (!IsLocallyControlled() || World == nullptr)
	{
		return;
	}

	const double LocalTimeSeconds = World->GetTimeSeconds();
	if (LocalTimeSeconds - LastLocalTransformationRequestTime < GetSafeTransformationCooldownSeconds())
	{
		return;
	}

	LastLocalTransformationRequestTime = LocalTimeSeconds;
	BP_OnPropTransformationAnticipated(false);
	APHPropTransformTarget* RequestedTarget = nullptr;
	TryResolvePresentationTransformTarget(RequestedTarget);
	if (HasAuthority())
	{
		PerformAuthoritativeTransformationRequest(RequestedTarget);
	}
	else
	{
		ServerRequestPropTransformation(RequestedTarget);
	}
}

void APHPropCharacter::RequestReturnToInitialForm()
{
	UWorld* World = GetWorld();
	if (!IsLocallyControlled() || World == nullptr)
	{
		return;
	}

	const double LocalTimeSeconds = World->GetTimeSeconds();
	if (LocalTimeSeconds - LastLocalTransformationRequestTime < GetSafeTransformationCooldownSeconds())
	{
		return;
	}

	LastLocalTransformationRequestTime = LocalTimeSeconds;
	BP_OnPropTransformationAnticipated(true);
	if (HasAuthority())
	{
		PerformAuthoritativeReturnRequest();
	}
	else
	{
		ServerRequestReturnToInitialForm();
	}
}

bool APHPropCharacter::IsPropFormAllowed(const UPHPropFormDataAsset* CandidateForm) const
{
	return CandidateForm != nullptr && AllowedPropForms.Contains(CandidateForm);
}

void APHPropCharacter::RequestStartObjectiveInteraction()
{
	if (!IsLocallyControlled() || CaptureState != EPHPropCaptureState::Free)
	{
		return;
	}

	APHObjectiveActor* RequestedObjective = nullptr;
	if (!TryResolveObjectiveTarget(RequestedObjective))
	{
		return;
	}

	BP_OnObjectiveInteractionAnticipated(true, RequestedObjective);
	if (HasAuthority())
	{
		PerformAuthoritativeStartObjectiveInteraction(RequestedObjective);
	}
	else
	{
		ServerRequestStartObjectiveInteraction(RequestedObjective);
	}
}

void APHPropCharacter::RequestStopObjectiveInteraction()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	BP_OnObjectiveInteractionAnticipated(false, ActiveObjective);
	if (HasAuthority())
	{
		PerformAuthoritativeStopObjectiveInteraction();
	}
	else
	{
		ServerRequestStopObjectiveInteraction();
	}
}

void APHPropCharacter::SetActiveObjectiveFromServer(APHObjectiveActor* Objective)
{
	if (!HasAuthority() || ActiveObjective == Objective)
	{
		return;
	}

	ActiveObjective = Objective;
	OnRep_ActiveObjective();
	ForceNetUpdate();
}

void APHPropCharacter::ClearActiveObjectiveFromServer(const APHObjectiveActor* ExpectedObjective)
{
	if (!HasAuthority() || ActiveObjective == nullptr || ActiveObjective != ExpectedObjective)
	{
		return;
	}

	ActiveObjective = nullptr;
	OnRep_ActiveObjective();
	ForceNetUpdate();
}

void APHPropCharacter::SetActiveExitGateFromServer(APHExitGate* ExitGate)
{
	if (!HasAuthority() || ActiveExitGate == ExitGate)
	{
		return;
	}
	ActiveExitGate = ExitGate;
	ForceNetUpdate();
}

void APHPropCharacter::ClearActiveExitGateFromServer(const APHExitGate* ExpectedGate)
{
	if (!HasAuthority() || ActiveExitGate == nullptr || ActiveExitGate != ExpectedGate)
	{
		return;
	}
	ActiveExitGate = nullptr;
	ForceNetUpdate();
}

void APHPropCharacter::ServerEscapeThroughGate(APHExitGate& ExitGate)
{
	if (!HasAuthority() || !ExitGate.IsGateOpen()
		|| (CaptureState != EPHPropCaptureState::Free && CaptureState != EPHPropCaptureState::Grace))
	{
		return;
	}
	PerformAuthoritativeStopObjectiveInteraction();
	PerformAuthoritativeStopExitGateInteraction();
	if (TransformationState.ActiveForm != nullptr)
	{
		TryReturnToInitialFormFromServer(TEXT("exit gate escape"));
	}
	SetCaptureState(EPHPropCaptureState::Escaped);
}

void APHPropCharacter::SetActiveRetentionRescueFromServer(APHRetentionPoint* Point)
{
	if (!HasAuthority() || Point == nullptr || ActiveRetentionRescuePoint == Point)
	{
		return;
	}

	StopRetentionRescue();
	ActiveRetentionRescuePoint = Point;
	OnRep_ActiveRetentionRescuePoint();
	ForceNetUpdate();
}

void APHPropCharacter::ClearActiveRetentionRescueFromServer(const APHRetentionPoint* ExpectedPoint)
{
	if (!HasAuthority() || ActiveRetentionRescuePoint == nullptr
		|| ActiveRetentionRescuePoint != ExpectedPoint)
	{
		return;
	}

	ActiveRetentionRescuePoint = nullptr;
	OnRep_ActiveRetentionRescuePoint();
	ForceNetUpdate();
}

FVector APHPropCharacter::GetMeleeLineOfSightPoint(const FVector& FromLocation) const
{
	const UPrimitiveComponent* ActiveHitbox = TransformationState.HitboxShape == EPHPropHitboxShape::Box
		? Cast<UPrimitiveComponent>(PropBoxHitbox)
		: Cast<UPrimitiveComponent>(PropCapsuleHitbox);
	if (ActiveHitbox == nullptr)
	{
		return GetActorLocation();
	}

	FVector ClosestPoint = ActiveHitbox->GetComponentLocation();
	ActiveHitbox->GetClosestPointOnCollision(FromLocation, ClosestPoint);
	return ClosestPoint;
}

void APHPropCharacter::ServerRequestPropTransformation_Implementation(APHPropTransformTarget* RequestedTarget)
{
	PerformAuthoritativeTransformationRequest(RequestedTarget);
}

void APHPropCharacter::ServerRequestReturnToInitialForm_Implementation()
{
	PerformAuthoritativeReturnRequest();
}

void APHPropCharacter::ServerRequestStartObjectiveInteraction_Implementation(APHObjectiveActor* RequestedObjective)
{
	PerformAuthoritativeStartObjectiveInteraction(RequestedObjective);
}

void APHPropCharacter::ServerRequestStopObjectiveInteraction_Implementation()
{
	PerformAuthoritativeStopObjectiveInteraction();
}

void APHPropCharacter::ServerRequestStartExitGateInteraction_Implementation(APHExitGate* RequestedGate)
{
	PerformAuthoritativeStartExitGateInteraction(RequestedGate);
}

void APHPropCharacter::ServerRequestStopExitGateInteraction_Implementation()
{
	PerformAuthoritativeStopExitGateInteraction();
}

void APHPropCharacter::ServerRequestCaptureRescue_Implementation(APHRetentionPoint* RequestedPoint)
{
	if (RequestedPoint != nullptr && PrepareHumanFormForSupport(TEXT("retention rescue")))
	{
		StopAssistingDownedTarget();
		RequestedPoint->ServerTryBeginRelease(*this);
	}
}

void APHPropCharacter::ServerRequestCaptureAssist_Implementation(APHPropCharacter* RequestedTarget)
{
	if (RequestedTarget != nullptr)
	{
		BeginAssistingDownedTarget(*RequestedTarget);
	}
}

void APHPropCharacter::ServerRequestStopCaptureAssist_Implementation()
{
	StopAssistingDownedTarget();
	StopRetentionRescue();
}

void APHPropCharacter::ServerSubmitCarryStruggleInput_Implementation(const int8 Direction)
{
	SubmitCarryStruggleInput(Direction);
}

void APHPropCharacter::ServerRequestSwitchHumanPrototype_Implementation(
	APHHumanPrototypeSelector* RequestedSelector)
{
	if (RequestedSelector != nullptr)
	{
		RequestedSelector->ServerTryUse(*this);
	}
}

void APHPropCharacter::OnRep_MeleeHitSequence()
{
	BP_OnAuthoritativeMeleeHit();
}

void APHPropCharacter::OnRep_TransformationState()
{
	ApplyTransformationState();
	ApplyLocalViewMode();
	ApplyMovementSpeed();
	if (TransformationState.LastResult != EPHPropTransformationResult::None)
	{
		BP_OnPropTransformationConfirmed(TransformationState.LastResult, TransformationState.ActiveForm);
	}
}

void APHPropCharacter::OnRep_ActiveObjective()
{
	BP_OnObjectiveInteractionConfirmed(ActiveObjective);
}

void APHPropCharacter::OnRep_ActiveRetentionRescuePoint()
{
	OnRep_CaptureProgress();
}

void APHPropCharacter::OnRep_HumanPrototype()
{
	ConfigureHumanPresentation();
	ApplyLocalViewMode();
	BP_OnHumanPrototypeChanged(GetActiveHumanPrototypeName());
}

void APHPropCharacter::StartHumanSprint()
{
	SetHumanSprintRequested(true);
}

void APHPropCharacter::StopHumanSprint()
{
	SetHumanSprintRequested(false);
}

void APHPropCharacter::SetHumanSprintRequested(const bool bRequested)
{
	if (!IsLocallyControlled())
	{
		return;
	}

	bWantsHumanSprint = bRequested && CanUseHumanSprint();
	ApplyMovementSpeed();
	if (!HasAuthority())
	{
		ServerSetHumanSprintRequested(bRequested);
	}
}

void APHPropCharacter::ServerSetHumanSprintRequested_Implementation(const bool bRequested)
{
	bWantsHumanSprint = bRequested && CanUseHumanSprint();
	ApplyMovementSpeed();
}

void APHPropCharacter::OnRep_HumanStamina()
{
	CurrentHumanStamina = FMath::Clamp(
		CurrentHumanStamina,
		0.0f,
		FMath::Max(1.0f, MaximumHumanStamina));
	if (CurrentHumanStamina <= KINDA_SMALL_NUMBER)
	{
		bWantsHumanSprint = false;
	}
	ApplyMovementSpeed();
	BP_OnHumanStaminaChanged(CurrentHumanStamina, GetHumanStaminaNormalized());
}

bool APHPropCharacter::CanUseHumanSprint() const
{
	return TransformationState.ActiveForm == nullptr
		&& (CaptureState == EPHPropCaptureState::Free || CaptureState == EPHPropCaptureState::Grace)
		&& CurrentHumanStamina > KINDA_SMALL_NUMBER;
}

void APHPropCharacter::UpdateHumanStamina(const float DeltaSeconds)
{
	if (DeltaSeconds <= 0.0f || GetWorld() == nullptr)
	{
		return;
	}

	const float SafeMaximum = FMath::Max(1.0f, MaximumHumanStamina);
	const bool bMoving = GetVelocity().SizeSquared2D() > FMath::Square(10.0f);
	const bool bDraining = bWantsHumanSprint && CanUseHumanSprint() && bMoving;
	float NewStamina = CurrentHumanStamina;
	if (bDraining)
	{
		NewStamina -= FMath::Max(0.1f, HumanSprintDrainPerSecond) * DeltaSeconds;
		LastHumanStaminaUseServerTime = GetWorld()->GetTimeSeconds();
	}
	else
	{
		const double RechargeDelay = FMath::Max(0.0f, HumanStaminaRechargeDelay);
		if (GetWorld()->GetTimeSeconds() - LastHumanStaminaUseServerTime >= RechargeDelay)
		{
			NewStamina += FMath::Max(0.1f, HumanStaminaRechargePerSecond) * DeltaSeconds;
		}
	}

	NewStamina = FMath::Clamp(NewStamina, 0.0f, SafeMaximum);
	if (FMath::IsNearlyEqual(NewStamina, CurrentHumanStamina, 0.01f))
	{
		return;
	}

	CurrentHumanStamina = NewStamina;
	if (CurrentHumanStamina <= KINDA_SMALL_NUMBER)
	{
		bWantsHumanSprint = false;
	}
	ApplyMovementSpeed();
	OnRep_HumanStamina();
	ForceNetUpdate();
}

void APHPropCharacter::UpdateDownedRecovery(const float DeltaSeconds)
{
	if (!HasAuthority() || CaptureState != EPHPropCaptureState::Downed
		|| bMementoInProgress || DeltaSeconds <= 0.0f)
	{
		return;
	}

	for (auto HelperIterator = RecoveryHelpers.CreateIterator(); HelperIterator; ++HelperIterator)
	{
		APHPropCharacter* Helper = HelperIterator->Get();
		if (Helper == nullptr || !Helper->CanAssistDownedTarget(*this))
		{
			if (Helper != nullptr && Helper->AssistedDownedProp == this)
			{
				Helper->AssistedDownedProp = nullptr;
				Helper->ForceNetUpdate();
			}
			HelperIterator.RemoveCurrent();
		}
	}

	const int32 NewHelperCount = FMath::Min(RecoveryHelpers.Num(), FMath::Clamp(MaximumRecoveryHelpers, 1, 4));
	const float SafeDuration = FMath::Clamp(DownedRecoveryDuration, 5.0f, 120.0f);
	const float HelperContribution = FMath::Clamp(AdditionalRecoveryContributionPerHelper, 0.0f, 3.0f);
	const float Contribution = 1.0f + NewHelperCount * HelperContribution;
	DownedRecoveryProgress = FMath::Clamp(
		DownedRecoveryProgress + DeltaSeconds * Contribution / SafeDuration,
		0.0f,
		1.0f);
	const bool bHelperCountChanged = RecoveryHelperCount != NewHelperCount;
	RecoveryHelperCount = NewHelperCount;

	if (bHelperCountChanged || DownedRecoveryProgress - LastPublishedDownedRecoveryProgress >= 0.01f
		|| DownedRecoveryProgress >= 1.0f - KINDA_SMALL_NUMBER)
	{
		LastPublishedDownedRecoveryProgress = DownedRecoveryProgress;
		OnRep_CaptureProgress();
		ForceNetUpdate();
	}

	if (DownedRecoveryProgress >= 1.0f - KINDA_SMALL_NUMBER)
	{
		ClearRecoveryHelpers();
		SetCaptureState(EPHPropCaptureState::Grace, GraceDuration);
	}
}

bool APHPropCharacter::CanJumpInternal_Implementation() const
{
	if (CaptureState == EPHPropCaptureState::Downed
		|| CaptureState == EPHPropCaptureState::Carried
		|| CaptureState == EPHPropCaptureState::Retained
		|| PHCaptureFlow::IsTerminal(CaptureState)
		|| bMementoInProgress)
	{
		return false;
	}

	if (!Super::CanJumpInternal_Implementation())
	{
		return false;
	}

	return TransformationState.ActiveForm != nullptr
		|| CurrentHumanStamina + KINDA_SMALL_NUMBER >= FMath::Max(0.0f, HumanJumpStaminaCost);
}

void APHPropCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	if (!HasAuthority() || TransformationState.ActiveForm != nullptr)
	{
		return;
	}

	CurrentHumanStamina = FMath::Clamp(
		CurrentHumanStamina - FMath::Max(0.0f, HumanJumpStaminaCost),
		0.0f,
		FMath::Max(1.0f, MaximumHumanStamina));
	LastHumanStaminaUseServerTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	OnRep_HumanStamina();
	ForceNetUpdate();
}

void APHPropCharacter::ApplyMovementSpeed()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement == nullptr)
	{
		return;
	}

	const float GraceMultiplier = CaptureState == EPHPropCaptureState::Grace
		? FMath::Clamp(GraceSpeedMultiplier, 1.0f, 2.0f)
		: 1.0f;
	if (CaptureState == EPHPropCaptureState::Downed)
	{
		Movement->MaxWalkSpeed = FMath::Clamp(DownedCrawlSpeed, 50.0f, 300.0f);
		return;
	}
	const float BaseSpeed = NormalWalkSpeed * GraceMultiplier;
	Movement->MaxWalkSpeed = bWantsHumanSprint && CanUseHumanSprint()
		? FMath::Max(BaseSpeed, HumanSprintSpeed * GraceMultiplier)
		: BaseSpeed;
}

void APHPropCharacter::ConfigureHumanPresentation()
{
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (CharacterMesh == nullptr)
	{
		return;
	}

	USkeletalMesh* ActiveHumanMesh = GetActiveHumanSkeletalMesh();
	const bool bHasHumanMesh = ActiveHumanMesh != nullptr;
	if (bHasHumanMesh)
	{
		CharacterMesh->SetSkeletalMeshAsset(ActiveHumanMesh);
		CharacterMesh->SetHiddenInGame(false);
		CharacterMesh->SetVisibility(true, true);
		CharacterMesh->SetOwnerNoSee(true);
		CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CharacterMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		CurrentHumanAnimation = nullptr;
		bCurrentHumanAnimationFrozen = false;
		UpdateLocomotionPresentation();
	}
	else
	{
		CharacterMesh->SetHiddenInGame(true);
	}

	bPlayingWalkAnimation = false;
}

void APHPropCharacter::PlayHumanIdlePresentation()
{
	if (TransformationState.ActiveForm != nullptr)
	{
		return;
	}

	UpdateLocomotionPresentation();
}

void APHPropCharacter::UpdateLocomotionPresentation()
{
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (CharacterMesh == nullptr || GetActiveHumanSkeletalMesh() == nullptr
		|| TransformationState.ActiveForm != nullptr)
	{
		return;
	}

	const bool bShouldWalk = GetVelocity().SizeSquared2D()
		> FMath::Square(FMath::Max(0.0f, HumanWalkAnimationThreshold));
	UAnimSequence* ActiveIdleAnimation = GetActiveHumanIdleAnimation();
	UAnimSequence* ActiveWalkAnimation = GetActiveHumanWalkAnimation();
	UAnimSequence* DesiredAnimation = nullptr;
	bool bFreezePose = false;
	if (CaptureState == EPHPropCaptureState::Carried)
	{
		const bool bCarrierMoving = Carrier != nullptr
			&& Carrier->GetVelocity().SizeSquared2D()
				> FMath::Square(FMath::Max(0.0f, HumanWalkAnimationThreshold));
		UAnimSequence* CarriedMoveAnimation = GetActiveHumanCarriedMoveAnimation();
		DesiredAnimation = bCarrierMoving && CarriedMoveAnimation != nullptr
			? CarriedMoveAnimation
			: GetActiveHumanCarriedAnimation();
	}
	else if (CaptureState == EPHPropCaptureState::Retained)
	{
		DesiredAnimation = GetActiveHumanCarriedAnimation();
	}
	else if (CaptureState == EPHPropCaptureState::Downed)
	{
		UAnimSequence* DownedMoveAnimation = GetActiveHumanDownedMoveAnimation();
		DesiredAnimation = bShouldWalk && DownedMoveAnimation != nullptr
			? DownedMoveAnimation
			: GetActiveHumanDownedIdleAnimation();
	}
	else
	{
		DesiredAnimation = bShouldWalk ? ActiveWalkAnimation : ActiveIdleAnimation;
		if (!bShouldWalk && DesiredAnimation == nullptr)
		{
			DesiredAnimation = ActiveWalkAnimation;
			bFreezePose = DesiredAnimation != nullptr;
		}
	}

	if (DesiredAnimation == nullptr)
	{
		DesiredAnimation = bShouldWalk ? ActiveWalkAnimation : ActiveIdleAnimation;
	}
	if (DesiredAnimation == nullptr)
	{
		return;
	}

	PlayHumanAnimation(DesiredAnimation, bFreezePose);
	if (CaptureState == EPHPropCaptureState::Carried
		&& Carrier != nullptr
		&& Carrier->GetMesh() != nullptr
		&& CharacterMesh->GetAnimationMode() == EAnimationMode::AnimationSingleNode)
	{
		CharacterMesh->SetPosition(Carrier->GetMesh()->GetPosition(), false);
	}
	bPlayingWalkAnimation = bShouldWalk && DesiredAnimation == ActiveWalkAnimation && !bFreezePose;
}

void APHPropCharacter::PlayHumanAnimation(UAnimSequence* Animation, const bool bFreezePose)
{
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (CharacterMesh == nullptr || Animation == nullptr)
	{
		return;
	}

	const bool bAnimationChanged = Animation != CurrentHumanAnimation;
	if (bAnimationChanged)
	{
		CharacterMesh->PlayAnimation(Animation, true);
		CurrentHumanAnimation = Animation;
	}

	if (bAnimationChanged || bFreezePose != bCurrentHumanAnimationFrozen)
	{
		if (bFreezePose)
		{
			CharacterMesh->SetPosition(0.0f, false);
		}
		CharacterMesh->SetPlayRate(bFreezePose ? 0.0f : 1.0f);
		bCurrentHumanAnimationFrozen = bFreezePose;
	}
}

void APHPropCharacter::ApplyLocalViewMode()
{
	if (HumanFirstPersonCamera == nullptr || ThirdPersonCamera == nullptr)
	{
		return;
	}

	const bool bUseThirdPerson = IsUsingPropThirdPersonCamera();
	const bool bUsePropForm = TransformationState.ActiveForm != nullptr;
	if (bUseThirdPerson)
	{
		bWantsHumanSprint = false;
	}
	HumanFirstPersonCamera->SetActive(!bUseThirdPerson);
	ThirdPersonCamera->SetActive(bUseThirdPerson);
	const bool bHasHumanMesh = GetMesh() != nullptr && GetActiveHumanSkeletalMesh() != nullptr;
	if (GetMesh() != nullptr)
	{
		GetMesh()->SetVisibility(!bUsePropForm && bHasHumanMesh, true);
		GetMesh()->SetOwnerNoSee(!bUseThirdPerson);
	}
	if (GrayboxPropBody != nullptr)
	{
		GrayboxPropBody->SetVisibility(bUsePropForm || !bHasHumanMesh, true);
		GrayboxPropBody->SetOwnerNoSee(!bUsePropForm);
	}
}

void APHPropCharacter::SetLocallySpectated(const bool bSpectated)
{
	if (bLocallySpectated == bSpectated)
	{
		return;
	}

	bLocallySpectated = bSpectated;
	if (bLocallySpectated)
	{
		SmoothedSpectatorViewRotation = ReplicatedSpectatorViewRotation;
	}
	ApplyLocalViewMode();
}

bool APHPropCharacter::TryResolveHumanPrototypeSelector(
	APHHumanPrototypeSelector*& OutSelector) const
{
	OutSelector = nullptr;
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	float BestDistanceSquared = MAX_flt;
	for (TActorIterator<APHHumanPrototypeSelector> SelectorIterator(World);
		SelectorIterator; ++SelectorIterator)
	{
		APHHumanPrototypeSelector* Candidate = *SelectorIterator;
		if (!IsValid(Candidate))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(
			GetActorLocation(), Candidate->GetActorLocation());
		const float MaximumDistance = FMath::Clamp(
			Candidate->GetInteractionDistance(), 100.0f, 600.0f);
		if (DistanceSquared <= FMath::Square(MaximumDistance)
			&& DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			OutSelector = Candidate;
		}
	}
	return OutSelector != nullptr;
}

USkeletalMesh* APHPropCharacter::GetActiveHumanSkeletalMesh() const
{
	return bUseAlternativeHumanPrototype && AlternativeHumanSkeletalMesh != nullptr
		? AlternativeHumanSkeletalMesh
		: HumanSkeletalMesh;
}

UAnimSequence* APHPropCharacter::GetActiveHumanIdleAnimation() const
{
	return bUseAlternativeHumanPrototype
		? AlternativeHumanIdleAnimation
		: HumanIdleAnimation;
}

UAnimSequence* APHPropCharacter::GetActiveHumanWalkAnimation() const
{
	return bUseAlternativeHumanPrototype && AlternativeHumanWalkAnimation != nullptr
		? AlternativeHumanWalkAnimation
		: HumanWalkAnimation;
}

UAnimSequence* APHPropCharacter::GetActiveHumanDownedIdleAnimation() const
{
	return bUseAlternativeHumanPrototype && AlternativeHumanDownedIdleAnimation != nullptr
		? AlternativeHumanDownedIdleAnimation
		: HumanDownedIdleAnimation;
}

UAnimSequence* APHPropCharacter::GetActiveHumanDownedMoveAnimation() const
{
	return bUseAlternativeHumanPrototype && AlternativeHumanDownedMoveAnimation != nullptr
		? AlternativeHumanDownedMoveAnimation
		: HumanDownedMoveAnimation;
}

UAnimSequence* APHPropCharacter::GetActiveHumanCarriedAnimation() const
{
	return bUseAlternativeHumanPrototype && AlternativeHumanCarriedAnimation != nullptr
		? AlternativeHumanCarriedAnimation
		: HumanCarriedAnimation;
}

UAnimSequence* APHPropCharacter::GetActiveHumanCarriedMoveAnimation() const
{
	return bUseAlternativeHumanPrototype && AlternativeHumanCarriedMoveAnimation != nullptr
		? AlternativeHumanCarriedMoveAnimation
		: HumanCarriedMoveAnimation;
}

FVector APHPropCharacter::GetActiveCarryAttachmentOffset() const
{
	return bUseAlternativeHumanPrototype
		? AlternativeCarryAttachmentOffset
		: PrimaryCarryAttachmentOffset;
}

FRotator APHPropCharacter::GetActiveCarryAttachmentRotation() const
{
	return bUseAlternativeHumanPrototype
		? AlternativeCarryAttachmentRotation
		: PrimaryCarryAttachmentRotation;
}

FVector APHPropCharacter::GetViewSelectionOrigin() const
{
	if (TransformationState.ActiveForm != nullptr && ThirdPersonCamera != nullptr)
	{
		return ThirdPersonCamera->GetComponentLocation();
	}
	if (HumanFirstPersonCamera != nullptr)
	{
		return HumanFirstPersonCamera->GetComponentLocation();
	}
	return GetActorLocation();
}

void APHPropCharacter::OnRep_CaptureState()
{
	LastLocalStruggleDirection = 0;
	LastLocalStruggleInputTime = -DBL_MAX;
	ApplyCaptureState();
	ApplyLocalViewMode();
	BP_OnCaptureStateChanged(CaptureState);
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<APHPlayerController> ControllerIterator(World); ControllerIterator; ++ControllerIterator)
		{
			if (ControllerIterator->IsLocalController())
			{
				ControllerIterator->HandlePropCaptureStateChanged(*this);
			}
		}
	}
}

void APHPropCharacter::OnRep_CaptureProgress()
{
	DownedRecoveryProgress = FMath::Clamp(DownedRecoveryProgress, 0.0f, 1.0f);
	RecoveryHelperCount = FMath::Max(0, RecoveryHelperCount);
	CarryStruggleProgress = FMath::Clamp(CarryStruggleProgress, 0.0f, 1.0f);
	BP_OnCaptureProgressChanged(
		DownedRecoveryProgress,
		RecoveryHelperCount,
		CarryStruggleProgress);
}

void APHPropCharacter::OnRep_MementoState()
{
	ApplyLocalViewMode();
	if (bMementoInProgress)
	{
		BP_OnMementoStarted(MementoImpactPoint);
	}
}

bool APHPropCharacter::CanRequestTransformation(const double ServerTimeSeconds) const
{
	if (!HasAuthority() || Controller == nullptr || (CaptureState != EPHPropCaptureState::Free
		&& CaptureState != EPHPropCaptureState::Grace))
	{
		return false;
	}

	const APHPlayerState* PHPlayerState = GetPlayerState<APHPlayerState>();
	if (PHPlayerState == nullptr || PHPlayerState->GetPlayerRole() != EPHPlayerRole::Prop)
	{
		return false;
	}

	const APHGameState* PHGameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	if (PHGameState == nullptr)
	{
		return false;
	}

	const EPHMatchPhase Phase = PHGameState->GetMatchPhase();
	if (Phase != EPHMatchPhase::Preparation && Phase != EPHMatchPhase::Hunt && Phase != EPHMatchPhase::Escape)
	{
		return false;
	}

	return ServerTimeSeconds - LastServerTransformationRequestTime >= GetSafeTransformationCooldownSeconds();
}

double APHPropCharacter::GetSafeTransformationCooldownSeconds() const
{
	return FMath::Clamp(static_cast<double>(TransformationCooldownSeconds), 0.1, 10.0);
}

void APHPropCharacter::PerformAuthoritativeTransformationRequest(APHPropTransformTarget* RequestedTarget)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const double ServerTimeSeconds = World->GetTimeSeconds();
	if (!CanRequestTransformation(ServerTimeSeconds))
	{
		const bool bCooldown = HasAuthority() && ServerTimeSeconds - LastServerTransformationRequestTime < GetSafeTransformationCooldownSeconds();
		if (!bCooldown)
		{
			PublishTransformationResult(EPHPropTransformationResult::RejectedRoleOrPhase, TransformationState.ActiveForm);
		}
		else
		{
			UE_LOG(LogPHTransformation, Verbose, TEXT("Rejected transformation spam for %s during server cooldown."), *GetName());
		}
		return;
	}
	LastServerTransformationRequestTime = ServerTimeSeconds;

	APHPropTransformTarget* Target = nullptr;
	EPHPropTransformationResult Failure = EPHPropTransformationResult::RejectedNoTarget;
	if (!TryResolveTransformationTarget(RequestedTarget, Target, Failure))
	{
		PublishTransformationResult(Failure, TransformationState.ActiveForm);
		return;
	}

	UPHPropFormDataAsset* NewForm = const_cast<UPHPropFormDataAsset*>(Target->GetPropForm());
	if (!TryApplyForm(NewForm, Target, Failure))
	{
		PublishTransformationResult(Failure, TransformationState.ActiveForm);
		return;
	}

	UE_LOG(LogPHTransformation, Log, TEXT("Authoritative transformation accepted for %s: %s."), *GetName(), *NewForm->FormId.ToString());
}

void APHPropCharacter::PerformAuthoritativeReturnRequest()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const double ServerTimeSeconds = World->GetTimeSeconds();
	if (!CanRequestTransformation(ServerTimeSeconds))
	{
		const bool bCooldown = HasAuthority() && ServerTimeSeconds - LastServerTransformationRequestTime < GetSafeTransformationCooldownSeconds();
		if (!bCooldown)
		{
			PublishTransformationResult(EPHPropTransformationResult::RejectedRoleOrPhase, TransformationState.ActiveForm);
		}
		else
		{
			UE_LOG(LogPHTransformation, Verbose, TEXT("Rejected return-to-initial spam for %s during server cooldown."), *GetName());
		}
		return;
	}
	LastServerTransformationRequestTime = ServerTimeSeconds;

	if (TransformationState.ActiveForm == nullptr)
	{
		PublishTransformationResult(EPHPropTransformationResult::RejectedAlreadyInitial, nullptr);
		return;
	}

	TryReturnToInitialFormFromServer(TEXT("manual request"));
}

bool APHPropCharacter::TryReturnToInitialFormFromServer(const TCHAR* Reason)
{
	if (!HasAuthority() || TransformationState.ActiveForm == nullptr)
	{
		return false;
	}

	FVector NewLocation;
	if (!TryFindPlacement(ResolveHitbox(nullptr), InitialCapsuleHalfHeight, nullptr, NewLocation))
	{
		PublishTransformationResult(EPHPropTransformationResult::RejectedPlacementBlocked, TransformationState.ActiveForm);
		UE_LOG(LogPHTransformation, Warning,
			TEXT("Authoritative return to initial form rejected for %s (%s): human placement blocked."),
			*GetName(),
			Reason != nullptr ? Reason : TEXT("unspecified"));
		return false;
	}

	// A simulating component is detached from the capsule. Stop it before moving
	// the actor, otherwise ApplyTransformationState() would stop it afterwards and
	// overwrite NewLocation with the old rigid-body centre.
	if (GrayboxPropBody != nullptr && GrayboxPropBody->IsSimulatingPhysics())
	{
		StopPhysicsPropSimulation();
	}
	SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
	PublishTransformationResult(EPHPropTransformationResult::ReturnedToInitial, nullptr);
	UE_LOG(LogPHTransformation, Log,
		TEXT("Authoritative return to initial form accepted for %s (%s)."),
		*GetName(),
		Reason != nullptr ? Reason : TEXT("unspecified"));
	return true;
}

void APHPropCharacter::PublishTransformationResult(
	const EPHPropTransformationResult Result,
	UPHPropFormDataAsset* ActiveForm)
{
	if (!HasAuthority())
	{
		return;
	}

	const FPHResolvedPropHitbox Hitbox = ResolveHitbox(ActiveForm);
	TransformationState.ActiveForm = ActiveForm;
	TransformationState.CapsuleRadius = ActiveForm != nullptr ? ActiveForm->CapsuleRadius : InitialCapsuleRadius;
	TransformationState.CapsuleHalfHeight = ActiveForm != nullptr ? ActiveForm->CapsuleHalfHeight : InitialCapsuleHalfHeight;
	TransformationState.HitboxShape = Hitbox.Shape;
	TransformationState.HitboxRelativeLocation = Hitbox.RelativeLocation;
	TransformationState.HitboxRelativeRotation = Hitbox.RelativeRotation;
	TransformationState.HitboxBoxHalfExtents = Hitbox.BoxHalfExtents;
	TransformationState.HitboxCapsuleRadius = Hitbox.CapsuleRadius;
	TransformationState.HitboxCapsuleHalfHeight = Hitbox.CapsuleHalfHeight;
	TransformationState.Sequence = TransformationState.Sequence == MAX_int32 ? 1 : TransformationState.Sequence + 1;
	TransformationState.LastResult = Result;
	ApplyTransformationState();
	BP_OnPropTransformationConfirmed(Result, ActiveForm);
	ForceNetUpdate();

	UE_LOG(LogPHTransformation, Log, TEXT("Transformation result %d for %s (sequence %d, capsule %.1f x %.1f, hitbox %s)."),
		static_cast<int32>(Result), *GetName(), TransformationState.Sequence,
		TransformationState.CapsuleRadius, TransformationState.CapsuleHalfHeight,
		Hitbox.Shape == EPHPropHitboxShape::Box ? TEXT("box") : TEXT("capsule"));
}

bool APHPropCharacter::TryResolveTransformationTarget(
	APHPropTransformTarget* RequestedTarget,
	APHPropTransformTarget*& OutTarget,
	EPHPropTransformationResult& OutFailure) const
{
	OutTarget = nullptr;
	OutFailure = EPHPropTransformationResult::RejectedNoTarget;

	const UWorld* World = GetWorld();
	if (!HasAuthority() || World == nullptr || Controller == nullptr)
	{
		OutFailure = EPHPropTransformationResult::RejectedRoleOrPhase;
		return false;
	}

	const float SafeTransformationDistance = FMath::Clamp(TransformationDistance, 100.0f, 1000.0f);
	const FVector TraceOrigin = GetActorLocation() + GetActorUpVector() * (GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.35f);
	const FVector SelectionOrigin = GetViewSelectionOrigin();
	const FVector AimDirection = Controller->GetControlRotation().Vector().GetSafeNormal();
	const float SafeTargetingHalfAngleDegrees = FMath::Clamp(TransformationTargetingHalfAngleDegrees, 2.0f, 30.0f);
	if (!IsValid(RequestedTarget) || RequestedTarget->GetWorld() != World)
	{
		return false;
	}

	OutTarget = RequestedTarget;
	if (FVector::Dist(GetActorLocation(), OutTarget->GetActorLocation()) > SafeTransformationDistance)
	{
		OutFailure = EPHPropTransformationResult::RejectedTooFar;
		return false;
	}

	const FVector ToTarget = OutTarget->GetActorLocation() - SelectionOrigin;
	const float CandidateDistance = ToTarget.Size();
	if (CandidateDistance <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float AimDot = FVector::DotProduct(AimDirection, ToTarget / CandidateDistance);
	FVector BoundsOrigin;
	FVector BoundsExtent;
	OutTarget->GetActorBounds(false, BoundsOrigin, BoundsExtent);
	const float AngularRadiusRadians = FMath::Asin(FMath::Clamp(BoundsExtent.Size() / CandidateDistance, 0.0f, 1.0f));
	const float CandidateHalfAngleRadians = FMath::DegreesToRadians(SafeTargetingHalfAngleDegrees) + AngularRadiusRadians;
	const float MinimumAimDot = FMath::Cos(FMath::Min(CandidateHalfAngleRadians, HALF_PI));
	if (AimDot < MinimumAimDot)
	{
		UE_LOG(LogPHTransformation, Verbose,
			TEXT("Rejected requested Prop target %s outside the %.1f degree authoritative cone for %s."),
			*OutTarget->GetName(), SafeTargetingHalfAngleDegrees, *GetName());
		OutTarget = nullptr;
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHPropTransformation), true, this);
	QueryParams.AddIgnoredActor(this);

	FHitResult HitResult;
	if (!World->LineTraceSingleByChannel(HitResult, TraceOrigin, OutTarget->GetActorLocation(), ECC_Visibility, QueryParams)
		|| HitResult.GetActor() != OutTarget)
	{
		UE_LOG(LogPHTransformation, Log, TEXT("Rejected body line of sight from %s to %s; first blocker is %s."),
			*GetName(), *OutTarget->GetName(), HitResult.GetActor() != nullptr ? *HitResult.GetActor()->GetName() : TEXT("none"));
		OutFailure = EPHPropTransformationResult::RejectedLineOfSight;
		return false;
	}

	const UPHPropFormDataAsset* CandidateForm = OutTarget->GetPropForm();
	if (CandidateForm == nullptr || !IsPropFormAllowed(CandidateForm))
	{
		OutFailure = EPHPropTransformationResult::RejectedNotAllowed;
		return false;
	}

	FText DefinitionError;
	if (!CandidateForm->HasValidDefinition(&DefinitionError))
	{
		UE_LOG(LogPHTransformation, Warning, TEXT("Rejected invalid Prop form on %s: %s"), *OutTarget->GetName(), *DefinitionError.ToString());
		OutFailure = EPHPropTransformationResult::RejectedInvalidForm;
		return false;
	}

	return true;
}

bool APHPropCharacter::TryResolvePresentationTransformTarget(APHPropTransformTarget*& OutTarget) const
{
	OutTarget = nullptr;
	const UWorld* World = GetWorld();
	if (!IsLocallyControlled() || World == nullptr || Controller == nullptr)
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const float SearchDistance = FMath::Clamp(TransformationSearchDistance, 500.0f, 5000.0f);
	FCollisionQueryParams ViewQuery(SCENE_QUERY_STAT(PHPropTransformationPresentation), true, this);
	ViewQuery.AddIgnoredActor(this);
	FHitResult ViewHit;
	if (!World->LineTraceSingleByChannel(
		ViewHit,
		ViewLocation,
		ViewLocation + ViewRotation.Vector() * SearchDistance,
		ECC_Visibility,
		ViewQuery))
	{
		return false;
	}

	APHPropTransformTarget* Candidate = Cast<APHPropTransformTarget>(ViewHit.GetActor());
	if (Candidate == nullptr
		|| FVector::Dist(GetActorLocation(), Candidate->GetActorLocation())
			> FMath::Clamp(TransformationDistance, 100.0f, 1000.0f)
		|| !IsPropFormAllowed(Candidate->GetPropForm())
		|| !Candidate->GetPropForm()->HasValidDefinition())
	{
		return false;
	}

	const FVector BodyTraceOrigin = GetActorLocation()
		+ GetActorUpVector() * (GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.35f);
	FCollisionQueryParams BodyQuery(SCENE_QUERY_STAT(PHPropTransformationPresentationBody), true, this);
	BodyQuery.AddIgnoredActor(this);
	FHitResult BodyHit;
	if (!World->LineTraceSingleByChannel(
		BodyHit, BodyTraceOrigin, Candidate->GetActorLocation(), ECC_Visibility, BodyQuery)
		|| BodyHit.GetActor() != Candidate)
	{
		return false;
	}

	OutTarget = Candidate;
	return true;
}

void APHPropCharacter::RefreshLocalInteractionPresentation()
{
	APHPropTransformTarget* NewHighlightedTarget = nullptr;
	FText NewPrompt;
	if (CanPerformCaptureRescue())
	{
		APHRetentionPoint* Point = nullptr;
		APHPropCharacter* DownedTarget = nullptr;
		APHObjectiveActor* Objective = nullptr;
		APHExitGate* ExitGate = nullptr;
		if (TryResolveRetentionPoint(Point) && Point != nullptr && Point->GetRetainedProp() != nullptr)
		{
			NewPrompt = FText::FromString(TEXT("MAINTENIR CLIC GAUCHE - LIBERER"));
		}
		else if (TryResolveDownedRecoveryTarget(DownedTarget) && DownedTarget != nullptr)
		{
			NewPrompt = FText::FromString(TEXT("MAINTENIR CLIC GAUCHE - SOIGNER"));
		}
		else if (TryResolveExitGateTarget(ExitGate) && ExitGate != nullptr)
		{
			NewPrompt = FText::FromString(TEXT("MAINTENIR CLIC GAUCHE - OUVRIR LA SORTIE"));
		}
		else if (TryResolveObjectiveTarget(Objective) && Objective != nullptr)
		{
			NewPrompt = FText::FromString(TEXT("MAINTENIR CLIC GAUCHE - INTERAGIR"));
		}
		else if (TryResolvePresentationTransformTarget(NewHighlightedTarget))
		{
			NewPrompt = FText::FromString(TEXT("CLIC GAUCHE - SE TRANSFORMER"));
		}
	}

	if (HighlightedTransformTarget.Get() != NewHighlightedTarget)
	{
		if (HighlightedTransformTarget.IsValid())
		{
			HighlightedTransformTarget->SetLocallyHighlighted(false);
		}
		HighlightedTransformTarget = NewHighlightedTarget;
		if (NewHighlightedTarget != nullptr)
		{
			NewHighlightedTarget->SetLocallyHighlighted(true);
		}
	}
	CachedPrimaryInteractionPrompt = MoveTemp(NewPrompt);
}

bool APHPropCharacter::TryApplyForm(
	UPHPropFormDataAsset* NewForm,
	const APHPropTransformTarget* CopiedTarget,
	EPHPropTransformationResult& OutFailure)
{
	if (!IsPropFormAllowed(NewForm))
	{
		OutFailure = EPHPropTransformationResult::RejectedNotAllowed;
		return false;
	}

	FText DefinitionError;
	if (!NewForm->HasValidDefinition(&DefinitionError))
	{
		OutFailure = EPHPropTransformationResult::RejectedInvalidForm;
		return false;
	}

	FVector NewLocation;
	if (!TryFindPlacement(ResolveHitbox(NewForm), NewForm->CapsuleHalfHeight, CopiedTarget, NewLocation))
	{
		OutFailure = EPHPropTransformationResult::RejectedPlacementBlocked;
		return false;
	}

	if (GrayboxPropBody != nullptr && GrayboxPropBody->IsSimulatingPhysics())
	{
		StopPhysicsPropSimulation();
	}
	SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
	PublishTransformationResult(EPHPropTransformationResult::Transformed, NewForm);
	return true;
}

bool APHPropCharacter::TryFindPlacement(
	const FPHResolvedPropHitbox& Hitbox,
	const float NewCapsuleHalfHeight,
	const APHPropTransformTarget* CopiedTarget,
	FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || !FMath::IsFinite(NewCapsuleHalfHeight)
		|| NewCapsuleHalfHeight < 15.0f || NewCapsuleHalfHeight > 250.0f)
	{
		return false;
	}

	if (GrayboxPropBody != nullptr && GrayboxPropBody->IsSimulatingPhysics())
	{
		// During Chaos the actor follows the rigid-body component origin, which is
		// not guaranteed to be the geometric centre authored for the gameplay
		// capsule. Preserve the lowest point of the actual Chaos body, not the
		// render bounds: decorative vertices can extend below the authored convexes.
		const FBox CollisionBounds = GrayboxPropBody->BodyInstance.GetBodyBounds();
		const FBoxSphereBounds RenderBounds = GrayboxPropBody->Bounds;
		const float PhysicsBodyBottom = CollisionBounds.IsValid
			? CollisionBounds.Min.Z
			: RenderBounds.Origin.Z - RenderBounds.BoxExtent.Z;
		OutLocation = GetActorLocation();
		OutLocation.Z = PhysicsBodyBottom + NewCapsuleHalfHeight;
	}
	else
	{
		const float CurrentHalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		OutLocation = GetActorLocation() + GetActorUpVector() * (NewCapsuleHalfHeight - CurrentHalfHeight);
	}
	if (OutLocation.ContainsNaN() || OutLocation.GetAbsMax() >= HALF_WORLD_MAX)
	{
		return false;
	}

	TArray<AActor*> BlockingActors;
	if (QueryPlacementBlockers(Hitbox, OutLocation, BlockingActors))
	{
		return true;
	}

	const float SafeMaximumAdjustment = FMath::Clamp(MaximumPlacementAdjustment, 0.0f, 25.0f);
	const float SafeAdjustmentStep = FMath::Clamp(PlacementAdjustmentStep, 1.0f, FMath::Max(1.0f, SafeMaximumAdjustment));
	if (SafeMaximumAdjustment <= 0.0f)
	{
		return false;
	}

	if (CopiedTarget == nullptr)
	{
		// Chaos can settle a rigid body slightly into the floor contact tolerance.
		// Search only upward inside the authored clearance budget. Every candidate
		// still passes the full overlap query, so walls, ceilings and Pawns block it.
		const int32 VerticalSampleCount = FMath::CeilToInt(SafeMaximumAdjustment / SafeAdjustmentStep);
		for (int32 SampleIndex = 1; SampleIndex <= VerticalSampleCount; ++SampleIndex)
		{
			const float Distance = FMath::Min(SampleIndex * SafeAdjustmentStep, SafeMaximumAdjustment);
			const FVector CandidateLocation = OutLocation + GetActorUpVector() * Distance;
			BlockingActors.Reset();
			if (QueryPlacementBlockers(Hitbox, CandidateLocation, BlockingActors))
			{
				OutLocation = CandidateLocation;
				UE_LOG(LogPHTransformation, Log,
					TEXT("Server return-to-human floor clearance moved %s upward by %.1f cm."),
					*GetName(), Distance);
				return true;
			}
		}
		FString BlockerNames;
		for (const AActor* BlockingActor : BlockingActors)
		{
			if (!BlockerNames.IsEmpty())
			{
				BlockerNames += TEXT(", ");
			}
			BlockerNames += GetNameSafe(BlockingActor);
		}
		UE_LOG(LogPHTransformation, Warning,
			TEXT("Return placement rejected for %s at Z=%.1f after %.1f cm clearance; blockers: %s."),
			*GetName(), OutLocation.Z, SafeMaximumAdjustment,
			BlockerNames.IsEmpty() ? TEXT("none") : *BlockerNames);
		return false;
	}

	if (BlockingActors.IsEmpty()
		|| BlockingActors.ContainsByPredicate([CopiedTarget](const AActor* Actor) { return Actor != CopiedTarget; }))
	{
		return false;
	}

	FVector AwayFromTarget = GetActorLocation() - CopiedTarget->GetActorLocation();
	AwayFromTarget.Z = 0.0f;
	if (!AwayFromTarget.Normalize())
	{
		AwayFromTarget = -GetActorForwardVector().GetSafeNormal2D();
	}

	const int32 AdjustmentSampleCount = FMath::CeilToInt(SafeMaximumAdjustment / SafeAdjustmentStep);
	for (int32 SampleIndex = 1; SampleIndex <= AdjustmentSampleCount; ++SampleIndex)
	{
		const float Distance = FMath::Min(SampleIndex * SafeAdjustmentStep, SafeMaximumAdjustment);
		const FVector CandidateLocation = OutLocation + AwayFromTarget * Distance;
		BlockingActors.Reset();
		if (QueryPlacementBlockers(Hitbox, CandidateLocation, BlockingActors))
		{
			OutLocation = CandidateLocation;
			UE_LOG(LogPHTransformation, Log, TEXT("Server placement clearance moved %s by %.1f cm away from copied target %s."),
				*GetName(), Distance, *CopiedTarget->GetName());
			return true;
		}
	}
	FString BlockerNames;
	for (const AActor* BlockingActor : BlockingActors)
	{
		if (!BlockerNames.IsEmpty())
		{
			BlockerNames += TEXT(", ");
		}
		BlockerNames += GetNameSafe(BlockingActor);
	}
	UE_LOG(LogPHTransformation, Warning,
		TEXT("Prop placement rejected for %s near %s after %.1f cm clearance; blockers: %s."),
		*GetName(), *GetNameSafe(CopiedTarget), SafeMaximumAdjustment,
		BlockerNames.IsEmpty() ? TEXT("none") : *BlockerNames);

	return false;
}

bool APHPropCharacter::QueryPlacementBlockers(
	const FPHResolvedPropHitbox& Hitbox,
	const FVector& ActorLocation,
	TArray<AActor*>& OutBlockingActors) const
{
	OutBlockingActors.Reset();
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const FQuat ActorRotation = GetActorQuat();
	const FVector ShapeLocation = ActorLocation + ActorRotation.RotateVector(Hitbox.RelativeLocation);
	const FQuat ShapeRotation = ActorRotation * Hitbox.RelativeRotation.Quaternion();
	const float ShrinkAmount = 0.5f;
	FCollisionShape Shape;
	if (Hitbox.Shape == EPHPropHitboxShape::Box)
	{
		Shape = FCollisionShape::MakeBox(FVector(
			FMath::Max(1.0f, Hitbox.BoxHalfExtents.X - ShrinkAmount),
			FMath::Max(1.0f, Hitbox.BoxHalfExtents.Y - ShrinkAmount),
			FMath::Max(1.0f, Hitbox.BoxHalfExtents.Z - ShrinkAmount)));
	}
	else
	{
		const float Radius = FMath::Max(1.0f, Hitbox.CapsuleRadius - ShrinkAmount);
		Shape = FCollisionShape::MakeCapsule(Radius, FMath::Max(Radius, Hitbox.CapsuleHalfHeight - ShrinkAmount));
	}

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectQueryParams.AddObjectTypesToQuery(PHCollision::PropHitbox);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHPropTransformationPlacement), false, this);
	QueryParams.AddIgnoredActor(this);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, ShapeLocation, ShapeRotation, ObjectQueryParams, Shape, QueryParams);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* OtherActor = Overlap.GetActor();
		const UPrimitiveComponent* OtherComponent = Overlap.GetComponent();
		if (OtherActor == nullptr || OtherActor == this || OtherComponent == nullptr)
		{
			continue;
		}

		const bool bAuthoredPropHitbox = OtherComponent->GetCollisionObjectType() == PHCollision::PropHitbox;
		const bool bBlocksCharacterMovement = OtherComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
		if (bAuthoredPropHitbox || bBlocksCharacterMovement)
		{
			OutBlockingActors.AddUnique(OtherActor);
		}
	}

	return OutBlockingActors.IsEmpty();
}

FPHResolvedPropHitbox APHPropCharacter::ResolveHitbox(const UPHPropFormDataAsset* Form) const
{
	FPHResolvedPropHitbox Result;
	if (Form != nullptr && Form->HasValidDefinition())
	{
		Result.Shape = Form->HitboxShape;
		Result.RelativeLocation = Form->HitboxRelativeLocation;
		Result.RelativeRotation = Form->HitboxRelativeRotation;
		Result.BoxHalfExtents = Form->HitboxBoxHalfExtents;
		Result.CapsuleRadius = Form->HitboxCapsuleRadius;
		Result.CapsuleHalfHeight = Form->HitboxCapsuleHalfHeight;
	}
	else
	{
		Result.Shape = EPHPropHitboxShape::Capsule;
		Result.BoxHalfExtents = FVector(InitialCapsuleRadius, InitialCapsuleRadius, InitialCapsuleHalfHeight);
		Result.CapsuleRadius = InitialCapsuleRadius;
		Result.CapsuleHalfHeight = InitialCapsuleHalfHeight;
	}
	return Result;
}

void APHPropCharacter::ApplyHitbox(const FPHResolvedPropHitbox& Hitbox)
{
	if (PropBoxHitbox == nullptr || PropCapsuleHitbox == nullptr)
	{
		return;
	}

	PropBoxHitbox->SetRelativeLocation(Hitbox.RelativeLocation);
	PropBoxHitbox->SetRelativeRotation(Hitbox.RelativeRotation);
	PropCapsuleHitbox->SetRelativeLocation(Hitbox.RelativeLocation);
	PropCapsuleHitbox->SetRelativeRotation(Hitbox.RelativeRotation);
	if (Hitbox.Shape == EPHPropHitboxShape::Box)
	{
		PropCapsuleHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PropBoxHitbox->SetBoxExtent(Hitbox.BoxHalfExtents, true);
		PropBoxHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	else
	{
		PropBoxHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PropCapsuleHitbox->SetCapsuleSize(Hitbox.CapsuleRadius, Hitbox.CapsuleHalfHeight, true);
		PropCapsuleHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
}

void APHPropCharacter::ApplyTransformationState()
{
	if (GrayboxPropBody == nullptr || GetCapsuleComponent() == nullptr)
	{
		return;
	}
	if (GrayboxPropBody->IsSimulatingPhysics())
	{
		StopPhysicsPropSimulation();
	}

	const float Radius = TransformationState.CapsuleRadius > 0.0f ? TransformationState.CapsuleRadius : InitialCapsuleRadius;
	const float HalfHeight = TransformationState.CapsuleHalfHeight > 0.0f ? TransformationState.CapsuleHalfHeight : InitialCapsuleHalfHeight;
	GetCapsuleComponent()->SetCapsuleSize(Radius, HalfHeight, true);

	FPHResolvedPropHitbox Hitbox;
	Hitbox.Shape = TransformationState.HitboxShape;
	Hitbox.RelativeLocation = TransformationState.HitboxRelativeLocation;
	Hitbox.RelativeRotation = TransformationState.HitboxRelativeRotation;
	Hitbox.BoxHalfExtents = TransformationState.HitboxBoxHalfExtents;
	Hitbox.CapsuleRadius = TransformationState.HitboxCapsuleRadius;
	Hitbox.CapsuleHalfHeight = TransformationState.HitboxCapsuleHalfHeight;
	if ((Hitbox.Shape == EPHPropHitboxShape::Box && Hitbox.BoxHalfExtents.GetMin() <= 0.0f)
		|| (Hitbox.Shape == EPHPropHitboxShape::Capsule && (Hitbox.CapsuleRadius <= 0.0f || Hitbox.CapsuleHalfHeight < Hitbox.CapsuleRadius)))
	{
		Hitbox = ResolveHitbox(TransformationState.ActiveForm);
	}
	ApplyHitbox(Hitbox);

	if (TransformationState.ActiveForm != nullptr && TransformationState.ActiveForm->HasValidDefinition())
	{
		GrayboxPropBody->SetStaticMesh(TransformationState.ActiveForm->StaticMesh);
		GrayboxPropBody->SetRelativeLocation(TransformationState.ActiveForm->MeshRelativeLocation);
		GrayboxPropBody->SetRelativeRotation(TransformationState.ActiveForm->MeshRelativeRotation);
		GrayboxPropBody->SetRelativeScale3D(TransformationState.ActiveForm->MeshRelativeScale);
	}
	else
	{
		GrayboxPropBody->SetStaticMesh(InitialPresentationMesh);
		GrayboxPropBody->SetRelativeLocation(InitialMeshRelativeLocation);
		GrayboxPropBody->SetRelativeRotation(InitialMeshRelativeRotation);
		GrayboxPropBody->SetRelativeScale3D(InitialMeshRelativeScale);
	}
	RefreshPhysicsPropMovement();
	ApplyLocalViewMode();
}

bool APHPropCharacter::TryResolveObjectiveTarget(APHObjectiveActor*& OutObjective) const
{
	OutObjective = nullptr;
	const UWorld* World = GetWorld();
	if (World == nullptr || Controller == nullptr)
	{
		return false;
	}

	const FVector SelectionOrigin = GetViewSelectionOrigin();
	const FVector AimDirection = Controller->GetControlRotation().Vector().GetSafeNormal();
	const float SafeSearchDistance = FMath::Clamp(ObjectiveSearchDistance, 250.0f, 3000.0f);
	const float SafeHalfAngle = FMath::Clamp(ObjectiveTargetingHalfAngleDegrees, 2.0f, 30.0f);
	float BestAimDot = -1.0f;
	float BestDistance = MAX_flt;

	for (TActorIterator<APHObjectiveActor> ObjectiveIterator(World); ObjectiveIterator; ++ObjectiveIterator)
	{
		APHObjectiveActor* Candidate = *ObjectiveIterator;
		if (!IsValid(Candidate) || !Candidate->IsObjectiveActive() || Candidate->IsObjectiveCompleted())
		{
			continue;
		}

		const FVector ToObjective = Candidate->GetInteractionPoint() - SelectionOrigin;
		const float Distance = ToObjective.Size();
		const float CandidateInteractionDistance = FMath::Clamp(
			Candidate->GetInteractionDistance(), 100.0f, 500.0f);
		if (Distance <= KINDA_SMALL_NUMBER || Distance > SafeSearchDistance
			|| FVector::DistSquared(GetActorLocation(), Candidate->GetActorLocation())
				> FMath::Square(CandidateInteractionDistance))
		{
			continue;
		}

		const UCapsuleComponent* Capsule = GetCapsuleComponent();
		const float CapsuleHalfHeight = Capsule != nullptr ? Capsule->GetScaledCapsuleHalfHeight() : 60.0f;
		const FVector TraceOrigin = GetActorLocation() + GetActorUpVector() * (CapsuleHalfHeight * 0.35f);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHObjectiveInteractionPresentation), true, this);
		QueryParams.AddIgnoredActor(this);
		FHitResult HitResult;
		if (!World->LineTraceSingleByChannel(
			HitResult, TraceOrigin, Candidate->GetInteractionPoint(), ECC_Visibility, QueryParams)
			|| HitResult.GetActor() != Candidate)
		{
			continue;
		}

		const float AimDot = FVector::DotProduct(AimDirection, ToObjective / Distance);
		const float MinimumAimDot = FMath::Cos(FMath::DegreesToRadians(SafeHalfAngle));
		const bool bBetterAngle = AimDot > BestAimDot + KINDA_SMALL_NUMBER;
		const bool bSameAngleButCloser = FMath::IsNearlyEqual(AimDot, BestAimDot) && Distance < BestDistance;
		if (AimDot >= MinimumAimDot && (OutObjective == nullptr || bBetterAngle || bSameAngleButCloser))
		{
			OutObjective = Candidate;
			BestAimDot = AimDot;
			BestDistance = Distance;
		}
	}

	return OutObjective != nullptr;
}

bool APHPropCharacter::TryResolveExitGateTarget(APHExitGate*& OutExitGate) const
{
	OutExitGate = nullptr;
	const UWorld* World = GetWorld();
	const APHGameState* GameState = World != nullptr ? World->GetGameState<APHGameState>() : nullptr;
	if (World == nullptr || Controller == nullptr || GameState == nullptr
		|| GameState->GetMatchPhase() != EPHMatchPhase::Escape)
	{
		return false;
	}

	const FVector SelectionOrigin = GetViewSelectionOrigin();
	const FVector AimDirection = Controller->GetControlRotation().Vector().GetSafeNormal();
	const float MinimumAimDot = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(ObjectiveTargetingHalfAngleDegrees, 2.0f, 30.0f)));
	float BestAimDot = -1.0f;
	float BestDistance = MAX_flt;
	for (TActorIterator<APHExitGate> GateIterator(World); GateIterator; ++GateIterator)
	{
		APHExitGate* Candidate = *GateIterator;
		if (!IsValid(Candidate) || !Candidate->IsGateEnabled() || Candidate->IsGateOpen())
		{
			continue;
		}

		if (!Candidate->IsWithinInteractionRange(GetActorLocation()))
		{
			continue;
		}

		const FVector ToGate = Candidate->GetInteractionPoint() - SelectionOrigin;
		const float Distance = ToGate.Size();
		if (Distance <= KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const float AimDot = FVector::DotProduct(AimDirection, ToGate / Distance);
		if (AimDot < MinimumAimDot)
		{
			continue;
		}

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHExitGateInteractionPresentation), true, this);
		QueryParams.AddIgnoredActor(this);
		FHitResult HitResult;
		if (!World->LineTraceSingleByChannel(
			HitResult, GetActorLocation() + FVector(0.0f, 0.0f, 45.0f), Candidate->GetInteractionPoint(), ECC_Visibility, QueryParams)
			|| HitResult.GetActor() != Candidate)
		{
			continue;
		}

		const bool bBetterAngle = AimDot > BestAimDot + KINDA_SMALL_NUMBER;
		const bool bSameAngleButCloser = FMath::IsNearlyEqual(AimDot, BestAimDot) && Distance < BestDistance;
		if (OutExitGate == nullptr || bBetterAngle || bSameAngleButCloser)
		{
			OutExitGate = Candidate;
			BestAimDot = AimDot;
			BestDistance = Distance;
		}
	}
	return OutExitGate != nullptr;
}

void APHPropCharacter::PerformAuthoritativeStartObjectiveInteraction(APHObjectiveActor* RequestedObjective)
{
	if (!HasAuthority() || RequestedObjective == nullptr || CaptureState != EPHPropCaptureState::Free)
	{
		return;
	}

	// Validate the client-provided objective before changing form. The objective
	// repeats this validation when the interaction actually starts, after the
	// human placement may have adjusted the character location.
	if (!RequestedObjective->CanPropInteract(*this))
	{
		return;
	}

	if (TransformationState.ActiveForm != nullptr)
	{
		if (!TryReturnToInitialFormFromServer(TEXT("objective interaction")))
		{
			return;
		}

		// The automatic return deliberately bypasses the manual cooldown, but a
		// player must not immediately remorph while the objective begins.
		if (const UWorld* World = GetWorld())
		{
			LastServerTransformationRequestTime = World->GetTimeSeconds();
		}
	}

	if (ActiveObjective != nullptr && ActiveObjective != RequestedObjective)
	{
		ActiveObjective->ServerEndInteraction(*this);
	}

	if (ActiveObjective == nullptr)
	{
		RequestedObjective->ServerTryBeginInteraction(*this);
	}
}

void APHPropCharacter::PerformAuthoritativeStartExitGateInteraction(APHExitGate* RequestedGate)
{
	if (!HasAuthority() || RequestedGate == nullptr
		|| (CaptureState != EPHPropCaptureState::Free && CaptureState != EPHPropCaptureState::Grace)
		|| !RequestedGate->CanPropInteract(*this))
	{
		return;
	}

	if (TransformationState.ActiveForm != nullptr)
	{
		if (!TryReturnToInitialFormFromServer(TEXT("exit gate interaction")))
		{
			return;
		}
		if (const UWorld* World = GetWorld())
		{
			LastServerTransformationRequestTime = World->GetTimeSeconds();
		}
	}

	PerformAuthoritativeStopObjectiveInteraction();
	if (ActiveExitGate != nullptr && ActiveExitGate != RequestedGate)
	{
		ActiveExitGate->ServerEndInteraction(*this);
	}
	if (ActiveExitGate == nullptr)
	{
		RequestedGate->ServerTryBeginInteraction(*this);
	}
}

void APHPropCharacter::PerformAuthoritativeStopObjectiveInteraction()
{
	if (HasAuthority() && ActiveObjective != nullptr)
	{
		ActiveObjective->ServerEndInteraction(*this);
	}
}

void APHPropCharacter::PerformAuthoritativeStopExitGateInteraction()
{
	if (HasAuthority() && ActiveExitGate != nullptr)
	{
		ActiveExitGate->ServerEndInteraction(*this);
	}
}

bool APHPropCharacter::TryResolveRetentionPoint(APHRetentionPoint*& OutRetentionPoint) const
{
	OutRetentionPoint = nullptr;
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const float SafeDistance = FMath::Clamp(CaptureInteractionDistance, 100.0f, 500.0f);
	float BestDistance = SafeDistance;

	for (TActorIterator<APHRetentionPoint> PointIterator(World); PointIterator; ++PointIterator)
	{
		APHRetentionPoint* Candidate = *PointIterator;
		if (!IsValid(Candidate))
		{
			continue;
		}

		const float InteractionDistanceFromBody = FVector::Dist(GetActorLocation(), Candidate->GetActorLocation());
		if (InteractionDistanceFromBody > SafeDistance)
		{
			continue;
		}

		if (OutRetentionPoint == nullptr || InteractionDistanceFromBody < BestDistance)
		{
			OutRetentionPoint = Candidate;
			BestDistance = InteractionDistanceFromBody;
		}
	}

	return OutRetentionPoint != nullptr;
}

bool APHPropCharacter::TryResolveDownedRecoveryTarget(APHPropCharacter*& OutTarget) const
{
	OutTarget = nullptr;
	const UWorld* World = GetWorld();
	if (World == nullptr || !CanPerformCaptureRescue())
	{
		return false;
	}

	const float SafeDistance = FMath::Clamp(CaptureInteractionDistance, 100.0f, 500.0f);
	float BestDistanceSquared = FMath::Square(SafeDistance);
	for (TActorIterator<APHPropCharacter> PropIterator(World); PropIterator; ++PropIterator)
	{
		APHPropCharacter* Candidate = *PropIterator;
		if (!IsValid(Candidate) || Candidate == this
			|| Candidate->GetCaptureState() != EPHPropCaptureState::Downed)
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSquared <= BestDistanceSquared && HasCaptureLineOfSightTo(*Candidate))
		{
			OutTarget = Candidate;
			BestDistanceSquared = DistanceSquared;
		}
	}
	return OutTarget != nullptr;
}

bool APHPropCharacter::CanAssistDownedTarget(const APHPropCharacter& Target) const
{
	const APHPlayerState* HelperState = GetPlayerState<APHPlayerState>();
	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	const EPHMatchPhase Phase = GameState != nullptr ? GameState->GetMatchPhase() : EPHMatchPhase::Lobby;
	const float SafeDistance = FMath::Clamp(CaptureInteractionDistance, 100.0f, 500.0f);
	return HasAuthority()
		&& &Target != this
		&& HelperState != nullptr
		&& HelperState->GetPlayerRole() == EPHPlayerRole::Prop
		&& PHCaptureFlow::CanStartAllySupport(
			CaptureState,
			Target.GetCaptureState(),
			TransformationState.ActiveForm == nullptr)
		&& (Phase == EPHMatchPhase::Hunt || Phase == EPHMatchPhase::Escape)
		&& FVector::DistSquared(GetActorLocation(), Target.GetActorLocation()) <= FMath::Square(SafeDistance)
		&& HasCaptureLineOfSightTo(Target);
}

bool APHPropCharacter::HasCaptureLineOfSightTo(const APHPropCharacter& Target) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const float HalfHeight = GetCapsuleComponent() != nullptr
		? GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 50.0f;
	const FVector TraceStart = GetActorLocation() + FVector::UpVector * HalfHeight;
	const FVector TargetPoint = Target.GetMeleeLineOfSightPoint(TraceStart);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHDownedRecoveryLineOfSight), false, this);
	QueryParams.AddIgnoredActor(this);
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit, TraceStart, TargetPoint, ECC_Visibility, QueryParams);
	return !bHit || Hit.GetActor() == &Target;
}

void APHPropCharacter::BeginAssistingDownedTarget(APHPropCharacter& Target)
{
	if (!PrepareHumanFormForSupport(TEXT("downed ally support")) || !CanAssistDownedTarget(Target))
	{
		return;
	}

	StopRetentionRescue();
	StopAssistingDownedTarget();
	AssistedDownedProp = &Target;
	ForceNetUpdate();
	Target.AddRecoveryHelper(*this);
	if (!Target.RecoveryHelpers.Contains(this))
	{
		AssistedDownedProp = nullptr;
		ForceNetUpdate();
	}
}

void APHPropCharacter::StopAssistingDownedTarget()
{
	if (!HasAuthority())
	{
		return;
	}

	APHPropCharacter* PreviousTarget = AssistedDownedProp;
	AssistedDownedProp = nullptr;
	ForceNetUpdate();
	if (PreviousTarget != nullptr)
	{
		PreviousTarget->RemoveRecoveryHelper(*this);
	}
}

void APHPropCharacter::StopRetentionRescue()
{
	if (HasAuthority() && ActiveRetentionRescuePoint != nullptr)
	{
		ActiveRetentionRescuePoint->ServerEndRelease(*this);
	}
}

bool APHPropCharacter::PrepareHumanFormForSupport(const TCHAR* InteractionName)
{
	if (!HasAuthority())
	{
		return false;
	}
	if (TransformationState.ActiveForm == nullptr)
	{
		return true;
	}
	if (!TryReturnToInitialFormFromServer(InteractionName))
	{
		return false;
	}
	if (const UWorld* World = GetWorld())
	{
		LastServerTransformationRequestTime = World->GetTimeSeconds();
	}
	return true;
}

void APHPropCharacter::AddRecoveryHelper(APHPropCharacter& Helper)
{
	if (!HasAuthority() || CaptureState != EPHPropCaptureState::Downed
		|| RecoveryHelpers.Num() >= FMath::Clamp(MaximumRecoveryHelpers, 1, 4))
	{
		return;
	}

	RecoveryHelpers.Add(&Helper);
	RecoveryHelperCount = RecoveryHelpers.Num();
	OnRep_CaptureProgress();
	ForceNetUpdate();
}

void APHPropCharacter::RemoveRecoveryHelper(APHPropCharacter& Helper)
{
	if (!HasAuthority())
	{
		return;
	}

	RecoveryHelpers.Remove(&Helper);
	RecoveryHelperCount = RecoveryHelpers.Num();
	OnRep_CaptureProgress();
	ForceNetUpdate();
}

void APHPropCharacter::ClearRecoveryHelpers()
{
	if (!HasAuthority())
	{
		return;
	}

	for (const TWeakObjectPtr<APHPropCharacter>& HelperPointer : RecoveryHelpers)
	{
		if (APHPropCharacter* Helper = HelperPointer.Get();
			Helper != nullptr && Helper->AssistedDownedProp == this)
		{
			Helper->AssistedDownedProp = nullptr;
			Helper->ForceNetUpdate();
		}
	}
	RecoveryHelpers.Reset();
	RecoveryHelperCount = 0;
	OnRep_CaptureProgress();
	ForceNetUpdate();
}

void APHPropCharacter::SubmitCarryStruggleInput(const int8 Direction)
{
	if (!HasAuthority() || CaptureState != EPHPropCaptureState::Carried
		|| Carrier == nullptr || GetWorld() == nullptr || (Direction != -1 && Direction != 1))
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	const double SafeInterval = FMath::Clamp(
		static_cast<double>(CarryStruggleMinimumInputInterval), 0.05, 0.5);
	if (Direction == LastServerStruggleDirection || Now - LastServerStruggleInputTime < SafeInterval)
	{
		return;
	}

	LastServerStruggleDirection = Direction;
	LastServerStruggleInputTime = Now;
	const int32 RequiredAlternations = FMath::Clamp(CarryStruggleRequiredAlternations, 6, 100);
	CarryStruggleProgress = FMath::Clamp(
		CarryStruggleProgress + 1.0f / RequiredAlternations,
		0.0f,
		1.0f);

	const float SafeShoveInterval = FMath::Clamp(CarryStruggleShoveProgressInterval, 0.05f, 0.5f);
	if (CarryStruggleProgress + KINDA_SMALL_NUMBER >= NextCarryStruggleShoveProgress)
	{
		const float RandomDirection = FMath::RandBool() ? -1.0f : 1.0f;
		Carrier->ApplyCarryStruggleShove(RandomDirection, CarryStruggleHunterShoveDistance);
		NextCarryStruggleShoveProgress += SafeShoveInterval;
	}

	OnRep_CaptureProgress();
	ForceNetUpdate();
	if (CarryStruggleProgress >= 1.0f - KINDA_SMALL_NUMBER)
	{
		EscapeCarrierWithGrace();
	}
}

void APHPropCharacter::EscapeCarrierWithGrace()
{
	if (!HasAuthority() || CaptureState != EPHPropCaptureState::Carried || Carrier == nullptr)
	{
		return;
	}

	APHHunterCharacter* PreviousCarrier = Carrier;
	PreviousCarrier->ClearCarriedProp(this);
	Carrier = nullptr;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	const FRotator EscapeRotation = GetActorRotation();
	SetActorRotation(FRotator(0.0f, EscapeRotation.Yaw, 0.0f), ETeleportType::TeleportPhysics);
	SetActorLocation(
		GetActorLocation() - PreviousCarrier->GetActorForwardVector() * 80.0f
			+ PreviousCarrier->GetActorRightVector() * (FMath::RandBool() ? -80.0f : 80.0f),
		true);
	ResetCaptureProgress();
	SetCaptureState(EPHPropCaptureState::Grace, GraceDuration);
}

void APHPropCharacter::ResetCaptureProgress()
{
	if (!HasAuthority())
	{
		return;
	}

	DownedRecoveryProgress = 0.0f;
	LastPublishedDownedRecoveryProgress = 0.0f;
	CarryStruggleProgress = 0.0f;
	ResetCarryStruggleInputWindow();
	NextCarryStruggleShoveProgress = FMath::Clamp(CarryStruggleShoveProgressInterval, 0.05f, 0.5f);
	OnRep_CaptureProgress();
	ForceNetUpdate();
}

void APHPropCharacter::ResetCarryStruggleInputWindow()
{
	LastLocalStruggleDirection = 0;
	LastServerStruggleDirection = 0;
	LastLocalStruggleInputTime = -DBL_MAX;
	LastServerStruggleInputTime = -DBL_MAX;
}

void APHPropCharacter::SetCaptureState(const EPHPropCaptureState NewState, const float DurationSeconds)
{
	if (!HasAuthority())
	{
		return;
	}

	const EPHPropCaptureState PreviousState = CaptureState;
	if (PreviousState == EPHPropCaptureState::Downed && NewState != EPHPropCaptureState::Downed)
	{
		ClearRecoveryHelpers();
	}
	if (NewState != EPHPropCaptureState::Free && NewState != EPHPropCaptureState::Grace)
	{
		StopAssistingDownedTarget();
		StopRetentionRescue();
	}
	if (PHCaptureFlow::IsTerminal(NewState))
	{
		PerformAuthoritativeStopObjectiveInteraction();
		PerformAuthoritativeStopExitGateInteraction();
		if (Carrier != nullptr)
		{
			Carrier->ClearCarriedProp(this);
		}
		if (RetentionPoint != nullptr)
		{
			RetentionPoint->ClearRetainedProp(this);
		}
		Carrier = nullptr;
		RetentionPoint = nullptr;
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}

	GetWorldTimerManager().ClearTimer(CaptureStateTimerHandle);
	CaptureState = NewState;
	CaptureStateEndServerTime = DurationSeconds > 0.0f && GetWorld() != nullptr
		? GetWorld()->GetTimeSeconds() + DurationSeconds
		: 0.0f;

	if (NewState == EPHPropCaptureState::Retained && DurationSeconds > 0.0f)
	{
		GetWorldTimerManager().SetTimer(
			CaptureStateTimerHandle, this, &APHPropCharacter::FinishRetention, DurationSeconds, false);
	}
	else if (NewState == EPHPropCaptureState::Grace && DurationSeconds > 0.0f)
	{
		GetWorldTimerManager().SetTimer(
			CaptureStateTimerHandle, this, &APHPropCharacter::FinishGrace, DurationSeconds, false);
	}

	OnRep_CaptureState();
	ForceNetUpdate();
	if (PreviousState != EPHPropCaptureState::Eliminated
		&& NewState == EPHPropCaptureState::Eliminated)
	{
		if (APHGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<APHGameMode>() : nullptr)
		{
			GameMode->NotifyPropEliminated(*this);
		}
	}
	else if (PreviousState != EPHPropCaptureState::Escaped
		&& NewState == EPHPropCaptureState::Escaped)
	{
		if (APHGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<APHGameMode>() : nullptr)
		{
			GameMode->NotifyPropEscaped(*this);
		}
	}
}

void APHPropCharacter::ApplyCaptureState()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement == nullptr)
	{
		return;
	}

	const bool bAttachedCapture = CaptureState == EPHPropCaptureState::Carried
		|| CaptureState == EPHPropCaptureState::Retained;
	const bool bTerminal = PHCaptureFlow::IsTerminal(CaptureState);
	const bool bMovementDisabled = bAttachedCapture || bTerminal;
	const bool bDowned = CaptureState == EPHPropCaptureState::Downed;

	SetActorHiddenInGame(bTerminal);
	SetActorEnableCollision(!bAttachedCapture && !bTerminal);
	if (bMovementDisabled || bDowned)
	{
		bWantsHumanSprint = false;
	}
	ApplyMovementSpeed();
	UpdateLocomotionPresentation();

	if (bMovementDisabled)
	{
		Movement->DisableMovement();
	}
	else if (bDowned)
	{
		if (Movement->MovementMode == MOVE_None)
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
	else if (Movement->MovementMode == MOVE_None)
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
	RefreshPhysicsPropMovement();
}

void APHPropCharacter::FinishGrace()
{
	if (HasAuthority() && CaptureState == EPHPropCaptureState::Grace)
	{
		SetCaptureState(EPHPropCaptureState::Free);
	}
}

void APHPropCharacter::FinishRetention()
{
	if (!HasAuthority() || CaptureState != EPHPropCaptureState::Retained)
	{
		return;
	}

	// The release interaction is authoritative on the retention point. Do not let
	// the independent elimination timer win while a valid rescuer is actively
	// completing that interaction. CanRescuerRelease() resets the release state
	// as soon as the rescuer lets go, moves away or becomes invalid.
	if (RetentionPoint != nullptr && RetentionPoint->IsReleaseInProgressFor(*this))
	{
		GetWorldTimerManager().SetTimer(
			CaptureStateTimerHandle, this, &APHPropCharacter::FinishRetention, 0.1f, false);
		return;
	}

	if (RetentionPoint != nullptr)
	{
		RetentionPoint->ClearRetainedProp(this);
	}
	RetentionPoint = nullptr;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetCaptureState(EPHPropCaptureState::Eliminated);
}

void APHPropCharacter::FinishMemento()
{
	if (!HasAuthority() || !bMementoInProgress)
	{
		return;
	}

	bMementoInProgress = false;
	OnRep_MementoState();
	SetCaptureState(EPHPropCaptureState::Eliminated);
	ForceNetUpdate();
}

void APHPropCharacter::ClearCaptureRelationships()
{
	GetWorldTimerManager().ClearTimer(CaptureStateTimerHandle);
	GetWorldTimerManager().ClearTimer(MementoTimerHandle);
	bMementoInProgress = false;
	StopAssistingDownedTarget();
	StopRetentionRescue();
	PerformAuthoritativeStopObjectiveInteraction();
	PerformAuthoritativeStopExitGateInteraction();
	ClearRecoveryHelpers();
	if (Carrier != nullptr)
	{
		Carrier->ClearCarriedProp(this);
	}
	if (RetentionPoint != nullptr)
	{
		RetentionPoint->ClearRetainedProp(this);
	}
	Carrier = nullptr;
	RetentionPoint = nullptr;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
}
