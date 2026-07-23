#include "Gameplay/Escape/PHExitGate.h"

#include "Characters/PHPropCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/PHGameState.h"
#include "Game/PHPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHExitGate, Log, All);

APHExitGate::APHExitGate()
	: ExitDisplayName(NSLOCTEXT("PropHuntExitGate", "DefaultDisplayName", "SORTIE"))
	, bShowNativeFallbackVisuals(true)
	, OpenDurationSeconds(20.0f)
	, InteractionDistance(225.0f)
	, OpenProgress(0.0f)
	, bGateEnabled(false)
	, bGateOpen(false)
	, ActiveInteractorCount(0)
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.0f);
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PresentationRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PresentationRoot"));
	PresentationRoot->SetupAttachment(SceneRoot);

	InteractionAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("InteractionAnchor"));
	InteractionAnchor->SetupAttachment(SceneRoot);
	InteractionAnchor->SetRelativeLocation(FVector(-55.0f, -230.0f, 120.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* CubeMeshObject = CubeMesh.Succeeded() ? CubeMesh.Object : nullptr;
	auto CreatePart = [this, CubeMeshObject](const FName Name, const FVector& Location, const FVector& Scale)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(PresentationRoot);
		if (CubeMeshObject != nullptr)
		{
			Part->SetStaticMesh(CubeMeshObject);
		}
		Part->SetRelativeLocation(Location);
		Part->SetRelativeScale3D(Scale);
		Part->SetCollisionProfileName(TEXT("BlockAll"));
		Part->SetCanEverAffectNavigation(false);
		return Part;
	};

	FrameLeft = CreatePart(TEXT("FrameLeft"), FVector(0.0f, -190.0f, 160.0f), FVector(0.35f, 0.35f, 3.2f));
	FrameRight = CreatePart(TEXT("FrameRight"), FVector(0.0f, 190.0f, 160.0f), FVector(0.35f, 0.35f, 3.2f));
	FrameTop = CreatePart(TEXT("FrameTop"), FVector(0.0f, 0.0f, 330.0f), FVector(0.35f, 4.15f, 0.35f));
	DoorLeft = CreatePart(TEXT("DoorLeft"), FVector(0.0f, -85.0f, 150.0f), FVector(0.20f, 1.70f, 3.0f));
	DoorRight = CreatePart(TEXT("DoorRight"), FVector(0.0f, 85.0f, 150.0f), FVector(0.20f, 1.70f, 3.0f));
	ControlBox = CreatePart(TEXT("ControlBox"), FVector(-55.0f, -230.0f, 120.0f), FVector(0.35f, 0.30f, 0.55f));
	ControlBox->SetCollisionProfileName(TEXT("BlockAllDynamic"));

	ExitVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ExitVolume"));
	ExitVolume->SetupAttachment(SceneRoot);
	ExitVolume->SetRelativeLocation(FVector(120.0f, 0.0f, 130.0f));
	ExitVolume->SetBoxExtent(FVector(140.0f, 155.0f, 130.0f));
	ExitVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ExitVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	ExitVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ExitVolume->OnComponentBeginOverlap.AddDynamic(this, &APHExitGate::HandleExitVolumeBeginOverlap);
}

void APHExitGate::BeginPlay()
{
	Super::BeginPlay();
	RefreshPresentation();
	BP_OnExitGateStateChanged();
}

FText APHExitGate::GetExitDisplayName() const
{
	return ExitDisplayName.IsEmpty()
		? NSLOCTEXT("PropHuntExitGate", "DefaultDisplayName", "SORTIE")
		: ExitDisplayName;
}

void APHExitGate::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !bGateEnabled || bGateOpen || ActiveInteractors.IsEmpty())
	{
		return;
	}

	for (auto Iterator = ActiveInteractors.CreateIterator(); Iterator; ++Iterator)
	{
		APHPropCharacter* PropCharacter = Iterator->Get();
		if (!IsValid(PropCharacter) || PropCharacter->GetActiveExitGate() != this || !CanPropInteract(*PropCharacter))
		{
			if (IsValid(PropCharacter))
			{
				PropCharacter->ClearActiveExitGateFromServer(this);
			}
			Iterator.RemoveCurrent();
		}
	}
	RefreshInteractorCount();
	if (ActiveInteractors.IsEmpty())
	{
		return;
	}

	const float SafeDuration = FMath::Clamp(OpenDurationSeconds, 1.0f, 120.0f);
	const float NewProgress = FMath::Clamp(OpenProgress + DeltaSeconds / SafeDuration, 0.0f, 1.0f);
	if (PHExitGateFlow::HasReachedOpenThreshold(NewProgress))
	{
		// Publish progress=100%, open state and disabled door collision in one
		// authoritative update. Clients never observe a full bar with a closed door.
		OpenProgress = 1.0f;
		CompleteOpening();
		return;
	}
	OpenProgress = NewProgress;
	OnRep_GateState();
	ForceNetUpdate();
}

void APHExitGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APHExitGate, OpenProgress);
	DOREPLIFETIME(APHExitGate, bGateEnabled);
	DOREPLIFETIME(APHExitGate, bGateOpen);
	DOREPLIFETIME(APHExitGate, ActiveInteractorCount);
}

FVector APHExitGate::GetInteractionPoint() const
{
	return InteractionAnchor != nullptr ? InteractionAnchor->GetComponentLocation() : GetActorLocation();
}

bool APHExitGate::IsWithinInteractionRange(const FVector& CharacterLocation) const
{
	const float SafeDistance = FMath::Clamp(InteractionDistance, 100.0f, 500.0f);
	return FVector::DistSquared(CharacterLocation, GetInteractionPoint()) <= FMath::Square(SafeDistance);
}

bool APHExitGate::CanPropInteract(const APHPropCharacter& PropCharacter) const
{
	const UWorld* World = GetWorld();
	const APHGameState* GameState = World != nullptr ? World->GetGameState<APHGameState>() : nullptr;
	const APHPlayerState* PlayerState = PropCharacter.GetPlayerState<APHPlayerState>();
	if (!HasAuthority() || World == nullptr || !bGateEnabled || bGateOpen
		|| PlayerState == nullptr || PlayerState->GetPlayerRole() != EPHPlayerRole::Prop
		|| GameState == nullptr || GameState->GetMatchPhase() != EPHMatchPhase::Escape
		|| (PropCharacter.GetCaptureState() != EPHPropCaptureState::Free
			&& PropCharacter.GetCaptureState() != EPHPropCaptureState::Grace))
	{
		return false;
	}

	if (!IsWithinInteractionRange(PropCharacter.GetActorLocation()))
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHExitGateInteraction), true, &PropCharacter);
	QueryParams.AddIgnoredActor(&PropCharacter);
	FHitResult HitResult;
	return World->LineTraceSingleByChannel(
		HitResult, PropCharacter.GetActorLocation() + FVector(0.0f, 0.0f, 45.0f), GetInteractionPoint(), ECC_Visibility, QueryParams)
		&& HitResult.GetActor() == this;
}

bool APHExitGate::ServerTryBeginInteraction(APHPropCharacter& PropCharacter)
{
	if (!CanPropInteract(PropCharacter))
	{
		return false;
	}
	ActiveInteractors.Add(&PropCharacter);
	PropCharacter.SetActiveExitGateFromServer(this);
	RefreshInteractorCount();
	UE_LOG(LogPHExitGate, Log, TEXT("%s began opening exit gate %s (%.1f seconds)."),
		*PropCharacter.GetName(), *GetName(), OpenDurationSeconds);
	return true;
}

void APHExitGate::ServerEndInteraction(APHPropCharacter& PropCharacter)
{
	if (!HasAuthority())
	{
		return;
	}
	if (ActiveInteractors.Remove(&PropCharacter) > 0)
	{
		PropCharacter.ClearActiveExitGateFromServer(this);
		RefreshInteractorCount();
	}
}

void APHExitGate::ResetForMatch()
{
	if (!HasAuthority())
	{
		return;
	}
	for (const TWeakObjectPtr<APHPropCharacter>& Interactor : ActiveInteractors)
	{
		if (Interactor.IsValid())
		{
			Interactor->ClearActiveExitGateFromServer(this);
		}
	}
	ActiveInteractors.Reset();
	OpenProgress = 0.0f;
	bGateEnabled = false;
	bGateOpen = false;
	ActiveInteractorCount = 0;
	OnRep_GateState();
	ForceNetUpdate();
}

void APHExitGate::SetGateEnabled(const bool bEnabled)
{
	if (!HasAuthority() || bGateOpen || bGateEnabled == bEnabled)
	{
		return;
	}
	bGateEnabled = bEnabled;
	OnRep_GateState();
	ForceNetUpdate();
}

#if !UE_BUILD_SHIPPING
FVector APHExitGate::GetExitVolumeLocationForSmoke() const
{
	return ExitVolume != nullptr ? ExitVolume->GetComponentLocation() : GetActorLocation();
}
#endif

void APHExitGate::OnRep_GateState()
{
	RefreshPresentation();
	BP_OnExitGateStateChanged();
}

void APHExitGate::HandleExitVolumeBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	const int32 OtherBodyIndex,
	const bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (HasAuthority() && bGateOpen)
	{
		if (APHPropCharacter* PropCharacter = Cast<APHPropCharacter>(OtherActor))
		{
			PropCharacter->ServerEscapeThroughGate(*this);
		}
	}
}

void APHExitGate::RefreshPresentation()
{
	for (UStaticMeshComponent* NativeVisual : { FrameLeft.Get(), FrameRight.Get(), FrameTop.Get(), DoorLeft.Get(), DoorRight.Get(), ControlBox.Get() })
	{
		if (NativeVisual != nullptr)
		{
			NativeVisual->SetVisibility(bShowNativeFallbackVisuals, false);
		}
	}

	const float OpenAlpha = bGateOpen ? 1.0f : FMath::SmoothStep(0.0f, 1.0f, OpenProgress);
	if (DoorLeft != nullptr)
	{
		DoorLeft->SetRelativeLocation(FVector(0.0f, FMath::Lerp(-85.0f, -280.0f, OpenAlpha), 150.0f));
		DoorLeft->SetCollisionEnabled(bGateOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	}
	if (DoorRight != nullptr)
	{
		DoorRight->SetRelativeLocation(FVector(0.0f, FMath::Lerp(85.0f, 280.0f, OpenAlpha), 150.0f));
		DoorRight->SetCollisionEnabled(bGateOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	}
	if (ControlBox != nullptr)
	{
		ControlBox->SetCustomDepthStencilValue(bGateEnabled && !bGateOpen ? 1 : 0);
		ControlBox->SetRenderCustomDepth(bGateEnabled && !bGateOpen);
	}
}

void APHExitGate::RefreshInteractorCount()
{
	ActiveInteractorCount = ActiveInteractors.Num();
	OnRep_GateState();
	ForceNetUpdate();
}

void APHExitGate::CompleteOpening()
{
	for (const TWeakObjectPtr<APHPropCharacter>& Interactor : ActiveInteractors)
	{
		if (Interactor.IsValid())
		{
			Interactor->ClearActiveExitGateFromServer(this);
		}
	}
	ActiveInteractors.Reset();
	OpenProgress = 1.0f;
	bGateOpen = true;
	ActiveInteractorCount = 0;
	OnRep_GateState();
	ForceNetUpdate();
	UE_LOG(LogPHExitGate, Log, TEXT("Exit gate %s opened authoritatively."), *GetName());
}
