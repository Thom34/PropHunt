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
	float PreparationDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Timing", meta = (ClampMin = "1.0", Units = "s"))
	float HuntDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Timing", meta = (ClampMin = "0.0", Units = "s"))
	float ResultsDuration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Objectives", meta = (ClampMin = "1"))
	int32 ActiveObjectiveCount;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|Match|Objectives", meta = (ClampMin = "1"))
	int32 RequiredObjectiveCount;

	bool HasValidRules(FText* OutError = nullptr) const;
};
