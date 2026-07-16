#include "Characters/PHPropCharacter.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Characters/PHHunterCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Game/PHGameState.h"
#include "Game/PHPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Gameplay/PHCollisionChannels.h"
#include "Gameplay/Capture/PHRetentionPoint.h"
#include "Gameplay/Characters/PHHumanPrototypeSelector.h"
#include "Gameplay/Objectives/PHObjectiveActor.h"
#include "Gameplay/Transformation/PHPropFormDataAsset.h"
#include "Gameplay/Transformation/PHPropTransformTarget.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHTransformation, Log, All);

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
	, CameraSocketOffset(0.0f, 45.0f, 35.0f)
	, HumanFirstPersonCameraOffset(0.0f, 0.0f, 64.0f)
	, MaximumHumanStamina(100.0f)
	, HumanSprintSpeed(700.0f)
	, HumanSprintDrainPerSecond(40.0f)
	, HumanStaminaRechargePerSecond(15.0f)
	, HumanStaminaRechargeDelay(0.75f)
	, HumanJumpStaminaCost(20.0f)
	, TransformationDistance(400.0f)
	, TransformationCooldownSeconds(1.0f)
	, TransformationSearchDistance(3000.0f)
	, TransformationTargetingHalfAngleDegrees(12.0f)
	, MaximumPlacementAdjustment(25.0f)
	, PlacementAdjustmentStep(5.0f)
	, ObjectiveSearchDistance(1500.0f)
	, ObjectiveTargetingHalfAngleDegrees(15.0f)
	, CaptureInteractionDistance(225.0f)
	, FirstRetentionDuration(30.0f)
	, SecondRetentionDuration(20.0f)
	, MaximumRetentionCount(3)
	, GraceDuration(5.0f)
	, GraceSpeedMultiplier(1.25f)
	, DownedCrawlSpeed(150.0f)
	, DownedRecoveryDuration(30.0f)
	, AdditionalRecoveryContributionPerHelper(0.75f)
	, MaximumRecoveryHelpers(3)
	, CarryStruggleRequiredAlternations(24)
	, CarryStruggleMinimumInputInterval(0.08f)
	, CarryStruggleShoveProgressInterval(0.20f)
	, CarryStruggleHunterShoveDistance(90.0f)
	, MeleeHitSequence(0)
	, CurrentHumanStamina(100.0f)
	, CaptureState(EPHPropCaptureState::Free)
	, RetentionCount(0)
	, CaptureStateEndServerTime(0.0f)
	, Carrier(nullptr)
	, RetentionPoint(nullptr)
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
	, NormalWalkSpeed(500.0f)
	, bWantsHumanSprint(false)
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

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
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

	GrayboxPropBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GrayboxPropBody"));
	GrayboxPropBody->SetupAttachment(GetCapsuleComponent());
	GrayboxPropBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrayboxPropBody->SetCanEverAffectNavigation(false);
	GrayboxPropBody->SetRelativeLocation(FVector(0.0f, 0.0f, -8.0f));
	GrayboxPropBody->SetRelativeScale3D(FVector(0.65f, 0.65f, 0.90f));

	PropBoxHitbox = CreateDefaultSubobject<UBoxComponent>(TEXT("PropBoxHitbox"));
	PropBoxHitbox->SetupAttachment(GetCapsuleComponent());
	PropBoxHitbox->InitBoxExtent(FVector(40.0f, 40.0f, 60.0f));
	PropBoxHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PropBoxHitbox->SetCollisionObjectType(PHCollision::PropHitbox);
	PropBoxHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
	PropBoxHitbox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PropBoxHitbox->SetGenerateOverlapEvents(false);
	PropBoxHitbox->SetCanEverAffectNavigation(false);
	PropBoxHitbox->CanCharacterStepUpOn = ECB_No;

	PropCapsuleHitbox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("PropCapsuleHitbox"));
	PropCapsuleHitbox->SetupAttachment(GetCapsuleComponent());
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
}

void APHPropCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APHPropCharacter, MeleeHitSequence);
	DOREPLIFETIME(APHPropCharacter, TransformationState);
	DOREPLIFETIME(APHPropCharacter, ActiveObjective);
	DOREPLIFETIME(APHPropCharacter, ActiveRetentionRescuePoint);
	DOREPLIFETIME_CONDITION(APHPropCharacter, CurrentHumanStamina, COND_OwnerOnly);
	DOREPLIFETIME(APHPropCharacter, CaptureState);
	DOREPLIFETIME(APHPropCharacter, RetentionCount);
	DOREPLIFETIME(APHPropCharacter, CaptureStateEndServerTime);
	DOREPLIFETIME(APHPropCharacter, Carrier);
	DOREPLIFETIME(APHPropCharacter, RetentionPoint);
	DOREPLIFETIME(APHPropCharacter, DownedRecoveryProgress);
	DOREPLIFETIME(APHPropCharacter, RecoveryHelperCount);
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
	if (HasAuthority())
	{
		UpdateHumanStamina(DeltaSeconds);
		UpdateDownedRecovery(DeltaSeconds);
	}
	UpdateLocomotionPresentation();
}

void APHPropCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
}

bool APHPropCharacter::IsMoveInputIgnored() const
{
	return Super::IsMoveInputIgnored()
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
		ResetCaptureProgress();
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		SetCaptureState(EPHPropCaptureState::Downed);
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

	RequestStartObjectiveInteraction();
}

void APHPropCharacter::StopSurvivorInteraction()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	RequestStopCaptureAssist();
	RequestStopObjectiveInteraction();
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
			RequestedPoint->ServerTryBeginRelease(*this);
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

bool APHPropCharacter::ServerTryBeCarriedBy(APHHunterCharacter& Hunter)
{
	if (!HasAuthority() || CaptureState != EPHPropCaptureState::Downed || Carrier != nullptr)
	{
		return false;
	}

	ClearRecoveryHelpers();
	StopAssistingDownedTarget();
	ResetCaptureProgress();
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
	ResetCaptureProgress();
	SetCaptureState(EPHPropCaptureState::Downed);
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

void APHPropCharacter::ResetCaptureForMatch()
{
	if (!HasAuthority())
	{
		return;
	}

	ClearCaptureRelationships();
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
	if (HasAuthority())
	{
		PerformAuthoritativeTransformationRequest();
	}
	else
	{
		ServerRequestPropTransformation();
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

void APHPropCharacter::ServerRequestPropTransformation_Implementation()
{
	PerformAuthoritativeTransformationRequest();
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

void APHPropCharacter::ServerRequestCaptureRescue_Implementation(APHRetentionPoint* RequestedPoint)
{
	if (RequestedPoint != nullptr)
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
		|| DeltaSeconds <= 0.0f)
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
		|| CaptureState == EPHPropCaptureState::Eliminated)
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

void APHPropCharacter::PerformAuthoritativeTransformationRequest()
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
	if (!TryResolveTransformationTarget(Target, Failure))
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

	FVector NewLocation;
	if (!TryFindPlacement(ResolveHitbox(nullptr), InitialCapsuleHalfHeight, nullptr, NewLocation))
	{
		PublishTransformationResult(EPHPropTransformationResult::RejectedPlacementBlocked, TransformationState.ActiveForm);
		return;
	}

	SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
	PublishTransformationResult(EPHPropTransformationResult::ReturnedToInitial, nullptr);
	UE_LOG(LogPHTransformation, Log, TEXT("Authoritative return to initial form accepted for %s."), *GetName());
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

	const float SafeSearchDistance = FMath::Clamp(TransformationSearchDistance, 500.0f, 5000.0f);
	const float SafeTransformationDistance = FMath::Clamp(TransformationDistance, 100.0f, 1000.0f);
	const FVector TraceOrigin = GetActorLocation() + GetActorUpVector() * (GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.35f);
	const FVector SelectionOrigin = GetViewSelectionOrigin();
	const FVector AimDirection = Controller->GetControlRotation().Vector().GetSafeNormal();
	const float SafeTargetingHalfAngleDegrees = FMath::Clamp(TransformationTargetingHalfAngleDegrees, 2.0f, 30.0f);
	float BestAimDot = -1.0f;
	float BestDistance = MAX_flt;

	for (TActorIterator<APHPropTransformTarget> TargetIterator(World); TargetIterator; ++TargetIterator)
	{
		APHPropTransformTarget* CandidateTarget = *TargetIterator;
		if (!IsValid(CandidateTarget))
		{
			continue;
		}

		const FVector ToTarget = CandidateTarget->GetActorLocation() - SelectionOrigin;
		const float CandidateDistance = ToTarget.Size();
		if (CandidateDistance <= KINDA_SMALL_NUMBER || CandidateDistance > SafeSearchDistance)
		{
			continue;
		}

		const float AimDot = FVector::DotProduct(AimDirection, ToTarget / CandidateDistance);
		FVector BoundsOrigin;
		FVector BoundsExtent;
		CandidateTarget->GetActorBounds(false, BoundsOrigin, BoundsExtent);
		const float AngularRadiusRadians = FMath::Asin(FMath::Clamp(BoundsExtent.Size() / CandidateDistance, 0.0f, 1.0f));
		const float CandidateHalfAngleRadians = FMath::DegreesToRadians(SafeTargetingHalfAngleDegrees) + AngularRadiusRadians;
		const float CandidateMinimumAimDot = FMath::Cos(FMath::Min(CandidateHalfAngleRadians, HALF_PI));
		const bool bBetterAngle = AimDot > BestAimDot + KINDA_SMALL_NUMBER;
		const bool bSameAngleButCloser = FMath::IsNearlyEqual(AimDot, BestAimDot) && CandidateDistance < BestDistance;
		if (AimDot >= CandidateMinimumAimDot && (OutTarget == nullptr || bBetterAngle || bSameAngleButCloser))
		{
			OutTarget = CandidateTarget;
			BestAimDot = AimDot;
			BestDistance = CandidateDistance;
		}
	}

	if (OutTarget == nullptr)
	{
		UE_LOG(LogPHTransformation, Verbose, TEXT("No authored Prop target inside the %.1f degree server targeting cone for %s."),
			SafeTargetingHalfAngleDegrees, *GetName());
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

	if (FVector::Dist(GetActorLocation(), OutTarget->GetActorLocation()) > SafeTransformationDistance)
	{
		OutFailure = EPHPropTransformationResult::RejectedTooFar;
		return false;
	}

	const UPHPropFormDataAsset* CandidateForm = OutTarget->GetPropForm();
	if (!IsPropFormAllowed(CandidateForm))
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

	const float CurrentHalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	OutLocation = GetActorLocation() + GetActorUpVector() * (NewCapsuleHalfHeight - CurrentHalfHeight);
	if (OutLocation.ContainsNaN() || OutLocation.GetAbsMax() >= HALF_WORLD_MAX)
	{
		return false;
	}

	TArray<AActor*> BlockingActors;
	if (QueryPlacementBlockers(Hitbox, OutLocation, BlockingActors))
	{
		return true;
	}

	if (CopiedTarget == nullptr || BlockingActors.IsEmpty()
		|| BlockingActors.ContainsByPredicate([CopiedTarget](const AActor* Actor) { return Actor != CopiedTarget; }))
	{
		return false;
	}

	const float SafeMaximumAdjustment = FMath::Clamp(MaximumPlacementAdjustment, 0.0f, 25.0f);
	const float SafeAdjustmentStep = FMath::Clamp(PlacementAdjustmentStep, 1.0f, FMath::Max(1.0f, SafeMaximumAdjustment));
	if (SafeMaximumAdjustment <= 0.0f)
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
		if (Distance <= KINDA_SMALL_NUMBER || Distance > SafeSearchDistance)
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

void APHPropCharacter::PerformAuthoritativeStartObjectiveInteraction(APHObjectiveActor* RequestedObjective)
{
	if (!HasAuthority() || RequestedObjective == nullptr || CaptureState != EPHPropCaptureState::Free)
	{
		return;
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

void APHPropCharacter::PerformAuthoritativeStopObjectiveInteraction()
{
	if (HasAuthority() && ActiveObjective != nullptr)
	{
		ActiveObjective->ServerEndInteraction(*this);
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
		&& CanPerformCaptureRescue()
		&& Target.GetCaptureState() == EPHPropCaptureState::Downed
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
	if (!CanAssistDownedTarget(Target))
	{
		return;
	}

	StopRetentionRescue();
	StopAssistingDownedTarget();
	AssistedDownedProp = &Target;
	Target.AddRecoveryHelper(*this);
	if (!Target.RecoveryHelpers.Contains(this))
	{
		AssistedDownedProp = nullptr;
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
	LastLocalStruggleDirection = 0;
	LastServerStruggleDirection = 0;
	LastLocalStruggleInputTime = -DBL_MAX;
	LastServerStruggleInputTime = -DBL_MAX;
	NextCarryStruggleShoveProgress = FMath::Clamp(CarryStruggleShoveProgressInterval, 0.05f, 0.5f);
	OnRep_CaptureProgress();
	ForceNetUpdate();
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
	const bool bMovementDisabled = bAttachedCapture
		|| CaptureState == EPHPropCaptureState::Eliminated;
	const bool bDowned = CaptureState == EPHPropCaptureState::Downed;

	SetActorHiddenInGame(CaptureState == EPHPropCaptureState::Eliminated);
	SetActorEnableCollision(!bAttachedCapture && CaptureState != EPHPropCaptureState::Eliminated);
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

	if (RetentionPoint != nullptr)
	{
		RetentionPoint->ClearRetainedProp(this);
	}
	RetentionPoint = nullptr;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetCaptureState(EPHPropCaptureState::Eliminated);
}

void APHPropCharacter::ClearCaptureRelationships()
{
	GetWorldTimerManager().ClearTimer(CaptureStateTimerHandle);
	StopAssistingDownedTarget();
	StopRetentionRescue();
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
