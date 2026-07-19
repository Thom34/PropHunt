#pragma once

#include "CoreMinimal.h"
#include "OnlineSessionClient.h"

#include "PHOnlineSessionClient.generated.h"

/** Routes Steam lobby invitations into the social party without client travel. */
UCLASS()
class PROPHUNT_API UPHOnlineSessionClient : public UOnlineSessionClient
{
	GENERATED_BODY()

public:
	virtual void OnSessionUserInviteAccepted(
		bool bWasSuccessful,
		int32 ControllerId,
		FUniqueNetIdPtr UserId,
		const FOnlineSessionSearchResult& SearchResult) override;
};
