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
	AllPropsGone UMETA(DisplayName = "All Props Gone")
};

namespace PHMatchFlow
{
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
