#include "Online/PHOnlineSessionClient.h"

#include "Engine/GameInstance.h"
#include "Online/PHSocialInviteSubsystem.h"

void UPHOnlineSessionClient::OnSessionUserInviteAccepted(
	const bool bWasSuccessful,
	const int32 ControllerId,
	FUniqueNetIdPtr UserId,
	const FOnlineSessionSearchResult& SearchResult)
{
	(void)UserId;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UPHSocialInviteSubsystem* Social = GameInstance->GetSubsystem<UPHSocialInviteSubsystem>())
		{
			Social->AcceptSteamLobbyInvite(bWasSuccessful, ControllerId, SearchResult);
			return;
		}
	}
}
