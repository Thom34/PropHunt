#include "Game/PHGameState.h"

#include "Game/PHPlayerController.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHGameState, Log, All);

APHGameState::APHGameState()
	: MatchPhase(EPHMatchPhase::Lobby)
	, PhaseEndServerTime(0.0f)
	, MatchSeed(0)
	, CompletedObjectiveCount(0)
	, RequiredObjectiveCount(0)
	, ExpectedPlayerCount(0)
	, MatchEndReason(EPHMatchEndReason::None)
{
	bReplicates = true;
}

void APHGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APHGameState, MatchPhase);
	DOREPLIFETIME(APHGameState, PhaseEndServerTime);
	DOREPLIFETIME(APHGameState, MatchSeed);
	DOREPLIFETIME(APHGameState, CompletedObjectiveCount);
	DOREPLIFETIME(APHGameState, RequiredObjectiveCount);
	DOREPLIFETIME(APHGameState, ExpectedPlayerCount);
	DOREPLIFETIME(APHGameState, MatchEndReason);
	DOREPLIFETIME(APHGameState, MatchResultsSnapshot);
}

float APHGameState::GetRemainingPhaseTime() const
{
	if (PhaseEndServerTime <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Max(0.0f, PhaseEndServerTime - GetServerWorldTimeSeconds());
}

void APHGameState::SetMatchPhase(const EPHMatchPhase NewPhase, const float DurationSeconds)
{
	if (!HasAuthority())
	{
		return;
	}

	const EPHMatchPhase PreviousPhase = MatchPhase;
	MatchPhase = NewPhase;
	PhaseEndServerTime = DurationSeconds > 0.0f ? GetServerWorldTimeSeconds() + DurationSeconds : 0.0f;
	BP_OnMatchPhaseChanged(PreviousPhase, MatchPhase);
	BP_OnMatchStateChanged();
	ForceNetUpdate();
}

void APHGameState::SetMatchSeed(const int32 NewSeed)
{
	if (HasAuthority() && MatchSeed != NewSeed)
	{
		MatchSeed = NewSeed;
		BP_OnMatchStateChanged();
		ForceNetUpdate();
	}
}

void APHGameState::SetCompletedObjectiveCount(const int32 NewCount)
{
	if (HasAuthority() && CompletedObjectiveCount != NewCount)
	{
		CompletedObjectiveCount = FMath::Max(0, NewCount);
		BP_OnMatchStateChanged();
		ForceNetUpdate();
	}
}

void APHGameState::SetRequiredObjectiveCount(const int32 NewCount)
{
	if (HasAuthority() && RequiredObjectiveCount != NewCount)
	{
		RequiredObjectiveCount = FMath::Max(0, NewCount);
		BP_OnMatchStateChanged();
		ForceNetUpdate();
	}
}

void APHGameState::SetExpectedPlayerCount(const int32 NewCount)
{
	if (HasAuthority() && ExpectedPlayerCount != NewCount)
	{
		ExpectedPlayerCount = FMath::Max(0, NewCount);
		BP_OnMatchStateChanged();
		ForceNetUpdate();
	}
}

void APHGameState::SetMatchEndReason(const EPHMatchEndReason NewReason)
{
	if (HasAuthority() && MatchEndReason != NewReason)
	{
		MatchEndReason = NewReason;
		BP_OnMatchStateChanged();
		ForceNetUpdate();
	}
}

void APHGameState::SetMatchResultsSnapshot(const FPHMatchResultsSnapshot& NewSnapshot)
{
	if (!HasAuthority())
	{
		return;
	}

	MatchResultsSnapshot = NewSnapshot;
	OnRep_MatchResultsSnapshot();
	ForceNetUpdate();
}

void APHGameState::ClearMatchResultsSnapshot()
{
	if (!HasAuthority())
	{
		return;
	}

	MatchResultsSnapshot = FPHMatchResultsSnapshot();
	OnRep_MatchResultsSnapshot();
	ForceNetUpdate();
}

void APHGameState::OnRep_MatchPhase(const EPHMatchPhase PreviousPhase)
{
	UE_LOG(LogPHGameState, Log, TEXT("Replicated match phase %d -> %d (%.1f seconds remaining)."),
		static_cast<int32>(PreviousPhase), static_cast<int32>(MatchPhase), GetRemainingPhaseTime());
	BP_OnMatchPhaseChanged(PreviousPhase, MatchPhase);
}

void APHGameState::OnRep_MatchStateChanged()
{
	BP_OnMatchStateChanged();
}

void APHGameState::OnRep_MatchResultsSnapshot()
{
	BP_OnMatchResultsChanged();

	// The reliable controller RPC remains the primary path. Replication gives
	// clients a second authoritative route if the connection closes while the
	// dedicated worker is returning to its waiting room.
	if (MatchResultsSnapshot.bFinalized && GetWorld() != nullptr)
	{
		if (APHPlayerController* LocalController =
			Cast<APHPlayerController>(GetWorld()->GetFirstPlayerController()))
		{
			LocalController->HandleReplicatedFinalResults(MatchResultsSnapshot);
		}
	}
}
