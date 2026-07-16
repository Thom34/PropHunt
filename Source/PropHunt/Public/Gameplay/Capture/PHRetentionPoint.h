#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PHRetentionPoint.generated.h"

class APHHunterCharacter;
class APHPropCharacter;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS(Blueprintable)
class PROPHUNT_API APHRetentionPoint : public AActor
{
	GENERATED_BODY()

public:
	APHRetentionPoint();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	APHPropCharacter* GetRetainedProp() const { return RetainedProp; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	float GetInteractionDistance() const { return InteractionDistance; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	float GetReleaseDuration() const { return ReleaseDuration; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	float GetReleaseProgressNormalized() const { return FMath::Clamp(ReleaseProgress, 0.0f, 1.0f); }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	APHPropCharacter* GetReleaseRescuer() const { return ReleaseRescuer; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Capture")
	FVector GetInteractionPoint() const;

	USceneComponent* GetRetentionAnchor() const { return RetentionAnchor; }

	bool CanHunterRetain(const APHHunterCharacter& Hunter, const APHPropCharacter& Prop) const;
	bool ServerTryRetain(APHHunterCharacter& Hunter, APHPropCharacter& Prop);
	bool ServerTryBeginRelease(APHPropCharacter& Rescuer);
	void ServerEndRelease(APHPropCharacter& Rescuer);
	void ClearRetainedProp(const APHPropCharacter* ExpectedProp);

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Capture", meta = (DisplayName = "On Retention State Changed"))
	void BP_OnRetentionStateChanged();

private:
	UFUNCTION()
	void OnRep_RetainedProp();

	UFUNCTION()
	void OnRep_ReleaseState();

	bool CanRescuerRelease(const APHPropCharacter& Rescuer) const;
	bool HasLineOfSightFrom(const AActor& Interactor) const;
	void CompleteRelease();
	void ResetReleaseState();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> GrayboxBody;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> RetentionPole;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTextRenderComponent> FrontLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTextRenderComponent> BackLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> RetentionAnchor;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture", meta = (ClampMin = "100.0", ClampMax = "500.0", Units = "cm", AllowPrivateAccess = "true"))
	float InteractionDistance;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Capture", meta = (ClampMin = "0.5", ClampMax = "10.0", Units = "s", AllowPrivateAccess = "true"))
	float ReleaseDuration;

	UPROPERTY(ReplicatedUsing = OnRep_RetainedProp, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<APHPropCharacter> RetainedProp;

	UPROPERTY(ReplicatedUsing = OnRep_ReleaseState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<APHPropCharacter> ReleaseRescuer;

	UPROPERTY(ReplicatedUsing = OnRep_ReleaseState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Capture", meta = (AllowPrivateAccess = "true"))
	float ReleaseProgress;
};
