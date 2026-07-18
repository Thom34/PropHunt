#pragma once

#include "CoreMinimal.h"

#include "PHMatchTypes.generated.h"

UENUM(BlueprintType)
enum class EPHMatchPhase : uint8
{
	Lobby UMETA(DisplayName = "Lobby"),
	Preparation UMETA(DisplayName = "Preparation"),
	Hunt UMETA(DisplayName = "Hunt"),
	Escape UMETA(DisplayName = "Escape"),
	Results UMETA(DisplayName = "Results")
};

UENUM(BlueprintType)
enum class EPHPlayerRole : uint8
{
	Unassigned UMETA(DisplayName = "Unassigned"),
	Hunter UMETA(DisplayName = "Hunter"),
	Prop UMETA(DisplayName = "Prop")
};

UENUM(BlueprintType)
enum class EPHMatchEndReason : uint8
{
	None UMETA(DisplayName = "None"),
	TimeExpired UMETA(DisplayName = "Time Expired"),
	HunterDisconnected UMETA(DisplayName = "Hunter Disconnected"),
	AllPropsGone UMETA(DisplayName = "All Props Gone"),
	AtLeastOnePropEscaped UMETA(DisplayName = "At Least One Prop Escaped")
};

UENUM(BlueprintType)
enum class EPHMatchPlayerOutcome : uint8
{
	Hunter UMETA(DisplayName = "Hunter"),
	Survived UMETA(DisplayName = "Survived"),
	Eliminated UMETA(DisplayName = "Eliminated"),
	Disconnected UMETA(DisplayName = "Disconnected")
};

USTRUCT(BlueprintType)
struct FPHPlayerMatchStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	int32 ObjectivesCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	int32 AlliesRescued = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	int32 HunterDowns = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	int32 HunterRetentions = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	int32 HunterEliminations = 0;
};

USTRUCT(BlueprintType)
struct FPHMatchResultRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	int32 PlayerId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	FString PlayerName;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	EPHPlayerRole PlayerRole = EPHPlayerRole::Unassigned;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	EPHMatchPlayerOutcome Outcome = EPHMatchPlayerOutcome::Disconnected;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	FPHPlayerMatchStats Stats;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	int32 TotalScore = 0;

	// Score actually eligible for persistent progression. A disconnected player
	// keeps their participation score and raw actions in the results, but earns no reward.
	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	int32 GrantedScore = 0;
};

USTRUCT(BlueprintType)
struct FPHMatchResultsSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	bool bFinalized = false;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	int32 MatchSeed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	EPHMatchEndReason EndReason = EPHMatchEndReason::None;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	int32 CompletedObjectiveCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Results")
	TArray<FPHMatchResultRow> PlayerRows;
};

namespace PHMatchResults
{
	inline int32 CalculateTotalScore(
		const FPHPlayerMatchStats& Stats,
		const int32 ObjectiveScore,
		const int32 RescueScore,
		const int32 DownScore,
		const int32 RetentionScore,
		const int32 EliminationScore)
	{
		const int64 WeightedScore =
			static_cast<int64>(FMath::Max(0, Stats.ObjectivesCompleted)) * FMath::Max(0, ObjectiveScore)
			+ static_cast<int64>(FMath::Max(0, Stats.AlliesRescued)) * FMath::Max(0, RescueScore)
			+ static_cast<int64>(FMath::Max(0, Stats.HunterDowns)) * FMath::Max(0, DownScore)
			+ static_cast<int64>(FMath::Max(0, Stats.HunterRetentions)) * FMath::Max(0, RetentionScore)
			+ static_cast<int64>(FMath::Max(0, Stats.HunterEliminations)) * FMath::Max(0, EliminationScore);
		return static_cast<int32>(FMath::Clamp<int64>(WeightedScore, 0, MAX_int32));
	}

	inline int32 CalculateGrantedScore(
		const int32 TotalScore,
		const EPHMatchPlayerOutcome Outcome)
	{
		return Outcome == EPHMatchPlayerOutcome::Disconnected
			? 0
			: FMath::Max(0, TotalScore);
	}
}

namespace PHMatchFlow
{
	inline int32 ChooseMostSeparatedStartIndex(
		const TArray<FVector>& StartLocations,
		const TArray<FVector>& OccupiedLocations)
	{
		if (StartLocations.IsEmpty())
		{
			return INDEX_NONE;
		}
		if (OccupiedLocations.IsEmpty())
		{
			return 0;
		}

		int32 BestIndex = 0;
		double BestClosestDistanceSquared = -1.0;
		for (int32 StartIndex = 0; StartIndex < StartLocations.Num(); ++StartIndex)
		{
			double ClosestDistanceSquared = MAX_dbl;
			for (const FVector& OccupiedLocation : OccupiedLocations)
			{
				ClosestDistanceSquared = FMath::Min(
					ClosestDistanceSquared,
					static_cast<double>(FVector::DistSquared2D(StartLocations[StartIndex], OccupiedLocation)));
			}
			if (ClosestDistanceSquared > BestClosestDistanceSquared)
			{
				BestIndex = StartIndex;
				BestClosestDistanceSquared = ClosestDistanceSquared;
			}
		}
		return BestIndex;
	}

	inline bool ShouldOpenEscape(const int32 CompletedObjectiveCount, const int32 RequiredObjectiveCount)
	{
		return RequiredObjectiveCount > 0 && CompletedObjectiveCount >= RequiredObjectiveCount;
	}

	inline int32 GetRemainingRoleCountAfterDeparture(
		const int32 CurrentRoleCount,
		const EPHPlayerRole DepartingRole,
		const EPHPlayerRole CountedRole)
	{
		return FMath::Max(0, CurrentRoleCount - (DepartingRole == CountedRole ? 1 : 0));
	}

	inline EPHMatchPhase GetNextTimedPhase(const EPHMatchPhase CurrentPhase)
	{
		switch (CurrentPhase)
		{
		case EPHMatchPhase::Preparation:
			return EPHMatchPhase::Hunt;
		case EPHMatchPhase::Hunt:
		case EPHMatchPhase::Escape:
			return EPHMatchPhase::Results;
		case EPHMatchPhase::Results:
			return EPHMatchPhase::Lobby;
		case EPHMatchPhase::Lobby:
		default:
			return EPHMatchPhase::Lobby;
		}
	}
}
