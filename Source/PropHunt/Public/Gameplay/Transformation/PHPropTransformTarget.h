#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PHPropTransformTarget.generated.h"

class UBoxComponent;
class UCapsuleComponent;
class UPrimitiveComponent;
class UPHPropFormDataAsset;
class USceneComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class PROPHUNT_API APHPropTransformTarget : public AActor
{
	GENERATED_BODY()

public:
	APHPropTransformTarget();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Transformation")
	const UPHPropFormDataAsset* GetPropForm() const { return PropForm; }

	UPrimitiveComponent* GetActiveSelectionCollision() const;
	void SetLocallyHighlighted(bool bHighlighted);

private:
	void ApplyFormDefinition();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Transformation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Transformation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCapsuleComponent> SelectionCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Transformation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> SelectionBoxCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Transformation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> TargetMesh;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPHPropFormDataAsset> PropForm;
};
