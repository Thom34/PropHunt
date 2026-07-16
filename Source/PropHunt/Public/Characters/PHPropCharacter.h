#pragma once

#include "CoreMinimal.h"
#include "Characters/PHCharacterBase.h"
#include "Gameplay/Capture/PHCaptureTypes.h"
#include "Gameplay/Transformation/PHPropFormDataAsset.h"

#include "PHPropCharacter.generated.h"

class UCameraComponent;
class UAnimSequence;
class UBoxComponent;
class UCapsuleComponent;
class USkeletalMesh;
class USpringArmComponent;
class UStaticMeshComponent;
class APHHunterCharacter;
class APHObjectiveActor;
class APHPropTransformTarget;
class APHRetentionPoint;
class APHHumanPrototypeSelector;

struct FPHResolvedPropHitbox
{
	EPHPropHitboxShape Shape = EPHPropHitboxShape::Capsule;
	FVector RelativeLocation = FVector::ZeroVector;
	FRotator RelativeRotation = FRotator::ZeroRotator;
	FVector BoxHalfExtents = FVector(40.0f, 40.0f, 60.0f);
	float CapsuleRadius = 40.0f;
	float CapsuleHalfHeight = 60.0f;
};

UENUM(BlueprintType)
enum class EPHPropTransformationResult : uint8
{
	None,
	Transformed,
	ReturnedToInitial,
	RejectedRoleOrPhase,
	RejectedCooldown,
	RejectedNoTarget,
	RejectedTooFar,
	RejectedLineOfSight,
	RejectedNotAllowed,
	RejectedInvalidForm,
	RejectedPlacementBlocked,
	RejectedAlreadyInitial
};

USTRUCT(BlueprintType)
struct FPHPropTransformationState
{
	GENERATED_BODY()

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation")
	TObjectPtr<UPHPropFormDataAsset> ActiveForm = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation")
	float CapsuleRadius = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation")
	float CapsuleHalfHeight = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation")
	EPHPropHitboxShape HitboxShape = EPHPropHitboxShape::Capsule;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation")
	FVector HitboxRelativeLocation = FVector::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation")
	FRotator HitboxRelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation")
	FVector HitboxBoxHalfExtents = FVector::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation")
	float HitboxCapsuleRadius = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation")
	float HitboxCapsuleHalfHeight = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation")
	int32 Sequence = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation")
	EPHPropTransformationResult LastResult = EPHPropTransformationResult::None;
};

UCLASS(Blueprintable)
class PROPHUNT_API APHPropCharacter : public APHCharacterBase
{
	GENERATED_BODY()

public:
	APHPropCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual bool IsMoveInputIgnored() const override;
	virtual void Landed(const FHitResult& Hit) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintPure, Category = "PropHunt|Melee")
	int32 GetMeleeHitSequence() const { return MeleeHitSequence; }

	void ReceiveAuthoritativeMeleeHit(APHHunterCharacter& Attacker);

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Capture")
	void RequestCaptureRescue();

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	EPHPropCaptureState GetCaptureState() const { return CaptureState; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	int32 GetRetentionCount() const { return RetentionCount; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	float GetCaptureStateEndServerTime() const { return CaptureStateEndServerTime; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	const APHHunterCharacter* GetCarrier() const { return Carrier; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	const APHRetentionPoint* GetRetentionPoint() const { return RetentionPoint; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	bool CanPerformCaptureRescue() const;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Rescue")
	bool IsReleasingRetainedProp() const { return ActiveRetentionRescuePoint != nullptr; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Rescue")
	float GetRetentionRescueProgressNormalized() const;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Rescue")
	const APHRetentionPoint* GetActiveRetentionRescuePoint() const { return ActiveRetentionRescuePoint; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	float GetFirstRetentionDuration() const { return FirstRetentionDuration; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	float GetSecondRetentionDuration() const { return SecondRetentionDuration; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	float GetGraceDuration() const { return GraceDuration; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Recovery")
	float GetDownedCrawlSpeed() const { return DownedCrawlSpeed; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Recovery")
	float GetDownedRecoveryDuration() const { return DownedRecoveryDuration; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Recovery")
	float GetDownedRecoveryProgress() const { return DownedRecoveryProgress; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Recovery")
	float GetDownedRecoveryProgressNormalized() const
	{
		return FMath::Clamp(DownedRecoveryProgress, 0.0f, 1.0f);
	}

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Recovery")
	int32 GetRecoveryHelperCount() const { return RecoveryHelperCount; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Struggle")
	float GetCarryStruggleProgress() const { return CarryStruggleProgress; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Struggle")
	float GetCarryStruggleProgressNormalized() const
	{
		return FMath::Clamp(CarryStruggleProgress, 0.0f, 1.0f);
	}

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Struggle")
	int32 GetCarryStruggleRequiredAlternations() const { return CarryStruggleRequiredAlternations; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Struggle")
	float GetCarryStruggleHunterShoveDistance() const { return CarryStruggleHunterShoveDistance; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Presentation")
	FVector GetPrimaryCarryAttachmentOffset() const { return PrimaryCarryAttachmentOffset; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Presentation")
	FVector GetAlternativeCarryAttachmentOffset() const { return AlternativeCarryAttachmentOffset; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Presentation")
	UAnimSequence* GetPrimaryHumanCarriedMoveAnimation() const { return HumanCarriedMoveAnimation; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Presentation")
	UAnimSequence* GetAlternativeHumanCarriedMoveAnimation() const { return AlternativeHumanCarriedMoveAnimation; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture|Presentation")
	bool IsPlayingCarriedMoveAnimation() const;

	bool ServerTryBeCarriedBy(APHHunterCharacter& Hunter);
	void ServerDropFromCarrier();
	void ServerRetainAt(APHRetentionPoint& NewRetentionPoint);
	void ServerReleaseWithGrace();
	void ResetCaptureForMatch();

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Presentation|Human")
	void RequestSwitchHumanPrototype();

	UFUNCTION(BlueprintPure, Category = "PropHunt|Presentation|Human")
	bool HasAlternativeHumanPrototype() const;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Presentation|Human")
	bool IsUsingAlternativeHumanPrototype() const { return bUseAlternativeHumanPrototype; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Presentation|Human")
	FName GetActiveHumanPrototypeName() const;

	bool ServerToggleHumanPrototype();

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Transformation")
	void RequestPropTransformation();

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Transformation")
	void RequestReturnToInitialForm();

	UFUNCTION(BlueprintPure, Category = "PropHunt|Transformation")
	const UPHPropFormDataAsset* GetActivePropForm() const { return TransformationState.ActiveForm; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Transformation")
	EPHPropTransformationResult GetLastTransformationResult() const { return TransformationState.LastResult; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Transformation")
	int32 GetTransformationSequence() const { return TransformationState.Sequence; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Transformation")
	float GetTransformationDistance() const { return TransformationDistance; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Transformation")
	float GetTransformationCooldownSeconds() const { return TransformationCooldownSeconds; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Transformation")
	float GetTransformationTargetingHalfAngleDegrees() const { return TransformationTargetingHalfAngleDegrees; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Transformation")
	float GetMaximumPlacementAdjustment() const { return MaximumPlacementAdjustment; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Transformation")
	int32 GetAllowedPropFormCount() const { return AllowedPropForms.Num(); }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Camera")
	bool IsUsingPropThirdPersonCamera() const
	{
		return TransformationState.ActiveForm != nullptr
			|| CaptureState == EPHPropCaptureState::Downed
			|| CaptureState == EPHPropCaptureState::Carried
			|| CaptureState == EPHPropCaptureState::Retained;
	}

	UFUNCTION(BlueprintPure, Category = "PropHunt|Human|Stamina")
	float GetHumanStamina() const { return CurrentHumanStamina; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Human|Stamina")
	float GetMaximumHumanStamina() const { return MaximumHumanStamina; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Human|Stamina")
	float GetHumanSprintSpeed() const { return HumanSprintSpeed; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Human|Stamina")
	float GetHumanStaminaNormalized() const
	{
		return MaximumHumanStamina > KINDA_SMALL_NUMBER
			? FMath::Clamp(CurrentHumanStamina / MaximumHumanStamina, 0.0f, 1.0f)
			: 0.0f;
	}

	UFUNCTION(BlueprintPure, Category = "PropHunt|Human|Stamina")
	float GetHumanSprintDrainPerSecond() const { return HumanSprintDrainPerSecond; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Human|Stamina")
	float GetHumanStaminaRechargePerSecond() const { return HumanStaminaRechargePerSecond; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Human|Stamina")
	float GetHumanJumpStaminaCost() const { return HumanJumpStaminaCost; }

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Objective")
	void RequestStartObjectiveInteraction();

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Objective")
	void RequestStopObjectiveInteraction();

	UFUNCTION(BlueprintPure, Category = "PropHunt|Objective")
	bool IsInteractingWithObjective() const { return ActiveObjective != nullptr; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Objective")
	const APHObjectiveActor* GetActiveObjective() const { return ActiveObjective; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Objective")
	float GetObjectiveTargetingHalfAngleDegrees() const { return ObjectiveTargetingHalfAngleDegrees; }

	void SetActiveObjectiveFromServer(APHObjectiveActor* Objective);
	void ClearActiveObjectiveFromServer(const APHObjectiveActor* ExpectedObjective);
	void SetActiveRetentionRescueFromServer(APHRetentionPoint* Point);
	void ClearActiveRetentionRescueFromServer(const APHRetentionPoint* ExpectedPoint);

	bool IsPropFormAllowed(const UPHPropFormDataAsset* CandidateForm) const;
	FVector GetMeleeLineOfSightPoint(const FVector& FromLocation) const;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Melee", meta = (DisplayName = "On Authoritative Melee Hit"))
	void BP_OnAuthoritativeMeleeHit();

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Capture", meta = (DisplayName = "On Capture State Changed"))
	void BP_OnCaptureStateChanged(EPHPropCaptureState NewState);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Capture", meta = (DisplayName = "On Capture Progress Changed"))
	void BP_OnCaptureProgressChanged(float DownedProgress, int32 HelperCount, float StruggleProgress);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Transformation", meta = (DisplayName = "On Prop Transformation Anticipated"))
	void BP_OnPropTransformationAnticipated(bool bReturningToInitial);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Transformation", meta = (DisplayName = "On Prop Transformation Confirmed"))
	void BP_OnPropTransformationConfirmed(EPHPropTransformationResult Result, const UPHPropFormDataAsset* ActiveForm);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Objective", meta = (DisplayName = "On Objective Interaction Anticipated"))
	void BP_OnObjectiveInteractionAnticipated(bool bStarting, const APHObjectiveActor* Objective);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Objective", meta = (DisplayName = "On Objective Interaction Confirmed"))
	void BP_OnObjectiveInteractionConfirmed(const APHObjectiveActor* Objective);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Human|Stamina", meta = (DisplayName = "On Human Stamina Changed"))
	void BP_OnHumanStaminaChanged(float Stamina, float NormalizedStamina);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Presentation|Human", meta = (DisplayName = "On Human Prototype Changed"))
	void BP_OnHumanPrototypeChanged(FName PrototypeName);

	virtual bool CanJumpInternal_Implementation() const override;
	virtual void OnJumped_Implementation() override;

private:
	UFUNCTION()
	void OnRep_MeleeHitSequence();

	UFUNCTION(Server, Reliable)
	void ServerRequestCaptureRescue(APHRetentionPoint* RequestedPoint);

	UFUNCTION(Server, Reliable)
	void ServerRequestCaptureAssist(APHPropCharacter* RequestedTarget);

	UFUNCTION(Server, Reliable)
	void ServerRequestStopCaptureAssist();

	UFUNCTION(Server, Unreliable)
	void ServerSubmitCarryStruggleInput(int8 Direction);

	UFUNCTION()
	void OnRep_CaptureState();

	UFUNCTION()
	void OnRep_CaptureProgress();

	UFUNCTION(Server, Reliable)
	void ServerRequestPropTransformation();

	UFUNCTION(Server, Reliable)
	void ServerRequestReturnToInitialForm();

	UFUNCTION(Server, Reliable)
	void ServerRequestStartObjectiveInteraction(APHObjectiveActor* RequestedObjective);

	UFUNCTION(Server, Reliable)
	void ServerRequestStopObjectiveInteraction();

	UFUNCTION()
	void OnRep_TransformationState();

	UFUNCTION()
	void OnRep_ActiveObjective();

	UFUNCTION()
	void OnRep_ActiveRetentionRescuePoint();

	UFUNCTION(Server, Reliable)
	void ServerSetHumanSprintRequested(bool bRequested);

	UFUNCTION(Server, Reliable)
	void ServerRequestSwitchHumanPrototype(APHHumanPrototypeSelector* RequestedSelector);

	UFUNCTION()
	void OnRep_HumanStamina();

	UFUNCTION()
	void OnRep_HumanPrototype();

	void StartHumanSprint();
	void StopHumanSprint();
	void SetHumanSprintRequested(bool bRequested);
	void RequestStopCaptureAssist();
	void HandleCaptureStruggleAxis(float Value);
	bool CanUseHumanSprint() const;
	void UpdateHumanStamina(float DeltaSeconds);
	void UpdateDownedRecovery(float DeltaSeconds);
	void ApplyMovementSpeed();
	void ConfigureHumanPresentation();
	void PlayHumanIdlePresentation();
	void UpdateLocomotionPresentation();
	void ApplyLocalViewMode();
	void PlayHumanAnimation(UAnimSequence* Animation, bool bFreezePose);
	bool TryResolveHumanPrototypeSelector(APHHumanPrototypeSelector*& OutSelector) const;
	USkeletalMesh* GetActiveHumanSkeletalMesh() const;
	UAnimSequence* GetActiveHumanIdleAnimation() const;
	UAnimSequence* GetActiveHumanWalkAnimation() const;
	UAnimSequence* GetActiveHumanDownedIdleAnimation() const;
	UAnimSequence* GetActiveHumanDownedMoveAnimation() const;
	UAnimSequence* GetActiveHumanCarriedAnimation() const;
	UAnimSequence* GetActiveHumanCarriedMoveAnimation() const;
	FVector GetActiveCarryAttachmentOffset() const;
	FRotator GetActiveCarryAttachmentRotation() const;
	FVector GetViewSelectionOrigin() const;
	bool CanRequestTransformation(double ServerTimeSeconds) const;
	double GetSafeTransformationCooldownSeconds() const;
	void PerformAuthoritativeTransformationRequest();
	void PerformAuthoritativeReturnRequest();
	void PublishTransformationResult(EPHPropTransformationResult Result, UPHPropFormDataAsset* ActiveForm);
	bool TryResolveTransformationTarget(APHPropTransformTarget*& OutTarget, EPHPropTransformationResult& OutFailure) const;
	bool TryApplyForm(UPHPropFormDataAsset* NewForm, const APHPropTransformTarget* CopiedTarget, EPHPropTransformationResult& OutFailure);
	bool TryFindPlacement(const FPHResolvedPropHitbox& Hitbox, float NewCapsuleHalfHeight, const APHPropTransformTarget* CopiedTarget, FVector& OutLocation) const;
	bool QueryPlacementBlockers(const FPHResolvedPropHitbox& Hitbox, const FVector& ActorLocation, TArray<AActor*>& OutBlockingActors) const;
	FPHResolvedPropHitbox ResolveHitbox(const UPHPropFormDataAsset* Form) const;
	void ApplyHitbox(const FPHResolvedPropHitbox& Hitbox);
	void ApplyTransformationState();
	bool TryResolveObjectiveTarget(APHObjectiveActor*& OutObjective) const;
	void PerformAuthoritativeStartObjectiveInteraction(APHObjectiveActor* RequestedObjective);
	void PerformAuthoritativeStopObjectiveInteraction();
	void RequestSurvivorInteraction();
	void StopSurvivorInteraction();
	bool TryResolveRetentionPoint(APHRetentionPoint*& OutRetentionPoint) const;
	bool TryResolveDownedRecoveryTarget(APHPropCharacter*& OutTarget) const;
	bool CanAssistDownedTarget(const APHPropCharacter& Target) const;
	bool HasCaptureLineOfSightTo(const APHPropCharacter& Target) const;
	void BeginAssistingDownedTarget(APHPropCharacter& Target);
	void StopAssistingDownedTarget();
	void StopRetentionRescue();
	void AddRecoveryHelper(APHPropCharacter& Helper);
	void RemoveRecoveryHelper(APHPropCharacter& Helper);
	void ClearRecoveryHelpers();
	void SubmitCarryStruggleInput(int8 Direction);
	void EscapeCarrierWithGrace();
	void ResetCaptureProgress();
	void SetCaptureState(EPHPropCaptureState NewState, float DurationSeconds = 0.0f);
	void ApplyCaptureState();
	void FinishGrace();
	void FinishRetention();
	void ClearCaptureRelationships();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> HumanFirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> ThirdPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> GrayboxPropBody;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMesh> HumanSkeletalMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> HumanIdleAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> HumanWalkAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> HumanDownedIdleAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> HumanDownedMoveAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> HumanCarriedAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> HumanCarriedMoveAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human", meta = (AllowPrivateAccess = "true"))
	FName PrimaryHumanPrototypeName;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Alternative", meta = (AllowPrivateAccess = "true"))
	FName AlternativeHumanPrototypeName;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Alternative", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMesh> AlternativeHumanSkeletalMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Alternative", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> AlternativeHumanIdleAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Alternative", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> AlternativeHumanWalkAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Alternative|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> AlternativeHumanDownedIdleAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Alternative|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> AlternativeHumanDownedMoveAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Alternative|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> AlternativeHumanCarriedAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Alternative|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimSequence> AlternativeHumanCarriedMoveAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Capture", meta = (AllowPrivateAccess = "true"))
	FVector PrimaryCarryAttachmentOffset;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Capture", meta = (AllowPrivateAccess = "true"))
	FRotator PrimaryCarryAttachmentRotation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Alternative|Capture", meta = (AllowPrivateAccess = "true"))
	FVector AlternativeCarryAttachmentOffset;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human|Alternative|Capture", meta = (AllowPrivateAccess = "true"))
	FRotator AlternativeCarryAttachmentRotation;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Presentation|Human", meta = (ClampMin = "0.0", ClampMax = "200.0", Units = "cm/s", AllowPrivateAccess = "true"))
	float HumanWalkAnimationThreshold;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Collision", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> PropBoxHitbox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Collision", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCapsuleComponent> PropCapsuleHitbox;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Camera", meta = (ClampMin = "150.0", ClampMax = "450.0", Units = "cm", AllowPrivateAccess = "true"))
	float CameraArmLength;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Camera", meta = (ClampMin = "4.0", ClampMax = "24.0", Units = "cm", AllowPrivateAccess = "true"))
	float CameraCollisionProbeSize;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Camera", meta = (ClampMin = "60.0", ClampMax = "120.0", Units = "deg", AllowPrivateAccess = "true"))
	float CameraFieldOfView;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Camera", meta = (AllowPrivateAccess = "true"))
	FVector CameraSocketOffset;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Camera", meta = (AllowPrivateAccess = "true"))
	FVector HumanFirstPersonCameraOffset;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Human|Stamina", meta = (ClampMin = "1.0", ClampMax = "500.0", AllowPrivateAccess = "true"))
	float MaximumHumanStamina;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Human|Stamina", meta = (ClampMin = "0.0", ClampMax = "1200.0", Units = "cm/s", AllowPrivateAccess = "true"))
	float HumanSprintSpeed;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Human|Stamina", meta = (ClampMin = "0.1", ClampMax = "200.0", AllowPrivateAccess = "true"))
	float HumanSprintDrainPerSecond;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Human|Stamina", meta = (ClampMin = "0.1", ClampMax = "200.0", AllowPrivateAccess = "true"))
	float HumanStaminaRechargePerSecond;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Human|Stamina", meta = (ClampMin = "0.0", ClampMax = "5.0", Units = "s", AllowPrivateAccess = "true"))
	float HumanStaminaRechargeDelay;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Human|Stamina", meta = (ClampMin = "0.0", ClampMax = "100.0", AllowPrivateAccess = "true"))
	float HumanJumpStaminaCost;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Transformation", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<UPHPropFormDataAsset>> AllowedPropForms;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Transformation", meta = (ClampMin = "100.0", ClampMax = "1000.0", Units = "cm", AllowPrivateAccess = "true"))
	float TransformationDistance;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Transformation", meta = (ClampMin = "0.1", ClampMax = "10.0", Units = "s", AllowPrivateAccess = "true"))
	float TransformationCooldownSeconds;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Transformation", meta = (ClampMin = "500.0", ClampMax = "5000.0", Units = "cm", AllowPrivateAccess = "true"))
	float TransformationSearchDistance;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Transformation", meta = (ClampMin = "2.0", ClampMax = "30.0", Units = "deg", AllowPrivateAccess = "true"))
	float TransformationTargetingHalfAngleDegrees;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Transformation", meta = (ClampMin = "0.0", ClampMax = "25.0", Units = "cm", AllowPrivateAccess = "true"))
	float MaximumPlacementAdjustment;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Transformation", meta = (ClampMin = "1.0", ClampMax = "25.0", Units = "cm", AllowPrivateAccess = "true"))
	float PlacementAdjustmentStep;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Objective", meta = (ClampMin = "250.0", ClampMax = "3000.0", Units = "cm", AllowPrivateAccess = "true"))
	float ObjectiveSearchDistance;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Objective", meta = (ClampMin = "2.0", ClampMax = "30.0", Units = "deg", AllowPrivateAccess = "true"))
	float ObjectiveTargetingHalfAngleDegrees;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture", meta = (ClampMin = "100.0", ClampMax = "500.0", Units = "cm", AllowPrivateAccess = "true"))
	float CaptureInteractionDistance;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture", meta = (ClampMin = "5.0", ClampMax = "180.0", Units = "s", AllowPrivateAccess = "true"))
	float FirstRetentionDuration;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture", meta = (ClampMin = "5.0", ClampMax = "180.0", Units = "s", AllowPrivateAccess = "true"))
	float SecondRetentionDuration;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture", meta = (ClampMin = "1", ClampMax = "5", AllowPrivateAccess = "true"))
	int32 MaximumRetentionCount;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture", meta = (ClampMin = "0.5", ClampMax = "20.0", Units = "s", AllowPrivateAccess = "true"))
	float GraceDuration;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture", meta = (ClampMin = "1.0", ClampMax = "2.0", AllowPrivateAccess = "true"))
	float GraceSpeedMultiplier;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture|Recovery", meta = (ClampMin = "50.0", ClampMax = "300.0", Units = "cm/s", AllowPrivateAccess = "true"))
	float DownedCrawlSpeed;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture|Recovery", meta = (ClampMin = "5.0", ClampMax = "120.0", Units = "s", AllowPrivateAccess = "true"))
	float DownedRecoveryDuration;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture|Recovery", meta = (ClampMin = "0.0", ClampMax = "3.0", AllowPrivateAccess = "true"))
	float AdditionalRecoveryContributionPerHelper;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture|Recovery", meta = (ClampMin = "1", ClampMax = "4", AllowPrivateAccess = "true"))
	int32 MaximumRecoveryHelpers;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture|Struggle", meta = (ClampMin = "6", ClampMax = "100", AllowPrivateAccess = "true"))
	int32 CarryStruggleRequiredAlternations;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture|Struggle", meta = (ClampMin = "0.05", ClampMax = "0.5", Units = "s", AllowPrivateAccess = "true"))
	float CarryStruggleMinimumInputInterval;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture|Struggle", meta = (ClampMin = "0.05", ClampMax = "0.5", AllowPrivateAccess = "true"))
	float CarryStruggleShoveProgressInterval;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture|Struggle", meta = (ClampMin = "25.0", ClampMax = "250.0", Units = "cm", AllowPrivateAccess = "true"))
	float CarryStruggleHunterShoveDistance;

	UPROPERTY(ReplicatedUsing = OnRep_MeleeHitSequence, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Melee", meta = (AllowPrivateAccess = "true"))
	int32 MeleeHitSequence;

	UPROPERTY(ReplicatedUsing = OnRep_TransformationState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Transformation", meta = (AllowPrivateAccess = "true"))
	FPHPropTransformationState TransformationState;

	UPROPERTY(ReplicatedUsing = OnRep_ActiveObjective, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Objective", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<APHObjectiveActor> ActiveObjective;

	UPROPERTY(ReplicatedUsing = OnRep_ActiveRetentionRescuePoint, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture|Rescue", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<APHRetentionPoint> ActiveRetentionRescuePoint;

	UPROPERTY(ReplicatedUsing = OnRep_HumanStamina, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Human|Stamina", meta = (AllowPrivateAccess = "true"))
	float CurrentHumanStamina;

	UPROPERTY(ReplicatedUsing = OnRep_CaptureState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	EPHPropCaptureState CaptureState;

	UPROPERTY(ReplicatedUsing = OnRep_CaptureState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	int32 RetentionCount;

	UPROPERTY(ReplicatedUsing = OnRep_CaptureState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	float CaptureStateEndServerTime;

	UPROPERTY(ReplicatedUsing = OnRep_CaptureState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<APHHunterCharacter> Carrier;

	UPROPERTY(ReplicatedUsing = OnRep_CaptureState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<APHRetentionPoint> RetentionPoint;

	UPROPERTY(ReplicatedUsing = OnRep_CaptureProgress, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture|Recovery", meta = (AllowPrivateAccess = "true"))
	float DownedRecoveryProgress;

	UPROPERTY(ReplicatedUsing = OnRep_CaptureProgress, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture|Recovery", meta = (AllowPrivateAccess = "true"))
	int32 RecoveryHelperCount;

	UPROPERTY(ReplicatedUsing = OnRep_CaptureProgress, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture|Struggle", meta = (AllowPrivateAccess = "true"))
	float CarryStruggleProgress;

	UPROPERTY(ReplicatedUsing = OnRep_HumanPrototype, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Presentation|Human", meta = (AllowPrivateAccess = "true"))
	bool bUseAlternativeHumanPrototype;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> InitialPresentationMesh;
	FVector InitialMeshRelativeLocation;
	FRotator InitialMeshRelativeRotation;
	FVector InitialMeshRelativeScale;
	float InitialCapsuleRadius;
	float InitialCapsuleHalfHeight;
	double LastLocalTransformationRequestTime;
	double LastServerTransformationRequestTime;
	double LastHumanStaminaUseServerTime;
	double LastLocalStruggleInputTime;
	double LastServerStruggleInputTime;
	float NormalWalkSpeed;
	bool bWantsHumanSprint;
	bool bPlayingWalkAnimation;
	bool bCurrentHumanAnimationFrozen;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CurrentHumanAnimation;

	int8 LastLocalStruggleDirection;
	int8 LastServerStruggleDirection;
	float NextCarryStruggleShoveProgress;
	float LastPublishedDownedRecoveryProgress;

	UPROPERTY(Transient)
	TObjectPtr<APHPropCharacter> AssistedDownedProp;

	TSet<TWeakObjectPtr<APHPropCharacter>> RecoveryHelpers;
	FTimerHandle CaptureStateTimerHandle;
};
