#include "Characters/PHCharacterBase.h"

#include "Components/AudioComponent.h"
#include "Components/InputComponent.h"
#include "Components/CapsuleComponent.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Gameplay/PHCollisionChannels.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UI/PHSoundRadialWidget.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHSoundEmote, Log, All);

APHCharacterBase::APHCharacterBase()
	: MaximumWalkSpeed(500.0f)
	, MaximumAcceleration(1600.0f)
	, BrakingDeceleration(1400.0f)
	, JumpVelocity(420.0f)
	, AirControlRatio(0.25f)
	, YawInputScale(1.0f)
	, PitchInputScale(1.0f)
	, SoundEmoteCooldownSeconds(3.0f)
	, SoundRadialWidget(nullptr)
	, LastServerSoundEmoteTime(-DBL_MAX)
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

	SoundEmoteLabels = {
		FText::FromString(TEXT("COUCOU !")),
		FText::FromString(TEXT("PAR ICI !")),
		FText::FromString(TEXT("AU SECOURS !")),
		FText::FromString(TEXT("POUET !"))
	};
	static ConstructorHelpers::FObjectFinder<USoundBase> ChickenSound(
		TEXT("/Game/PropHunt/Audio/Props/ScreamingChicken/S_PH_ScreamingChicken_Impact"));
	if (ChickenSound.Succeeded())
	{
		SoundEmoteSounds.Init(ChickenSound.Object, SoundEmoteLabels.Num());
	}
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
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &APHCharacterBase::RequestJump);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &APHCharacterBase::RequestStopJump);
	PlayerInputComponent->BindAction(TEXT("SoundRadial"), IE_Pressed, this, &APHCharacterBase::OpenSoundRadial);
	PlayerInputComponent->BindAction(TEXT("SoundRadial"), IE_Released, this, &APHCharacterBase::CloseSoundRadial);
}

void APHCharacterBase::PawnClientRestart()
{
	Super::PawnClientRestart();

	if (IsLocallyControlled())
	{
		BP_OnLocalViewReady();
	}
}

void APHCharacterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupSoundRadial();
	Super::EndPlay(EndPlayReason);
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

void APHCharacterBase::RequestJump()
{
	Jump();
}

void APHCharacterBase::RequestStopJump()
{
	StopJumping();
}

bool APHCharacterBase::CanUseSoundEmote() const
{
	return true;
}

#if !UE_BUILD_SHIPPING
void APHCharacterBase::RequestSoundEmoteForSmoke(const int32 EmoteIndex)
{
	if (IsLocallyControlled())
	{
		ServerRequestSoundEmote(EmoteIndex);
	}
}
#endif

void APHCharacterBase::OpenSoundRadial()
{
	if (!IsLocallyControlled() || SoundRadialWidget != nullptr || !CanUseSoundEmote())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (PlayerController == nullptr)
	{
		return;
	}

	SoundRadialWidget = CreateWidget<UPHSoundRadialWidget>(
		PlayerController, UPHSoundRadialWidget::StaticClass());
	if (SoundRadialWidget == nullptr)
	{
		return;
	}

	SoundRadialWidget->Configure(SoundEmoteLabels);
	SoundRadialWidget->AddToViewport(80);
	PlayerController->bShowMouseCursor = true;
	PlayerController->SetIgnoreLookInput(true);
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
	PlayerController->SetMouseLocation(ViewportWidth / 2, ViewportHeight / 2);
}

void APHCharacterBase::CloseSoundRadial()
{
	if (!IsLocallyControlled() || SoundRadialWidget == nullptr)
	{
		return;
	}

	const int32 SelectedEmote = SoundRadialWidget->GetSelectedEmoteIndex();
	CleanupSoundRadial();
	if (SelectedEmote >= 0)
	{
		ServerRequestSoundEmote(SelectedEmote);
	}
}

void APHCharacterBase::CleanupSoundRadial()
{
	if (SoundRadialWidget != nullptr)
	{
		SoundRadialWidget->RemoveFromParent();
		SoundRadialWidget = nullptr;
	}
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		PlayerController->SetIgnoreLookInput(false);
		PlayerController->bShowMouseCursor = false;
	}
}

void APHCharacterBase::ServerRequestSoundEmote_Implementation(const int32 EmoteIndex)
{
	const int32 EmoteCount = GetSoundEmoteCount();
	if (!CanUseSoundEmote() || GetWorld() == nullptr || EmoteIndex < 0 || EmoteIndex >= EmoteCount
		|| SoundEmoteSounds[EmoteIndex] == nullptr)
	{
		UE_LOG(LogPHSoundEmote, Warning,
			TEXT("Rejected sound emote %d for %s because the request is invalid in the current state."),
			EmoteIndex, *GetName());
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	const double SafeCooldown = FMath::Clamp(static_cast<double>(SoundEmoteCooldownSeconds), 0.5, 30.0);
	if (Now - LastServerSoundEmoteTime < SafeCooldown)
	{
		UE_LOG(LogPHSoundEmote, Log,
			TEXT("Rejected sound emote %d for %s during the %.2fs server cooldown."),
			EmoteIndex, *GetName(), SafeCooldown);
		return;
	}

	LastServerSoundEmoteTime = Now;
#if !UE_BUILD_SHIPPING
	++SoundEmoteSmokeAcceptedCount;
#endif
	UE_LOG(LogPHSoundEmote, Log, TEXT("Accepted sound emote %d for %s on the server."),
		EmoteIndex, *GetName());
	MulticastPlaySoundEmote(EmoteIndex);
}

void APHCharacterBase::MulticastPlaySoundEmote_Implementation(const int32 EmoteIndex)
{
	if (GetWorld() == nullptr || !SoundEmoteSounds.IsValidIndex(EmoteIndex)
		|| !SoundEmoteLabels.IsValidIndex(EmoteIndex) || SoundEmoteSounds[EmoteIndex] == nullptr)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	++SoundEmoteSmokePlaybackCount;
#endif
	UE_LOG(LogPHSoundEmote, Log,
		TEXT("Played spatial sound emote %d for %s on net mode %d."),
		EmoteIndex, *GetName(), static_cast<int32>(GetNetMode()));

	UAudioComponent* AudioComponent = NewObject<UAudioComponent>(this);
	if (AudioComponent != nullptr)
	{
		AudioComponent->bAutoActivate = false;
		AudioComponent->bAutoDestroy = true;
		AudioComponent->bAllowSpatialization = true;
		AudioComponent->SetSound(SoundEmoteSounds[EmoteIndex]);
		static constexpr float PitchMultipliers[] = {0.78f, 0.95f, 1.15f, 1.38f};
		AudioComponent->SetPitchMultiplier(PitchMultipliers[EmoteIndex % UE_ARRAY_COUNT(PitchMultipliers)]);
		AudioComponent->SetupAttachment(GetRootComponent());
		AudioComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));

		FSoundAttenuationSettings Attenuation;
		Attenuation.bAttenuate = true;
		Attenuation.bSpatialize = true;
		Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		Attenuation.AttenuationShapeExtents = FVector(150.0f, 0.0f, 0.0f);
		Attenuation.FalloffDistance = 1800.0f;
		Attenuation.bEnableOcclusion = true;
		Attenuation.OcclusionTraceChannel = ECC_Visibility;
		Attenuation.OcclusionLowPassFilterFrequency = 1200.0f;
		Attenuation.OcclusionVolumeAttenuation = 0.35f;
		AudioComponent->AdjustAttenuation(Attenuation);
		AudioComponent->RegisterComponent();
		AudioComponent->Play();
	}

	BP_OnSoundEmotePlayed(EmoteIndex, SoundEmoteLabels[EmoteIndex]);
}
