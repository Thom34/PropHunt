#include "Online/PHSessionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHSteam, Log, All);

namespace
{
const FName PHSessionModeKey(TEXT("PHM"));
const FName PHSessionProtocolKey(TEXT("PHP"));
const FName PHSessionStatusKey(TEXT("PHS"));
const FName PHSessionCreatedAtKey(TEXT("PHT"));
const FString PHHunterLobbyMode(TEXT("H"));
const FString PHLobbyState(TEXT("Lobby"));
const FString PHStatusLobbyUnassigned(TEXT("L0"));
const FString PHStatusLobbyAssigned(TEXT("L1"));
const FString PHStatusMatchAssigned(TEXT("M1"));
const FName SteamSubsystemName(TEXT("STEAM"));

bool IsLobbyStatus(const FString& Status)
{
	return Status.StartsWith(TEXT("L"));
}

bool IsHunterAssignedStatus(const FString& Status)
{
	return !Status.EndsWith(TEXT("0"));
}

const FString& MakeSessionStatus(const bool bJoinable, const bool bHunterAssigned)
{
	if (!bJoinable)
	{
		return PHStatusMatchAssigned;
	}
	return bHunterAssigned ? PHStatusLobbyAssigned : PHStatusLobbyUnassigned;
}
}

bool PHMatchmakingPolicy::IsCompatibleLobby(
	const int32 OpenPublicSlots,
	const int32 AdvertisedProtocol,
	const FString& AdvertisedMode,
	const FString& AdvertisedState,
	const int32 RequiredProtocol)
{
	return OpenPublicSlots > 0
		&& AdvertisedProtocol == RequiredProtocol
		&& AdvertisedMode == PHHunterLobbyMode
		&& AdvertisedState == PHLobbyState;
}

void PHMatchmakingPolicy::SortLobbyPriorities(TArray<FPHLobbyPriority>& Priorities)
{
	Priorities.StableSort([](const FPHLobbyPriority& Left, const FPHLobbyPriority& Right)
	{
		const bool bLeftHasCreationTime = Left.CreatedAtUnixSeconds > 0;
		const bool bRightHasCreationTime = Right.CreatedAtUnixSeconds > 0;
		if (bLeftHasCreationTime != bRightHasCreationTime)
		{
			return bLeftHasCreationTime;
		}
		if (bLeftHasCreationTime && Left.CreatedAtUnixSeconds != Right.CreatedAtUnixSeconds)
		{
			return Left.CreatedAtUnixSeconds < Right.CreatedAtUnixSeconds;
		}

		const bool bLeftHasPing = Left.PingMilliseconds >= 0;
		const bool bRightHasPing = Right.PingMilliseconds >= 0;
		if (bLeftHasPing != bRightHasPing)
		{
			return bLeftHasPing;
		}
		if (bLeftHasPing && Left.PingMilliseconds != Right.PingMilliseconds)
		{
			return Left.PingMilliseconds < Right.PingMilliseconds;
		}
		if (Left.SessionId != Right.SessionId)
		{
			return Left.SessionId < Right.SessionId;
		}
		return Left.OriginalSearchIndex < Right.OriginalSearchIndex;
	});
}

EPHMatchmakingSearchDecision PHMatchmakingPolicy::DecideAfterSearch(
	const EPHMatchmakingPreference Preference,
	const int32 VisibleHunterLobbyCount,
	const int32 AvailableHunterLobbyCount,
	const int32 MaximumHunterLobbyCount,
	const int32 AvailableUnassignedHunterLobbyCount)
{
	const bool bHasAvailableLobby = AvailableHunterLobbyCount > 0;
	const bool bHunterLobbyCapReached = VisibleHunterLobbyCount >= FMath::Max(1, MaximumHunterLobbyCount);

	switch (Preference)
	{
	case EPHMatchmakingPreference::Hunter:
		if (AvailableUnassignedHunterLobbyCount > 0)
		{
			return EPHMatchmakingSearchDecision::JoinHunterLobby;
		}
		if (!bHunterLobbyCapReached)
		{
			return EPHMatchmakingSearchDecision::CreateHunterLobby;
		}
		return EPHMatchmakingSearchDecision::WaitForAvailableSlot;
	case EPHMatchmakingPreference::Random:
		if (bHasAvailableLobby)
		{
			return EPHMatchmakingSearchDecision::JoinPropLobby;
		}
		return !bHunterLobbyCapReached
			? EPHMatchmakingSearchDecision::CreateHunterLobby
			: EPHMatchmakingSearchDecision::WaitForAvailableSlot;
	case EPHMatchmakingPreference::Prop:
	case EPHMatchmakingPreference::Availability:
	default:
		return bHasAvailableLobby
			? EPHMatchmakingSearchDecision::JoinPropLobby
			: EPHMatchmakingSearchDecision::WaitForAvailableSlot;
	}
}

FPHSessionHostingPolicy PHMatchmakingPolicy::ResolveSessionHostingPolicy(const bool bIsDedicatedServer)
{
	FPHSessionHostingPolicy Policy;
	Policy.bIsDedicated = bIsDedicatedServer;
	Policy.bUsesPresence = !bIsDedicatedServer;
	Policy.bUseLobbies = !bIsDedicatedServer;
	Policy.bAllowInvites = !bIsDedicatedServer;
	return Policy;
}

EPHPlayerRole PHMatchmakingPolicy::ResolveListenServerRole(
	const bool bIsLocalController,
	const bool bHunterAlreadyAssigned)
{
	return ResolveAuthoritativeServerRole(bIsLocalController, false, bHunterAlreadyAssigned);
}

EPHPlayerRole PHMatchmakingPolicy::ResolveAuthoritativeServerRole(
	const bool bIsLocalController,
	const bool bIsDedicatedServer,
	const bool bHunterAlreadyAssigned)
{
	return !bHunterAlreadyAssigned && (bIsLocalController || bIsDedicatedServer)
		? EPHPlayerRole::Hunter
		: EPHPlayerRole::Prop;
}

bool PHMatchmakingPolicy::IsJoinedClientConnection(
	const bool bIsClientWorld,
	const bool bHasServerConnection,
	const bool bServerConnectionOpen)
{
	return bIsClientWorld && bHasServerConnection && bServerConnectionOpen;
}

void UPHSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PostLoadMapDelegateHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UPHSessionSubsystem::HandlePostLoadMap);
	if (GEngine != nullptr)
	{
		GEngine->OnNetworkFailure().AddUObject(this, &UPHSessionSubsystem::HandleNetworkFailure);
		GEngine->OnTravelFailure().AddUObject(this, &UPHSessionSubsystem::HandleTravelFailure);
	}
}

void UPHSessionSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JoinedTravelConfirmationTimer);
	}

	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		if (CreateSessionDelegateHandle.IsValid())
		{
			Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionDelegateHandle);
		}
		if (FindSessionsDelegateHandle.IsValid())
		{
			Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsDelegateHandle);
		}
		if (JoinSessionDelegateHandle.IsValid())
		{
			Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionDelegateHandle);
		}
		if (DestroySessionDelegateHandle.IsValid())
		{
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionDelegateHandle);
		}
	}

	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapDelegateHandle);
	if (GEngine != nullptr)
	{
		GEngine->OnNetworkFailure().RemoveAll(this);
		GEngine->OnTravelFailure().RemoveAll(this);
	}

	ActiveSessionSettings.Reset();
	ActiveSessionSearch.Reset();
	RankedSearchResults.Reset();
	Super::Deinitialize();
}

void UPHSessionSubsystem::HostHunterLobby()
{
	if (!ValidateSteamForOperation(TEXT("HostHunterLobby")))
	{
		return;
	}
	if (MatchmakingState == EPHMatchmakingState::CreatingLobby
		|| MatchmakingState == EPHMatchmakingState::Searching
		|| MatchmakingState == EPHMatchmakingState::Joining
		|| MatchmakingState == EPHMatchmakingState::Leaving)
	{
		FinishOperation(false, TEXT("Une opération Steam est déjà en cours."));
		return;
	}
	ClearCachedMatchResults();

	PendingPreference = EPHMatchmakingPreference::Hunter;
	bAutoJoinFirstResult = true;
	bWaitingForHunterSlot = true;
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		BeginDestroySession(EPHPendingAfterDestroy::SearchLobbies);
		return;
	}

	StartSearchLobbies();
}

void UPHSessionSubsystem::FindPropLobby(const bool bAutoJoinFirstAvailable)
{
	if (!ValidateSteamForOperation(TEXT("FindPropLobby")))
	{
		return;
	}
	if (MatchmakingState == EPHMatchmakingState::CreatingLobby
		|| MatchmakingState == EPHMatchmakingState::Searching
		|| MatchmakingState == EPHMatchmakingState::Joining
		|| MatchmakingState == EPHMatchmakingState::Leaving)
	{
		FinishOperation(false, TEXT("Une opération Steam est déjà en cours."));
		return;
	}
	ClearCachedMatchResults();

	bAutoJoinFirstResult = bAutoJoinFirstAvailable;
	PendingPreference = EPHMatchmakingPreference::Prop;
	bWaitingForHunterSlot = false;
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		BeginDestroySession(EPHPendingAfterDestroy::SearchLobbies);
		return;
	}

	StartSearchLobbies();
}

void UPHSessionSubsystem::FindRandomRole()
{
	if (!ValidateSteamForOperation(TEXT("FindRandomRole")))
	{
		return;
	}
	if (MatchmakingState == EPHMatchmakingState::CreatingLobby
		|| MatchmakingState == EPHMatchmakingState::Searching
		|| MatchmakingState == EPHMatchmakingState::Joining
		|| MatchmakingState == EPHMatchmakingState::Leaving)
	{
		FinishOperation(false, TEXT("Une opération Steam est déjà en cours."));
		return;
	}
	ClearCachedMatchResults();

	PendingPreference = EPHMatchmakingPreference::Random;
	bAutoJoinFirstResult = true;
	bWaitingForHunterSlot = false;
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		BeginDestroySession(EPHPendingAfterDestroy::SearchLobbies);
		return;
	}

	StartSearchLobbies();
}

void UPHSessionSubsystem::RefreshAvailability()
{
	if (!IsSteamAvailable()
		|| MatchmakingState == EPHMatchmakingState::CreatingLobby
		|| MatchmakingState == EPHMatchmakingState::Searching
		|| MatchmakingState == EPHMatchmakingState::Joining
		|| MatchmakingState == EPHMatchmakingState::Leaving
		|| MatchmakingState == EPHMatchmakingState::Hosting
		|| MatchmakingState == EPHMatchmakingState::InSession)
	{
		return;
	}

	PendingPreference = bWaitingForHunterSlot
		? EPHMatchmakingPreference::Hunter
		: EPHMatchmakingPreference::Availability;
	bAutoJoinFirstResult = false;
	StartSearchLobbies();
}

void UPHSessionSubsystem::JoinSuggestedLobby()
{
	if (RankedSearchResults.IsEmpty())
	{
		FinishOperation(false, TEXT("Aucun lobby proposé n’est encore disponible."));
		return;
	}
	if (MatchmakingState == EPHMatchmakingState::Joining)
	{
		return;
	}
	ClearCachedMatchResults();

	NextJoinIndex = 0;
	TryJoinNextLobby();
}

void UPHSessionSubsystem::LeaveSessionAndReturnToLobby()
{
	SetMatchmakingState(EPHMatchmakingState::Leaving);
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || Sessions->GetNamedSession(NAME_GameSession) == nullptr)
	{
		ReturnToLobbyMap();
		return;
	}

	BeginDestroySession(EPHPendingAfterDestroy::ReturnToLobby);
}

void UPHSessionSubsystem::LeaveSessionAndShowResults()
{
	SetMatchmakingState(EPHMatchmakingState::Leaving);
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || Sessions->GetNamedSession(NAME_GameSession) == nullptr)
	{
		ReturnToResultsMap();
		return;
	}

	BeginDestroySession(EPHPendingAfterDestroy::ReturnToResults);
}

void UPHSessionSubsystem::ContinueFromResultsToLobby()
{
	if (!IsCurrentWorldResultsMap())
	{
		return;
	}

	ClearCachedMatchResults();
	ReturnToLobbyMap();
}

bool UPHSessionSubsystem::IsCurrentWorldResultsMap() const
{
	const UWorld* World = GetWorld();
	return World != nullptr
		&& World->GetMapName().EndsWith(FPackageName::GetShortName(ResultsMap));
}

bool UPHSessionSubsystem::IsCurrentWorldLobbyMap() const
{
	const UWorld* World = GetWorld();
	return World != nullptr
		&& World->GetMapName().EndsWith(FPackageName::GetShortName(ReturnMap));
}

void UPHSessionSubsystem::CacheMatchResults(
	const FPHMatchResultsSnapshot& Snapshot,
	const EPHPlayerRole LocalPlayerRole,
	const FString& LocalPlayerName,
	const int32 LocalPlayerId)
{
	if (!Snapshot.bFinalized)
	{
		return;
	}

	CachedMatchResults = Snapshot;
	CachedResultsLocalRole = LocalPlayerRole;
	CachedResultsLocalPlayerName = LocalPlayerName;
	CachedResultsLocalPlayerId = LocalPlayerId;
	UE_LOG(LogPHSteam, Log, TEXT("Cached post-match snapshot with %d player row(s)."),
		CachedMatchResults.PlayerRows.Num());
}

void UPHSessionSubsystem::ClearCachedMatchResults()
{
	CachedMatchResults = FPHMatchResultsSnapshot();
	CachedResultsLocalRole = EPHPlayerRole::Unassigned;
	CachedResultsLocalPlayerName.Reset();
	CachedResultsLocalPlayerId = INDEX_NONE;
}

void UPHSessionSubsystem::SetHunterLobbyJoinable(const bool bJoinable)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	FNamedOnlineSession* NamedSession = Sessions.IsValid()
		? Sessions->GetNamedSession(NAME_GameSession)
		: nullptr;
	if (NamedSession == nullptr || !NamedSession->bHosting)
	{
		return;
	}

	FOnlineSessionSettings UpdatedSettings = NamedSession->SessionSettings;
	UpdatedSettings.bAllowJoinInProgress = bJoinable;
	UpdatedSettings.bAllowJoinViaPresence = bJoinable && !UpdatedSettings.bIsDedicated;
	FString ExistingStatus = PHStatusLobbyAssigned;
	UpdatedSettings.Get(PHSessionStatusKey, ExistingStatus);
	UpdatedSettings.Set(
		PHSessionStatusKey,
		MakeSessionStatus(bJoinable, IsHunterAssignedStatus(ExistingStatus)),
		EOnlineDataAdvertisementType::ViaOnlineService);
	if (!Sessions->UpdateSession(NAME_GameSession, UpdatedSettings, true))
	{
		UE_LOG(LogPHSteam, Warning, TEXT("Steam lobby joinability update could not be started (joinable=%s)."),
			bJoinable ? TEXT("true") : TEXT("false"));
	}
}

void UPHSessionSubsystem::SetHunterAssigned(const bool bAssigned)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	FNamedOnlineSession* NamedSession = Sessions.IsValid()
		? Sessions->GetNamedSession(NAME_GameSession)
		: nullptr;
	if (NamedSession == nullptr || !NamedSession->bHosting)
	{
		return;
	}

	FOnlineSessionSettings UpdatedSettings = NamedSession->SessionSettings;
	FString ExistingStatus = PHStatusLobbyAssigned;
	UpdatedSettings.Get(PHSessionStatusKey, ExistingStatus);
	UpdatedSettings.Set(
		PHSessionStatusKey,
		MakeSessionStatus(IsLobbyStatus(ExistingStatus), bAssigned),
		EOnlineDataAdvertisementType::ViaOnlineService);
	if (!Sessions->UpdateSession(NAME_GameSession, UpdatedSettings, true))
	{
		UE_LOG(LogPHSteam, Warning, TEXT("Steam Hunter assignment update could not be started (assigned=%s)."),
			bAssigned ? TEXT("true") : TEXT("false"));
	}
}

bool UPHSessionSubsystem::IsSteamAvailable() const
{
	const IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	return Subsystem != nullptr
		&& Subsystem->GetSubsystemName() == SteamSubsystemName
		&& Subsystem->IsEnabled()
		&& Subsystem->GetSessionInterface().IsValid();
}

IOnlineSessionPtr UPHSessionSubsystem::GetSessionInterface() const
{
	return Online::GetSessionInterface(GetWorld());
}

bool UPHSessionSubsystem::ValidateSteamForOperation(const TCHAR* OperationName)
{
	if (IsSteamAvailable())
	{
		return true;
	}

	const FString Message = FString::Printf(
		TEXT("%s impossible : Steam n’est pas initialisé (protocole %d). Lance l’App ID 1551300 depuis sa vraie fiche Steam avec un compte autorisé ; un raccourci non-Steam ne suffit pas. Sous Linux, utilise Proton."),
		OperationName,
		ProtocolVersion);
	UE_LOG(LogPHSteam, Error, TEXT("%s"), *Message);
	SetMatchmakingState(EPHMatchmakingState::Failed);
	FinishOperation(false, Message);
	return false;
}

void UPHSessionSubsystem::SetMatchmakingState(const EPHMatchmakingState NewState)
{
	if (MatchmakingState == NewState)
	{
		return;
	}

	MatchmakingState = NewState;
	OnMatchmakingStateChanged.Broadcast(MatchmakingState);
}

void UPHSessionSubsystem::FinishOperation(const bool bSucceeded, const FString& Message)
{
	if (bSucceeded)
	{
		UE_LOG(LogPHSteam, Log, TEXT("%s"), *Message);
	}
	else
	{
		UE_LOG(LogPHSteam, Warning, TEXT("%s"), *Message);
	}
	if (!bSucceeded)
	{
		SetMatchmakingState(EPHMatchmakingState::Failed);
	}
	OnMatchmakingFinished.Broadcast(bSucceeded, Message);
}

void UPHSessionSubsystem::StartCreateLobby()
{
	IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	IOnlineSessionPtr Sessions = Subsystem != nullptr ? Subsystem->GetSessionInterface() : nullptr;
	IOnlineIdentityPtr Identity = Subsystem != nullptr ? Subsystem->GetIdentityInterface() : nullptr;
	const FUniqueNetIdPtr LocalUserId = Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr;
	const UWorld* World = GetWorld();
	const bool bDedicatedServer = World != nullptr && World->GetNetMode() == NM_DedicatedServer;
	if (!Sessions.IsValid() || (!bDedicatedServer && !LocalUserId.IsValid()))
	{
		FinishOperation(false, bDedicatedServer
			? TEXT("Steam n'a pas fourni d'interface de session au serveur dédié.")
			: TEXT("Steam n'a pas fourni d'identité locale valide pour créer le lobby."));
		return;
	}

	const FPHSessionHostingPolicy HostingPolicy =
		PHMatchmakingPolicy::ResolveSessionHostingPolicy(bDedicatedServer);
	SetMatchmakingState(EPHMatchmakingState::CreatingLobby);
	ActiveSessionSettings = MakeShared<FOnlineSessionSettings>();
	ActiveSessionSettings->NumPublicConnections = FMath::Clamp(MaxLobbyPlayers, 2, 5);
	ActiveSessionSettings->NumPrivateConnections = 0;
	ActiveSessionSettings->bIsLANMatch = false;
	ActiveSessionSettings->bIsDedicated = HostingPolicy.bIsDedicated;
	ActiveSessionSettings->bShouldAdvertise = true;
	ActiveSessionSettings->bAllowJoinInProgress = true;
	ActiveSessionSettings->bAllowInvites = HostingPolicy.bAllowInvites;
	ActiveSessionSettings->bUsesPresence = HostingPolicy.bUsesPresence;
	ActiveSessionSettings->bAllowJoinViaPresence = HostingPolicy.bUsesPresence;
	ActiveSessionSettings->bAllowJoinViaPresenceFriendsOnly = false;
	ActiveSessionSettings->bUseLobbiesIfAvailable = HostingPolicy.bUseLobbies;
	if (!bDedicatedServer)
	{
		ActiveSessionSettings->Set(SETTING_MAPNAME, MatchMap, EOnlineDataAdvertisementType::ViaOnlineService);
	}
	ActiveSessionSettings->Set(PHSessionModeKey, PHHunterLobbyMode, EOnlineDataAdvertisementType::ViaOnlineService);
	ActiveSessionSettings->Set(PHSessionProtocolKey, ProtocolVersion, EOnlineDataAdvertisementType::ViaOnlineService);
	ActiveSessionSettings->Set(
		PHSessionStatusKey,
		MakeSessionStatus(true, !bDedicatedServer),
		EOnlineDataAdvertisementType::ViaOnlineService);
	if (!bDedicatedServer)
	{
		ActiveSessionSettings->Set(
			PHSessionCreatedAtKey,
			LexToString(FDateTime::UtcNow().ToUnixTimestamp()),
			EOnlineDataAdvertisementType::ViaOnlineService);
	}

	CreateSessionDelegateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UPHSessionSubsystem::HandleCreateSessionComplete));
	const bool bCreateStarted = bDedicatedServer
		? Sessions->CreateSession(0, NAME_GameSession, *ActiveSessionSettings)
		: Sessions->CreateSession(*LocalUserId, NAME_GameSession, *ActiveSessionSettings);
	if (!bCreateStarted)
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionDelegateHandle);
		CreateSessionDelegateHandle.Reset();
		FinishOperation(false, bDedicatedServer
			? TEXT("Steam a refusé la publication de la session dédiée.")
			: TEXT("Steam a refusé le démarrage de la création du lobby Hunter."));
	}
}

void UPHSessionSubsystem::StartSearchLobbies()
{
	SetMatchmakingState(EPHMatchmakingState::Searching);
	SuggestedLobby = FPHLobbySummary();
	RankedSearchResults.Reset();
	RankedUnassignedSearchResults.Reset();
	NextJoinIndex = 0;
	SearchVisibleHunterLobbyCount = 0;
	SearchAssignedHunterLobbyCount = 0;
	SearchUnassignedHunterLobbyCount = 0;
	SearchJoinableUnassignedHunterLobbyCount = 0;
	bAnySearchPassSucceeded = false;
	LastJoinFailure.Reset();
	StartSearchPass(EPHSearchPass::DedicatedServer);
}

void UPHSessionSubsystem::StartSearchPass(const EPHSearchPass SearchPass)
{
	IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	IOnlineSessionPtr Sessions = Subsystem != nullptr ? Subsystem->GetSessionInterface() : nullptr;
	IOnlineIdentityPtr Identity = Subsystem != nullptr ? Subsystem->GetIdentityInterface() : nullptr;
	const FUniqueNetIdPtr LocalUserId = Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr;
	if (!Sessions.IsValid() || !LocalUserId.IsValid())
	{
		FinishOperation(false, TEXT("Steam n’a pas fourni d’identité locale valide pour rechercher un lobby."));
		return;
	}

	ActiveSearchPass = SearchPass;
	ActiveSessionSearch = MakeShared<FOnlineSessionSearch>();
	ActiveSessionSearch->bIsLanQuery = false;
	ActiveSessionSearch->MaxSearchResults = FMath::Clamp(MaxSearchResults, 1, 200);
	ActiveSessionSearch->PingBucketSize = 50;
	if (SearchPass == EPHSearchPass::DedicatedServer)
	{
		ActiveSessionSearch->QuerySettings.Set(SEARCH_DEDICATED_ONLY, true, EOnlineComparisonOp::Equals);
	}
	else
	{
		ActiveSessionSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	}
	ActiveSessionSearch->QuerySettings.Set(
		PHSessionModeKey, PHHunterLobbyMode, EOnlineComparisonOp::Equals);
	ActiveSessionSearch->QuerySettings.Set(
		PHSessionProtocolKey, ProtocolVersion, EOnlineComparisonOp::Equals);

	FindSessionsDelegateHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UPHSessionSubsystem::HandleFindSessionsComplete));
	if (!Sessions->FindSessions(*LocalUserId, ActiveSessionSearch.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsDelegateHandle);
		FindSessionsDelegateHandle.Reset();
		if (SearchPass == EPHSearchPass::DedicatedServer)
		{
			StartSearchPass(EPHSearchPass::Lobby);
		}
		else
		{
			FinishOperation(false, TEXT("Steam a refusé le démarrage de la recherche de sessions."));
		}
	}
}

void UPHSessionSubsystem::TryJoinNextLobby()
{
	IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	IOnlineSessionPtr Sessions = Subsystem != nullptr ? Subsystem->GetSessionInterface() : nullptr;
	IOnlineIdentityPtr Identity = Subsystem != nullptr ? Subsystem->GetIdentityInterface() : nullptr;
	const FUniqueNetIdPtr LocalUserId = Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr;
	if (!Sessions.IsValid() || !LocalUserId.IsValid())
	{
		FinishOperation(false, TEXT("La session Steam n’est plus disponible pendant la tentative de connexion."));
		return;
	}

	if (!RankedSearchResults.IsValidIndex(NextJoinIndex))
	{
		const FString FailureMessage = LastJoinFailure.IsEmpty()
			? TEXT("Tous les lobbies compatibles ont été essayés. Relance la recherche.")
			: FString::Printf(
				TEXT("Tous les lobbies compatibles ont été essayés. Dernière erreur : %s"),
				*LastJoinFailure);
		FinishOperation(false, FailureMessage);
		return;
	}

	if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		BeginDestroySession(EPHPendingAfterDestroy::RetryNextLobby);
		return;
	}

	SetMatchmakingState(EPHMatchmakingState::Joining);
	const FOnlineSessionSearchResult& Candidate = RankedSearchResults[NextJoinIndex++];
	SuggestedLobby = BuildLobbySummary(Candidate);
	OnLobbySuggested.Broadcast(SuggestedLobby);
	UE_LOG(LogPHSteam, Log, TEXT("Trying lobby %s (%d/%d, ping %d ms)."),
		*SuggestedLobby.SessionId,
		NextJoinIndex,
		RankedSearchResults.Num(),
		SuggestedLobby.PingMilliseconds);

	JoinSessionDelegateHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UPHSessionSubsystem::HandleJoinSessionComplete));
	if (!Sessions->JoinSession(*LocalUserId, NAME_GameSession, Candidate))
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionDelegateHandle);
		JoinSessionDelegateHandle.Reset();
		TryJoinNextLobby();
	}
}

void UPHSessionSubsystem::BeginDestroySession(const EPHPendingAfterDestroy PendingAction)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	PendingAfterDestroy = PendingAction;
	if (!Sessions.IsValid() || Sessions->GetNamedSession(NAME_GameSession) == nullptr)
	{
		HandleDestroySessionComplete(NAME_GameSession, true);
		return;
	}

	if (PendingAction == EPHPendingAfterDestroy::ReturnToLobby
		|| PendingAction == EPHPendingAfterDestroy::ReturnToResults)
	{
		SetMatchmakingState(EPHMatchmakingState::Leaving);
	}
	DestroySessionDelegateHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UPHSessionSubsystem::HandleDestroySessionComplete));
	if (!Sessions->DestroySession(NAME_GameSession))
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionDelegateHandle);
		DestroySessionDelegateHandle.Reset();
		HandleDestroySessionComplete(NAME_GameSession, false);
	}
}

void UPHSessionSubsystem::ReturnToResultsMap()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JoinedTravelConfirmationTimer);
	}
	bAwaitingHostTravel = false;
	bAwaitingJoinedTravel = false;
	RankedSearchResults.Reset();
	SetMatchmakingState(EPHMatchmakingState::Idle);
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::OpenLevel(World, FName(*ResultsMap), true);
	}
}

void UPHSessionSubsystem::ReturnToLobbyMap()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JoinedTravelConfirmationTimer);
	}
	bAwaitingHostTravel = false;
	bAwaitingJoinedTravel = false;
	RankedSearchResults.Reset();
	SetMatchmakingState(EPHMatchmakingState::Idle);
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::OpenLevel(World, FName(*ReturnMap), true);
	}
}

void UPHSessionSubsystem::HandleCreateSessionComplete(const FName SessionName, const bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionDelegateHandle);
	}
	CreateSessionDelegateHandle.Reset();

	if (!bWasSuccessful || SessionName != NAME_GameSession)
	{
		FinishOperation(false, GetWorld() != nullptr && GetWorld()->GetNetMode() == NM_DedicatedServer
			? TEXT("La publication de la session dédiée a échoué.")
			: TEXT("La création du lobby Hunter a échoué."));
		return;
	}

	if (GetWorld() != nullptr && GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		SetMatchmakingState(EPHMatchmakingState::InSession);
		FinishOperation(true, TEXT("Serveur dédié publié et prêt à recevoir les clients."));
		return;
	}

	bAwaitingHostTravel = true;
	SetMatchmakingState(EPHMatchmakingState::Hosting);
	FinishOperation(true, TEXT("Lobby Hunter créé. Ouverture du listen server."));
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::OpenLevel(World, FName(*MatchMap), true, TEXT("listen"));
	}
}

void UPHSessionSubsystem::HandleFindSessionsComplete(const bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsDelegateHandle);
	}
	FindSessionsDelegateHandle.Reset();

	if (!bWasSuccessful || !ActiveSessionSearch.IsValid())
	{
		if (ActiveSearchPass == EPHSearchPass::DedicatedServer)
		{
			StartSearchPass(EPHSearchPass::Lobby);
			return;
		}
		if (!bAnySearchPassSucceeded)
		{
			FinishOperation(false, TEXT("La recherche Steam de sessions a échoué."));
			return;
		}
		if (!ActiveSessionSearch.IsValid())
		{
			ActiveSessionSearch = MakeShared<FOnlineSessionSearch>();
		}
	}
	else
	{
		bAnySearchPassSucceeded = true;
	}

	TArray<FPHLobbyPriority> Priorities;
	for (int32 SearchIndex = 0; SearchIndex < ActiveSessionSearch->SearchResults.Num(); ++SearchIndex)
	{
		const FOnlineSessionSearchResult& SearchResult = ActiveSessionSearch->SearchResults[SearchIndex];
		if (!SearchResult.IsValid())
		{
			continue;
		}

		FString Mode;
		FString Status;
		int32 AdvertisedProtocol = 0;
		FString CreatedAtText;
		SearchResult.Session.SessionSettings.Get(PHSessionModeKey, Mode);
		SearchResult.Session.SessionSettings.Get(PHSessionStatusKey, Status);
		SearchResult.Session.SessionSettings.Get(PHSessionProtocolKey, AdvertisedProtocol);
		SearchResult.Session.SessionSettings.Get(PHSessionCreatedAtKey, CreatedAtText);
		const bool bHunterAssigned = IsHunterAssignedStatus(Status);
		const FString State = IsLobbyStatus(Status) ? PHLobbyState : TEXT("Match");
		const bool bCompatibleHunterLobby = AdvertisedProtocol == ProtocolVersion
			&& Mode == PHHunterLobbyMode;
		if (!bCompatibleHunterLobby)
		{
			continue;
		}
		++SearchVisibleHunterLobbyCount;
		if (bHunterAssigned)
		{
			++SearchAssignedHunterLobbyCount;
		}
		else
		{
			++SearchUnassignedHunterLobbyCount;
		}
		if (!PHMatchmakingPolicy::IsCompatibleLobby(
			SearchResult.Session.NumOpenPublicConnections,
			AdvertisedProtocol,
			Mode,
			State,
			ProtocolVersion))
		{
			continue;
		}

		FPHLobbyPriority& Priority = Priorities.AddDefaulted_GetRef();
		Priority.CreatedAtUnixSeconds = FCString::Atoi64(*CreatedAtText);
		Priority.PingMilliseconds = SearchResult.PingInMs;
		Priority.OriginalSearchIndex = SearchIndex;
		Priority.SessionId = SearchResult.GetSessionIdStr();
	}

	PHMatchmakingPolicy::SortLobbyPriorities(Priorities);
	for (const FPHLobbyPriority& Priority : Priorities)
	{
		const FOnlineSessionSearchResult& RankedResult =
			ActiveSessionSearch->SearchResults[Priority.OriginalSearchIndex];
		RankedSearchResults.Add(RankedResult);
		FString Status;
		RankedResult.Session.SessionSettings.Get(PHSessionStatusKey, Status);
		if (!IsHunterAssignedStatus(Status))
		{
			RankedUnassignedSearchResults.Add(RankedResult);
			++SearchJoinableUnassignedHunterLobbyCount;
		}
	}

	if (ActiveSearchPass == EPHSearchPass::DedicatedServer)
	{
		StartSearchPass(EPHSearchPass::Lobby);
		return;
	}

	Availability.VisibleHunterLobbies = SearchVisibleHunterLobbyCount;
	Availability.JoinableHunterLobbies = RankedSearchResults.Num();
	Availability.AssignedHunterLobbies = SearchAssignedHunterLobbyCount;
	Availability.UnassignedHunterLobbies = SearchUnassignedHunterLobbyCount;
	Availability.bCanCreateHunterLobby = SearchVisibleHunterLobbyCount < FMath::Max(1, MaxHunterLobbies);
	OnAvailabilityChanged.Broadcast(Availability);

	if (PendingPreference == EPHMatchmakingPreference::Availability)
	{
		SetMatchmakingState(EPHMatchmakingState::Idle);
		return;
	}

	const EPHMatchmakingSearchDecision Decision = PHMatchmakingPolicy::DecideAfterSearch(
		PendingPreference,
		SearchVisibleHunterLobbyCount,
		RankedSearchResults.Num(),
		MaxHunterLobbies,
		SearchJoinableUnassignedHunterLobbyCount);
	if (Decision == EPHMatchmakingSearchDecision::WaitForAvailableSlot)
	{
		if (SearchVisibleHunterLobbyCount >= FMath::Max(1, MaxHunterLobbies))
		{
			FinishOperation(false, FString::Printf(
				TEXT("Tueur indisponible : les %d lobbies Hunter existent déjà. Tu restes en attente Tueur, ou tu peux choisir Survivant / Aléatoire."),
				FMath::Max(1, MaxHunterLobbies)));
		}
		else
		{
			FinishOperation(false, TEXT("Aucun lobby Hunter compatible et disponible n’a été trouvé."));
		}
		return;
	}

	if (Decision == EPHMatchmakingSearchDecision::CreateHunterLobby)
	{
		bWaitingForHunterSlot = false;
		FinishOperation(true, FString::Printf(
			TEXT("%d/%d lobby(s) Hunter visible(s). Création d’un nouveau lobby Hunter."),
			SearchVisibleHunterLobbyCount,
			FMath::Max(1, MaxHunterLobbies)));
		StartCreateLobby();
		return;
	}

	if (Decision == EPHMatchmakingSearchDecision::JoinHunterLobby)
	{
		RankedSearchResults = RankedUnassignedSearchResults;
	}

	SuggestedLobby = BuildLobbySummary(RankedSearchResults[0]);
	OnLobbySuggested.Broadcast(SuggestedLobby);
	SetMatchmakingState(EPHMatchmakingState::Idle);
	if (Decision == EPHMatchmakingSearchDecision::JoinHunterLobby)
	{
		FinishOperation(true, FString::Printf(
			TEXT("%d serveur(s) avec place Tueur disponible(s). Le premier serveur a la priorité."),
			RankedSearchResults.Num()));
	}
	else
	{
		FinishOperation(true, FString::Printf(
			TEXT("%d session(s) disponible(s). La première session a la priorité."),
			RankedSearchResults.Num()));
	}

	if (bAutoJoinFirstResult)
	{
		NextJoinIndex = 0;
		TryJoinNextLobby();
	}
}

void UPHSessionSubsystem::HandleJoinSessionComplete(
	const FName SessionName,
	const EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionDelegateHandle);
	}
	JoinSessionDelegateHandle.Reset();

	if (Result != EOnJoinSessionCompleteResult::Success || SessionName != NAME_GameSession)
	{
		LastJoinFailure = FString::Printf(
			TEXT("Steam JoinSession a échoué (code %d)."),
			static_cast<int32>(Result));
		UE_LOG(LogPHSteam, Warning, TEXT("Lobby join failed with result %d; trying next candidate."),
			static_cast<int32>(Result));
		BeginDestroySession(EPHPendingAfterDestroy::RetryNextLobby);
		return;
	}

	FString ConnectString;
	if (!Sessions.IsValid() || !Sessions->GetResolvedConnectString(NAME_GameSession, ConnectString))
	{
		LastJoinFailure = TEXT("Le lobby ne fournit aucune adresse de connexion Steam.");
		UE_LOG(LogPHSteam, Warning, TEXT("Joined lobby did not expose a connect string; trying next candidate."));
		BeginDestroySession(EPHPendingAfterDestroy::RetryNextLobby);
		return;
	}

	APlayerController* PlayerController = GetGameInstance() != nullptr
		? GetGameInstance()->GetFirstLocalPlayerController(GetWorld())
		: nullptr;
	if (PlayerController == nullptr)
	{
		LastJoinFailure = TEXT("Le contrôleur joueur local est introuvable avant ClientTravel.");
		BeginDestroySession(EPHPendingAfterDestroy::RetryNextLobby);
		return;
	}

	bAwaitingJoinedTravel = true;
	PlayerController->ClientTravel(ConnectString, TRAVEL_Absolute);
}

void UPHSessionSubsystem::HandleDestroySessionComplete(const FName SessionName, const bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		if (DestroySessionDelegateHandle.IsValid())
		{
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionDelegateHandle);
		}
	}
	DestroySessionDelegateHandle.Reset();

	const EPHPendingAfterDestroy Action = PendingAfterDestroy;
	PendingAfterDestroy = EPHPendingAfterDestroy::None;
	if (!bWasSuccessful)
	{
		UE_LOG(LogPHSteam, Warning, TEXT("DestroySession reported failure before action %d."),
			static_cast<int32>(Action));
	}

	switch (Action)
	{
	case EPHPendingAfterDestroy::CreateLobby:
		StartCreateLobby();
		break;
	case EPHPendingAfterDestroy::SearchLobbies:
		StartSearchLobbies();
		break;
	case EPHPendingAfterDestroy::RetryNextLobby:
		TryJoinNextLobby();
		break;
	case EPHPendingAfterDestroy::ReturnToResults:
		ReturnToResultsMap();
		break;
	case EPHPendingAfterDestroy::ReturnToLobby:
		ReturnToLobbyMap();
		break;
	case EPHPendingAfterDestroy::None:
	default:
		break;
	}
}

void UPHSessionSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (LoadedWorld == nullptr || LoadedWorld != GetWorld())
	{
		return;
	}

	if (LoadedWorld->GetNetMode() == NM_DedicatedServer
		&& bAutoCreateDedicatedSession
		&& MatchmakingState != EPHMatchmakingState::CreatingLobby
		&& GetSessionInterface().IsValid()
		&& GetSessionInterface()->GetNamedSession(NAME_GameSession) == nullptr)
	{
		UE_LOG(LogPHSteam, Log, TEXT("Dedicated world loaded; publishing the server session without a local player."));
		StartCreateLobby();
	}

	if (bAwaitingJoinedTravel)
	{
		const UNetDriver* NetDriver = LoadedWorld->GetNetDriver();
		const UNetConnection* ServerConnection = NetDriver != nullptr
			? NetDriver->ServerConnection
			: nullptr;
		const EConnectionState ConnectionState = ServerConnection != nullptr
			? ServerConnection->GetConnectionState()
			: EConnectionState::USOCK_Invalid;
		const bool bJoinedClientConnection = PHMatchmakingPolicy::IsJoinedClientConnection(
			LoadedWorld->GetNetMode() == NM_Client,
			ServerConnection != nullptr,
			ConnectionState == EConnectionState::USOCK_Open);
		if (bJoinedClientConnection)
		{
			CompleteJoinedTravelSuccess();
			return;
		}

		if (LoadedWorld->GetNetMode() == NM_Client
			&& ServerConnection != nullptr
			&& ConnectionState == EConnectionState::USOCK_Pending)
		{
			JoinedTravelConfirmationDeadlineSeconds = FPlatformTime::Seconds()
				+ FMath::Max(1.0f, JoinedTravelConfirmationTimeoutSeconds);
			LoadedWorld->GetTimerManager().SetTimer(
				JoinedTravelConfirmationTimer,
				this,
				&UPHSessionSubsystem::ConfirmJoinedTravel,
				0.1f,
				true);
			UE_LOG(LogPHSteam, Log,
				TEXT("Client map loaded with a pending Steam server connection; waiting up to %.1f seconds."),
				JoinedTravelConfirmationTimeoutSeconds);
			FinishOperation(true, TEXT("Carte chargée. Finalisation de la connexion réseau Steam..."));
			return;
		}

		if (!bJoinedClientConnection)
		{
			const FString FailureMessage = FString::Printf(
				TEXT("Le lobby Steam a été rejoint, mais la connexion au listen server n’a pas abouti (net mode %d, driver %s, connexion %s)."),
				static_cast<int32>(LoadedWorld->GetNetMode()),
				NetDriver != nullptr ? *NetDriver->GetClass()->GetName() : TEXT("absent"),
				ServerConnection != nullptr
					? LexToString(ConnectionState)
					: TEXT("absente"));
			UE_LOG(LogPHSteam, Error, TEXT("%s"), *FailureMessage);
			HandleJoinedTravelFailure(FailureMessage);
			return;
		}
	}
	else if (bAwaitingHostTravel)
	{
		bAwaitingHostTravel = false;
		if (LoadedWorld->GetNetMode() != NM_ListenServer)
		{
			FinishOperation(false, TEXT("Le lobby Hunter existe, mais la carte n’a pas démarré en listen server."));
			BeginDestroySession(EPHPendingAfterDestroy::ReturnToLobby);
			return;
		}

		SetMatchmakingState(EPHMatchmakingState::InSession);
		FinishOperation(true, TEXT("Listen server Hunter prêt."));
	}
}

void UPHSessionSubsystem::ConfirmJoinedTravel()
{
	if (!bAwaitingJoinedTravel)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(JoinedTravelConfirmationTimer);
		}
		return;
	}

	UWorld* World = GetWorld();
	const UNetDriver* NetDriver = World != nullptr ? World->GetNetDriver() : nullptr;
	const UNetConnection* ServerConnection = NetDriver != nullptr ? NetDriver->ServerConnection : nullptr;
	const EConnectionState ConnectionState = ServerConnection != nullptr
		? ServerConnection->GetConnectionState()
		: EConnectionState::USOCK_Invalid;

	if (World != nullptr
		&& PHMatchmakingPolicy::IsJoinedClientConnection(
			World->GetNetMode() == NM_Client,
			ServerConnection != nullptr,
			ConnectionState == EConnectionState::USOCK_Open))
	{
		CompleteJoinedTravelSuccess();
		return;
	}

	if (ConnectionState == EConnectionState::USOCK_Closed
		|| FPlatformTime::Seconds() >= JoinedTravelConfirmationDeadlineSeconds)
	{
		const FString FailureMessage = FString::Printf(
			TEXT("La connexion Steam au listen server n’est pas devenue active dans le délai (net mode %d, driver %s, connexion %s)."),
			World != nullptr ? static_cast<int32>(World->GetNetMode()) : -1,
			NetDriver != nullptr ? *NetDriver->GetClass()->GetName() : TEXT("absent"),
			ServerConnection != nullptr ? LexToString(ConnectionState) : TEXT("absente"));
		HandleJoinedTravelFailure(FailureMessage);
	}
}

void UPHSessionSubsystem::CompleteJoinedTravelSuccess()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JoinedTravelConfirmationTimer);
	}
	bAwaitingJoinedTravel = false;
	RankedSearchResults.Reset();
	LastJoinFailure.Reset();
	SetMatchmakingState(EPHMatchmakingState::InSession);
	FinishOperation(true, TEXT("Connexion au listen server Hunter réussie."));
}

void UPHSessionSubsystem::HandleNetworkFailure(
	UWorld* World,
	UNetDriver* NetDriver,
	const ENetworkFailure::Type FailureType,
	const FString& ErrorString)
{
	if (World != GetWorld())
	{
		return;
	}

	if (bAwaitingJoinedTravel)
	{
		HandleJoinedTravelFailure(ErrorString);
		return;
	}

	if (MatchmakingState == EPHMatchmakingState::InSession)
	{
		UE_LOG(LogPHSteam, Warning, TEXT("Host connection lost (%d): %s"),
			static_cast<int32>(FailureType), *ErrorString);
		LeaveSessionAndReturnToLobby();
	}
}

void UPHSessionSubsystem::HandleTravelFailure(
	UWorld* World,
	const ETravelFailure::Type FailureType,
	const FString& ErrorString)
{
	if (World != GetWorld())
	{
		return;
	}

	if (bAwaitingJoinedTravel)
	{
		UE_LOG(LogPHSteam, Warning, TEXT("Joined lobby travel failed (%d): %s"),
			static_cast<int32>(FailureType), *ErrorString);
		HandleJoinedTravelFailure(ErrorString);
	}
	else if (bAwaitingHostTravel)
	{
		bAwaitingHostTravel = false;
		FinishOperation(false, FString::Printf(TEXT("Le voyage du lobby Hunter a échoué : %s"), *ErrorString));
		BeginDestroySession(EPHPendingAfterDestroy::ReturnToLobby);
	}
}

void UPHSessionSubsystem::HandleJoinedTravelFailure(const FString& ErrorString)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JoinedTravelConfirmationTimer);
	}
	bAwaitingJoinedTravel = false;
	LastJoinFailure = ErrorString.IsEmpty()
		? TEXT("La connexion réseau au listen server a échoué sans détail.")
		: ErrorString;
	UE_LOG(LogPHSteam, Warning, TEXT("Lobby candidate became unusable: %s. Trying next lobby."), *ErrorString);
	BeginDestroySession(EPHPendingAfterDestroy::RetryNextLobby);
}

FPHLobbySummary UPHSessionSubsystem::BuildLobbySummary(const FOnlineSessionSearchResult& SearchResult) const
{
	FPHLobbySummary Summary;
	Summary.HostName = SearchResult.Session.OwningUserName;
	Summary.SessionId = SearchResult.GetSessionIdStr();
	Summary.PingMilliseconds = SearchResult.PingInMs;
	Summary.OpenSlots = SearchResult.Session.NumOpenPublicConnections;
	Summary.MaximumPlayers = SearchResult.Session.SessionSettings.NumPublicConnections;
	return Summary;
}
