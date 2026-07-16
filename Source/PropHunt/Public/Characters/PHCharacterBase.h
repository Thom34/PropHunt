#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "PHCharacterBase.generated.h"

UCLASS(Abstract, Blueprintable)
class PROPHUNT_API APHCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	APHCharacterBase();

	virtual void PostInitializeComponents() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

protected:
	void MoveForward(float Value);
	void MoveRight(float Value);
	void LookYaw(float Value);
	void LookPitch(float Value);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Presentation", meta = (DisplayName = "On Local View Ready"))
	void BP_OnLocalViewReady();

private:
	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Movement", meta = (ClampMin = "0.0", Units = "cm/s", AllowPrivateAccess = "true"))
	float MaximumWalkSpeed;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Movement", meta = (ClampMin = "0.0", Units = "cm/s^2", AllowPrivateAccess = "true"))
	float MaximumAcceleration;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Movement", meta = (ClampMin = "0.0", Units = "cm/s^2", AllowPrivateAccess = "true"))
	float BrakingDeceleration;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Movement", meta = (ClampMin = "0.0", Units = "cm/s", AllowPrivateAccess = "true"))
	float JumpVelocity;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0", AllowPrivateAccess = "true"))
	float AirControlRatio;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Input", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float YawInputScale;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Input", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float PitchInputScale;
};
