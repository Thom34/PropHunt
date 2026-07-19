#include "Online/PHSocialInviteSubsystem.h"

#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "Online/PHMatchmakingGatewayContract.h"
#include "Online/PHMatchmakingGatewaySubsystem.h"
#include "Online/PHSessionSubsystem.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

#if PLATFORM_WINDOWS || PLATFORM_LINUX
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END
#endif

namespace
{
const FName InviteSessionName(TEXT("PHLobbyInvite"));
const FName InviteMarkerKey(TEXT("PHINV"));
const FName InviteProtocolKey(TEXT("PHIVP"));
const FName InviteSecretKey(TEXT("PHIVS"));
const FString InviteMarkerValue(TEXT("GatewayLobbyInvite"));
const FString DefaultFriendsList(TEXT("default"));
}

void UPHSocialInviteSubsystem::Deinitialize()
{
	if (AvatarRefreshTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(AvatarRefreshTickerHandle);
		AvatarRefreshTickerHandle.Reset();
	}
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		if (CreateTransportDelegateHandle.IsValid())
		{
			Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateTransportDelegateHandle);
		}
		if (DestroyTransportDelegateHandle.IsValid())
		{
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyTransportDelegateHandle);
		}
		if (Sessions->GetNamedSession(InviteSessionName) != nullptr)
		{
			Sessions->DestroySession(InviteSessionName);
		}
	}
	Super::Deinitialize();
}

IOnlineSessionPtr UPHSocialInviteSubsystem::GetSessionInterface() const
{
	IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	return Subsystem != nullptr ? Subsystem->GetSessionInterface() : nullptr;
}

FPHSocialFriendEntry UPHSocialInviteSubsystem::GetLocalSteamProfile()
{
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	ISteamUser* SteamUserInterface = SteamUser();
	ISteamFriends* SteamFriendsInterface = SteamFriends();
	if (SteamUserInterface == nullptr || SteamFriendsInterface == nullptr
		|| !SteamUserInterface->BLoggedOn())
	{
		return FPHSocialFriendEntry();
	}
	const CSteamID SteamId = SteamUserInterface->GetSteamID();
	LocalSteamProfile.UserId = FString::Printf(TEXT("%llu"), SteamId.ConvertToUint64());
	LocalSteamProfile.DisplayName = UTF8_TO_TCHAR(SteamFriendsInterface->GetPersonaName());
	LocalSteamProfile.bOnline = true;
	LocalSteamProfile.bPlayingPropHunt = true;
	if (LocalSteamProfile.AvatarTexture == nullptr)
	{
		LocalSteamProfile.AvatarTexture = TryLoadSteamAvatar(LocalSteamProfile.UserId);
	}
	return LocalSteamProfile;
#else
	return FPHSocialFriendEntry();
#endif
}

bool UPHSocialInviteSubsystem::RefreshSteamFriends()
{
	IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	IOnlineFriendsPtr Interface = Subsystem != nullptr ? Subsystem->GetFriendsInterface() : nullptr;
	if (Subsystem == nullptr || Subsystem->GetSubsystemName() != FName(TEXT("STEAM"))
		|| !Interface.IsValid())
	{
		OnFriendsChanged.Broadcast(false, TEXT("La liste d'amis Steam est indisponible."));
		return false;
	}
	const bool bStarted = Interface->ReadFriendsList(
		0,
		DefaultFriendsList,
		FOnReadFriendsListComplete::CreateUObject(
			this, &UPHSocialInviteSubsystem::HandleReadFriendsComplete));
	if (!bStarted)
	{
		OnFriendsChanged.Broadcast(false, TEXT("Steam a refusé le chargement des amis."));
	}
	return bStarted;
}

void UPHSocialInviteSubsystem::HandleReadFriendsComplete(
	const int32 LocalUserNum,
	const bool bWasSuccessful,
	const FString& ListName,
	const FString& ErrorString)
{
	Friends.Reset();
	FriendIds.Reset();
	IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	IOnlineFriendsPtr Interface = Subsystem != nullptr ? Subsystem->GetFriendsInterface() : nullptr;
	TArray<TSharedRef<FOnlineFriend>> SteamFriends;
	if (!bWasSuccessful || !Interface.IsValid()
		|| !Interface->GetFriendsList(LocalUserNum, ListName, SteamFriends))
	{
		OnFriendsChanged.Broadcast(false, ErrorString.IsEmpty()
			? TEXT("Impossible de lire les amis Steam.") : ErrorString);
		return;
	}
	for (const TSharedRef<FOnlineFriend>& Friend : SteamFriends)
	{
		const FUniqueNetIdRef FriendId = Friend->GetUserId();
		FPHSocialFriendEntry Entry;
		Entry.DisplayName = Friend->GetDisplayName();
		Entry.UserId = FriendId->ToString();
		Entry.bOnline = Friend->GetPresence().bIsOnline;
		Entry.bPlayingPropHunt = Friend->GetPresence().bIsPlayingThisGame;
		if (Entry.bOnline)
		{
			Entry.AvatarTexture = TryLoadSteamAvatar(Entry.UserId);
		}
		if (!Entry.UserId.IsEmpty())
		{
			FriendIds.Add(Entry.UserId, FriendId);
			Friends.Add(MoveTemp(Entry));
		}
	}
	Friends.StableSort([](const FPHSocialFriendEntry& Left, const FPHSocialFriendEntry& Right)
	{
		if (Left.bPlayingPropHunt != Right.bPlayingPropHunt) return Left.bPlayingPropHunt;
		if (Left.bOnline != Right.bOnline) return Left.bOnline;
		return Left.DisplayName.Compare(Right.DisplayName, ESearchCase::IgnoreCase) < 0;
	});
	ScheduleAvatarRefresh();
	OnFriendsChanged.Broadcast(true, FString::Printf(TEXT("%d ami(s) Steam."), Friends.Num()));
}

UTexture2D* UPHSocialInviteSubsystem::TryLoadSteamAvatar(const FString& FriendUserId) const
{
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	ISteamFriends* SteamFriendsInterface = SteamFriends();
	ISteamUtils* SteamUtilsInterface = SteamUtils();
	if (SteamFriendsInterface == nullptr || SteamUtilsInterface == nullptr || FriendUserId.IsEmpty())
	{
		return nullptr;
	}

	const uint64 SteamIdValue = FCString::Strtoui64(*FriendUserId, nullptr, 10);
	const CSteamID SteamId(SteamIdValue);
	if (!SteamId.IsValid())
	{
		return nullptr;
	}

	const int32 ImageHandle = SteamFriendsInterface->GetMediumFriendAvatar(SteamId);
	if (ImageHandle <= 0)
	{
		SteamFriendsInterface->RequestUserInformation(SteamId, false);
		return nullptr;
	}

	uint32 Width = 0;
	uint32 Height = 0;
	if (!SteamUtilsInterface->GetImageSize(ImageHandle, &Width, &Height)
		|| Width == 0 || Height == 0 || Width > 512 || Height > 512)
	{
		return nullptr;
	}

	TArray<uint8> Pixels;
	Pixels.SetNumUninitialized(static_cast<int32>(Width * Height * 4));
	if (!SteamUtilsInterface->GetImageRGBA(ImageHandle, Pixels.GetData(), Pixels.Num()))
	{
		return nullptr;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(
		static_cast<int32>(Width), static_cast<int32>(Height), PF_R8G8B8A8);
	if (Texture == nullptr || Texture->GetPlatformData() == nullptr
		|| Texture->GetPlatformData()->Mips.IsEmpty())
	{
		return nullptr;
	}
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num());
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
#else
	return nullptr;
#endif
}

void UPHSocialInviteSubsystem::ScheduleAvatarRefresh()
{
	bool bHasMissingOnlineAvatar = false;
	for (const FPHSocialFriendEntry& Entry : Friends)
	{
		bHasMissingOnlineAvatar |= Entry.bOnline && Entry.AvatarTexture == nullptr;
	}
	if (!bHasMissingOnlineAvatar || AvatarRefreshTickerHandle.IsValid())
	{
		return;
	}
	AvatarRefreshAttemptsRemaining = 8;
	AvatarRefreshTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UPHSocialInviteSubsystem::HandleAvatarRefreshTicker),
		0.5f);
}

bool UPHSocialInviteSubsystem::HandleAvatarRefreshTicker(const float DeltaSeconds)
{
	bool bChanged = false;
	bool bStillMissing = false;
	for (FPHSocialFriendEntry& Entry : Friends)
	{
		if (!Entry.bOnline || Entry.AvatarTexture != nullptr)
		{
			continue;
		}
		Entry.AvatarTexture = TryLoadSteamAvatar(Entry.UserId);
		bChanged |= Entry.AvatarTexture != nullptr;
		bStillMissing |= Entry.AvatarTexture == nullptr;
	}
	if (bChanged)
	{
		OnFriendsChanged.Broadcast(true, TEXT("Avatars Steam actualisés."));
	}

	--AvatarRefreshAttemptsRemaining;
	if (!bStillMissing || AvatarRefreshAttemptsRemaining <= 0)
	{
		AvatarRefreshTickerHandle.Reset();
		return false;
	}
	return true;
}

bool UPHSocialInviteSubsystem::InviteFriend(const FString& FriendUserId)
{
	if (bInviteOperationInProgress)
	{
		OnInviteChanged.Broadcast(false, TEXT("Une invitation est déjà en préparation."));
		return false;
	}
	const FUniqueNetIdPtr* FriendId = FriendIds.Find(FriendUserId);
	UPHMatchmakingGatewaySubsystem* Gateway = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPHMatchmakingGatewaySubsystem>() : nullptr;
	if (FriendId == nullptr || !FriendId->IsValid())
	{
		OnInviteChanged.Broadcast(false, TEXT("Cet ami n'est plus dans la liste Steam."));
		return false;
	}
	if (Gateway == nullptr || !Gateway->HasLobbySnapshot() || Gateway->IsLobbyLocked())
	{
		OnInviteChanged.Broadcast(false, TEXT("Le salon doit être ouvert avant d'inviter un ami."));
		return false;
	}
	PendingFriendUserId = FriendUserId;
	if (IsInviteTransportActive() && ActiveGatewayLobbyId == Gateway->GetLobbyId()
		&& PHMatchmakingGatewayContract::IsSafeLobbyInviteSecret(ActiveInviteSecret))
	{
		return SendPendingInvite();
	}
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid() && Sessions->GetNamedSession(InviteSessionName) != nullptr)
	{
		bInviteOperationInProgress = true;
		DestroyTransportDelegateHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(
				this, &UPHSocialInviteSubsystem::HandleDestroyTransportComplete));
		if (!Sessions->DestroySession(InviteSessionName))
		{
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyTransportDelegateHandle);
			DestroyTransportDelegateHandle.Reset();
			bInviteOperationInProgress = false;
			OnInviteChanged.Broadcast(false, TEXT("Impossible de renouveler l'invitation Steam."));
			return false;
		}
		return true;
	}
	RequestInviteToken();
	return true;
}

void UPHSocialInviteSubsystem::HandleDestroyTransportComplete(
	const FName SessionName,
	const bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyTransportDelegateHandle);
	}
	DestroyTransportDelegateHandle.Reset();
	bInviteOperationInProgress = false;
	ActiveInviteSecret.Reset();
	ActiveGatewayLobbyId.Reset();
	if (!bWasSuccessful || SessionName != InviteSessionName)
	{
		OnInviteChanged.Broadcast(false, TEXT("Le renouvellement de l'invitation Steam a échoué."));
		return;
	}
	RequestInviteToken();
}

void UPHSocialInviteSubsystem::RequestInviteToken()
{
	UPHMatchmakingGatewaySubsystem* Gateway = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPHMatchmakingGatewaySubsystem>() : nullptr;
	if (Gateway == nullptr)
	{
		OnInviteChanged.Broadcast(false, TEXT("La gateway PropHunt est indisponible."));
		return;
	}
	bInviteOperationInProgress = true;
	Gateway->RequestLobbyInviteToken(FPHLobbyInviteTokenCallback::CreateUObject(
		this, &UPHSocialInviteSubsystem::HandleInviteTokenReady));
}

void UPHSocialInviteSubsystem::HandleInviteTokenReady(
	const bool bSucceeded,
	const FString& InviteSecret,
	const FString& Message)
{
	bInviteOperationInProgress = false;
	if (!bSucceeded || !PHMatchmakingGatewayContract::IsSafeLobbyInviteSecret(InviteSecret))
	{
		OnInviteChanged.Broadcast(false, Message);
		return;
	}
	const UPHMatchmakingGatewaySubsystem* Gateway = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPHMatchmakingGatewaySubsystem>() : nullptr;
	ActiveInviteSecret = InviteSecret;
	ActiveGatewayLobbyId = Gateway != nullptr ? Gateway->GetLobbyId() : FString();
	StartCreateInviteTransport();
}

bool UPHSocialInviteSubsystem::StartCreateInviteTransport()
{
	IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	IOnlineSessionPtr Sessions = Subsystem != nullptr ? Subsystem->GetSessionInterface() : nullptr;
	IOnlineIdentityPtr Identity = Subsystem != nullptr ? Subsystem->GetIdentityInterface() : nullptr;
	const FUniqueNetIdPtr LocalUserId = Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr;
	if (!Sessions.IsValid() || !LocalUserId.IsValid())
	{
		OnInviteChanged.Broadcast(false, TEXT("Steam ne peut pas préparer l'invitation."));
		return false;
	}
	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = 5;
	Settings.bIsLANMatch = false;
	Settings.bIsDedicated = false;
	Settings.bShouldAdvertise = false;
	Settings.bAllowJoinInProgress = true;
	Settings.bAllowInvites = true;
	Settings.bUsesPresence = true;
	Settings.bAllowJoinViaPresence = false;
	Settings.bUseLobbiesIfAvailable = true;
	Settings.Set(InviteMarkerKey, InviteMarkerValue, EOnlineDataAdvertisementType::ViaOnlineService);
	const UPHSessionSubsystem* GameSessions = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPHSessionSubsystem>() : nullptr;
	Settings.Set(InviteProtocolKey, GameSessions != nullptr ? GameSessions->GetProtocolVersion() : 0,
		EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(InviteSecretKey, ActiveInviteSecret, EOnlineDataAdvertisementType::ViaOnlineService);
	bInviteOperationInProgress = true;
	CreateTransportDelegateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(
			this, &UPHSocialInviteSubsystem::HandleCreateTransportComplete));
	if (!Sessions->CreateSession(*LocalUserId, InviteSessionName, Settings))
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateTransportDelegateHandle);
		CreateTransportDelegateHandle.Reset();
		bInviteOperationInProgress = false;
		OnInviteChanged.Broadcast(false, TEXT("Steam a refusé de préparer l'invitation."));
		return false;
	}
	OnInviteChanged.Broadcast(true, TEXT("Préparation de l'invitation Steam..."));
	return true;
}

void UPHSocialInviteSubsystem::HandleCreateTransportComplete(
	const FName SessionName,
	const bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateTransportDelegateHandle);
	}
	CreateTransportDelegateHandle.Reset();
	bInviteOperationInProgress = false;
	if (!bWasSuccessful || SessionName != InviteSessionName || !IsInviteTransportActive())
	{
		OnInviteChanged.Broadcast(false, TEXT("Steam n'a pas créé le support d'invitation."));
		return;
	}
	SendPendingInvite();
}

bool UPHSocialInviteSubsystem::SendPendingInvite()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	const FUniqueNetIdPtr* FriendId = FriendIds.Find(PendingFriendUserId);
	if (!Sessions.IsValid() || FriendId == nullptr || !FriendId->IsValid()
		|| !IsInviteTransportActive())
	{
		OnInviteChanged.Broadcast(false, TEXT("L'ami ou le lobby Steam n'est plus disponible."));
		return false;
	}
	const bool bSent = Sessions->SendSessionInviteToFriend(0, InviteSessionName, **FriendId);
	PendingFriendUserId.Reset();
	OnInviteChanged.Broadcast(bSent, bSent
		? TEXT("Invitation envoyée pour rejoindre ce lobby PropHunt.")
		: TEXT("Steam n'a pas pu envoyer l'invitation."));
	return bSent;
}

bool UPHSocialInviteSubsystem::IsInviteTransportActive() const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* Session = Sessions.IsValid()
		? Sessions->GetNamedSession(InviteSessionName) : nullptr;
	return Session != nullptr && Session->SessionInfo.IsValid();
}

bool UPHSocialInviteSubsystem::IsCompatibleLobbyInvite(
	const FOnlineSessionSearchResult& InviteResult,
	FString& OutSecret) const
{
	OutSecret.Reset();
	FString Marker;
	int32 Protocol = 0;
	const UPHSessionSubsystem* GameSessions = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPHSessionSubsystem>() : nullptr;
	return InviteResult.IsValid()
		&& InviteResult.Session.SessionSettings.Get(InviteMarkerKey, Marker)
		&& Marker == InviteMarkerValue
		&& InviteResult.Session.SessionSettings.Get(InviteProtocolKey, Protocol)
		&& GameSessions != nullptr && Protocol == GameSessions->GetProtocolVersion()
		&& InviteResult.Session.SessionSettings.Get(InviteSecretKey, OutSecret)
		&& PHMatchmakingGatewayContract::IsSafeLobbyInviteSecret(OutSecret);
}

void UPHSocialInviteSubsystem::AcceptSteamLobbyInvite(
	const bool bWasSuccessful,
	const int32 ControllerId,
	const FOnlineSessionSearchResult& InviteResult)
{
	FString InviteSecret;
	if (!bWasSuccessful || !IsCompatibleLobbyInvite(InviteResult, InviteSecret))
	{
		OnInviteChanged.Broadcast(false, TEXT("Cette invitation n'est pas un lobby PropHunt compatible."));
		return;
	}
	UPHMatchmakingGatewaySubsystem* Gateway = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPHMatchmakingGatewaySubsystem>() : nullptr;
	if (Gateway == nullptr || Gateway->IsBusy())
	{
		OnInviteChanged.Broadcast(false, TEXT("Annule d'abord le matchmaking courant."));
		return;
	}
	Gateway->RequestTicketFromLobbyInvite(InviteSecret, ControllerId);
}
