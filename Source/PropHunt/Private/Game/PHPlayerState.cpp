#include "Game/PHPlayerState.h"

#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHPlayerState, Log, All);

APHPlayerState::APHPlayerState()
	: PlayerRole(EPHPlayerRole::Unassigned)
{
	bReplicates = true;
}

void APHPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APHPlayerState, PlayerRole);
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
