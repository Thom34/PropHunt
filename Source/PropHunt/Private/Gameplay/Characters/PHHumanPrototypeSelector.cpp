#include "Gameplay/Characters/PHHumanPrototypeSelector.h"

#include "Characters/PHPropCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHHumanPrototypeSelector, Log, All);

APHHumanPrototypeSelector::APHHumanPrototypeSelector()
	: InteractionDistance(350.0f)
{
	bReplicates = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	GrayboxBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GrayboxBody"));
	GrayboxBody->SetupAttachment(SceneRoot);
	GrayboxBody->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	GrayboxBody->SetRelativeScale3D(FVector(0.65f, 0.65f, 1.2f));
	GrayboxBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GrayboxBody->SetCollisionObjectType(ECC_WorldDynamic);
	GrayboxBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	GrayboxBody->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GrayboxBody->SetGenerateOverlapEvents(false);
	GrayboxBody->SetCanEverAffectNavigation(false);
	GrayboxBody->CanCharacterStepUpOn = ECB_No;

	FrontLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("FrontLabel"));
	FrontLabel->SetupAttachment(SceneRoot);
	FrontLabel->SetRelativeLocation(FVector(-34.0f, 0.0f, 145.0f));
	FrontLabel->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
	FrontLabel->SetHorizontalAlignment(EHTA_Center);
	FrontLabel->SetWorldSize(24.0f);
	FrontLabel->SetText(FText::FromString(TEXT("T : ISAAC / JUN")));

	BackLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("BackLabel"));
	BackLabel->SetupAttachment(SceneRoot);
	BackLabel->SetRelativeLocation(FVector(34.0f, 0.0f, 145.0f));
	BackLabel->SetHorizontalAlignment(EHTA_Center);
	BackLabel->SetWorldSize(24.0f);
	BackLabel->SetText(FText::FromString(TEXT("T : ISAAC / JUN")));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		GrayboxBody->SetStaticMesh(CylinderMesh.Object);
	}
}

FVector APHHumanPrototypeSelector::GetInteractionPoint() const
{
	return GrayboxBody != nullptr ? GrayboxBody->GetComponentLocation() : GetActorLocation();
}

bool APHHumanPrototypeSelector::ServerTryUse(APHPropCharacter& Survivor)
{
	if (!HasAuthority() || !Survivor.HasAuthority()
		|| FVector::DistSquared(Survivor.GetActorLocation(), GetActorLocation())
			> FMath::Square(FMath::Clamp(InteractionDistance, 100.0f, 600.0f))
		|| !HasLineOfSightFrom(Survivor))
	{
		return false;
	}

	const bool bSwitched = Survivor.ServerToggleHumanPrototype();
	if (bSwitched)
	{
		UE_LOG(LogPHHumanPrototypeSelector, Log, TEXT("%s switched to survivor prototype %s at %s."),
			*Survivor.GetName(), *Survivor.GetActiveHumanPrototypeName().ToString(), *GetName());
	}
	return bSwitched;
}

bool APHHumanPrototypeSelector::HasLineOfSightFrom(const APHPropCharacter& Survivor) const
{
	if (GetWorld() == nullptr)
	{
		return false;
	}

	const float HalfHeight = Survivor.GetCapsuleComponent() != nullptr
		? Survivor.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 50.0f;
	const FVector TraceStart = Survivor.GetActorLocation() + FVector::UpVector * HalfHeight;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHHumanPrototypeSelectorLineOfSight), false, &Survivor);
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, TraceStart, GetInteractionPoint(), ECC_Visibility, QueryParams);
	return !bHit || Hit.GetActor() == this;
}
