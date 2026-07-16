#include "Characters/PHCharacterBase.h"

#include "Components/InputComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Gameplay/PHCollisionChannels.h"

APHCharacterBase::APHCharacterBase()
	: MaximumWalkSpeed(500.0f)
	, MaximumAcceleration(1600.0f)
	, BrakingDeceleration(1400.0f)
	, JumpVelocity(420.0f)
	, AirControlRatio(0.25f)
	, YawInputScale(1.0f)
	, PitchInputScale(1.0f)
{
	bReplicates = true;
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick = false;
	SetNetUpdateFrequency(100.0f);
	SetMinNetUpdateFrequency(30.0f);

	// ACharacter still requires its root capsule for CharacterMovement. Player capsules deliberately
	// ignore one another and the query-only Prop hitboxes so they cannot push or become invisible steps.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(PHCollision::PropHitbox, ECR_Ignore);
	GetCapsuleComponent()->CanCharacterStepUpOn = ECB_No;
}

void APHCharacterBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = MaximumWalkSpeed;
		Movement->MaxAcceleration = MaximumAcceleration;
		Movement->BrakingDecelerationWalking = BrakingDeceleration;
		Movement->JumpZVelocity = JumpVelocity;
		Movement->AirControl = AirControlRatio;
	}
}

void APHCharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);

	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &APHCharacterBase::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &APHCharacterBase::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("LookYaw"), this, &APHCharacterBase::LookYaw);
	PlayerInputComponent->BindAxis(TEXT("LookPitch"), this, &APHCharacterBase::LookPitch);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &ACharacter::StopJumping);
}

void APHCharacterBase::PawnClientRestart()
{
	Super::PawnClientRestart();

	if (IsLocallyControlled())
	{
		BP_OnLocalViewReady();
	}
}

void APHCharacterBase::MoveForward(const float Value)
{
	if (!Controller || FMath::IsNearlyZero(Value))
	{
		return;
	}

	const FRotator ControlRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	AddMovementInput(FRotationMatrix(ControlRotation).GetUnitAxis(EAxis::X), Value);
}

void APHCharacterBase::MoveRight(const float Value)
{
	if (!Controller || FMath::IsNearlyZero(Value))
	{
		return;
	}

	const FRotator ControlRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	AddMovementInput(FRotationMatrix(ControlRotation).GetUnitAxis(EAxis::Y), Value);
}

void APHCharacterBase::LookYaw(const float Value)
{
	AddControllerYawInput(Value * YawInputScale);
}

void APHCharacterBase::LookPitch(const float Value)
{
	AddControllerPitchInput(Value * PitchInputScale);
}
