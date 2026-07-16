#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#include "PHPhysicsPropPrototype.generated.h"

class UCameraComponent;
class UPrimitiveComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UPHPhysicsPropDataAsset;

UCLASS(Blueprintable)
class PROPHUNT_API APHPhysicsPropPrototype : public APawn
{
	GENERATED_BODY()

public:
	APHPhysicsPropPrototype();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Physics Prop")
	const UPHPhysicsPropDataAsset* GetDefinition() const { return Definition; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Physics Prop")
	int32 GetImpactSequence() const { return ImpactSequence; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Physics Prop")
	float GetCurrentImpactThreshold() const;

	UStaticMeshComponent* GetPhysicsMesh() const { return PhysicsMesh; }

private:
	void MoveForward(float Value);
	void MoveRight(float Value);
	void LookYaw(float Value);
	void LookPitch(float Value);
	void RequestJump();
	void StartStraighten();
	void StopStraighten();
	void ApplyServerControlIntent(float DeltaSeconds);
	void ApplyStraightenState(float DeltaSeconds);
	void SetServerControlIntent(float Forward, float Right, float ViewYaw, bool bStraighten);
	void TryJump();
	bool IsGrounded() const;
#if !UE_BUILD_SHIPPING
	void BeginJumpSmoke();
	void RunJumpSmokeSecondJump();
	void RunJumpSmokeThirdAttempt();
	void ReportJumpSmoke();
#endif

	UFUNCTION(Server, Unreliable)
	void ServerSetControlIntent(float Forward, float Right, float ViewYaw, bool bStraighten);

	UFUNCTION(Server, Reliable)
	void ServerRequestJump();

	UFUNCTION()
	void OnPhysicsHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	UFUNCTION()
	void OnRep_Definition();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayImpactSound(FVector_NetQuantize ImpactLocation);

	void ApplyDefinition(bool bAllowSimulation);
	void ResetTestDrop();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PhysicsMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> ThirdPersonCamera;

	UPROPERTY(EditInstanceOnly, ReplicatedUsing = OnRep_Definition, BlueprintReadOnly, Category = "PropHunt|Physics Prop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPHPhysicsPropDataAsset> Definition;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Physics Prop", meta = (AllowPrivateAccess = "true"))
	int32 ImpactSequence;

	UPROPERTY(EditInstanceOnly, Category = "PropHunt|Physics Prop|Test", meta = (AllowPrivateAccess = "true"))
	bool bAutoResetForNetworkTesting;

	UPROPERTY(EditInstanceOnly, Category = "PropHunt|Physics Prop|Test", meta = (ClampMin = "2.0", ClampMax = "30.0", Units = "s", AllowPrivateAccess = "true"))
	float AutoResetIntervalSeconds;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Physics Prop|Camera", meta = (ClampMin = "100.0", ClampMax = "800.0", Units = "cm", AllowPrivateAccess = "true"))
	float CameraArmLength;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Physics Prop|Camera", meta = (ClampMin = "0.0", ClampMax = "300.0", Units = "cm", AllowPrivateAccess = "true"))
	float CameraTargetHeight;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Physics Prop|Camera", meta = (ClampMin = "40.0", ClampMax = "120.0", Units = "deg", AllowPrivateAccess = "true"))
	float CameraFieldOfView;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Physics Prop|Input", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float YawInputScale;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Physics Prop|Input", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float PitchInputScale;

	float LocalForwardInput;
	float LocalRightInput;
	bool bLocalStraightenHeld;
	float ServerForwardInput;
	float ServerRightInput;
	float ServerViewYaw;
	bool bServerStraightenHeld;
	bool bStraightenWasActive;
	bool bMovementWasActive;
	bool bWasGrounded;
	int32 JumpsUsed;
	float CurrentStraightenInterpSpeed;
	double LastServerControlIntentTime;
	double LastJumpTime;
	double LastImpactSoundTime;
	FTransform InitialTestTransform;
	FTimerHandle AutoResetTimerHandle;
#if !UE_BUILD_SHIPPING
	FTimerHandle JumpSmokeTimerHandle;
	float JumpSmokePreJumpHorizontalSpeed;
	float JumpSmokeFirstHorizontalSpeed;
	float JumpSmokeBeforeSecondHorizontalSpeed;
	float JumpSmokeSecondHorizontalSpeed;
	float JumpSmokeSecondRightDot;
	int32 JumpSmokeCountAfterThirdAttempt;
#endif
};
