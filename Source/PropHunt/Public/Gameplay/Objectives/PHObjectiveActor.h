#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PHObjectiveActor.generated.h"

class APHHunterCharacter;
class APHPropCharacter;
class USceneComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EPHObjectiveInterruptionPolicy : uint8
{
	Preserve UMETA(DisplayName = "Preserve Progress"),
	Reset UMETA(DisplayName = "Reset Progress"),
	Regress UMETA(DisplayName = "Regress Progress")
};

namespace PHObjectiveFlow
{
	inline bool IsStationaryForInteraction(
		const FVector& Velocity,
		const float MaximumHorizontalSpeed = 10.0f)
	{
		return Velocity.SizeSquared2D() <= FMath::Square(FMath::Max(0.0f, MaximumHorizontalSpeed));
	}

	inline float GetContributionMultiplier(
		const int32 InteractorCount,
		const float AdditionalInteractorContribution)
	{
		return InteractorCount > 0
			? 1.0f + static_cast<float>(InteractorCount - 1) * FMath::Clamp(AdditionalInteractorContribution, 0.0f, 1.0f)
			: 0.0f;
	}

	inline float AdvanceProgress(
		const float CurrentProgress,
		const float DeltaSeconds,
		const float DurationSeconds,
		const int32 InteractorCount,
		const float AdditionalInteractorContribution)
	{
		if (DeltaSeconds <= 0.0f || DurationSeconds <= 0.0f || InteractorCount <= 0)
		{
			return FMath::Clamp(CurrentProgress, 0.0f, 1.0f);
		}

		const float Contribution = GetContributionMultiplier(InteractorCount, AdditionalInteractorContribution);
		return FMath::Clamp(CurrentProgress + DeltaSeconds * Contribution / DurationSeconds, 0.0f, 1.0f);
	}

	inline float ApplyHunterMeleeRegression(
		const float CurrentProgress,
		const float RegressionFraction)
	{
		return FMath::Clamp(
			FMath::Clamp(CurrentProgress, 0.0f, 1.0f)
				- FMath::Clamp(RegressionFraction, 0.0f, 1.0f),
			0.0f,
			1.0f);
	}
}

UCLASS(Blueprintable)
class PROPHUNT_API APHObjectiveActor : public AActor
{
	GENERATED_BODY()

public:
	APHObjectiveActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Objective")
	float GetObjectiveProgress() const { return ObjectiveProgress; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Objective")
	FText GetObjectiveDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Objective")
	bool IsObjectiveCompleted() const { return bCompleted; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Objective")
	bool IsObjectiveActive() const { return bObjectiveActive; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Objective")
	int32 GetActiveInteractorCount() const { return ActiveInteractorCount; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Objective")
	float GetInteractionDurationSeconds() const { return InteractionDurationSeconds; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Objective")
	float GetInteractionDistance() const { return InteractionDistance; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Objective")
	EPHObjectiveInterruptionPolicy GetInterruptionPolicy() const { return InterruptionPolicy; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Objective|Hunter")
	float GetHunterMeleeRegressionFraction() const { return HunterMeleeRegressionFraction; }

	FVector GetInteractionPoint() const;
	bool CanPropInteract(const APHPropCharacter& PropCharacter) const;
	bool ServerTryBeginInteraction(APHPropCharacter& PropCharacter);
	void ServerEndInteraction(APHPropCharacter& PropCharacter);
	bool ServerApplyHunterMeleeRegression(APHHunterCharacter& HunterCharacter);
	void ResetForMatch(bool bShouldBeActive);

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Objective", meta = (DisplayName = "On Objective State Changed"))
	void BP_OnObjectiveStateChanged();

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Objective", meta = (DisplayName = "On Objective Completed"))
	void BP_OnObjectiveCompleted();

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Objective", meta = (DisplayName = "On Objective Hit By Hunter"))
	void BP_OnObjectiveHitByHunter(float RegressionAmount);

private:
	UFUNCTION()
	void OnRep_ObjectiveState();

	void RefreshPresentation();
	void RefreshInteractorCount();
	void CompleteObjective();
	void ApplyIdleProgressRule(float DeltaSeconds);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Objective", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Objective", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> ObjectiveBody;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Objective|Presentation", meta = (AllowPrivateAccess = "true"))
	FText ObjectiveDisplayName;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Objective", meta = (ClampMin = "1.0", ClampMax = "120.0", Units = "s", AllowPrivateAccess = "true"))
	float InteractionDurationSeconds;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Objective", meta = (ClampMin = "100.0", ClampMax = "500.0", Units = "cm", AllowPrivateAccess = "true"))
	float InteractionDistance;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Objective", meta = (ClampMin = "1", ClampMax = "4", AllowPrivateAccess = "true"))
	int32 MaximumConcurrentInteractors;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Objective", meta = (ClampMin = "0.0", ClampMax = "1.0", AllowPrivateAccess = "true"))
	float AdditionalInteractorContribution;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Objective", meta = (AllowPrivateAccess = "true"))
	EPHObjectiveInterruptionPolicy InterruptionPolicy;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Objective", meta = (ClampMin = "0.0", ClampMax = "1.0", AllowPrivateAccess = "true"))
	float ProgressRegressionPerSecond;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Objective|Hunter", meta = (ClampMin = "0.01", ClampMax = "0.5", AllowPrivateAccess = "true"))
	float HunterMeleeRegressionFraction;

	UPROPERTY(ReplicatedUsing = OnRep_ObjectiveState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Objective", meta = (AllowPrivateAccess = "true"))
	float ObjectiveProgress;

	UPROPERTY(ReplicatedUsing = OnRep_ObjectiveState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Objective", meta = (AllowPrivateAccess = "true"))
	bool bObjectiveActive;

	UPROPERTY(ReplicatedUsing = OnRep_ObjectiveState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Objective", meta = (AllowPrivateAccess = "true"))
	bool bCompleted;

	UPROPERTY(ReplicatedUsing = OnRep_ObjectiveState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Objective", meta = (AllowPrivateAccess = "true"))
	int32 ActiveInteractorCount;

	TSet<TWeakObjectPtr<APHPropCharacter>> ActiveInteractors;
};
