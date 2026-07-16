#include "Gameplay/Physics/PHPhysicsPropPrototype.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Gameplay/PHCollisionChannels.h"
#include "Gameplay/Physics/PHPhysicsPropDataAsset.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHPhysicsProp, Log, All);

APHPhysicsPropPrototype::APHPhysicsPropPrototype()
	: ImpactSequence(0)
	, bAutoResetForNetworkTesting(false)
	, AutoResetIntervalSeconds(5.0f)
	, CameraArmLength(325.0f)
	, CameraTargetHeight(95.0f)
	, CameraFieldOfView(90.0f)
	, YawInputScale(1.0f)
	, PitchInputScale(1.0f)
	, LocalForwardInput(0.0f)
	, LocalRightInput(0.0f)
	, bLocalStraightenHeld(false)
	, ServerForwardInput(0.0f)
	, ServerRightInput(0.0f)
	, ServerViewYaw(0.0f)
	, bServerStraightenHeld(false)
	, bStraightenWasActive(false)
	, bMovementWasActive(false)
	, bWasGrounded(false)
	, JumpsUsed(0)
	, CurrentStraightenInterpSpeed(0.0f)
	, LastServerControlIntentTime(-DBL_MAX)
	, LastJumpTime(-DBL_MAX)
	, LastImpactSoundTime(-DBL_MAX)
#if !UE_BUILD_SHIPPING
	, JumpSmokePreJumpHorizontalSpeed(0.0f)
	, JumpSmokeFirstHorizontalSpeed(0.0f)
	, JumpSmokeBeforeSecondHorizontalSpeed(0.0f)
	, JumpSmokeSecondHorizontalSpeed(0.0f)
	, JumpSmokeSecondRightDot(0.0f)
	, JumpSmokeCountAfterThirdAttempt(0)
#endif
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(10.0f);
	AutoPossessPlayer = EAutoReceiveInput::Player0;
	bUseControllerRotationYaw = false;

	PhysicsMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PhysicsMesh"));
	SetRootComponent(PhysicsMesh);
	PhysicsMesh->SetIsReplicated(true);
	PhysicsMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PhysicsMesh->SetCollisionObjectType(ECC_PhysicsBody);
	PhysicsMesh->SetCollisionResponseToAllChannels(ECR_Block);
	PhysicsMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	PhysicsMesh->SetCollisionResponseToChannel(PHCollision::PropHitbox, ECR_Ignore);
	PhysicsMesh->SetGenerateOverlapEvents(false);
	PhysicsMesh->SetNotifyRigidBodyCollision(true);
	PhysicsMesh->SetCanEverAffectNavigation(false);
	PhysicsMesh->CanCharacterStepUpOn = ECB_No;
	PhysicsMesh->OnComponentHit.AddDynamic(this, &APHPhysicsPropPrototype::OnPhysicsHit);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(PhysicsMesh);
	CameraBoom->SetAbsolute(false, true, false);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeChannel = ECC_Camera;
	CameraBoom->TargetArmLength = CameraArmLength;
	CameraBoom->TargetOffset = FVector(0.0f, 0.0f, CameraTargetHeight);
	CameraBoom->SocketOffset = FVector::ZeroVector;
	CameraBoom->bInheritRoll = false;

	ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdPersonCamera"));
	ThirdPersonCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	ThirdPersonCamera->bUsePawnControlRotation = false;
	ThirdPersonCamera->SetFieldOfView(CameraFieldOfView);
}

void APHPhysicsPropPrototype::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyDefinition(false);
}

void APHPhysicsPropPrototype::BeginPlay()
{
	Super::BeginPlay();
	InitialTestTransform = GetActorTransform();
	CameraBoom->TargetArmLength = CameraArmLength;
	CameraBoom->TargetOffset = FVector(0.0f, 0.0f, CameraTargetHeight);
	CameraBoom->SocketOffset = FVector::ZeroVector;
	ThirdPersonCamera->SetFieldOfView(CameraFieldOfView);
	ApplyDefinition(true);
	bWasGrounded = IsGrounded();

	if (HasAuthority() && bAutoResetForNetworkTesting && Controller == nullptr)
	{
		GetWorldTimerManager().SetTimer(
			AutoResetTimerHandle,
			this,
			&APHPhysicsPropPrototype::ResetTestDrop,
			FMath::Clamp(AutoResetIntervalSeconds, 2.0f, 30.0f),
			true);
	}

#if !UE_BUILD_SHIPPING
	if (HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("PHPhysicsJumpSmoke")))
	{
		GetWorldTimerManager().SetTimer(
			JumpSmokeTimerHandle,
			this,
			&APHPhysicsPropPrototype::BeginJumpSmoke,
			8.0f,
			false);
	}
#endif
}

void APHPhysicsPropPrototype::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled())
	{
		const float ViewYaw = Controller != nullptr ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw;
		if (HasAuthority())
		{
			SetServerControlIntent(LocalForwardInput, LocalRightInput, ViewYaw, bLocalStraightenHeld);
		}
		else
		{
			ServerSetControlIntent(LocalForwardInput, LocalRightInput, ViewYaw, bLocalStraightenHeld);
		}
	}

	if (HasAuthority())
	{
		const UWorld* World = GetWorld();
		if (!IsLocallyControlled() && World != nullptr && World->GetTimeSeconds() - LastServerControlIntentTime > 0.25)
		{
			ServerForwardInput = 0.0f;
			ServerRightInput = 0.0f;
			bServerStraightenHeld = false;
		}
		ApplyServerControlIntent(DeltaSeconds);
	}
}

void APHPhysicsPropPrototype::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);

	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &APHPhysicsPropPrototype::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &APHPhysicsPropPrototype::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("LookYaw"), this, &APHPhysicsPropPrototype::LookYaw);
	PlayerInputComponent->BindAxis(TEXT("LookPitch"), this, &APHPhysicsPropPrototype::LookPitch);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &APHPhysicsPropPrototype::RequestJump);
	PlayerInputComponent->BindAction(TEXT("StraightenProp"), IE_Pressed, this, &APHPhysicsPropPrototype::StartStraighten);
	PlayerInputComponent->BindAction(TEXT("StraightenProp"), IE_Released, this, &APHPhysicsPropPrototype::StopStraighten);
}

void APHPhysicsPropPrototype::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	GetWorldTimerManager().ClearTimer(AutoResetTimerHandle);
	UE_LOG(LogPHPhysicsProp, Log, TEXT("Playable Physics Prop %s possessed by %s on authority."),
		*GetName(), NewController != nullptr ? *NewController->GetName() : TEXT("none"));
}

void APHPhysicsPropPrototype::UnPossessed()
{
	UE_LOG(LogPHPhysicsProp, Log, TEXT("Playable Physics Prop %s unpossessed on authority."), *GetName());
	Super::UnPossessed();
	ServerForwardInput = 0.0f;
	ServerRightInput = 0.0f;
	bServerStraightenHeld = false;
}

void APHPhysicsPropPrototype::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APHPhysicsPropPrototype, Definition);
	DOREPLIFETIME(APHPhysicsPropPrototype, ImpactSequence);
}

void APHPhysicsPropPrototype::MoveForward(const float Value)
{
	LocalForwardInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void APHPhysicsPropPrototype::MoveRight(const float Value)
{
	LocalRightInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void APHPhysicsPropPrototype::LookYaw(const float Value)
{
	AddControllerYawInput(Value * YawInputScale);
}

void APHPhysicsPropPrototype::LookPitch(const float Value)
{
	AddControllerPitchInput(Value * PitchInputScale);
}

void APHPhysicsPropPrototype::RequestJump()
{
	if (HasAuthority())
	{
		TryJump();
	}
	else
	{
		ServerRequestJump();
	}
}

void APHPhysicsPropPrototype::StartStraighten()
{
	bLocalStraightenHeld = true;
}

void APHPhysicsPropPrototype::StopStraighten()
{
	bLocalStraightenHeld = false;
}

void APHPhysicsPropPrototype::ServerSetControlIntent_Implementation(
	const float Forward,
	const float Right,
	const float ViewYaw,
	const bool bStraighten)
{
	SetServerControlIntent(Forward, Right, ViewYaw, bStraighten);
}

void APHPhysicsPropPrototype::ServerRequestJump_Implementation()
{
	TryJump();
}

void APHPhysicsPropPrototype::SetServerControlIntent(
	const float Forward,
	const float Right,
	const float ViewYaw,
	const bool bStraighten)
{
	if (!HasAuthority())
	{
		return;
	}

	ServerForwardInput = FMath::IsFinite(Forward) ? FMath::Clamp(Forward, -1.0f, 1.0f) : 0.0f;
	ServerRightInput = FMath::IsFinite(Right) ? FMath::Clamp(Right, -1.0f, 1.0f) : 0.0f;
	ServerViewYaw = FMath::IsFinite(ViewYaw) ? FRotator::NormalizeAxis(ViewYaw) : GetActorRotation().Yaw;
	bServerStraightenHeld = bStraighten;
	LastServerControlIntentTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
}

void APHPhysicsPropPrototype::ApplyServerControlIntent(const float DeltaSeconds)
{
	if (PhysicsMesh == nullptr || Definition == nullptr || !PhysicsMesh->IsSimulatingPhysics())
	{
		return;
	}

	const bool bGroundedNow = IsGrounded();
	if (bGroundedNow && !bWasGrounded && JumpsUsed > 0)
	{
		JumpsUsed = 0;
		UE_LOG(LogPHPhysicsProp, Verbose, TEXT("Jump count reset after %s landed."), *GetName());
	}
	bWasGrounded = bGroundedNow;

	const FVector2D RawInput(ServerForwardInput, ServerRightInput);
	const bool bHasMovementInput = RawInput.SizeSquared() > FMath::Square(0.05f);
	if (bHasMovementInput)
	{
		if (!bMovementWasActive)
		{
			UE_LOG(LogPHPhysicsProp, Log, TEXT("Authoritative movement started for %s at %s."),
				*GetName(), *PhysicsMesh->GetComponentLocation().ToCompactString());
		}

		const FVector2D ClampedInput = RawInput.GetClampedToMaxSize(1.0f);
		const FRotator YawRotation(0.0f, ServerViewYaw, 0.0f);
		const FVector DesiredDirection = (
			FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X) * ClampedInput.X
			+ FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y) * ClampedInput.Y).GetSafeNormal();
		const FVector LinearVelocity = PhysicsMesh->GetPhysicsLinearVelocity();
		const FVector HorizontalVelocity(LinearVelocity.X, LinearVelocity.Y, 0.0f);
		const float NormalMaximumSpeed = FMath::Clamp(Definition->MaximumHorizontalSpeed, 1.0f, 2000.0f);
		const float JumpMaximumSpeed = FMath::Clamp(
			Definition->MaximumJumpHorizontalSpeed,
			NormalMaximumSpeed,
			2500.0f);
		const float SafeMaximumSpeed = !bGroundedNow && JumpsUsed > 0
			? JumpMaximumSpeed
			: NormalMaximumSpeed;
		float ExperimentalTranslationScale = 1.0f;
		if (Definition->bUseExperimentalAngularAssists)
		{
			const FVector TorqueAxis = FVector::CrossProduct(FVector::UpVector, DesiredDirection).GetSafeNormal();
			const float SafeTorque = FMath::Clamp(Definition->MovementTorqueDegrees, 0.0f, 5000000.0f);
			PhysicsMesh->AddTorqueInDegrees(TorqueAxis * SafeTorque * ClampedInput.Size(), NAME_None, false);

			FVector ControlledAngularVelocity = PhysicsMesh->GetPhysicsAngularVelocityInDegrees();
			const FVector LongAxis = PhysicsMesh->GetComponentQuat().GetAxisZ().GetSafeNormal();
			const float LyingAlpha = 1.0f - FMath::Abs(FVector::DotProduct(LongAxis, FVector::UpVector));
			const float EndBalanceAlpha = 1.0f - FMath::SmoothStep(0.0f, 0.45f, LyingAlpha);
			const float MinimumRotationSpeed = FMath::Clamp(
				Definition->MinimumMovementRotationSpeedDegrees,
				0.0f,
				Definition->MaximumAngularVelocityDegrees);
			const float DesiredMainRotationSpeed = FMath::Lerp(
				Definition->MaximumAngularVelocityDegrees,
				MinimumRotationSpeed,
				LyingAlpha) * ClampedInput.Size();
			const float MainRotationSpeed = FVector::DotProduct(ControlledAngularVelocity, TorqueAxis);
			const float MainAngularAcceleration = FMath::Lerp(
				FMath::Clamp(Definition->MovementAngularAccelerationDegrees, 0.0f, 5000.0f),
				FMath::Clamp(Definition->EndTipAngularAccelerationDegrees, 0.0f, 5000.0f),
				EndBalanceAlpha);
			const float NewMainRotationSpeed = FMath::FInterpConstantTo(
				MainRotationSpeed,
				DesiredMainRotationSpeed,
				DeltaSeconds,
				MainAngularAcceleration);
			ControlledAngularVelocity += TorqueAxis * (NewMainRotationSpeed - MainRotationSpeed);

			if (LyingAlpha > 0.1f)
			{
				const float DesiredTumbleSpeed = FMath::Clamp(
					Definition->MinimumLyingTumbleSpeedDegrees,
					0.0f,
					Definition->MaximumAngularVelocityDegrees) * LyingAlpha;
				const float TumbleSign = FVector::DotProduct(LongAxis, DesiredDirection) >= 0.0f ? 1.0f : -1.0f;
				const float TargetTumbleSpeed = DesiredTumbleSpeed * TumbleSign * ClampedInput.Size();
				const float CurrentTumbleSpeed = FVector::DotProduct(ControlledAngularVelocity, LongAxis);
				const float NewTumbleSpeed = FMath::FInterpConstantTo(
					CurrentTumbleSpeed,
					TargetTumbleSpeed,
					DeltaSeconds,
					FMath::Clamp(Definition->LyingTumbleAngularAccelerationDegrees, 0.0f, 5000.0f));
				ControlledAngularVelocity += LongAxis * (NewTumbleSpeed - CurrentTumbleSpeed);
			}
			PhysicsMesh->SetPhysicsAngularVelocityInDegrees(ControlledAngularVelocity, false);

			const float AxialAlignment = FMath::Abs(FVector::DotProduct(LongAxis, DesiredDirection));
			const float SpeedAlongInput = FMath::Abs(FVector::DotProduct(HorizontalVelocity, DesiredDirection));
			const bool bAxialVaultActive = LyingAlpha > 0.55f
				&& AxialAlignment >= FMath::Clamp(Definition->AxialEscapeAlignmentThreshold, 0.0f, 1.0f)
				&& SpeedAlongInput <= FMath::Clamp(Definition->AxialEscapeStallSpeed, 0.0f, 500.0f)
				&& IsGrounded();
			if (bAxialVaultActive)
			{
				const float LeadingSign = FVector::DotProduct(LongAxis, DesiredDirection) >= 0.0f ? 1.0f : -1.0f;
				const FVector LeadingAxis = LongAxis * LeadingSign;
				const float LeverArmScale = FMath::Clamp(Definition->AxialVaultLeverArmScale, 0.1f, 1.0f);
				const FVector TrailingApplicationPoint = PhysicsMesh->GetCenterOfMass()
					- LeadingAxis * PhysicsMesh->Bounds.SphereRadius * LeverArmScale;
				const float LiftAcceleration = FMath::Clamp(
					Definition->AxialVaultLiftAcceleration,
					0.0f,
					5000.0f);
				PhysicsMesh->AddForceAtLocation(
					FVector::UpVector * PhysicsMesh->GetMass() * LiftAcceleration,
					TrailingApplicationPoint);
			}

			const float EndTranslationScale = FMath::Clamp(
				Definition->EndBalanceTranslationScale,
				0.0f,
				1.0f);
			const float OrientationTranslationScale = FMath::Lerp(
				EndTranslationScale,
				1.0f,
				FMath::SmoothStep(0.0f, 0.65f, LyingAlpha));
			const float RotationTranslationScale = DesiredMainRotationSpeed > UE_SMALL_NUMBER
				? FMath::Clamp(FMath::Abs(NewMainRotationSpeed) / DesiredMainRotationSpeed, 0.0f, 1.0f)
				: 0.0f;
			const float AxialVaultTranslationScale = bAxialVaultActive ? 0.0f : 1.0f;
			ExperimentalTranslationScale = OrientationTranslationScale
				* RotationTranslationScale
				* AxialVaultTranslationScale;
		}
		if (!bGroundedNow && JumpsUsed > 0)
		{
			ExperimentalTranslationScale = 1.0f;
		}

		const FVector DesiredHorizontalVelocity = DesiredDirection
			* SafeMaximumSpeed
			* ClampedInput.Size()
			* ExperimentalTranslationScale;
		const float SafeVelocityInterpSpeed = FMath::Clamp(
			Definition->MovementVelocityInterpSpeed,
			0.1f,
			50.0f);
		const FVector NewHorizontalVelocity = FMath::VInterpTo(
			HorizontalVelocity,
			DesiredHorizontalVelocity,
			DeltaSeconds,
			SafeVelocityInterpSpeed);
		PhysicsMesh->SetPhysicsLinearVelocity(
			FVector(NewHorizontalVelocity.X, NewHorizontalVelocity.Y, LinearVelocity.Z),
			false);

		if (!bMovementWasActive && Definition->MovementHopImpulse > 0.0f)
		{
			const float SafeHopVelocity = FMath::Clamp(Definition->MovementHopImpulse, 0.0f, 250.0f);
			PhysicsMesh->AddImpulse(FVector::UpVector * PhysicsMesh->GetMass() * SafeHopVelocity);
		}
	}
	else if (bMovementWasActive)
	{
		UE_LOG(LogPHPhysicsProp, Log, TEXT("Authoritative movement stopped for %s at %s with linear %s and angular %s deg/s."),
			*GetName(),
			*PhysicsMesh->GetComponentLocation().ToCompactString(),
			*PhysicsMesh->GetPhysicsLinearVelocity().ToCompactString(),
			*PhysicsMesh->GetPhysicsAngularVelocityInDegrees().ToCompactString());
	}

	if (!bHasMovementInput)
	{
		const FVector LinearVelocity = PhysicsMesh->GetPhysicsLinearVelocity();
		const FVector HorizontalVelocity(LinearVelocity.X, LinearVelocity.Y, 0.0f);
		const float SafeStopInterpSpeed = FMath::Clamp(
			Definition->MovementStopInterpSpeed,
			0.1f,
			100.0f);
		const FVector NewHorizontalVelocity = FMath::VInterpTo(
			HorizontalVelocity,
			FVector::ZeroVector,
			DeltaSeconds,
			SafeStopInterpSpeed);
		PhysicsMesh->SetPhysicsLinearVelocity(
			FVector(NewHorizontalVelocity.X, NewHorizontalVelocity.Y, LinearVelocity.Z),
			false);
	}
	bMovementWasActive = bHasMovementInput;

	ApplyStraightenState(DeltaSeconds);
}

void APHPhysicsPropPrototype::TryJump()
{
	if (!HasAuthority() || PhysicsMesh == nullptr || Definition == nullptr || !PhysicsMesh->IsSimulatingPhysics())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const double SafeCooldown = FMath::Clamp(static_cast<double>(Definition->JumpCooldownSeconds), 0.05, 2.0);
	if (Now - LastJumpTime < SafeCooldown)
	{
		return;
	}

	const bool bGrounded = IsGrounded();
	const int32 SafeMaximumJumpCount = FMath::Clamp(Definition->MaximumJumpCount, 1, 3);
	if (bGrounded && !bWasGrounded)
	{
		JumpsUsed = 0;
	}
	if ((!bGrounded && JumpsUsed == 0) || JumpsUsed >= SafeMaximumJumpCount)
	{
		return;
	}

	const float SafeJumpVelocity = FMath::Clamp(Definition->JumpVelocity, 0.0f, 1000.0f);
	if (SafeJumpVelocity <= 0.0f)
	{
		return;
	}

	const FVector CurrentVelocity = PhysicsMesh->GetPhysicsLinearVelocity();
	FVector HorizontalVelocity(CurrentVelocity.X, CurrentVelocity.Y, 0.0f);
	const FVector2D RawInput(ServerForwardInput, ServerRightInput);
	if (RawInput.SizeSquared() > FMath::Square(0.05f))
	{
		const FVector2D ClampedInput = RawInput.GetClampedToMaxSize(1.0f);
		const FRotator YawRotation(0.0f, ServerViewYaw, 0.0f);
		const FVector BoostDirection = (
			FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X) * ClampedInput.X
			+ FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y) * ClampedInput.Y).GetSafeNormal();
		const float SafeBoostVelocity = FMath::Clamp(Definition->JumpHorizontalBoostVelocity, 0.0f, 1000.0f);
		const float RedirectedSpeed = HorizontalVelocity.Size()
			+ SafeBoostVelocity * ClampedInput.Size();
		HorizontalVelocity = BoostDirection * RedirectedSpeed;
	}

	const float SafeMaximumJumpHorizontalSpeed = FMath::Clamp(
		Definition->MaximumJumpHorizontalSpeed,
		FMath::Max(1.0f, Definition->MaximumHorizontalSpeed),
		2500.0f);
	HorizontalVelocity = HorizontalVelocity.GetClampedToMaxSize(SafeMaximumJumpHorizontalSpeed);

	LastJumpTime = Now;
	++JumpsUsed;
	PhysicsMesh->SetPhysicsLinearVelocity(
		FVector(HorizontalVelocity.X, HorizontalVelocity.Y, SafeJumpVelocity),
		false);
	PhysicsMesh->WakeAllRigidBodies();
	ForceNetUpdate();
	UE_LOG(LogPHPhysicsProp, Log,
		TEXT("Authoritative jump %d/%d accepted for %s: vertical=%.1f horizontal=%.1f grounded=%s."),
		JumpsUsed,
		SafeMaximumJumpCount,
		*GetName(),
		SafeJumpVelocity,
		HorizontalVelocity.Size(),
		bGrounded ? TEXT("yes") : TEXT("no"));
}

bool APHPhysicsPropPrototype::IsGrounded() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || PhysicsMesh == nullptr || Definition == nullptr)
	{
		return false;
	}

	const FBoxSphereBounds Bounds = PhysicsMesh->Bounds;
	const float ProbeDistance = FMath::Clamp(Definition->GroundProbeDistance, 1.0f, 100.0f);
	const FVector Start = Bounds.Origin;
	const FVector End = Start - FVector::UpVector * (Bounds.BoxExtent.Z + ProbeDistance);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHPhysicsPropJumpGround), false, this);
	FHitResult GroundHit;
	return World->SweepSingleByChannel(
		GroundHit,
		Start,
		End,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(8.0f),
		QueryParams);
}

void APHPhysicsPropPrototype::ApplyStraightenState(const float DeltaSeconds)
{
	const bool bStraightenActive = bServerStraightenHeld && Definition != nullptr;
	if (bStraightenActive != bStraightenWasActive)
	{
		PhysicsMesh->SetAngularDamping(bStraightenActive
			? FMath::Max(0.0f, Definition->StraightenAngularDamping)
			: FMath::Max(0.0f, Definition->FreeAngularDamping));
		PhysicsMesh->SetPhysMaterialOverride(bStraightenActive
			? Definition->StraightenPhysicalMaterial
			: Definition->FreePhysicalMaterial);
		if (!bStraightenActive)
		{
			CurrentStraightenInterpSpeed = 0.0f;
		}
		bStraightenWasActive = bStraightenActive;
	}

	if (!bStraightenActive)
	{
		return;
	}

	const FVector2D RawInput(ServerForwardInput, ServerRightInput);
	if (RawInput.SizeSquared() > FMath::Square(0.05f))
	{
		const FVector2D ClampedInput = RawInput.GetClampedToMaxSize(1.0f);
		const FRotator YawRotation(0.0f, ServerViewYaw, 0.0f);
		const FVector DesiredMovement =
			FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X) * ClampedInput.X
			+ FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y) * ClampedInput.Y;
		const FVector TorqueDirection = FVector::CrossProduct(FVector::UpVector, DesiredMovement);
		const FVector LinearVelocity = PhysicsMesh->GetPhysicsLinearVelocity();
		const float HorizontalSpeed = FVector(LinearVelocity.X, LinearVelocity.Y, 0.0f).Size();
		const float TargetSpeed = FMath::Max(1.0f, Definition->MaximumHorizontalSpeed * ClampedInput.Size());
		const float SpeedRatio = FMath::Clamp(HorizontalSpeed / TargetSpeed, 0.0f, 1.0f);
		const float SafeTorque = FMath::Clamp(Definition->MovementTorqueDegrees, 0.0f, 5000000.0f);
		PhysicsMesh->AddTorqueInDegrees(TorqueDirection * SafeTorque * SpeedRatio, NAME_None, false);
	}

	CurrentStraightenInterpSpeed = FMath::FInterpTo(
		CurrentStraightenInterpSpeed,
		FMath::Max(0.0f, Definition->StraightenTargetInterpSpeed),
		DeltaSeconds,
		FMath::Max(0.0f, Definition->StraightenInterpSpeedAcceleration));

	const FRotator CurrentRotation = PhysicsMesh->GetComponentRotation();
	const FRotator TargetRotation(0.0f, ServerViewYaw, 0.0f);
	const FRotator CandidateRotation = FMath::RInterpTo(
		CurrentRotation,
		TargetRotation,
		DeltaSeconds,
		CurrentStraightenInterpSpeed);

	FHitResult RotationHit;
	PhysicsMesh->MoveComponent(
		FVector::ZeroVector,
		CandidateRotation.Quaternion(),
		true,
		&RotationHit,
		MOVECOMP_NoFlags,
		ETeleportType::TeleportPhysics);

	if (RotationHit.bBlockingHit)
	{
		const float BlockedSpeed = CurrentStraightenInterpSpeed
			* FMath::Clamp(Definition->BlockedRotationMultiplier, 0.0f, 1.0f);
		const FRotator BlockedRotation = FMath::RInterpTo(
			PhysicsMesh->GetComponentRotation(),
			TargetRotation,
			DeltaSeconds,
			BlockedSpeed);
		PhysicsMesh->MoveComponent(
			FVector::ZeroVector,
			BlockedRotation.Quaternion(),
			true,
			nullptr,
			MOVECOMP_NoFlags,
			ETeleportType::TeleportPhysics);
	}
}

float APHPhysicsPropPrototype::GetCurrentImpactThreshold() const
{
	if (PhysicsMesh == nullptr || Definition == nullptr)
	{
		return 0.0f;
	}
	return Definition->ImpactImpulsePerMassThreshold * PhysicsMesh->GetMass();
}

void APHPhysicsPropPrototype::OnPhysicsHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	const FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (!HasAuthority() || HitComponent != PhysicsMesh || Definition == nullptr || OtherComponent == nullptr)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const float Threshold = GetCurrentImpactThreshold();
	if (Threshold <= 0.0f || NormalImpulse.SizeSquared() <= FMath::Square(Threshold))
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const double SafeCooldown = FMath::Clamp(static_cast<double>(Definition->ImpactSoundCooldownSeconds), 0.05, 2.0);
	if (Now - LastImpactSoundTime < SafeCooldown)
	{
		return;
	}

	LastImpactSoundTime = Now;
	ImpactSequence = ImpactSequence == MAX_int32 ? 1 : ImpactSequence + 1;
	const FVector ImpactLocation = Hit.ImpactPoint.IsNearlyZero()
		? PhysicsMesh->GetComponentLocation()
		: FVector(Hit.ImpactPoint);
	MulticastPlayImpactSound(ImpactLocation);
	ForceNetUpdate();

	UE_LOG(LogPHPhysicsProp, Log, TEXT("Authoritative impact %d for %s: impulse %.2f, mass %.3f kg, threshold %.2f, other %s."),
		ImpactSequence, *GetName(), NormalImpulse.Size(), PhysicsMesh->GetMass(), Threshold,
		OtherActor != nullptr ? *OtherActor->GetName() : TEXT("none"));
}

void APHPhysicsPropPrototype::OnRep_Definition()
{
	ApplyDefinition(true);
	UE_LOG(LogPHPhysicsProp, Log, TEXT("Physics Prop definition replicated to %s on local role %d."),
		*GetName(), static_cast<int32>(GetLocalRole()));
}

void APHPhysicsPropPrototype::MulticastPlayImpactSound_Implementation(const FVector_NetQuantize ImpactLocation)
{
	if (Definition != nullptr && Definition->ImpactSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Definition->ImpactSound, ImpactLocation);
		UE_LOG(LogPHPhysicsProp, Log, TEXT("Impact sound multicast received for %s on local role %d at %s."),
			*GetName(), static_cast<int32>(GetLocalRole()), *FVector(ImpactLocation).ToCompactString());
	}
}

void APHPhysicsPropPrototype::ResetTestDrop()
{
	if (!HasAuthority() || PhysicsMesh == nullptr || Definition == nullptr)
	{
		return;
	}

	PhysicsMesh->SetSimulatePhysics(false);
	SetActorTransform(InitialTestTransform, false, nullptr, ETeleportType::TeleportPhysics);
	PhysicsMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
	PhysicsMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	PhysicsMesh->SetSimulatePhysics(true);
	PhysicsMesh->SetMassOverrideInKg(NAME_None, Definition->MassOverrideKilograms, true);
	PhysicsMesh->WakeAllRigidBodies();
	LastImpactSoundTime = -DBL_MAX;
	LastJumpTime = -DBL_MAX;
	JumpsUsed = 0;
	bWasGrounded = false;
	ForceNetUpdate();
	UE_LOG(LogPHPhysicsProp, Log, TEXT("Server reset Physics Prop test drop for %s."), *GetName());
}

#if !UE_BUILD_SHIPPING
void APHPhysicsPropPrototype::BeginJumpSmoke()
{
	if (!HasAuthority() || PhysicsMesh == nullptr || Definition == nullptr)
	{
		return;
	}

	SetServerControlIntent(1.0f, 0.0f, GetActorRotation().Yaw, false);
	JumpSmokePreJumpHorizontalSpeed = FVector(
		PhysicsMesh->GetPhysicsLinearVelocity().X,
		PhysicsMesh->GetPhysicsLinearVelocity().Y,
		0.0f).Size();
	TryJump();
	JumpSmokeFirstHorizontalSpeed = FVector(
		PhysicsMesh->GetPhysicsLinearVelocity().X,
		PhysicsMesh->GetPhysicsLinearVelocity().Y,
		0.0f).Size();
	GetWorldTimerManager().SetTimer(
		JumpSmokeTimerHandle,
		this,
		&APHPhysicsPropPrototype::RunJumpSmokeSecondJump,
		0.2f,
		false);
}

void APHPhysicsPropPrototype::RunJumpSmokeSecondJump()
{
	SetServerControlIntent(0.0f, 1.0f, GetActorRotation().Yaw, false);
	JumpSmokeBeforeSecondHorizontalSpeed = FVector(
		PhysicsMesh->GetPhysicsLinearVelocity().X,
		PhysicsMesh->GetPhysicsLinearVelocity().Y,
		0.0f).Size();
	TryJump();
	const FVector SecondHorizontalVelocity(
		PhysicsMesh->GetPhysicsLinearVelocity().X,
		PhysicsMesh->GetPhysicsLinearVelocity().Y,
		0.0f);
	JumpSmokeSecondHorizontalSpeed = SecondHorizontalVelocity.Size();
	JumpSmokeSecondRightDot = FVector::DotProduct(
		SecondHorizontalVelocity.GetSafeNormal(),
		FRotationMatrix(FRotator(0.0f, GetActorRotation().Yaw, 0.0f)).GetUnitAxis(EAxis::Y));
	GetWorldTimerManager().SetTimer(
		JumpSmokeTimerHandle,
		this,
		&APHPhysicsPropPrototype::RunJumpSmokeThirdAttempt,
		0.2f,
		false);
}

void APHPhysicsPropPrototype::RunJumpSmokeThirdAttempt()
{
	TryJump();
	JumpSmokeCountAfterThirdAttempt = JumpsUsed;
	GetWorldTimerManager().SetTimer(
		JumpSmokeTimerHandle,
		this,
		&APHPhysicsPropPrototype::ReportJumpSmoke,
		0.05f,
		false);
}

void APHPhysicsPropPrototype::ReportJumpSmoke()
{
	const bool bSucceeded = Definition != nullptr
		&& JumpSmokeFirstHorizontalSpeed > JumpSmokePreJumpHorizontalSpeed + 100.0f
		&& JumpSmokeSecondHorizontalSpeed > JumpSmokeBeforeSecondHorizontalSpeed + 100.0f
		&& JumpSmokeSecondRightDot > 0.95f
		&& JumpSmokeCountAfterThirdAttempt == Definition->MaximumJumpCount;
	if (bSucceeded)
	{
		UE_LOG(LogPHPhysicsProp, Log,
			TEXT("Physics jump smoke result: before-first=%.1f first=%.1f before-second=%.1f second=%.1f right-dot=%.3f jumps=%d/%d result=success."),
			JumpSmokePreJumpHorizontalSpeed,
			JumpSmokeFirstHorizontalSpeed,
			JumpSmokeBeforeSecondHorizontalSpeed,
			JumpSmokeSecondHorizontalSpeed,
			JumpSmokeSecondRightDot,
			JumpSmokeCountAfterThirdAttempt,
			Definition->MaximumJumpCount);
	}
	else
	{
		UE_LOG(LogPHPhysicsProp, Error,
			TEXT("Physics jump smoke result: before-first=%.1f first=%.1f before-second=%.1f second=%.1f right-dot=%.3f jumps=%d/%d result=failure."),
			JumpSmokePreJumpHorizontalSpeed,
			JumpSmokeFirstHorizontalSpeed,
			JumpSmokeBeforeSecondHorizontalSpeed,
			JumpSmokeSecondHorizontalSpeed,
			JumpSmokeSecondRightDot,
			JumpSmokeCountAfterThirdAttempt,
			Definition != nullptr ? Definition->MaximumJumpCount : -1);
	}
}
#endif

void APHPhysicsPropPrototype::ApplyDefinition(const bool bAllowSimulation)
{
	if (PhysicsMesh == nullptr)
	{
		return;
	}

	FText Error;
	if (Definition == nullptr || !Definition->HasValidDefinition(&Error))
	{
		PhysicsMesh->SetSimulatePhysics(false);
		PhysicsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PhysicsMesh->SetStaticMesh(nullptr);
		return;
	}

	PhysicsMesh->SetStaticMesh(Definition->StaticMesh);
	PhysicsMesh->SetPhysMaterialOverride(Definition->FreePhysicalMaterial);
	PhysicsMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PhysicsMesh->SetCollisionObjectType(ECC_PhysicsBody);
	PhysicsMesh->SetCollisionResponseToAllChannels(ECR_Block);
	PhysicsMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	PhysicsMesh->SetCollisionResponseToChannel(PHCollision::PropHitbox, ECR_Ignore);
	PhysicsMesh->SetLinearDamping(FMath::Max(0.0f, Definition->FreeLinearDamping));
	PhysicsMesh->SetAngularDamping(FMath::Max(0.0f, Definition->FreeAngularDamping));
	PhysicsMesh->BodyInstance.bUseCCD = Definition->bUseContinuousCollisionDetection;
	PhysicsMesh->SetPhysicsMaxAngularVelocityInDegrees(
		FMath::Clamp(Definition->MaximumAngularVelocityDegrees, 1.0f, 2000.0f),
		false);
	SetNetUpdateFrequency(FMath::Clamp(Definition->NetworkUpdateFrequency, 1.0f, 120.0f));
	SetMinNetUpdateFrequency(FMath::Min(10.0f, GetNetUpdateFrequency()));

	if (bAllowSimulation)
	{
		PhysicsMesh->SetSimulatePhysics(true);
		PhysicsMesh->SetMassOverrideInKg(NAME_None, Definition->MassOverrideKilograms, true);
		PhysicsMesh->WakeAllRigidBodies();
	}
}
