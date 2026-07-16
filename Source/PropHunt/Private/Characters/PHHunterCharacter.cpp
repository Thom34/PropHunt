#include "Characters/PHHunterCharacter.h"

#include "Camera/CameraComponent.h"
#include "Characters/PHPropCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Game/PHGameState.h"
#include "Game/PHPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Gameplay/PHCollisionChannels.h"
#include "Gameplay/Capture/PHRetentionPoint.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHMelee, Log, All);

APHHunterCharacter::APHHunterCharacter()
	: HumanWalkAnimationThreshold(10.0f)
	, CameraFieldOfView(90.0f)
	, CameraOffset(0.0f, 0.0f, 64.0f)
	, CarryCameraArmLength(325.0f)
	, CarryCameraSocketOffset(0.0f, 65.0f, 55.0f)
	, MeleeRange(175.0f)
	, MeleeWidth(90.0f)
	, MeleeVerticalTolerance(70.0f)
	, MeleeForwardOffset(20.0f)
	, MeleeRecoverySeconds(0.8f)
	, bDrawMeleeDebug(false)
	, MeleeDebugDuration(1.5f)
	, CaptureInteractionDistance(225.0f)
	, CaptureTargetingHalfAngleDegrees(35.0f)
	, CarryAnchorOffset(FVector::ZeroVector)
	, CarryAnchorRotation(FRotator::ZeroRotator)
	, CarryStruggleShoveDuration(0.35f)
	, CarriedProp(nullptr)
	, LastLocalMeleeRequestTime(-DBL_MAX)
	, LastServerMeleeAttackTime(-DBL_MAX)
	, bPlayingWalkAnimation(false)
	, bCurrentHumanAnimationFrozen(false)
	, ActiveCarryStruggleShoveDirection(0.0f)
	, RemainingCarryStruggleShoveDistance(0.0f)
	, ActiveCarryStruggleShoveSpeed(0.0f)
	, CurrentHumanAnimation(nullptr)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -96.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	GetMesh()->SetHiddenInGame(true);
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetAutoActivate(true);

	CarryCameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CarryCameraBoom"));
	CarryCameraBoom->SetupAttachment(GetCapsuleComponent());
	CarryCameraBoom->bUsePawnControlRotation = true;
	CarryCameraBoom->bDoCollisionTest = true;
	CarryCameraBoom->ProbeChannel = ECC_Camera;
	CarryCameraBoom->bEnableCameraLag = false;
	CarryCameraBoom->bInheritPitch = true;
	CarryCameraBoom->bInheritYaw = true;
	CarryCameraBoom->bInheritRoll = false;

	CarryThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CarryThirdPersonCamera"));
	CarryThirdPersonCamera->SetupAttachment(CarryCameraBoom, USpringArmComponent::SocketName);
	CarryThirdPersonCamera->bUsePawnControlRotation = false;
	CarryThirdPersonCamera->SetAutoActivate(false);

	GrayboxBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GrayboxBody"));
	GrayboxBody->SetupAttachment(GetCapsuleComponent());
	GrayboxBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrayboxBody->SetOwnerNoSee(true);
	GrayboxBody->SetCanEverAffectNavigation(false);
	GrayboxBody->SetRelativeScale3D(FVector(0.72f, 0.72f, 1.80f));

	FirstPersonPresentationRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FirstPersonPresentationRoot"));
	FirstPersonPresentationRoot->SetupAttachment(FirstPersonCamera);

	GrayboxLeftArm = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GrayboxLeftArm"));
	GrayboxLeftArm->SetupAttachment(FirstPersonPresentationRoot);
	GrayboxLeftArm->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrayboxLeftArm->SetOnlyOwnerSee(true);
	GrayboxLeftArm->SetCanEverAffectNavigation(false);
	GrayboxLeftArm->SetRelativeLocation(FVector(48.0f, -18.0f, -22.0f));
	GrayboxLeftArm->SetRelativeScale3D(FVector(0.55f, 0.08f, 0.08f));

	GrayboxRightArm = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GrayboxRightArm"));
	GrayboxRightArm->SetupAttachment(FirstPersonPresentationRoot);
	GrayboxRightArm->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrayboxRightArm->SetOnlyOwnerSee(true);
	GrayboxRightArm->SetCanEverAffectNavigation(false);
	GrayboxRightArm->SetRelativeLocation(FVector(48.0f, 18.0f, -22.0f));
	GrayboxRightArm->SetRelativeScale3D(FVector(0.55f, 0.08f, 0.08f));

	CarryAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("CarryAnchor"));
	CarryAnchor->SetupAttachment(GetCapsuleComponent());
	CarryAnchor->SetRelativeLocation(CarryAnchorOffset);
	CarryAnchor->SetRelativeRotation(CarryAnchorRotation);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		GrayboxBody->SetStaticMesh(CylinderMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		GrayboxLeftArm->SetStaticMesh(CubeMesh.Object);
		GrayboxRightArm->SetStaticMesh(CubeMesh.Object);
	}
}

void APHHunterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APHHunterCharacter, MeleeAttackState);
	DOREPLIFETIME(APHHunterCharacter, CarriedProp);
}

void APHHunterCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ConfigureHumanPresentation();
}

#if WITH_EDITOR
void APHHunterCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ConfigureHumanPresentation();
}
#endif

void APHHunterCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	FirstPersonCamera->SetRelativeLocation(CameraOffset);
	FirstPersonCamera->SetFieldOfView(CameraFieldOfView);
	CarryCameraBoom->TargetArmLength = CarryCameraArmLength;
	CarryCameraBoom->SocketOffset = CarryCameraSocketOffset;
	CarryThirdPersonCamera->SetFieldOfView(CameraFieldOfView);
	CarryAnchor->SetRelativeLocation(CarryAnchorOffset);
	CarryAnchor->SetRelativeRotation(CarryAnchorRotation);
	ConfigureHumanPresentation();
	ApplyLocalCameraMode();
}

void APHHunterCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCarryStruggleShove(DeltaSeconds);
	UpdateLocomotionPresentation();
}

void APHHunterCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && CarriedProp != nullptr)
	{
		CarriedProp->ServerDropFromCarrier();
	}
	Super::EndPlay(EndPlayReason);
}

void APHHunterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);

	PlayerInputComponent->BindAction(TEXT("MeleeAttack"), IE_Pressed, this, &APHHunterCharacter::RequestMeleeAttack);
	PlayerInputComponent->BindAction(TEXT("HunterInteract"), IE_Pressed, this, &APHHunterCharacter::RequestCaptureInteraction);
}

void APHHunterCharacter::RequestMeleeAttack()
{
	UWorld* World = GetWorld();
	if (!IsLocallyControlled() || World == nullptr)
	{
		return;
	}

	const double LocalTimeSeconds = World->GetTimeSeconds();
	if (LocalTimeSeconds - LastLocalMeleeRequestTime < GetSafeMeleeRecoverySeconds())
	{
		return;
	}

	LastLocalMeleeRequestTime = LocalTimeSeconds;
	BP_OnMeleeAttackAnticipated();
	if (HasAuthority())
	{
		PerformAuthoritativeMeleeAttack();
	}
	else
	{
		ServerRequestMeleeAttack();
	}
}

void APHHunterCharacter::ServerRequestMeleeAttack_Implementation()
{
	PerformAuthoritativeMeleeAttack();
}

void APHHunterCharacter::RequestCaptureInteraction()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	APHPropCharacter* RequestedProp = nullptr;
	APHRetentionPoint* RequestedPoint = nullptr;
	if (CarriedProp != nullptr)
	{
		TryResolveRetentionPoint(RequestedPoint);
	}
	else
	{
		TryResolveDownedProp(RequestedProp);
	}
	UE_LOG(LogPHMelee, Log, TEXT("Capture interaction request on %s: carried=%s prop=%s point=%s."),
		*GetName(),
		CarriedProp != nullptr ? *CarriedProp->GetName() : TEXT("None"),
		RequestedProp != nullptr ? *RequestedProp->GetName() : TEXT("None"),
		RequestedPoint != nullptr ? *RequestedPoint->GetName() : TEXT("None"));

	if (HasAuthority())
	{
		PerformAuthoritativeCaptureInteraction(RequestedProp, RequestedPoint);
	}
	else
	{
		ServerRequestCaptureInteraction(RequestedProp, RequestedPoint);
	}
}

void APHHunterCharacter::ClearCarriedProp(const APHPropCharacter* ExpectedProp)
{
	if (!HasAuthority() || CarriedProp == nullptr || CarriedProp != ExpectedProp)
	{
		return;
	}

	CarriedProp = nullptr;
	ClearCarryStruggleShove();
	OnRep_CarriedProp();
	ForceNetUpdate();
}

void APHHunterCharacter::ApplyCarryStruggleShove(
	const float SideDirection,
	const float ShoveDistance)
{
	if (!HasAuthority() || CarriedProp == nullptr || IsNearAvailableRetentionPoint())
	{
		return;
	}

	const float SafeDistance = FMath::Clamp(ShoveDistance, 25.0f, 250.0f);
	const float SafeDuration = FMath::Clamp(CarryStruggleShoveDuration, 0.1f, 1.0f);
	ActiveCarryStruggleShoveDirection = SideDirection < 0.0f ? -1.0f : 1.0f;
	RemainingCarryStruggleShoveDistance = SafeDistance;
	ActiveCarryStruggleShoveSpeed = SafeDistance / SafeDuration;
}

void APHHunterCharacter::ServerRequestCaptureInteraction_Implementation(
	APHPropCharacter* RequestedProp,
	APHRetentionPoint* RequestedPoint)
{
	PerformAuthoritativeCaptureInteraction(RequestedProp, RequestedPoint);
}

void APHHunterCharacter::OnRep_MeleeAttackState()
{
	BP_OnMeleeAttackConfirmed(MeleeAttackState.bHitProp);
}

void APHHunterCharacter::OnRep_CarriedProp()
{
	ApplyLocalCameraMode();
	BP_OnCarriedPropChanged(CarriedProp);
}

void APHHunterCharacter::ApplyLocalCameraMode()
{
	if (FirstPersonCamera == nullptr || CarryThirdPersonCamera == nullptr)
	{
		return;
	}

	const bool bUseCarryThirdPerson = CarriedProp != nullptr;
	FirstPersonCamera->SetActive(!bUseCarryThirdPerson);
	CarryThirdPersonCamera->SetActive(bUseCarryThirdPerson);
	if (FirstPersonPresentationRoot != nullptr)
	{
		FirstPersonPresentationRoot->SetVisibility(!bUseCarryThirdPerson, true);
	}
	if (GrayboxBody != nullptr)
	{
		GrayboxBody->SetOwnerNoSee(!bUseCarryThirdPerson);
	}
	if (GetMesh() != nullptr && HumanSkeletalMesh != nullptr)
	{
		GetMesh()->SetOwnerNoSee(!bUseCarryThirdPerson);
	}
}

void APHHunterCharacter::ConfigureHumanPresentation()
{
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (CharacterMesh == nullptr)
	{
		return;
	}

	const bool bHasHumanMesh = HumanSkeletalMesh != nullptr;
	if (bHasHumanMesh)
	{
		CharacterMesh->SetSkeletalMeshAsset(HumanSkeletalMesh);
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

	if (GrayboxBody != nullptr)
	{
		GrayboxBody->SetVisibility(!bHasHumanMesh, true);
	}
	bPlayingWalkAnimation = false;
}

void APHHunterCharacter::UpdateLocomotionPresentation()
{
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (CharacterMesh == nullptr || HumanSkeletalMesh == nullptr)
	{
		return;
	}

	const bool bShouldWalk = GetVelocity().SizeSquared2D()
		> FMath::Square(FMath::Max(0.0f, HumanWalkAnimationThreshold));
	UAnimSequence* DesiredAnimation = nullptr;
	bool bFreezePose = false;
	if (CarriedProp != nullptr)
	{
		DesiredAnimation = bShouldWalk && HumanCarryWalkAnimation != nullptr
			? HumanCarryWalkAnimation
			: HumanCarryIdleAnimation;
	}
	else
	{
		DesiredAnimation = bShouldWalk ? HumanWalkAnimation : HumanIdleAnimation;
		if (!bShouldWalk && DesiredAnimation == nullptr)
		{
			DesiredAnimation = HumanWalkAnimation;
			bFreezePose = DesiredAnimation != nullptr;
		}
	}

	if (DesiredAnimation == nullptr)
	{
		DesiredAnimation = bShouldWalk ? HumanWalkAnimation : HumanIdleAnimation;
	}
	if (DesiredAnimation == nullptr)
	{
		return;
	}

	PlayHumanAnimation(DesiredAnimation, bFreezePose);
	bPlayingWalkAnimation = bShouldWalk && DesiredAnimation == HumanWalkAnimation && !bFreezePose;
}

void APHHunterCharacter::PlayHumanAnimation(UAnimSequence* Animation, const bool bFreezePose)
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

void APHHunterCharacter::UpdateCarryStruggleShove(const float DeltaSeconds)
{
	if (!HasAuthority() || RemainingCarryStruggleShoveDistance <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	if (CarriedProp == nullptr || IsNearAvailableRetentionPoint())
	{
		ClearCarryStruggleShove();
		return;
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement == nullptr || Movement->MovementMode == MOVE_None)
	{
		ClearCarryStruggleShove();
		return;
	}

	const float StepDistance = FMath::Min(
		RemainingCarryStruggleShoveDistance,
		FMath::Max(0.0f, ActiveCarryStruggleShoveSpeed) * FMath::Max(0.0f, DeltaSeconds));
	if (StepDistance <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FHitResult Hit;
	Movement->SafeMoveUpdatedComponent(
		GetActorRightVector() * ActiveCarryStruggleShoveDirection * StepDistance,
		GetActorQuat(),
		true,
		Hit);
	RemainingCarryStruggleShoveDistance -= StepDistance;
	if (Hit.IsValidBlockingHit() || RemainingCarryStruggleShoveDistance <= KINDA_SMALL_NUMBER)
	{
		ClearCarryStruggleShove();
	}
}

void APHHunterCharacter::ClearCarryStruggleShove()
{
	ActiveCarryStruggleShoveDirection = 0.0f;
	RemainingCarryStruggleShoveDistance = 0.0f;
	ActiveCarryStruggleShoveSpeed = 0.0f;
}

bool APHHunterCharacter::CanPerformMeleeAttack(const double ServerTimeSeconds) const
{
	if (!HasAuthority() || Controller == nullptr)
	{
		return false;
	}

	const APHPlayerState* PHPlayerState = GetPlayerState<APHPlayerState>();
	if (PHPlayerState == nullptr || PHPlayerState->GetPlayerRole() != EPHPlayerRole::Hunter)
	{
		return false;
	}

	const APHGameState* PHGameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	if (PHGameState == nullptr || (PHGameState->GetMatchPhase() != EPHMatchPhase::Hunt && PHGameState->GetMatchPhase() != EPHMatchPhase::Escape))
	{
		return false;
	}

	return ServerTimeSeconds - LastServerMeleeAttackTime >= GetSafeMeleeRecoverySeconds();
}

double APHHunterCharacter::GetSafeMeleeRecoverySeconds() const
{
	return FMath::Clamp(static_cast<double>(MeleeRecoverySeconds), 0.1, 5.0);
}

void APHHunterCharacter::PerformAuthoritativeMeleeAttack()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const double ServerTimeSeconds = World->GetTimeSeconds();
	if (!CanPerformMeleeAttack(ServerTimeSeconds))
	{
		return;
	}

	LastServerMeleeAttackTime = ServerTimeSeconds;

	const FVector ViewDirection = Controller->GetControlRotation().Vector();
	const float SafeForwardOffset = FMath::Clamp(MeleeForwardOffset, 0.0f, 75.0f);
	const float SafeRange = FMath::Clamp(MeleeRange, 50.0f, 300.0f);
	const float SafeWidth = FMath::Clamp(MeleeWidth, 10.0f, 200.0f);
	const float SafeVerticalTolerance = FMath::Clamp(MeleeVerticalTolerance, 10.0f, 150.0f);
	const float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector MeleeOrigin = GetActorLocation() + GetActorUpVector() * (CapsuleHalfHeight * 0.5f);
	const FVector TraceStart = MeleeOrigin + ViewDirection * SafeForwardOffset;
	const FVector TraceEnd = MeleeOrigin + ViewDirection * (SafeForwardOffset + SafeRange);

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(PHCollision::PropHitbox);
	FCollisionQueryParams SweepQueryParams(SCENE_QUERY_STAT(PHMeleeAttack), false, this);
	SweepQueryParams.AddIgnoredActor(this);
	TArray<FHitResult> HitResults;
	World->SweepMultiByObjectType(
		HitResults,
		TraceStart,
		TraceEnd,
		Controller->GetControlRotation().Quaternion(),
		ObjectQueryParams,
		FCollisionShape::MakeBox(FVector(5.0f, SafeWidth * 0.5f, SafeVerticalTolerance)),
		SweepQueryParams);
	UE_LOG(LogPHMelee, VeryVerbose, TEXT("Melee sweep found %d result(s), start %s, end %s, view %s."),
		HitResults.Num(), *TraceStart.ToCompactString(), *TraceEnd.ToCompactString(), *ViewDirection.ToCompactString());

	HitResults.Sort([](const FHitResult& Left, const FHitResult& Right)
	{
		return Left.Distance < Right.Distance;
	});

	APHPropCharacter* HitProp = nullptr;
	for (const FHitResult& CandidateHit : HitResults)
	{
		UE_LOG(LogPHMelee, VeryVerbose, TEXT("Melee candidate actor %s, component %s, object channel %d, distance %.1f."),
			CandidateHit.GetActor() != nullptr ? *CandidateHit.GetActor()->GetName() : TEXT("None"),
			CandidateHit.GetComponent() != nullptr ? *CandidateHit.GetComponent()->GetName() : TEXT("None"),
			CandidateHit.GetComponent() != nullptr ? static_cast<int32>(CandidateHit.GetComponent()->GetCollisionObjectType()) : -1,
			CandidateHit.Distance);
		APHPropCharacter* CandidateProp = Cast<APHPropCharacter>(CandidateHit.GetActor());
		if (CandidateProp == nullptr)
		{
			continue;
		}

		const FVector LineOfSightPoint = CandidateProp->GetMeleeLineOfSightPoint(TraceStart);
		FCollisionQueryParams LineOfSightParams(SCENE_QUERY_STAT(PHMeleeLineOfSight), false, this);
		LineOfSightParams.AddIgnoredActor(this);
		FHitResult LineOfSightHit;
		const bool bLineOfSightHit = World->LineTraceSingleByChannel(
			LineOfSightHit, TraceStart, LineOfSightPoint, ECC_Visibility, LineOfSightParams);
		const bool bHasLineOfSight = !bLineOfSightHit || LineOfSightHit.GetActor() == CandidateProp;
		if (bDrawMeleeDebug)
		{
			DrawDebugLine(World, TraceStart, LineOfSightPoint,
				bHasLineOfSight ? FColor::Cyan : FColor::Red, false, MeleeDebugDuration, 0, 2.0f);
			DrawDebugPoint(World, LineOfSightPoint, 10.0f,
				bHasLineOfSight ? FColor::Cyan : FColor::Red, false, MeleeDebugDuration);
		}
		UE_LOG(LogPHMelee, VeryVerbose, TEXT("Melee line of sight to %s at %s: %s (%s/%s)."),
			*CandidateProp->GetName(), *LineOfSightPoint.ToCompactString(),
			bLineOfSightHit ? TEXT("blocked") : TEXT("clear"),
			LineOfSightHit.GetActor() != nullptr ? *LineOfSightHit.GetActor()->GetName() : TEXT("None"),
			LineOfSightHit.GetComponent() != nullptr ? *LineOfSightHit.GetComponent()->GetName() : TEXT("None"));
		if (bHasLineOfSight)
		{
			HitProp = CandidateProp;
			break;
		}
	}

	if (HitProp != nullptr)
	{
		HitProp->ReceiveAuthoritativeMeleeHit(*this);
	}

	if (bDrawMeleeDebug)
	{
		const FVector DebugExtent(5.0f, SafeWidth * 0.5f, SafeVerticalTolerance);
		const FQuat DebugRotation = Controller->GetControlRotation().Quaternion();
		const FColor DebugColor = HitProp != nullptr ? FColor::Green : FColor::Red;
		for (int32 SampleIndex = 0; SampleIndex <= 8; ++SampleIndex)
		{
			const float Alpha = static_cast<float>(SampleIndex) / 8.0f;
			DrawDebugBox(World, FMath::Lerp(TraceStart, TraceEnd, Alpha), DebugExtent,
				DebugRotation, DebugColor, false, MeleeDebugDuration, 0, 1.0f);
		}
		DrawDebugDirectionalArrow(World, TraceStart, TraceEnd, 18.0f, DebugColor,
			false, MeleeDebugDuration, 0, 2.0f);
	}

	MeleeAttackState.Sequence = MeleeAttackState.Sequence == MAX_int32 ? 1 : MeleeAttackState.Sequence + 1;
	MeleeAttackState.bHitProp = HitProp != nullptr;
	OnRep_MeleeAttackState();
	ForceNetUpdate();

	UE_LOG(LogPHMelee, Log, TEXT("Authoritative melee attack %d: %s%s"),
		MeleeAttackState.Sequence,
		HitProp != nullptr ? TEXT("hit ") : TEXT("miss"),
		HitProp != nullptr ? *HitProp->GetName() : TEXT(""));
}

bool APHHunterCharacter::TryResolveDownedProp(APHPropCharacter*& OutProp) const
{
	OutProp = nullptr;
	const UWorld* World = GetWorld();
	if (World == nullptr || Controller == nullptr)
	{
		return false;
	}

	const FVector Origin = FirstPersonCamera != nullptr
		? FirstPersonCamera->GetComponentLocation()
		: GetActorLocation();
	const FVector AimDirection = Controller->GetControlRotation().Vector().GetSafeNormal();
	const float SafeDistance = FMath::Clamp(CaptureInteractionDistance, 100.0f, 500.0f);
	const float MinimumAimDot = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(CaptureTargetingHalfAngleDegrees, 5.0f, 45.0f)));
	float BestDistance = SafeDistance;

	for (TActorIterator<APHPropCharacter> PropIterator(World); PropIterator; ++PropIterator)
	{
		APHPropCharacter* Candidate = *PropIterator;
		if (!IsValid(Candidate) || Candidate->GetCaptureState() != EPHPropCaptureState::Downed)
		{
			continue;
		}

		const FVector TargetPoint = Candidate->GetMeleeLineOfSightPoint(Origin);
		const FVector ToProp = Candidate->GetActorLocation() - Origin;
		const float Distance = FVector::Distance(GetActorLocation(), Candidate->GetActorLocation());
		const float AimDot = PHCaptureFlow::GetHorizontalAimDot(AimDirection, ToProp);
		const bool bHasLineOfSight = HasCaptureLineOfSight(*Candidate, TargetPoint);
		UE_LOG(LogPHMelee, Log, TEXT("Capture candidate %s: distance=%.1f dot=%.3f minimum=%.3f los=%s."),
			*Candidate->GetName(), Distance, AimDot, MinimumAimDot, bHasLineOfSight ? TEXT("yes") : TEXT("no"));
		if (Distance > SafeDistance
			|| !PHCaptureFlow::IsDownedPickupDirectionAllowed(AimDirection, ToProp, MinimumAimDot)
			|| !bHasLineOfSight)
		{
			continue;
		}

		if (OutProp == nullptr || Distance < BestDistance)
		{
			OutProp = Candidate;
			BestDistance = Distance;
		}
	}

	return OutProp != nullptr;
}

bool APHHunterCharacter::TryResolveRetentionPoint(APHRetentionPoint*& OutPoint) const
{
	OutPoint = nullptr;
	const UWorld* World = GetWorld();
	if (World == nullptr || Controller == nullptr)
	{
		return false;
	}

	const FVector Origin = FirstPersonCamera != nullptr
		? FirstPersonCamera->GetComponentLocation()
		: GetActorLocation();
	const FVector AimDirection = Controller->GetControlRotation().Vector().GetSafeNormal();
	const float SafeDistance = FMath::Clamp(CaptureInteractionDistance, 100.0f, 500.0f);
	const float MinimumAimDot = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(CaptureTargetingHalfAngleDegrees, 5.0f, 45.0f)));
	float BestDistance = SafeDistance;

	for (TActorIterator<APHRetentionPoint> PointIterator(World); PointIterator; ++PointIterator)
	{
		APHRetentionPoint* Candidate = *PointIterator;
		if (!IsValid(Candidate) || Candidate->GetRetainedProp() != nullptr)
		{
			continue;
		}

		const FVector ToPoint = Candidate->GetInteractionPoint() - Origin;
		const float Distance = ToPoint.Size();
		if (Distance <= KINDA_SMALL_NUMBER || Distance > SafeDistance
			|| FVector::DotProduct(AimDirection, ToPoint / Distance) < MinimumAimDot
			|| !HasCaptureLineOfSight(*Candidate, Candidate->GetInteractionPoint()))
		{
			continue;
		}

		if (OutPoint == nullptr || Distance < BestDistance)
		{
			OutPoint = Candidate;
			BestDistance = Distance;
		}
	}

	return OutPoint != nullptr;
}

bool APHHunterCharacter::IsNearAvailableRetentionPoint() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || CarriedProp == nullptr)
	{
		return false;
	}

	const float SafeDistance = FMath::Clamp(CaptureInteractionDistance, 100.0f, 500.0f);
	for (TActorIterator<APHRetentionPoint> PointIterator(World); PointIterator; ++PointIterator)
	{
		const APHRetentionPoint* Point = *PointIterator;
		if (IsValid(Point) && Point->GetRetainedProp() == nullptr
			&& FVector::DistSquared(GetActorLocation(), Point->GetActorLocation()) <= FMath::Square(SafeDistance)
			&& Point->CanHunterRetain(*this, *CarriedProp))
		{
			return true;
		}
	}
	return false;
}

void APHHunterCharacter::PerformAuthoritativeCaptureInteraction(
	APHPropCharacter* RequestedProp,
	APHRetentionPoint* RequestedPoint)
{
	const APHPlayerState* HunterState = GetPlayerState<APHPlayerState>();
	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	const EPHMatchPhase Phase = GameState != nullptr ? GameState->GetMatchPhase() : EPHMatchPhase::Lobby;
	if (!HasAuthority() || HunterState == nullptr || HunterState->GetPlayerRole() != EPHPlayerRole::Hunter
		|| (Phase != EPHMatchPhase::Hunt && Phase != EPHMatchPhase::Escape))
	{
		return;
	}

	if (CarriedProp != nullptr)
	{
		if (RequestedPoint != nullptr && RequestedPoint->ServerTryRetain(*this, *CarriedProp))
		{
			return;
		}
		CarriedProp->ServerDropFromCarrier();
		return;
	}

	if (RequestedProp == nullptr || RequestedProp->GetCaptureState() != EPHPropCaptureState::Downed)
	{
		return;
	}

	const float SafeDistance = FMath::Clamp(CaptureInteractionDistance, 100.0f, 500.0f);
	const FVector TargetPoint = RequestedProp->GetMeleeLineOfSightPoint(GetActorLocation());
	if (FVector::DistSquared(GetActorLocation(), RequestedProp->GetActorLocation()) > FMath::Square(SafeDistance)
		|| !HasCaptureLineOfSight(*RequestedProp, TargetPoint)
		|| !RequestedProp->ServerTryBeCarriedBy(*this))
	{
		return;
	}

	CarriedProp = RequestedProp;
	OnRep_CarriedProp();
	ForceNetUpdate();
	UE_LOG(LogPHMelee, Log, TEXT("%s started carrying %s."), *GetName(), *RequestedProp->GetName());
}

bool APHHunterCharacter::HasCaptureLineOfSight(const AActor& Target, const FVector& TargetPoint) const
{
	if (GetWorld() == nullptr)
	{
		return false;
	}

	const FVector TraceStart = FirstPersonCamera != nullptr
		? FirstPersonCamera->GetComponentLocation()
		: GetActorLocation() + FVector::UpVector * GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHCaptureLineOfSight), false, this);
	QueryParams.AddIgnoredActor(this);
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, TraceStart, TargetPoint, ECC_Visibility, QueryParams);
	return !bHit || Hit.GetActor() == &Target;
}
