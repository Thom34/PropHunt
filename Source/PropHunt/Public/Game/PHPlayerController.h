#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Game/PHMatchTypes.h"

#include "PHPlayerController.generated.h"

class APHPropCharacter;
class UPHHumanStaminaWidget;

UCLASS(Blueprintable)
class PROPHUNT_API APHPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	APHPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnRep_Pawn() override;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Player")
	EPHPlayerRole GetControlledPlayerRole() const;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Presentation", meta = (DisplayName = "On Controlled Pawn Changed"))
	void BP_OnControlledPawnChanged(APawn* NewPawn, EPHPlayerRole PlayerRole);

private:
	void NotifyControlledPawnChanged();
	void EnsureLocalHumanStaminaWidget();

	UFUNCTION(Server, Reliable)
	void ServerRegisterCaptureSmokeRole(uint8 SmokeRole);

	UFUNCTION(Client, Reliable)
	void ClientPrepareCaptureSmokeVictim();

	UFUNCTION(Client, Reliable)
	void ClientBeginCaptureSmokeRescue();

	UFUNCTION(Client, Reliable)
	void ClientBeginCaptureSmokeStruggle(uint8 InputCount);

	UPROPERTY(Transient)
	TObjectPtr<UPHHumanStaminaWidget> HumanStaminaWidget;

#if !UE_BUILD_SHIPPING
	void TryRunObjectiveSmoke();
	void FinishObjectiveSmoke();
	void ReportObjectiveSmoke();

	void TryRegisterCaptureSmokeRole();
	void TryRunCaptureSmokeHost();
	void RunCaptureSmokeMelee();
	void RunCaptureSmokeCarry();
	void RunCaptureSmokeVerifyCarryMove();
	void RunCaptureSmokeVerifyFreeShove();
	void RunCaptureSmokeRetain();
	void RunCaptureSmokeRelease();
	void ReportCaptureSmokeHost();
	void ReportCaptureSmokeClient();
	void RunCaptureSmokeStruggleInput();
	APHPlayerController* FindCaptureSmokeController(uint8 SmokeRole) const;

	FTimerHandle ObjectiveSmokeRetryTimer;
	FTimerHandle ObjectiveSmokeFinishTimer;
	FTimerHandle CaptureSmokeTimer;
	FTimerHandle CaptureSmokeInputTimer;
	int32 ObjectiveSmokeRetryCount = 0;
	int32 CaptureSmokeRetryCount = 0;
	uint8 CaptureSmokeRole = 0;
	uint8 RequestedCaptureSmokeRole = 0;
	uint8 CaptureSmokeStruggleInputsRemaining = 0;
	int8 CaptureSmokeLastInjectedDirection = 0;
	bool bCaptureSmokeFreeShoveSucceeded = false;
	bool bCaptureSmokeProtectedShoveSucceeded = false;
	bool bCaptureSmokeCarryMoveSucceeded = false;
	FVector CaptureSmokeHunterLocationBeforeStruggle = FVector::ZeroVector;
	FVector CaptureSmokeHunterLocationNearRetention = FVector::ZeroVector;
	TWeakObjectPtr<APHPropCharacter> CaptureSmokeVictim;
#endif
};
