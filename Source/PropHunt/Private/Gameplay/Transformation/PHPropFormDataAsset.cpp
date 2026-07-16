#include "Gameplay/Transformation/PHPropFormDataAsset.h"

#include "Engine/StaticMesh.h"

UPHPropFormDataAsset::UPHPropFormDataAsset()
	: FormId(NAME_None)
	, MeshRelativeLocation(FVector::ZeroVector)
	, MeshRelativeRotation(FRotator::ZeroRotator)
	, MeshRelativeScale(FVector::OneVector)
	, CapsuleRadius(40.0f)
	, CapsuleHalfHeight(60.0f)
	, HitboxShape(EPHPropHitboxShape::Capsule)
	, HitboxRelativeLocation(FVector::ZeroVector)
	, HitboxRelativeRotation(FRotator::ZeroRotator)
	, HitboxBoxHalfExtents(40.0f, 40.0f, 60.0f)
	, HitboxCapsuleRadius(40.0f)
	, HitboxCapsuleHalfHeight(60.0f)
{
}

bool UPHPropFormDataAsset::HasValidDefinition(FText* OutError) const
{
	auto Fail = [OutError](const FText& Error)
	{
		if (OutError != nullptr)
		{
			*OutError = Error;
		}
		return false;
	};

	if (FormId.IsNone())
	{
		return Fail(NSLOCTEXT("PropHunt", "PropFormMissingId", "The Prop form needs a stable FormId."));
	}

	if (StaticMesh == nullptr)
	{
		return Fail(NSLOCTEXT("PropHunt", "PropFormMissingMesh", "The Prop form needs a cooked static mesh."));
	}

	const FString MeshPath = StaticMesh->GetPathName();
	if (!MeshPath.StartsWith(TEXT("/Game/PropHunt/")) || MeshPath.StartsWith(TEXT("/Game/Developers/")))
	{
		return Fail(FText::Format(
			NSLOCTEXT("PropHunt", "PropFormInvalidMeshPath", "The Prop form mesh must be a production asset under /Game/PropHunt, not {0}."),
			FText::FromString(MeshPath)));
	}

	if (MeshRelativeLocation.ContainsNaN() || MeshRelativeRotation.ContainsNaN() || MeshRelativeScale.ContainsNaN()
		|| MeshRelativeScale.GetMin() <= 0.0f)
	{
		return Fail(NSLOCTEXT("PropHunt", "PropFormInvalidMeshTransform", "The Prop form mesh transform is invalid."));
	}

	if (!FMath::IsFinite(CapsuleRadius) || !FMath::IsFinite(CapsuleHalfHeight)
		|| CapsuleRadius < 15.0f || CapsuleRadius > 150.0f
		|| CapsuleHalfHeight < CapsuleRadius || CapsuleHalfHeight > 250.0f)
	{
		return Fail(NSLOCTEXT("PropHunt", "PropFormInvalidCapsule", "The Prop form capsule dimensions are outside the server safety bounds."));
	}

	if (HitboxRelativeLocation.ContainsNaN() || HitboxRelativeRotation.ContainsNaN())
	{
		return Fail(NSLOCTEXT("PropHunt", "PropFormInvalidHitboxTransform", "The Prop form hitbox transform is invalid."));
	}

	if (HitboxShape == EPHPropHitboxShape::Box)
	{
		if (HitboxBoxHalfExtents.ContainsNaN() || HitboxBoxHalfExtents.GetMin() < 1.0f || HitboxBoxHalfExtents.GetMax() > 250.0f)
		{
			return Fail(NSLOCTEXT("PropHunt", "PropFormInvalidBoxHitbox", "The Prop form box hitbox dimensions are outside the server safety bounds."));
		}
	}
	else if (!FMath::IsFinite(HitboxCapsuleRadius) || !FMath::IsFinite(HitboxCapsuleHalfHeight)
		|| HitboxCapsuleRadius < 1.0f || HitboxCapsuleRadius > 150.0f
		|| HitboxCapsuleHalfHeight < HitboxCapsuleRadius || HitboxCapsuleHalfHeight > 250.0f)
	{
		return Fail(NSLOCTEXT("PropHunt", "PropFormInvalidCapsuleHitbox", "The Prop form capsule hitbox dimensions are outside the server safety bounds."));
	}

	if (OutError != nullptr)
	{
		*OutError = FText::GetEmpty();
	}
	return true;
}

FCollisionShape UPHPropFormDataAsset::MakePlacementShape(const float ShrinkAmount) const
{
	const float SafeShrink = FMath::Max(0.0f, ShrinkAmount);
	if (HitboxShape == EPHPropHitboxShape::Box)
	{
		return FCollisionShape::MakeBox(FVector(
			FMath::Max(1.0f, HitboxBoxHalfExtents.X - SafeShrink),
			FMath::Max(1.0f, HitboxBoxHalfExtents.Y - SafeShrink),
			FMath::Max(1.0f, HitboxBoxHalfExtents.Z - SafeShrink)));
	}

	const float Radius = FMath::Max(1.0f, HitboxCapsuleRadius - SafeShrink);
	return FCollisionShape::MakeCapsule(Radius, FMath::Max(Radius, HitboxCapsuleHalfHeight - SafeShrink));
}
