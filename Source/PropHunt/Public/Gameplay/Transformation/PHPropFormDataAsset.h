#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "PHPropFormDataAsset.generated.h"

class UStaticMesh;

UENUM(BlueprintType)
enum class EPHPropHitboxShape : uint8
{
	Box,
	Capsule
};

UCLASS(BlueprintType)
class PROPHUNT_API UPHPropFormDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPHPropFormDataAsset();

	bool HasValidDefinition(FText* OutError = nullptr) const;
	FCollisionShape MakePlacementShape(float ShrinkAmount = 0.0f) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Transformation")
	FName FormId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Transformation")
	TObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Transformation")
	FVector MeshRelativeLocation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Transformation")
	FRotator MeshRelativeRotation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Transformation", meta = (ClampMin = "0.01"))
	FVector MeshRelativeScale;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Transformation", meta = (ClampMin = "15.0", ClampMax = "150.0", Units = "cm"))
	float CapsuleRadius;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Transformation", meta = (ClampMin = "15.0", ClampMax = "250.0", Units = "cm"))
	float CapsuleHalfHeight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Hitbox")
	EPHPropHitboxShape HitboxShape;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Hitbox")
	FVector HitboxRelativeLocation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Hitbox")
	FRotator HitboxRelativeRotation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Hitbox", meta = (ClampMin = "1.0", Units = "cm"))
	FVector HitboxBoxHalfExtents;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Hitbox", meta = (ClampMin = "1.0", ClampMax = "150.0", Units = "cm"))
	float HitboxCapsuleRadius;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Hitbox", meta = (ClampMin = "1.0", ClampMax = "250.0", Units = "cm"))
	float HitboxCapsuleHalfHeight;
};
