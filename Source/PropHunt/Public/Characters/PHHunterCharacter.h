#pragma once

#include "CoreMinimal.h"
#include "Characters/PHCharacterBase.h"

#include "PHHunterCharacter.generated.h"

class UCameraComponent;
class UAnimSequence;
class USceneComponent;
class USkeletalMesh;
class USpringArmComponent;
class UStaticMeshComponent;
class APHPropCharacter;
class APHRetentionPoint;

USTRUCT(BlueprintType)
struct FPHMeleeAttackState
{
	GENERATED_BODY()

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Melee")
	int32 Sequence = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Melee")
	bool bHitProp = false;
};

UCLASS(Blueprintable)
class PROPHUNT_API APHHunterCharacter : public APHCharacterBase
{
	GENERATED_BODY()

public:
	APHHunterCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Melee")
	void RequestMeleeAttack();

	UFUNCTION(BlueprintPure, Category = "PropHunt|Melee")
	int32 GetMeleeAttackSequence() const { return MeleeAttackState.Sequence; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Melee")
	bool DidLastMeleeAttackHitProp() const { return MeleeAttackState.bHitProp; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Melee")
	float GetMeleeRange() const { return MeleeRange; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Melee")
	float GetMeleeWidth() const { return MeleeWidth; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Melee")
	float GetMeleeVerticalTolerance() const { return MeleeVerticalTolerance; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Melee")
	float GetMeleeRecoverySeconds() const { return MeleeRecoverySeconds; }

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Capture")
	void RequestCaptureInteraction();

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Memento")
	void RequestMemento();

	UFUNCTION(BlueprintPure, Category = "PropHunt|Memento")
	int32 GetMementoMinimumRetentionCount() const { return MementoMinimumRetentionCount; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Memento")
	float GetMementoWallSearchDistance() const { return MementoWallSearchDistance; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	APHPropCharacter* GetCarriedProp() const { return CarriedProp; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	float GetCaptureInteractionDistance() const { return CaptureInteractionDistance; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	FVector GetCarryAnchorOffset() const { return CarryAnchorOffset; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	FRotator GetCarryAnchorRotation() const { return CarryAnchorRotation; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Camera")
	bool IsUsingCarryThirdPersonCamera() const { return CarriedProp != nullptr; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Camera")
	float GetCarryCameraArmLength() const { return CarryCameraArmLength; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Struggle")
	float GetCarryStruggleShoveDuration() const { return CarryStruggleShoveDuration; }

	USceneComponent* GetCarryAnchor() const { return CarryAnchor; }
	void ClearCarriedProp(const APHPropCharacter* ExpectedProp);
	void ApplyCarryStruggleShove(float SideDirection, float ShoveDistance);

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Melee", meta = (DisplayName = "On Melee Attack Anticipated"))
	void BP_OnMeleeAttackAnticipated();

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Melee", meta = (DisplayName = "On Melee Attack Confirmed"))
	void BP_OnMeleeAttackConfirmed(bool bHitProp);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Capture", meta = (DisplayName = "On Carried Prop Changed"))
	void BP_OnCarriedPropChanged(APHPropCharacter* NewCarriedProp);

private:
	UFUNCTION(Server, Reliable)
	void ServerRequestMeleeAttack();

	UFUNCTION()
	void OnRep_MeleeAttackState();

	UFUNCTION(Server, Reliable)
	void ServerRequestCaptureInteraction(APHPropCharacter* RequestedProp, APHRetentionPoint* RequestedPoint);

	UFUNCTION(Server, Reliable)
	void ServerRequestMemento(APHPropCharacter* RequestedProp);

	UFUNCTION()
	void OnRep_CarriedProp();

	bool CanPerformMeleeAttack(double ServerTimeSeconds) const;
	double GetSafeMeleeRecoverySeconds() const;
	void PerformAuthoritativeMeleeAttack();
	bool TryResolveDownedProp(APHPropCharacter*& OutProp) const;
	bool TryResolveRetentionPoint(APHRetentionPoint*& OutPoint) const;
	bool IsNearAvailableRetentionPoint() const;
	void PerformAuthoritativeCaptureInteraction(APHPropCharacter* RequestedProp, APHRetentionPoint* RequestedPoint);
	void PerformAuthoritativeMemento(APHPropCharacter* RequestedProp);
	bool TryResolveMementoWall(const APHPropCharacter& Target, FVector& OutImpactPoint) const;
	bool HasCaptureLineOfSight(const AActor& Target, const FVector& TargetPoint) const;
	void ConfigureHumanPresentation();
	void UpdateLocomotionPresentation();
	void PlayHumanAnimation(UAnimSequence* Animation, bool bFreezePose);
	void UpdateCarryStruggleShove(float DeltaSeconds);
	void ClearCarryStruggleShove();
	void ApplyLocalCameraMode();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CarryCameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> CarryThirdPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> GrayboxBody;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMesh> HumanSkeletalMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> HumanIdleAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> HumanWalkAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> HumanCarryIdleAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> HumanCarryWalkAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human", meta = (ClampMin = "0.0", ClampMax = "200.0", Units = "cm/s", AllowPrivateAccess = "true"))
	float HumanWalkAnimationThreshold;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> FirstPersonPresentationRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> GrayboxLeftArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> GrayboxRightArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> CarryAnchor;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Camera", meta = (ClampMin = "60.0", ClampMax = "120.0", Units = "deg", AllowPrivateAccess = "true"))
	float CameraFieldOfView;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Camera", meta = (AllowPrivateAccess = "true"))
	FVector CameraOffset;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Camera|Carry", meta = (ClampMin = "150.0", ClampMax = "600.0", Units = "cm", AllowPrivateAccess = "true"))
	float CarryCameraArmLength;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Camera|Carry", meta = (AllowPrivateAccess = "true"))
	FVector CarryCameraSocketOffset;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Melee", meta = (ClampMin = "50.0", ClampMax = "300.0", Units = "cm", AllowPrivateAccess = "true"))
	float MeleeRange;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Melee", meta = (ClampMin = "10.0", ClampMax = "200.0", Units = "cm", AllowPrivateAccess = "true"))
	float MeleeWidth;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Melee", meta = (ClampMin = "10.0", ClampMax = "150.0", Units = "cm", AllowPrivateAccess = "true"))
	float MeleeVerticalTolerance;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Melee", meta = (ClampMin = "0.0", ClampMax = "75.0", Units = "cm", AllowPrivateAccess = "true"))
	float MeleeForwardOffset;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Melee", meta = (DisplayName = "Melee Cooldown Seconds", ClampMin = "0.1", ClampMax = "5.0", Units = "s", AllowPrivateAccess = "true"))
	float MeleeRecoverySeconds;

	UPROPERTY(EditAnywhere, Category = "PropHunt|Melee|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawMeleeDebug;

	UPROPERTY(EditAnywhere, Category = "PropHunt|Melee|Debug", meta = (ClampMin = "0.1", ClampMax = "10.0", Units = "s", EditCondition = "bDrawMeleeDebug", AllowPrivateAccess = "true"))
	float MeleeDebugDuration;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture", meta = (ClampMin = "100.0", ClampMax = "500.0", Units = "cm", AllowPrivateAccess = "true"))
	float CaptureInteractionDistance;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture", meta = (ClampMin = "5.0", ClampMax = "45.0", Units = "deg", AllowPrivateAccess = "true"))
	float CaptureTargetingHalfAngleDegrees;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture|Presentation", meta = (AllowPrivateAccess = "true"))
	FVector CarryAnchorOffset;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture|Presentation", meta = (AllowPrivateAccess = "true"))
	FRotator CarryAnchorRotation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture|Struggle", meta = (ClampMin = "0.1", ClampMax = "1.0", Units = "s", AllowPrivateAccess = "true"))
	float CarryStruggleShoveDuration;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Memento", meta = (ClampMin = "0", ClampMax = "3", AllowPrivateAccess = "true"))
	int32 MementoMinimumRetentionCount;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Memento", meta = (ClampMin = "300.0", ClampMax = "2000.0", Units = "cm", AllowPrivateAccess = "true"))
	float MementoWallSearchDistance;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Memento", meta = (ClampMin = "300.0", ClampMax = "2500.0", Units = "cm/s", AllowPrivateAccess = "true"))
	float MementoLaunchSpeed;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Memento", meta = (ClampMin = "0.0", ClampMax = "800.0", Units = "cm/s", AllowPrivateAccess = "true"))
	float MementoUpwardVelocity;

	UPROPERTY(ReplicatedUsing = OnRep_MeleeAttackState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Melee", meta = (AllowPrivateAccess = "true"))
	FPHMeleeAttackState MeleeAttackState;

	UPROPERTY(ReplicatedUsing = OnRep_CarriedProp, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<APHPropCharacter> CarriedProp;

	double LastLocalMeleeRequestTime;
	double LastServerMeleeAttackTime;
	bool bPlayingWalkAnimation;
	bool bCurrentHumanAnimationFrozen;
	float ActiveCarryStruggleShoveDirection;
	float RemainingCarryStruggleShoveDistance;
	float ActiveCarryStruggleShoveSpeed;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CurrentHumanAnimation;
};
