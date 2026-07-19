#include "Game/PHMatchRulesDataAsset.h"

#define LOCTEXT_NAMESPACE "PHMatchRulesDataAsset"

UPHMatchRulesDataAsset::UPHMatchRulesDataAsset()
	: MinimumPropPlayers(1)
	, MaximumPropPlayers(4)
	, LobbyWaitDuration(60.0f)
	, LobbyReadyCountdownDuration(15.0f)
	, RosterTravelTimeoutDuration(30.0f)
	, PreparationDuration(10.0f)
	, HuntDuration(900.0f)
	, EscapeDuration(180.0f)
	, AllPropsRetainedEliminationDelay(5.0f)
	, ActiveObjectiveCount(5)
	, RequiredObjectiveCount(4)
	, ObjectiveScore(1000)
	, RescueScore(750)
	, HunterDownScore(500)
	, HunterRetentionScore(750)
	, HunterEliminationScore(1500)
{
}

bool UPHMatchRulesDataAsset::HasValidRules(FText* OutError) const
{
	auto Fail = [OutError](const FText& Error)
	{
		if (OutError != nullptr)
		{
			*OutError = Error;
		}
		return false;
	};

	if (MinimumPropPlayers < 1 || MaximumPropPlayers > 4 || MinimumPropPlayers > MaximumPropPlayers)
	{
		return Fail(LOCTEXT("InvalidPlayerCounts", "Prop player counts must satisfy 1 <= minimum <= maximum <= 4."));
	}

	if (LobbyWaitDuration < 0.0f
		|| LobbyReadyCountdownDuration < 15.0f || LobbyReadyCountdownDuration > 60.0f
		|| RosterTravelTimeoutDuration < 5.0f || RosterTravelTimeoutDuration > 120.0f
		|| PreparationDuration < 0.0f || HuntDuration <= 0.0f || EscapeDuration <= 0.0f
		|| !FMath::IsFinite(AllPropsRetainedEliminationDelay)
		|| AllPropsRetainedEliminationDelay < 0.1f || AllPropsRetainedEliminationDelay > 5.0f)
	{
		return Fail(LOCTEXT("InvalidDurations", "Match durations are invalid; the joinable ready countdown must remain between 15 and 60 seconds, roster travel timeout between 5 and 120 seconds, and all-Props-retained elimination between 0.1 and 5 seconds."));
	}

	if (ActiveObjectiveCount < 1 || RequiredObjectiveCount < 1 || RequiredObjectiveCount > ActiveObjectiveCount)
	{
		return Fail(LOCTEXT("InvalidObjectiveCounts", "Objective counts must satisfy 1 <= required <= active."));
	}

	if (ObjectiveScore < 0 || RescueScore < 0 || HunterDownScore < 0
		|| HunterRetentionScore < 0 || HunterEliminationScore < 0)
	{
		return Fail(LOCTEXT("InvalidScoreWeights", "Match score weights cannot be negative."));
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
