#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"

#include "PHSocialFriendButton.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPHSocialFriendInviteRequested, const FString&, FriendUserId);

/** Small UMG bridge allowing a dynamic friend row to retain the targeted Steam ID. */
UCLASS()
class PROPHUNT_API UPHSocialFriendButton : public UButton
{
	GENERATED_BODY()

public:
	void InitializeFriend(const FString& InFriendUserId);

	UPROPERTY(BlueprintAssignable, Category = "PropHunt|Social")
	FPHSocialFriendInviteRequested OnInviteRequested;

private:
	UFUNCTION()
	void HandleClicked();

	FString FriendUserId;
};
