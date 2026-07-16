#include "Game/PHMatchRulesDataAsset.h"

#define LOCTEXT_NAMESPACE "PHMatchRulesDataAsset"

UPHMatchRulesDataAsset::UPHMatchRulesDataAsset()
	: MinimumPropPlayers(1)
	, MaximumPropPlayers(4)
	, PreparationDuration(10.0f)
	, HuntDuration(900.0f)
	, ResultsDuration(10.0f)
	, ActiveObjectiveCount(5)
	, RequiredObjectiveCount(4)
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

	if (PreparationDuration < 0.0f || HuntDuration <= 0.0f || ResultsDuration < 0.0f)
	{
		return Fail(LOCTEXT("InvalidDurations", "Match durations must be non-negative and Hunt Duration must be greater than zero."));
	}

	if (ActiveObjectiveCount < 1 || RequiredObjectiveCount < 1 || RequiredObjectiveCount > ActiveObjectiveCount)
	{
		return Fail(LOCTEXT("InvalidObjectiveCounts", "Objective counts must satisfy 1 <= required <= active."));
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
