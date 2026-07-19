#include "UI/PHSocialFriendButton.h"

void UPHSocialFriendButton::InitializeFriend(const FString& InFriendUserId)
{
	FriendUserId = InFriendUserId;
	OnClicked.AddUniqueDynamic(this, &UPHSocialFriendButton::HandleClicked);
}

void UPHSocialFriendButton::HandleClicked()
{
	if (!FriendUserId.IsEmpty())
	{
		OnInviteRequested.Broadcast(FriendUserId);
	}
}
