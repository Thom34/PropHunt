#include "Gameplay/Objectives/PHObjectiveActor.h"

#include "Characters/PHHunterCharacter.h"
#include "Characters/PHPropCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Game/PHGameMode.h"
#include "Game/PHGameState.h"
#include "Game/PHPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHObjective, Log, All);

APHObjectiveActor::APHObjectiveActor()
	: ObjectiveDisplayName(NSLOCTEXT("PropHuntObjective", "DefaultDisplayName", "OBJECTIF"))
	, InteractionDurationSeconds(24.0f)
	, InteractionDistance(225.0f)
	, MaximumConcurrentInteractors(4)
	, AdditionalInteractorContribution(0.5f)
	, InterruptionPolicy(EPHObjectiveInterruptionPolicy::Preserve)
	, ProgressRegressionPerSecond(0.1f)
	, HunterMeleeRegressionFraction(0.10f)
	, ObjectiveProgress(0.0f)
	, bObjectiveActive(true)
	, bCompleted(false)
	, ActiveInteractorCount(0)
{
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(15.0f);
	SetMinNetUpdateFrequency(5.0f);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ObjectiveBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObjectiveBody"));
	ObjectiveBody->SetupAttachment(SceneRoot);
	ObjectiveBody->SetRelativeLocation(FVector(0.0f, 0.0f, 55.0f));
	ObjectiveBody->SetRelativeScale3D(FVector(0.8f, 0.8f, 1.1f));
	ObjectiveBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ObjectiveBody->SetCollisionObjectType(ECC_WorldDynamic);
	ObjectiveBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	ObjectiveBody->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ObjectiveBody->SetGenerateOverlapEvents(false);
	ObjectiveBody->SetCanEverAffectNavigation(false);
	ObjectiveBody->CanCharacterStepUpOn = ECB_No;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		ObjectiveBody->SetStaticMesh(CubeMesh.Object);
	}
}

FText APHObjectiveActor::GetObjectiveDisplayName() const
{
	return ObjectiveDisplayName.IsEmpty()
		? NSLOCTEXT("PropHuntObjective", "DefaultDisplayName", "OBJECTIF")
		: ObjectiveDisplayName;
}

void APHObjectiveActor::BeginPlay()
{
	Super::BeginPlay();
	RefreshPresentation();
}

void APHObjectiveActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		TArray<TWeakObjectPtr<APHPropCharacter>> Interactors = ActiveInteractors.Array();
		for (const TWeakObjectPtr<APHPropCharacter>& Interactor : Interactors)
		{
			if (APHPropCharacter* PropCharacter = Interactor.Get())
			{
				ServerEndInteraction(*PropCharacter);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void APHObjectiveActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || !bObjectiveActive || bCompleted)
	{
		return;
	}

	TArray<TWeakObjectPtr<APHPropCharacter>> Interactors = ActiveInteractors.Array();
	for (const TWeakObjectPtr<APHPropCharacter>& Interactor : Interactors)
	{
		APHPropCharacter* PropCharacter = Interactor.Get();
		if (PropCharacter == nullptr)
		{
			ActiveInteractors.Remove(Interactor);
		}
		else if (!CanPropInteract(*PropCharacter))
		{
			ServerEndInteraction(*PropCharacter);
		}
	}
	RefreshInteractorCount();

	const float PreviousProgress = ObjectiveProgress;
	if (ActiveInteractorCount > 0)
	{
		ObjectiveProgress = PHObjectiveFlow::AdvanceProgress(
			ObjectiveProgress,
			DeltaSeconds,
			FMath::Clamp(InteractionDurationSeconds, 1.0f, 120.0f),
			ActiveInteractorCount,
			AdditionalInteractorContribution);
	}
	else
	{
		ApplyIdleProgressRule(DeltaSeconds);
	}

	if (!FMath::IsNearlyEqual(PreviousProgress, ObjectiveProgress))
	{
		RefreshPresentation();
		BP_OnObjectiveStateChanged();
	}

	if (ObjectiveProgress >= 1.0f - KINDA_SMALL_NUMBER)
	{
		CompleteObjective();
	}
}

void APHObjectiveActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APHObjectiveActor, ObjectiveProgress);
	DOREPLIFETIME(APHObjectiveActor, bObjectiveActive);
	DOREPLIFETIME(APHObjectiveActor, bCompleted);
	DOREPLIFETIME(APHObjectiveActor, ActiveInteractorCount);
}

FVector APHObjectiveActor::GetInteractionPoint() const
{
	return ObjectiveBody != nullptr ? ObjectiveBody->GetComponentLocation() : GetActorLocation();
}

bool APHObjectiveActor::CanPropInteract(const APHPropCharacter& PropCharacter) const
{
	const UWorld* World = GetWorld();
	if (!HasAuthority() || World == nullptr || !bObjectiveActive || bCompleted
		|| PropCharacter.GetController() == nullptr
		|| !PHObjectiveFlow::IsStationaryForInteraction(PropCharacter.GetVelocity()))
	{
		return false;
	}

	const APHPlayerState* PHPlayerState = PropCharacter.GetPlayerState<APHPlayerState>();
	const APHGameState* PHGameState = World->GetGameState<APHGameState>();
	if (PHPlayerState == nullptr || PHPlayerState->GetPlayerRole() != EPHPlayerRole::Prop
		|| PHGameState == nullptr || PHGameState->GetMatchPhase() != EPHMatchPhase::Hunt)
	{
		return false;
	}

	const float SafeInteractionDistance = FMath::Clamp(InteractionDistance, 100.0f, 500.0f);
	if (FVector::DistSquared(PropCharacter.GetActorLocation(), GetActorLocation()) > FMath::Square(SafeInteractionDistance))
	{
		return false;
	}

	const UCapsuleComponent* Capsule = PropCharacter.GetCapsuleComponent();
	const float CapsuleHalfHeight = Capsule != nullptr ? Capsule->GetScaledCapsuleHalfHeight() : 60.0f;
	const FVector TraceOrigin = PropCharacter.GetActorLocation() + PropCharacter.GetActorUpVector() * (CapsuleHalfHeight * 0.35f);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHObjectiveInteraction), true, &PropCharacter);
	QueryParams.AddIgnoredActor(&PropCharacter);

	FHitResult HitResult;
	return World->LineTraceSingleByChannel(HitResult, TraceOrigin, GetInteractionPoint(), ECC_Visibility, QueryParams)
		&& HitResult.GetActor() == this;
}

bool APHObjectiveActor::ServerTryBeginInteraction(APHPropCharacter& PropCharacter)
{
	if (!CanPropInteract(PropCharacter) || ActiveInteractors.Num() >= FMath::Clamp(MaximumConcurrentInteractors, 1, 4))
	{
		return false;
	}

	ActiveInteractors.Add(&PropCharacter);
	PropCharacter.SetActiveObjectiveFromServer(this);
	RefreshInteractorCount();
	ForceNetUpdate();
	UE_LOG(LogPHObjective, Log, TEXT("%s began objective interaction with %s."), *PropCharacter.GetName(), *GetName());
	return true;
}

void APHObjectiveActor::ServerEndInteraction(APHPropCharacter& PropCharacter)
{
	if (!HasAuthority())
	{
		return;
	}

	if (ActiveInteractors.Remove(&PropCharacter) > 0)
	{
		PropCharacter.ClearActiveObjectiveFromServer(this);
		RefreshInteractorCount();
		ForceNetUpdate();
		UE_LOG(LogPHObjective, Verbose, TEXT("%s ended objective interaction with %s."), *PropCharacter.GetName(), *GetName());
	}
}

bool APHObjectiveActor::ServerApplyHunterMeleeRegression(APHHunterCharacter& HunterCharacter)
{
	const UWorld* World = GetWorld();
	const APHPlayerState* HunterPlayerState = HunterCharacter.GetPlayerState<APHPlayerState>();
	const APHGameState* PHGameState = World != nullptr ? World->GetGameState<APHGameState>() : nullptr;
	if (!HasAuthority() || World == nullptr || !HunterCharacter.HasAuthority()
		|| !bObjectiveActive || bCompleted || ObjectiveProgress <= KINDA_SMALL_NUMBER
		|| HunterPlayerState == nullptr || HunterPlayerState->GetPlayerRole() != EPHPlayerRole::Hunter
		|| PHGameState == nullptr || PHGameState->GetMatchPhase() != EPHMatchPhase::Hunt)
	{
		return false;
	}

	const float PreviousProgress = ObjectiveProgress;
	ObjectiveProgress = PHObjectiveFlow::ApplyHunterMeleeRegression(
		ObjectiveProgress,
		HunterMeleeRegressionFraction);
	const float AppliedRegression = PreviousProgress - ObjectiveProgress;
	if (AppliedRegression <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	RefreshPresentation();
	BP_OnObjectiveStateChanged();
	BP_OnObjectiveHitByHunter(AppliedRegression);
	ForceNetUpdate();
	UE_LOG(LogPHObjective, Log,
		TEXT("Hunter %s regressed objective %s by %.1f%% to %.1f%%."),
		*HunterCharacter.GetName(),
		*GetName(),
		AppliedRegression * 100.0f,
		ObjectiveProgress * 100.0f);
	return true;
}

void APHObjectiveActor::ResetForMatch(const bool bShouldBeActive)
{
	if (!HasAuthority())
	{
		return;
	}

	TArray<TWeakObjectPtr<APHPropCharacter>> Interactors = ActiveInteractors.Array();
	for (const TWeakObjectPtr<APHPropCharacter>& Interactor : Interactors)
	{
		if (APHPropCharacter* PropCharacter = Interactor.Get())
		{
			ServerEndInteraction(*PropCharacter);
		}
	}
	ActiveInteractors.Reset();
	ObjectiveProgress = 0.0f;
	bObjectiveActive = bShouldBeActive;
	bCompleted = false;
	ActiveInteractorCount = 0;
	RefreshPresentation();
	BP_OnObjectiveStateChanged();
	ForceNetUpdate();
}

void APHObjectiveActor::OnRep_ObjectiveState()
{
	RefreshPresentation();
	BP_OnObjectiveStateChanged();
	if (bCompleted)
	{
		BP_OnObjectiveCompleted();
	}
}

void APHObjectiveActor::RefreshPresentation()
{
	const bool bVisible = bObjectiveActive;
	if (ObjectiveBody != nullptr)
	{
		ObjectiveBody->SetVisibility(bVisible, true);
		ObjectiveBody->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}

void APHObjectiveActor::RefreshInteractorCount()
{
	const int32 NewCount = ActiveInteractors.Num();
	if (ActiveInteractorCount != NewCount)
	{
		ActiveInteractorCount = NewCount;
		BP_OnObjectiveStateChanged();
		ForceNetUpdate();
	}
}

void APHObjectiveActor::CompleteObjective()
{
	if (!HasAuthority() || bCompleted)
	{
		return;
	}

	ObjectiveProgress = 1.0f;
	bCompleted = true;

	TArray<TWeakObjectPtr<APHPropCharacter>> Interactors = ActiveInteractors.Array();
	for (const TWeakObjectPtr<APHPropCharacter>& Interactor : Interactors)
	{
		if (APHPropCharacter* PropCharacter = Interactor.Get())
		{
			if (APHPlayerState* InteractorState = PropCharacter->GetPlayerState<APHPlayerState>())
			{
				InteractorState->RecordObjectiveCompletion();
			}
			ServerEndInteraction(*PropCharacter);
		}
	}
	ActiveInteractors.Reset();
	ActiveInteractorCount = 0;
	RefreshPresentation();
	BP_OnObjectiveStateChanged();
	BP_OnObjectiveCompleted();
	ForceNetUpdate();

	if (APHGameMode* PHGameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<APHGameMode>() : nullptr)
	{
		PHGameMode->NotifyObjectiveCompleted(*this);
	}
	UE_LOG(LogPHObjective, Log, TEXT("Objective %s completed authoritatively."), *GetName());
}

void APHObjectiveActor::ApplyIdleProgressRule(const float DeltaSeconds)
{
	switch (InterruptionPolicy)
	{
	case EPHObjectiveInterruptionPolicy::Reset:
		ObjectiveProgress = 0.0f;
		break;
	case EPHObjectiveInterruptionPolicy::Regress:
		ObjectiveProgress = FMath::Max(
			0.0f,
			ObjectiveProgress - FMath::Clamp(ProgressRegressionPerSecond, 0.0f, 1.0f) * FMath::Max(0.0f, DeltaSeconds));
		break;
	case EPHObjectiveInterruptionPolicy::Preserve:
	default:
		break;
	}
}
