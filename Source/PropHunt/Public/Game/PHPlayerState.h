#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Game/PHMatchTypes.h"

#include "PHPlayerState.generated.h"

UCLASS(Blueprintable)
class PROPHUNT_API APHPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	APHPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* PlayerState) override;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Player")
	EPHPlayerRole GetPlayerRole() const { return PlayerRole; }

	void SetPlayerRole(EPHPlayerRole NewRole);

	UFUNCTION(BlueprintPure, Category = "PropHunt|Lobby")
	bool IsLobbyReady() const { return bLobbyReady; }

	void SetLobbyReady(bool bNewLobbyReady);

	UFUNCTION(BlueprintPure, Category = "PropHunt|Results")
	const FPHPlayerMatchStats& GetMatchStats() const { return MatchStats; }

	void ResetMatchStats();
	void RecordObjectiveCompletion();
	void RecordAllyRescue();
	void RecordHunterDown();
	void RecordHunterRetention();
	void RecordHunterElimination();

protected:
	UFUNCTION()
	void OnRep_PlayerRole(EPHPlayerRole PreviousRole);

	UFUNCTION()
	void OnRep_MatchStats();

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Player", meta = (DisplayName = "On Player Role Changed"))
	void BP_OnPlayerRoleChanged(EPHPlayerRole PreviousRole, EPHPlayerRole NewRole);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Results", meta = (DisplayName = "On Match Stats Changed"))
	void BP_OnMatchStatsChanged();

private:
	UPROPERTY(ReplicatedUsing = OnRep_PlayerRole, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Player", meta = (AllowPrivateAccess = "true"))
	EPHPlayerRole PlayerRole;

	UPROPERTY(ReplicatedUsing = OnRep_MatchStats, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Results", meta = (AllowPrivateAccess = "true"))
	FPHPlayerMatchStats MatchStats;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Lobby", meta = (AllowPrivateAccess = "true"))
	bool bLobbyReady;
};
