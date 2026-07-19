#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "PHSocialInviteSubsystem.generated.h"

class UTexture2D;

USTRUCT(BlueprintType)
struct PROPHUNT_API FPHSocialFriendEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Social")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Social")
	FString UserId;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Social")
	bool bOnline = false;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Social")
	bool bPlayingPropHunt = false;

	/** Transient medium Steam avatar, cached after the RGBA image becomes available. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "PropHunt|Social")
	TObjectPtr<UTexture2D> AvatarTexture = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FPHSocialInviteOperationFinished,
	bool,
	bSucceeded,
	const FString&,
	Message);

/**
 * Lists Steam friends and transports a bounded P9 lobby invite through Steam.
 * Accepting it creates a gateway ticket in the exact waiting lobby; it never
 * travels to another player's machine and never hosts gameplay.
 */
UCLASS(BlueprintType)
class PROPHUNT_API UPHSocialInviteSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Social")
	bool RefreshSteamFriends();

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Social")
	bool InviteFriend(const FString& FriendUserId);

	UFUNCTION(BlueprintPure, Category = "PropHunt|Social")
	TArray<FPHSocialFriendEntry> GetFriends() const { return Friends; }

	/** Returns the signed-in Steam persona and avatar used by the lobby menu. */
	UFUNCTION(BlueprintCallable, Category = "PropHunt|Social")
	FPHSocialFriendEntry GetLocalSteamProfile();

	void AcceptSteamLobbyInvite(
		bool bWasSuccessful,
		int32 ControllerId,
		const FOnlineSessionSearchResult& InviteResult);

	UPROPERTY(BlueprintAssignable, Category = "PropHunt|Social")
	FPHSocialInviteOperationFinished OnFriendsChanged;

	UPROPERTY(BlueprintAssignable, Category = "PropHunt|Social")
	FPHSocialInviteOperationFinished OnInviteChanged;

private:
	IOnlineSessionPtr GetSessionInterface() const;
	void HandleReadFriendsComplete(
		int32 LocalUserNum,
		bool bWasSuccessful,
		const FString& ListName,
		const FString& ErrorString);
	void RequestInviteToken();
	void HandleInviteTokenReady(bool bSucceeded, const FString& InviteSecret, const FString& Message);
	bool StartCreateInviteTransport();
	bool SendPendingInvite();
	void HandleCreateTransportComplete(FName SessionName, bool bWasSuccessful);
	void HandleDestroyTransportComplete(FName SessionName, bool bWasSuccessful);
	bool IsInviteTransportActive() const;
	bool IsCompatibleLobbyInvite(const FOnlineSessionSearchResult& InviteResult, FString& OutSecret) const;
	UTexture2D* TryLoadSteamAvatar(const FString& FriendUserId) const;
	void ScheduleAvatarRefresh();
	bool HandleAvatarRefreshTicker(float DeltaSeconds);

	UPROPERTY(Transient)
	TArray<FPHSocialFriendEntry> Friends;

	UPROPERTY(Transient)
	FPHSocialFriendEntry LocalSteamProfile;

	TMap<FString, FUniqueNetIdPtr> FriendIds;
	FString PendingFriendUserId;
	FString ActiveInviteSecret;
	FString ActiveGatewayLobbyId;
	bool bInviteOperationInProgress = false;
	FDelegateHandle CreateTransportDelegateHandle;
	FDelegateHandle DestroyTransportDelegateHandle;
	FTSTicker::FDelegateHandle AvatarRefreshTickerHandle;
	int32 AvatarRefreshAttemptsRemaining = 0;
};
