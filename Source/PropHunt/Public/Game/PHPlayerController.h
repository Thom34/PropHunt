#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Game/PHMatchTypes.h"

#include "PHPlayerController.generated.h"

class APHPropCharacter;
class APHObjectiveActor;
class APHExitGate;
class UPHHumanStaminaWidget;
class UPHInGameMenuWidget;
class UPHMatchResultsWidget;
class UPHMatchmakingWidget;

UCLASS(Blueprintable, Config=Game)
class PROPHUNT_API APHPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	APHPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnRep_Pawn() override;
	virtual void SetupInputComponent() override;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Player")
	EPHPlayerRole GetControlledPlayerRole() const;

	void DismissLocalMatchmakingWidget();
	void CloseInGameMenu();
	void HandlePropCaptureStateChanged(APHPropCharacter& ChangedProp);

	UFUNCTION(Client, Reliable)
	void ClientReturnToLobbyAfterMatch(
		const FPHMatchResultsSnapshot& FinalResults,
		EPHPlayerRole LocalPlayerRole,
		const FString& LocalPlayerName,
		int32 LocalPlayerId);

	UFUNCTION(BlueprintPure, Category = "PropHunt|Spectator")
	bool IsSpectatingSurvivor() const { return bSpectatingProps && SpectatedProp.IsValid(); }

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Presentation", meta = (DisplayName = "On Controlled Pawn Changed"))
	void BP_OnControlledPawnChanged(APawn* NewPawn, EPHPlayerRole PlayerRole);

private:
	void NotifyControlledPawnChanged();
	void EnsureLocalHumanStaminaWidget();
	void EnsureLocalMatchResultsWidget();
	void EnsureLocalMatchmakingWidget();
	void ToggleInGameMenu();
	void SpectateNextSurvivor();
	void RefreshSpectatorTarget(bool bAdvance);
	void ToggleLobbyReady();

	UFUNCTION(Server, Reliable)
	void ServerSetLobbyReady(bool bReady);

	UFUNCTION(Server, Reliable)
	void ServerRegisterCaptureSmokeRole(uint8 SmokeRole);

	UFUNCTION(Client, Reliable)
	void ClientPrepareCaptureSmokeVictim();

	UFUNCTION(Client, Reliable)
	void ClientVerifyCaptureSmokeCameraAndSound();

	UFUNCTION(Server, Reliable)
	void ServerReportCaptureSmokeClientChecks(bool bJumpSucceeded, bool bCameraSucceeded, bool bSoundSucceeded);

	UFUNCTION(Client, Reliable)
	void ClientBeginCaptureSmokeRescue();

	UFUNCTION(Client, Reliable)
	void ClientBeginCaptureSmokeStruggle(uint8 InputCount);

	UFUNCTION(Client, Reliable)
	void ClientVerifySpectatorSmoke();

	UFUNCTION(Client, Reliable)
	void ClientVerifyExitGateSmoke(APHExitGate* FirstGate, APHExitGate* SecondGate, bool bExpectOpen);

	UPROPERTY(Transient)
	TObjectPtr<UPHHumanStaminaWidget> HumanStaminaWidget;

	UPROPERTY(Transient)
	TObjectPtr<UPHMatchmakingWidget> MatchmakingWidget;

	UPROPERTY(Config, EditDefaultsOnly, Category = "PropHunt|UI")
	TSoftClassPtr<UPHMatchmakingWidget> MatchmakingWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UPHMatchResultsWidget> MatchResultsWidget;

	UPROPERTY(Transient)
	TObjectPtr<UPHInGameMenuWidget> InGameMenuWidget;

	TWeakObjectPtr<APHPropCharacter> SpectatedProp;
	bool bSpectatingProps = false;
	double LastLobbyReadyRequestTimeSeconds = -1.0;

#if !UE_BUILD_SHIPPING
	void SeedMatchResultsPreview();
	void TryRunObjectiveSmoke();
	void FinishObjectiveSmoke();
	void ReportObjectiveSmoke();
	void TryRunObjectiveRegressionSmoke();
	void FinishObjectiveRegressionInteraction();
	void ReportObjectiveRegressionSmoke();

	void TryRegisterCaptureSmokeRole();
	void TryRunCaptureSmokeHost();
	void RunCaptureSmokeMelee();
	void RunCaptureSmokeVerifyHunterJump();
	void RunCaptureSmokePrepareCameraAndSound();
	void RunCaptureSmokeCarry();
	void RunCaptureSmokeVerifyCarryMove();
	void RunCaptureSmokeVerifyFreeShove();
	void RunCaptureSmokeVerifyDropProgress();
	void RunCaptureSmokeVerifyRepickProgress();
	void RunCaptureSmokeRetain();
	void RunCaptureSmokeRelease();
	void ReportCaptureSmokeHost();
	void RunCaptureSmokeMemento();
	void ReportCaptureSmokeMemento();
	void ReportCaptureSmokeClient();
	void RunCaptureSmokeStruggleInput();
	APHPlayerController* FindCaptureSmokeController(uint8 SmokeRole) const;
	void TryRunSpectatorSmokeHost();
	void VerifySpectatorSmokeHost();
	void FinishSpectatorSmokeHost();
	void ReportSpectatorSmokeHost();
	void TryRunExitGateSmokeHost();
	void VerifyExitGateSmokeMidpoint();
	void VerifyExitGateSmokeOpened();
	void VerifyFirstExitGateSmokeEscape();

	FTimerHandle ObjectiveSmokeRetryTimer;
	FTimerHandle ObjectiveSmokeFinishTimer;
	FTimerHandle CaptureSmokeTimer;
	FTimerHandle CaptureSmokeInputTimer;
	int32 ObjectiveSmokeRetryCount = 0;
	float ObjectiveRegressionProgressBeforeHit = 0.0f;
	TWeakObjectPtr<APHObjectiveActor> ObjectiveRegressionTarget;
	int32 CaptureSmokeRetryCount = 0;
	uint8 CaptureSmokeRole = 0;
	uint8 RequestedCaptureSmokeRole = 0;
	uint8 CaptureSmokeStruggleInputsRemaining = 0;
	int8 CaptureSmokeLastInjectedDirection = 0;
	bool bCaptureSmokeFreeShoveSucceeded = false;
	bool bCaptureSmokeProtectedShoveSucceeded = false;
	bool bCaptureSmokeCarryMoveSucceeded = false;
	bool bCaptureSmokeDropProgressSucceeded = false;
	bool bCaptureSmokeRepickProgressSucceeded = false;
	bool bCaptureSmokeHunterJumpSucceeded = false;
	bool bCaptureSmokeVictimJumpSucceeded = false;
	bool bCaptureSmokeCameraSucceeded = false;
	bool bCaptureSmokeSoundSucceeded = false;
	bool bCaptureSmokeToyBrickSucceeded = false;
	bool bCaptureSmokeClientJumpSucceeded = false;
	float CaptureSmokeHunterJumpStartZ = 0.0f;
	float CaptureSmokeClientJumpStartZ = 0.0f;
	float CaptureSmokeRecoveryBeforeDrop = 0.0f;
	float CaptureSmokeStruggleBeforeDrop = 0.0f;
	float CaptureSmokeRecoveryBeforeRepick = 0.0f;
	float CaptureSmokeStruggleBeforeRepick = 0.0f;
	FVector CaptureSmokeHunterLocationBeforeStruggle = FVector::ZeroVector;
	FVector CaptureSmokeHunterLocationNearRetention = FVector::ZeroVector;
	FVector CaptureSmokeMementoStartLocation = FVector::ZeroVector;
	TWeakObjectPtr<APHPropCharacter> CaptureSmokeVictim;
	TWeakObjectPtr<APHPropCharacter> ExitGateSmokeSecondProp;
	TWeakObjectPtr<APHExitGate> ExitGateSmokeFirstGate;
	TWeakObjectPtr<APHExitGate> ExitGateSmokeSecondGate;
#endif
};
