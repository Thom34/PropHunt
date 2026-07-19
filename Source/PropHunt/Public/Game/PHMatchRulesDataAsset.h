#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "PHMatchRulesDataAsset.generated.h"

UCLASS(BlueprintType, Const)
class PROPHUNT_API UPHMatchRulesDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPHMatchRulesDataAsset();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Players", meta = (ClampMin = "1", ClampMax = "4"))
	int32 MinimumPropPlayers;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Players", meta = (ClampMin = "1", ClampMax = "4"))
	int32 MaximumPropPlayers;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Timing", meta = (ClampMin = "0.0", Units = "s"))
	float LobbyWaitDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Timing", meta = (ClampMin = "15.0", ClampMax = "60.0", Units = "s"))
	float LobbyReadyCountdownDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Timing", meta = (ClampMin = "5.0", ClampMax = "120.0", Units = "s"))
	float RosterTravelTimeoutDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Timing", meta = (ClampMin = "0.0", Units = "s"))
	float PreparationDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Timing", meta = (ClampMin = "1.0", Units = "s"))
	float HuntDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Timing", meta = (ClampMin = "1.0", Units = "s"))
	float EscapeDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Timing", meta = (ClampMin = "0.1", ClampMax = "5.0", Units = "s"))
	float AllPropsRetainedEliminationDelay;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Objectives", meta = (ClampMin = "1"))
	int32 ActiveObjectiveCount;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Objectives", meta = (ClampMin = "1"))
	int32 RequiredObjectiveCount;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Score", meta = (ClampMin = "0"))
	int32 ObjectiveScore;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Score", meta = (ClampMin = "0"))
	int32 RescueScore;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Score", meta = (ClampMin = "0"))
	int32 HunterDownScore;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Score", meta = (ClampMin = "0"))
	int32 HunterRetentionScore;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Score", meta = (ClampMin = "0"))
	int32 HunterEliminationScore;

	bool HasValidRules(FText* OutError = nullptr) const;
};
