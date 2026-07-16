#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Game/PHMatchTypes.h"

#include "PHGameState.generated.h"

UCLASS(Blueprintable)
class PROPHUNT_API APHGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	APHGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Match")
	EPHMatchPhase GetMatchPhase() const { return MatchPhase; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Match")
	float GetRemainingPhaseTime() const;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Match")
	int32 GetMatchSeed() const { return MatchSeed; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Match")
	int32 GetCompletedObjectiveCount() const { return CompletedObjectiveCount; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Match")
	EPHMatchEndReason GetMatchEndReason() const { return MatchEndReason; }

	void SetMatchPhase(EPHMatchPhase NewPhase, float DurationSeconds);
	void SetMatchSeed(int32 NewSeed);
	void SetCompletedObjectiveCount(int32 NewCount);
	void SetMatchEndReason(EPHMatchEndReason NewReason);

protected:
	UFUNCTION()
	void OnRep_MatchPhase(EPHMatchPhase PreviousPhase);

	UFUNCTION()
	void OnRep_MatchStateChanged();

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Match", meta = (DisplayName = "On Match Phase Changed"))
	void BP_OnMatchPhaseChanged(EPHMatchPhase PreviousPhase, EPHMatchPhase NewPhase);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Match", meta = (DisplayName = "On Match State Changed"))
	void BP_OnMatchStateChanged();

private:
	UPROPERTY(ReplicatedUsing = OnRep_MatchPhase, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Match", meta = (AllowPrivateAccess = "true"))
	EPHMatchPhase MatchPhase;

	UPROPERTY(ReplicatedUsing = OnRep_MatchStateChanged, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Match", meta = (AllowPrivateAccess = "true"))
	float PhaseEndServerTime;

	UPROPERTY(ReplicatedUsing = OnRep_MatchStateChanged, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Match", meta = (AllowPrivateAccess = "true"))
	int32 MatchSeed;

	UPROPERTY(ReplicatedUsing = OnRep_MatchStateChanged, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Match", meta = (AllowPrivateAccess = "true"))
	int32 CompletedObjectiveCount;

	UPROPERTY(ReplicatedUsing = OnRep_MatchStateChanged, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Match", meta = (AllowPrivateAccess = "true"))
	EPHMatchEndReason MatchEndReason;
};
