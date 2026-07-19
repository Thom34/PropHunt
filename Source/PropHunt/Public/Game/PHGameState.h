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
	int32 GetRequiredObjectiveCount() const { return RequiredObjectiveCount; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Match")
	int32 GetExpectedPlayerCount() const { return ExpectedPlayerCount; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Match")
	EPHMatchEndReason GetMatchEndReason() const { return MatchEndReason; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Results")
	FPHMatchResultsSnapshot GetMatchResultsSnapshot() const { return MatchResultsSnapshot; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Results")
	bool HasFinalMatchResults() const { return MatchResultsSnapshot.bFinalized; }

	void SetMatchPhase(EPHMatchPhase NewPhase, float DurationSeconds);
	void SetMatchSeed(int32 NewSeed);
	void SetCompletedObjectiveCount(int32 NewCount);
	void SetRequiredObjectiveCount(int32 NewCount);
	void SetExpectedPlayerCount(int32 NewCount);
	void SetMatchEndReason(EPHMatchEndReason NewReason);
	void SetMatchResultsSnapshot(const FPHMatchResultsSnapshot& NewSnapshot);
	void ClearMatchResultsSnapshot();

protected:
	UFUNCTION()
	void OnRep_MatchPhase(EPHMatchPhase PreviousPhase);

	UFUNCTION()
	void OnRep_MatchStateChanged();

	UFUNCTION()
	void OnRep_MatchResultsSnapshot();

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Match", meta = (DisplayName = "On Match Phase Changed"))
	void BP_OnMatchPhaseChanged(EPHMatchPhase PreviousPhase, EPHMatchPhase NewPhase);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Match", meta = (DisplayName = "On Match State Changed"))
	void BP_OnMatchStateChanged();

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Results", meta = (DisplayName = "On Match Results Changed"))
	void BP_OnMatchResultsChanged();

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
	int32 RequiredObjectiveCount;

	UPROPERTY(ReplicatedUsing = OnRep_MatchStateChanged, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Match", meta = (AllowPrivateAccess = "true"))
	int32 ExpectedPlayerCount;

	UPROPERTY(ReplicatedUsing = OnRep_MatchStateChanged, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Match", meta = (AllowPrivateAccess = "true"))
	EPHMatchEndReason MatchEndReason;

	UPROPERTY(ReplicatedUsing = OnRep_MatchResultsSnapshot, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Results", meta = (AllowPrivateAccess = "true"))
	FPHMatchResultsSnapshot MatchResultsSnapshot;
};
