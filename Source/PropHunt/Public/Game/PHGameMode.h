#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Game/PHMatchTypes.h"

#include "PHGameMode.generated.h"

class APHGameState;
class APHHunterCharacter;
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

	UFUNCTION(BlueprintPure, Category = "PropHunt|Match")
	const UPHMatchRulesDataAsset* GetMatchRules() const;

	void NotifyObjectiveCompleted(APHObjectiveActor& Objective);

protected:
	void TryStartMatchFlow();
	void AssignRole(APHPlayerState& NewPlayerState);
	void BeginPhase(EPHMatchPhase NewPhase);
	void AdvanceTimedPhase();
	void FinishMatch(EPHMatchEndReason EndReason);
	void PrepareObjectivesForMatch();
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
	UPROPERTY(Transient)
	TSet<TObjectPtr<APHObjectiveActor>> CompletedObjectives;
	bool bMatchFlowHasStarted;
#if !UE_BUILD_SHIPPING
	int32 TestMinimumPropPlayersOverride;
#endif
};
