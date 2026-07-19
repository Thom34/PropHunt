#include "Game/PHPlayerState.h"

#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHPlayerState, Log, All);

APHPlayerState::APHPlayerState()
	: PlayerRole(EPHPlayerRole::Unassigned)
	, bLobbyReady(false)
{
	bReplicates = true;
}

void APHPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APHPlayerState, PlayerRole);
	DOREPLIFETIME(APHPlayerState, MatchStats);
	DOREPLIFETIME(APHPlayerState, bLobbyReady);
}

void APHPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	if (APHPlayerState* NewPlayerState = Cast<APHPlayerState>(PlayerState))
	{
		NewPlayerState->PlayerRole = PlayerRole;
		NewPlayerState->MatchStats = MatchStats;
		NewPlayerState->bLobbyReady = false;
	}
}

void APHPlayerState::SetPlayerRole(const EPHPlayerRole NewRole)
{
	if (!HasAuthority() || PlayerRole == NewRole)
	{
		return;
	}

	const EPHPlayerRole PreviousRole = PlayerRole;
	PlayerRole = NewRole;
	BP_OnPlayerRoleChanged(PreviousRole, PlayerRole);
	ForceNetUpdate();
}

void APHPlayerState::OnRep_PlayerRole(const EPHPlayerRole PreviousRole)
{
	UE_LOG(LogPHPlayerState, Log, TEXT("Replicated player role %d -> %d for %s."),
		static_cast<int32>(PreviousRole), static_cast<int32>(PlayerRole), *GetPlayerName());
	BP_OnPlayerRoleChanged(PreviousRole, PlayerRole);
}

void APHPlayerState::SetLobbyReady(const bool bNewLobbyReady)
{
	if (!HasAuthority() || bLobbyReady == bNewLobbyReady)
	{
		return;
	}

	bLobbyReady = bNewLobbyReady;
	ForceNetUpdate();
}

void APHPlayerState::ResetMatchStats()
{
	if (!HasAuthority())
	{
		return;
	}

	MatchStats = FPHPlayerMatchStats();
	OnRep_MatchStats();
	ForceNetUpdate();
}

namespace
{
void IncrementAuthoritativeStat(APHPlayerState& PlayerState, int32& Counter)
{
	if (!PlayerState.HasAuthority())
	{
		return;
	}

	Counter = Counter < MAX_int32 ? Counter + 1 : MAX_int32;
	PlayerState.ForceNetUpdate();
}
}

void APHPlayerState::RecordObjectiveCompletion()
{
	if (!HasAuthority())
	{
		return;
	}
	IncrementAuthoritativeStat(*this, MatchStats.ObjectivesCompleted);
	OnRep_MatchStats();
}

void APHPlayerState::RecordAllyRescue()
{
	if (!HasAuthority())
	{
		return;
	}
	IncrementAuthoritativeStat(*this, MatchStats.AlliesRescued);
	OnRep_MatchStats();
}

void APHPlayerState::RecordHunterDown()
{
	if (!HasAuthority())
	{
		return;
	}
	IncrementAuthoritativeStat(*this, MatchStats.HunterDowns);
	OnRep_MatchStats();
}

void APHPlayerState::RecordHunterRetention()
{
	if (!HasAuthority())
	{
		return;
	}
	IncrementAuthoritativeStat(*this, MatchStats.HunterRetentions);
	OnRep_MatchStats();
}

void APHPlayerState::RecordHunterElimination()
{
	if (!HasAuthority())
	{
		return;
	}
	IncrementAuthoritativeStat(*this, MatchStats.HunterEliminations);
	OnRep_MatchStats();
}

void APHPlayerState::OnRep_MatchStats()
{
	BP_OnMatchStatsChanged();
}
