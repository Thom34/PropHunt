#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PHHumanPrototypeSelector.generated.h"

class APHPropCharacter;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS(Blueprintable)
class PROPHUNT_API APHHumanPrototypeSelector : public AActor
{
	GENERATED_BODY()

public:
	APHHumanPrototypeSelector();

	UFUNCTION(BlueprintPure, Category = "PropHunt|Presentation|Human")
	float GetInteractionDistance() const { return InteractionDistance; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Presentation|Human")
	FVector GetInteractionPoint() const;

	bool ServerTryUse(APHPropCharacter& Survivor);

private:
	bool HasLineOfSightFrom(const APHPropCharacter& Survivor) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation|Human", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation|Human", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> GrayboxBody;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation|Human", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTextRenderComponent> FrontLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation|Human", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTextRenderComponent> BackLabel;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human", meta = (ClampMin = "100.0", ClampMax = "600.0", Units = "cm", AllowPrivateAccess = "true"))
	float InteractionDistance;
};
