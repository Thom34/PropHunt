#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PHExitGate.generated.h"

class APHPropCharacter;
class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class PROPHUNT_API APHExitGate : public AActor
{
	GENERATED_BODY()

public:
	APHExitGate();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Escape")
	bool IsGateEnabled() const { return bGateEnabled; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Escape")
	bool IsGateOpen() const { return bGateOpen; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Escape")
	float GetOpenProgress() const { return OpenProgress; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Escape")
	float GetOpenDurationSeconds() const { return OpenDurationSeconds; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Escape")
	float GetInteractionDistance() const { return InteractionDistance; }

	FVector GetInteractionPoint() const;
	bool IsWithinInteractionRange(const FVector& CharacterLocation) const;
	bool CanPropInteract(const APHPropCharacter& PropCharacter) const;
	bool ServerTryBeginInteraction(APHPropCharacter& PropCharacter);
	void ServerEndInteraction(APHPropCharacter& PropCharacter);
	void ResetForMatch();
	void SetGateEnabled(bool bEnabled);

#if !UE_BUILD_SHIPPING
	FVector GetExitVolumeLocationForSmoke() const;
#endif

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Escape", meta = (DisplayName = "On Exit Gate State Changed"))
	void BP_OnExitGateStateChanged();

private:
	UFUNCTION()
	void OnRep_GateState();

	UFUNCTION()
	void HandleExitVolumeBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	void RefreshPresentation();
	void RefreshInteractorCount();
	void CompleteOpening();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> FrameLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> FrameRight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> FrameTop;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> DoorLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> DoorRight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> ControlBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> ExitVolume;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (ClampMin = "1.0", ClampMax = "120.0", Units = "s", AllowPrivateAccess = "true"))
	float OpenDurationSeconds;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (ClampMin = "100.0", ClampMax = "500.0", Units = "cm", AllowPrivateAccess = "true"))
	float InteractionDistance;

	UPROPERTY(ReplicatedUsing = OnRep_GateState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (AllowPrivateAccess = "true"))
	float OpenProgress;

	UPROPERTY(ReplicatedUsing = OnRep_GateState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (AllowPrivateAccess = "true"))
	bool bGateEnabled;

	UPROPERTY(ReplicatedUsing = OnRep_GateState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (AllowPrivateAccess = "true"))
	bool bGateOpen;

	UPROPERTY(ReplicatedUsing = OnRep_GateState, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Escape", meta = (AllowPrivateAccess = "true"))
	int32 ActiveInteractorCount;

	TSet<TWeakObjectPtr<APHPropCharacter>> ActiveInteractors;
};
