#include "Gameplay/Transformation/PHPropTransformTarget.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Transformation/PHPropFormDataAsset.h"

APHPropTransformTarget::APHPropTransformTarget()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SelectionCollision = CreateDefaultSubobject<UCapsuleComponent>(TEXT("SelectionCollision"));
	SelectionCollision->SetupAttachment(SceneRoot);
	SelectionCollision->InitCapsuleSize(40.0f, 60.0f);
	SelectionCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SelectionCollision->SetCollisionObjectType(ECC_WorldStatic);
	SelectionCollision->SetCollisionResponseToAllChannels(ECR_Block);
	SelectionCollision->SetCanEverAffectNavigation(true);

	SelectionBoxCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("SelectionBoxCollision"));
	SelectionBoxCollision->SetupAttachment(SceneRoot);
	SelectionBoxCollision->InitBoxExtent(FVector(40.0f, 40.0f, 60.0f));
	SelectionBoxCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SelectionBoxCollision->SetCollisionObjectType(ECC_WorldStatic);
	SelectionBoxCollision->SetCollisionResponseToAllChannels(ECR_Block);
	SelectionBoxCollision->SetCanEverAffectNavigation(true);

	TargetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TargetMesh"));
	TargetMesh->SetupAttachment(SceneRoot);
	TargetMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TargetMesh->SetCanEverAffectNavigation(false);
}

UPrimitiveComponent* APHPropTransformTarget::GetActiveSelectionCollision() const
{
	if (SelectionBoxCollision != nullptr && SelectionBoxCollision->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
	{
		return SelectionBoxCollision;
	}
	return SelectionCollision;
}

void APHPropTransformTarget::SetLocallyHighlighted(const bool bHighlighted)
{
	if (TargetMesh == nullptr)
	{
		return;
	}

	TargetMesh->SetCustomDepthStencilValue(1);
	TargetMesh->SetRenderCustomDepth(bHighlighted);
}

void APHPropTransformTarget::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyFormDefinition();
}

void APHPropTransformTarget::BeginPlay()
{
	Super::BeginPlay();
	ApplyFormDefinition();
}

void APHPropTransformTarget::ApplyFormDefinition()
{
	if (PropForm == nullptr || !PropForm->HasValidDefinition())
	{
		TargetMesh->SetStaticMesh(nullptr);
		SelectionCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SelectionBoxCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	TargetMesh->SetStaticMesh(PropForm->StaticMesh);
	TargetMesh->SetRelativeLocation(PropForm->MeshRelativeLocation);
	TargetMesh->SetRelativeRotation(PropForm->MeshRelativeRotation);
	TargetMesh->SetRelativeScale3D(PropForm->MeshRelativeScale);
	SelectionCollision->SetRelativeLocation(PropForm->HitboxRelativeLocation);
	SelectionCollision->SetRelativeRotation(PropForm->HitboxRelativeRotation);
	SelectionBoxCollision->SetRelativeLocation(PropForm->HitboxRelativeLocation);
	SelectionBoxCollision->SetRelativeRotation(PropForm->HitboxRelativeRotation);
	if (PropForm->HitboxShape == EPHPropHitboxShape::Box)
	{
		SelectionCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SelectionBoxCollision->SetBoxExtent(PropForm->HitboxBoxHalfExtents, true);
		SelectionBoxCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	else
	{
		SelectionBoxCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SelectionCollision->SetCapsuleSize(PropForm->HitboxCapsuleRadius, PropForm->HitboxCapsuleHalfHeight, true);
		SelectionCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}
