#include "Gameplay/Capture/PHRetentionPoint.h"

#include "Characters/PHHunterCharacter.h"
#include "Characters/PHCharacterBase.h"
#include "Characters/PHPropCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Game/PHGameState.h"
#include "Game/PHPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHRetention, Log, All);

APHRetentionPoint::APHRetentionPoint()
	: InteractionDistance(225.0f)
	, ReleaseDuration(2.0f)
	, RetainedProp(nullptr)
	, ReleaseRescuer(nullptr)
	, ReleaseProgress(0.0f)
{
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(15.0f);
	SetMinNetUpdateFrequency(5.0f);
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	GrayboxBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GrayboxBody"));
	GrayboxBody->SetupAttachment(SceneRoot);
	GrayboxBody->SetRelativeLocation(FVector(0.0f, 0.0f, 30.0f));
	GrayboxBody->SetRelativeScale3D(FVector(0.85f, 0.85f, 0.6f));
	GrayboxBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GrayboxBody->SetCollisionObjectType(ECC_WorldDynamic);
	GrayboxBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	GrayboxBody->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GrayboxBody->SetGenerateOverlapEvents(false);
	GrayboxBody->SetCanEverAffectNavigation(false);
	GrayboxBody->CanCharacterStepUpOn = ECB_No;

	RetentionPole = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RetentionPole"));
	RetentionPole->SetupAttachment(SceneRoot);
	RetentionPole->SetRelativeLocation(FVector(0.0f, 0.0f, 145.0f));
	RetentionPole->SetRelativeScale3D(FVector(0.32f, 0.32f, 1.75f));
	RetentionPole->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RetentionPole->SetCanEverAffectNavigation(false);

	FrontLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("FrontLabel"));
	FrontLabel->SetupAttachment(SceneRoot);
	FrontLabel->SetRelativeLocation(FVector(-45.0f, 0.0f, 250.0f));
	FrontLabel->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
	FrontLabel->SetHorizontalAlignment(EHTA_Center);
	FrontLabel->SetWorldSize(32.0f);
	FrontLabel->SetText(FText::FromString(TEXT("PIQUET\nE : ACCROCHER | CLIC : LIBERER")));

	BackLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("BackLabel"));
	BackLabel->SetupAttachment(SceneRoot);
	BackLabel->SetRelativeLocation(FVector(45.0f, 0.0f, 250.0f));
	BackLabel->SetHorizontalAlignment(EHTA_Center);
	BackLabel->SetWorldSize(32.0f);
	BackLabel->SetText(FText::FromString(TEXT("PIQUET\nE : ACCROCHER | CLIC : LIBERER")));

	RetentionAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("RetentionAnchor"));
	RetentionAnchor->SetupAttachment(SceneRoot);
	RetentionAnchor->SetRelativeLocation(FVector(0.0f, 0.0f, 130.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		GrayboxBody->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		RetentionPole->SetStaticMesh(CylinderMesh.Object);
	}
}

void APHRetentionPoint::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || ReleaseRescuer == nullptr)
	{
		return;
	}

	if (!CanRescuerRelease(*ReleaseRescuer))
	{
		ResetReleaseState();
		return;
	}

	const float PreviousProgress = ReleaseProgress;
	ReleaseProgress = FMath::Clamp(
		ReleaseProgress + FMath::Max(0.0f, DeltaSeconds) / FMath::Clamp(ReleaseDuration, 0.5f, 10.0f),
		0.0f,
		1.0f);
	if (!FMath::IsNearlyEqual(PreviousProgress, ReleaseProgress))
	{
		OnRep_ReleaseState();
	}

	if (ReleaseProgress >= 1.0f - KINDA_SMALL_NUMBER)
	{
		CompleteRelease();
	}
}

void APHRetentionPoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		ResetReleaseState();
	}
	if (HasAuthority() && RetainedProp != nullptr)
	{
		RetainedProp->ServerReleaseWithGrace();
	}
	Super::EndPlay(EndPlayReason);
}

void APHRetentionPoint::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APHRetentionPoint, RetainedProp);
	DOREPLIFETIME(APHRetentionPoint, ReleaseRescuer);
	DOREPLIFETIME(APHRetentionPoint, ReleaseProgress);
}

FVector APHRetentionPoint::GetInteractionPoint() const
{
	return RetentionAnchor != nullptr ? RetentionAnchor->GetComponentLocation() : GetActorLocation();
}

bool APHRetentionPoint::CanHunterRetain(const APHHunterCharacter& Hunter, const APHPropCharacter& Prop) const
{
	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	const EPHMatchPhase Phase = GameState != nullptr ? GameState->GetMatchPhase() : EPHMatchPhase::Lobby;
	return HasAuthority()
		&& RetainedProp == nullptr
		&& Hunter.GetCarriedProp() == &Prop
		&& (Phase == EPHMatchPhase::Hunt || Phase == EPHMatchPhase::Escape)
		&& FVector::DistSquared(Hunter.GetActorLocation(), GetActorLocation())
			<= FMath::Square(FMath::Clamp(InteractionDistance, 100.0f, 500.0f))
		&& HasLineOfSightFrom(Hunter);
}

bool APHRetentionPoint::ServerTryRetain(APHHunterCharacter& Hunter, APHPropCharacter& Prop)
{
	if (!CanHunterRetain(Hunter, Prop))
	{
		return false;
	}

	ResetReleaseState();
	RetainedProp = &Prop;
	OnRep_RetainedProp();
	ForceNetUpdate();
	Prop.ServerRetainAt(*this);
	if (APHPlayerState* HunterState = Hunter.GetPlayerState<APHPlayerState>())
	{
		HunterState->RecordHunterRetention();
	}
	UE_LOG(LogPHRetention, Log, TEXT("%s retained %s at %s."),
		*Hunter.GetName(), *Prop.GetName(), *GetName());
	return true;
}

bool APHRetentionPoint::ServerTryBeginRelease(APHPropCharacter& Rescuer)
{
	if (!CanRescuerRelease(Rescuer) || (ReleaseRescuer != nullptr && ReleaseRescuer != &Rescuer))
	{
		return false;
	}

	if (ReleaseRescuer == &Rescuer)
	{
		return true;
	}

	ReleaseRescuer = &Rescuer;
	ReleaseProgress = 0.0f;
	Rescuer.SetActiveRetentionRescueFromServer(this);
	OnRep_ReleaseState();
	ForceNetUpdate();
	UE_LOG(LogPHRetention, Log, TEXT("%s began releasing %s from %s."),
		*Rescuer.GetName(), *RetainedProp->GetName(), *GetName());
	return true;
}

void APHRetentionPoint::ServerEndRelease(APHPropCharacter& Rescuer)
{
	if (!HasAuthority() || ReleaseRescuer != &Rescuer)
	{
		return;
	}

	ResetReleaseState();
}

void APHRetentionPoint::ClearRetainedProp(const APHPropCharacter* ExpectedProp)
{
	if (!HasAuthority() || RetainedProp == nullptr || RetainedProp != ExpectedProp)
	{
		return;
	}

	ResetReleaseState();
	RetainedProp = nullptr;
	OnRep_RetainedProp();
	ForceNetUpdate();
}

void APHRetentionPoint::OnRep_RetainedProp()
{
	BP_OnRetentionStateChanged();
}

void APHRetentionPoint::OnRep_ReleaseState()
{
	BP_OnRetentionStateChanged();
}

bool APHRetentionPoint::CanRescuerRelease(const APHPropCharacter& Rescuer) const
{
	const APHPlayerState* RescuerState = Rescuer.GetPlayerState<APHPlayerState>();
	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	const EPHMatchPhase Phase = GameState != nullptr ? GameState->GetMatchPhase() : EPHMatchPhase::Lobby;
	return HasAuthority()
		&& RetainedProp != nullptr
		&& RetainedProp != &Rescuer
		&& RescuerState != nullptr
		&& RescuerState->GetPlayerRole() == EPHPlayerRole::Prop
		&& Rescuer.GetActivePropForm() == nullptr
		&& (Phase == EPHMatchPhase::Hunt || Phase == EPHMatchPhase::Escape)
		&& Rescuer.CanPerformCaptureRescue()
		&& FVector::DistSquared(Rescuer.GetActorLocation(), GetActorLocation())
			<= FMath::Square(FMath::Clamp(InteractionDistance, 100.0f, 500.0f))
		&& HasLineOfSightFrom(Rescuer);
}

void APHRetentionPoint::CompleteRelease()
{
	if (!HasAuthority() || ReleaseRescuer == nullptr || RetainedProp == nullptr
		|| !CanRescuerRelease(*ReleaseRescuer))
	{
		ResetReleaseState();
		return;
	}

	APHPropCharacter* Rescuer = ReleaseRescuer;
	APHPropCharacter* ReleasedProp = RetainedProp;
	ResetReleaseState();
	RetainedProp = nullptr;
	OnRep_RetainedProp();
	ForceNetUpdate();
	ReleasedProp->ServerReleaseWithGrace();
	if (APHPlayerState* RescuerState = Rescuer->GetPlayerState<APHPlayerState>())
	{
		RescuerState->RecordAllyRescue();
	}
	UE_LOG(LogPHRetention, Log, TEXT("%s released %s from %s after %.1fs."),
		*Rescuer->GetName(), *ReleasedProp->GetName(), *GetName(), ReleaseDuration);
}

void APHRetentionPoint::ResetReleaseState()
{
	if (!HasAuthority())
	{
		return;
	}

	APHPropCharacter* PreviousRescuer = ReleaseRescuer;
	ReleaseRescuer = nullptr;
	ReleaseProgress = 0.0f;
	if (PreviousRescuer != nullptr)
	{
		PreviousRescuer->ClearActiveRetentionRescueFromServer(this);
	}
	OnRep_ReleaseState();
	ForceNetUpdate();
}

bool APHRetentionPoint::HasLineOfSightFrom(const AActor& Interactor) const
{
	if (GetWorld() == nullptr)
	{
		return false;
	}

	const APHCharacterBase* Character = Cast<APHCharacterBase>(&Interactor);
	const float HalfHeight = Character != nullptr && Character->GetCapsuleComponent() != nullptr
		? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 50.0f;
	const FVector TraceStart = Interactor.GetActorLocation() + FVector::UpVector * HalfHeight;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHRetentionLineOfSight), false, &Interactor);
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, TraceStart, GetInteractionPoint(), ECC_Visibility, QueryParams);
	return !bHit || Hit.GetActor() == this;
}
