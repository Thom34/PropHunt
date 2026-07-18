#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Game/PHMatchTypes.h"

#include "PHGameMode.generated.h"

class APHGameState;
class APHHunterCharacter;
class APHExitGate;
class APHObjectiveActor;
class APHPropCharacter;
class APHPlayerState;
class UPHMatchRulesDataAsset;

UCLASS(Blueprintable)
class PROPHUNT_API APHGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APHGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Match")
	const UPHMatchRulesDataAsset* GetMatchRules() const;

	void NotifyObjectiveCompleted(APHObjectiveActor& Objective);
	void NotifyPropEliminated(APHPropCharacter& EliminatedProp);
	void NotifyPropRetained(APHPropCharacter& RetainedProp);
	void NotifyPropEscaped(APHPropCharacter& EscapedProp);

#if !UE_BUILD_SHIPPING
	void ForceEscapePhaseForSmoke();
#endif

protected:
	void TryStartMatchFlow();
	void FinishLobbyWait();
	void CancelLobbyWait();
	void AssignRole(APlayerController& NewPlayer);
	void BeginPhase(EPHMatchPhase NewPhase);
	void AdvanceTimedPhase();
	void ReturnPlayersToLobbyAfterResults();
	void FinishMatch(EPHMatchEndReason EndReason);
	void BuildAndPublishMatchResults(EPHMatchEndReason EndReason);
	void EvaluateAllRemainingPropsRetained(const APHPropCharacter* IgnoredProp = nullptr);
	void PrepareObjectivesForMatch();
	void PrepareExitGatesForMatch();
	void SetExitGatesEnabled(bool bEnabled);
	int32 CountPlayersWithRole(EPHPlayerRole DesiredRole) const;
	APHGameState* GetPHGameState() const;

private:
	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Match", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPHMatchRulesDataAsset> MatchRules;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Players", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<APHHunterCharacter> HunterPawnClass;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Players", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<APHPropCharacter> PropPawnClass;

	FTimerHandle PhaseTimerHandle;
	FTimerHandle LobbyWaitTimerHandle;
	UPROPERTY(Transient)
	TSet<TObjectPtr<APHObjectiveActor>> CompletedObjectives;

	UPROPERTY(Transient)
	TArray<FPHMatchResultRow> DepartedResultRows;
	bool bMatchFlowHasStarted;
	bool bLobbyReadyCountdownActive;
	int32 ExpectedPlayerCount;
	int32 EscapedPropCount;
#if !UE_BUILD_SHIPPING
	int32 TestMinimumPropPlayersOverride;
#endif
};
