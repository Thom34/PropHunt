#pragma once

#include "CoreMinimal.h"
#include "Game/PHMatchTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "PHSessionSubsystem.generated.h"

UENUM(BlueprintType)
enum class EPHMatchmakingState : uint8
{
	Idle,
	CreatingLobby,
	Searching,
	Joining,
	Hosting,
	InSession,
	Leaving,
	Failed
};

UENUM(BlueprintType)
enum class EPHMatchmakingPreference : uint8
{
	Hunter,
	Prop,
	Random,
	Availability
};

enum class EPHMatchmakingSearchDecision : uint8
{
	CreateHunterLobby,
	JoinHunterLobby,
	JoinPropLobby,
	WaitForAvailableSlot
};

struct PROPHUNT_API FPHSessionHostingPolicy
{
	bool bIsDedicated = false;
	bool bUsesPresence = true;
	bool bUseLobbies = true;
	bool bAllowInvites = true;
};

USTRUCT(BlueprintType)
struct PROPHUNT_API FPHLobbySummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Steam")
	FString HostName;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Steam")
	FString SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Steam")
	int32 PingMilliseconds = -1;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Steam")
	int32 OpenSlots = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Steam")
	int32 MaximumPlayers = 0;
};

USTRUCT(BlueprintType)
struct PROPHUNT_API FPHMatchmakingAvailability
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Steam")
	int32 VisibleHunterLobbies = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Steam")
	int32 JoinableHunterLobbies = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Steam")
	int32 AssignedHunterLobbies = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Steam")
	int32 UnassignedHunterLobbies = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PropHunt|Steam")
	bool bCanCreateHunterLobby = true;
};

struct PROPHUNT_API FPHLobbyPriority
{
	int64 CreatedAtUnixSeconds = 0;
	int32 PingMilliseconds = -1;
	int32 OriginalSearchIndex = INDEX_NONE;
	FString SessionId;
};

namespace PHMatchmakingPolicy
{
	PROPHUNT_API bool IsCompatibleLobby(
		int32 OpenPublicSlots,
		int32 AdvertisedProtocol,
		const FString& AdvertisedMode,
		const FString& AdvertisedState,
		int32 RequiredProtocol);

	PROPHUNT_API void SortLobbyPriorities(TArray<FPHLobbyPriority>& Priorities);

	PROPHUNT_API EPHMatchmakingSearchDecision DecideAfterSearch(
		EPHMatchmakingPreference Preference,
		int32 VisibleHunterLobbyCount,
		int32 AvailableHunterLobbyCount,
		int32 MaximumHunterLobbyCount,
		int32 AvailableUnassignedHunterLobbyCount = 0,
		bool bAllowListenServerCreation = false);

	PROPHUNT_API FPHSessionHostingPolicy ResolveSessionHostingPolicy(bool bIsDedicatedServer);

	PROPHUNT_API EPHPlayerRole ResolveListenServerRole(
		bool bIsLocalController,
		bool bHunterAlreadyAssigned);

	PROPHUNT_API EPHPlayerRole ResolveAuthoritativeServerRole(
		bool bIsLocalController,
		bool bIsDedicatedServer,
		bool bHunterAlreadyAssigned);

	PROPHUNT_API int32 ChooseRandomHunterIndex(int32 CandidateCount, int32 RandomSeed);

	PROPHUNT_API bool IsJoinedClientConnection(
		bool bIsClientWorld,
		bool bHasServerConnection,
		bool bServerConnectionOpen);

	PROPHUNT_API bool IsDedicatedSteamBackendAvailable(
		FName SubsystemName,
		bool bSubsystemEnabled,
		bool bSessionInterfaceValid);

	PROPHUNT_API bool ShouldLaunchReadyTwoPlayerTest(
		int32 ConnectedPlayerCount,
		int32 ReadyPlayerCount,
		int32 MinimumPropPlayers,
		bool bDedicatedServer,
		bool bLobbyPhase);

	PROPHUNT_API bool IsRosterAdmissionLocked(
		bool bNetworkWaitingRoom,
		bool bMatchFlowHasStarted,
		bool bRosterLockedFromTravel);

	PROPHUNT_API bool IsValidDirectConnectEndpoint(const FString& Endpoint);
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPHMatchmakingStateChanged, EPHMatchmakingState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPHLobbySuggested, const FPHLobbySummary&, Lobby);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FPHMatchmakingAvailabilityChanged,
	const FPHMatchmakingAvailability&,
	Availability);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPHMatchmakingFinished, bool, bSucceeded, const FString&, Message);

UCLASS(Config = Game)
class PROPHUNT_API UPHSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Steam")
	void HostHunterLobby();

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Steam")
	void FindPropLobby(bool bAutoJoinFirstAvailable = true);

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Steam")
	void FindRandomRole();

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Steam")
	void RefreshAvailability();

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Steam")
	void JoinSuggestedLobby();

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Steam")
	void LeaveSessionAndReturnToLobby();

	/** Closes the Steam connection, releases the NOVA ticket, then quits once both operations finish. */
	UFUNCTION(BlueprintCallable, Category = "PropHunt|Steam")
	void LeaveSessionAndQuitGame();

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Results")
	void LeaveSessionAndShowResults();

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Results")
	void ContinueFromResultsToLobby();

	UFUNCTION(BlueprintPure, Category = "PropHunt|Results")
	bool IsCurrentWorldResultsMap() const;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Steam")
	bool IsCurrentWorldLobbyMap() const;

	bool IsCurrentWorldFrontendMap() const;

	const FString& GetMatchMap() const
	{
		return MatchMap;
	}

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Results")
	void CacheMatchResults(
		const FPHMatchResultsSnapshot& Snapshot,
		EPHPlayerRole LocalPlayerRole,
		const FString& LocalPlayerName,
		int32 LocalPlayerId);

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Results")
	void ClearCachedMatchResults();

	UFUNCTION(BlueprintPure, Category = "PropHunt|Results")
	bool HasCachedMatchResults() const { return CachedMatchResults.bFinalized; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Results")
	FPHMatchResultsSnapshot GetCachedMatchResults() const { return CachedMatchResults; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Results")
	EPHPlayerRole GetCachedResultsLocalRole() const { return CachedResultsLocalRole; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Results")
	FString GetCachedResultsLocalPlayerName() const { return CachedResultsLocalPlayerName; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Results")
	int32 GetCachedResultsLocalPlayerId() const { return CachedResultsLocalPlayerId; }

	void SetHunterLobbyJoinable(bool bJoinable);
	void SetHunterAssigned(bool bAssigned);

	UFUNCTION(BlueprintPure, Category = "PropHunt|Steam")
	EPHMatchmakingState GetMatchmakingState() const
	{
		return MatchmakingState;
	}

	UFUNCTION(BlueprintPure, Category = "PropHunt|Steam")
	FPHLobbySummary GetSuggestedLobby() const
	{
		return SuggestedLobby;
	}

	UFUNCTION(BlueprintPure, Category = "PropHunt|Steam")
	FPHMatchmakingAvailability GetAvailability() const
	{
		return Availability;
	}

	UFUNCTION(BlueprintPure, Category = "PropHunt|Steam")
	bool IsWaitingForHunterSlot() const
	{
		return bWaitingForHunterSlot;
	}

	UFUNCTION(BlueprintPure, Category = "PropHunt|Steam")
	bool IsSteamAvailable() const;

	/** Opens Steam's in-game friends panel so the player can invite a friend to launch PropHunt. */
	UFUNCTION(BlueprintCallable, Category = "PropHunt|Steam")
	bool ShowSteamFriendsUI();

	UFUNCTION(BlueprintPure, Category = "PropHunt|Steam")
	int32 GetProtocolVersion() const
	{
		return ProtocolVersion;
	}

	UFUNCTION(BlueprintPure, Category = "PropHunt|Steam")
	FString GetReleaseVersion() const
	{
		return ReleaseVersion;
	}

	float GetJoinedTravelConfirmationTimeoutSeconds() const
	{
		return JoinedTravelConfirmationTimeoutSeconds;
	}

	bool GetAutoCreateDedicatedSession() const
	{
		return bAutoCreateDedicatedSession;
	}

	UPROPERTY(BlueprintAssignable, Category = "PropHunt|Steam")
	FPHMatchmakingStateChanged OnMatchmakingStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "PropHunt|Steam")
	FPHLobbySuggested OnLobbySuggested;

	UPROPERTY(BlueprintAssignable, Category = "PropHunt|Steam")
	FPHMatchmakingAvailabilityChanged OnAvailabilityChanged;

	UPROPERTY(BlueprintAssignable, Category = "PropHunt|Steam")
	FPHMatchmakingFinished OnMatchmakingFinished;

private:
	enum class EPHPendingAfterDestroy : uint8
	{
		None,
		CreateLobby,
		SearchLobbies,
		RetryNextLobby,
		ReturnToResults,
		ReturnToLobby,
		QuitGame
	};

	enum class EPHSearchPass : uint8
	{
		DedicatedServer,
		Lobby
	};

	IOnlineSessionPtr GetSessionInterface() const;
	bool ValidateSteamForOperation(const TCHAR* OperationName);
	void SetMatchmakingState(EPHMatchmakingState NewState);
	void FinishOperation(bool bSucceeded, const FString& Message);
	void StartCreateLobby();
	void StartSearchLobbies();
	void StartSearchPass(EPHSearchPass SearchPass);
	void TryJoinNextLobby();
	void BeginDestroySession(EPHPendingAfterDestroy PendingAction);
	void ReturnToResultsMap();
	void ReturnToLobbyMap();
	void HandleQuitGatewayReleaseFinished(bool bSucceeded);
	void TryFinishRequestedQuit();
	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);
	void HandlePostLoadMap(UWorld* LoadedWorld);
#if !UE_BUILD_SHIPPING
	void TryDirectConnectFromCommandLine();
#endif
	void ConfirmJoinedTravel();
	void CompleteJoinedTravelSuccess();
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	void HandleJoinedTravelFailure(const FString& ErrorString);
	FPHLobbySummary BuildLobbySummary(const FOnlineSessionSearchResult& SearchResult) const;

	UPROPERTY(Config)
	FString MatchMap = TEXT("/Game/PropHunt/Tests/L_PH_CaptureTest");

	UPROPERTY(Config)
	FString ResultsMap = TEXT("/Game/PropHunt/Maps/L_PH_Results");

	UPROPERTY(Config)
	FString ReturnMap = TEXT("/Game/PropHunt/Tests/L_PH_Intro");

	UPROPERTY(Config)
	int32 MaxLobbyPlayers = 5;

	UPROPERTY(Config)
	int32 MaxSearchResults = 50;

	UPROPERTY(Config)
	int32 MaxHunterLobbies = 2;

	UPROPERTY(Config)
	int32 ProtocolVersion = 91;

	/** Exact client/server/gateway release identifier. Dotted because NOVA build IDs are strings. */
	UPROPERTY(Config)
	FString ReleaseVersion = TEXT("0.1.1907002");

	UPROPERTY(Config)
	float JoinedTravelConfirmationTimeoutSeconds = 30.0f;

	UPROPERTY(Config)
	bool bAutoCreateDedicatedSession = true;

	UPROPERTY(Transient)
	EPHMatchmakingState MatchmakingState = EPHMatchmakingState::Idle;

	UPROPERTY(Transient)
	FPHLobbySummary SuggestedLobby;

	UPROPERTY(Transient)
	FPHMatchmakingAvailability Availability;

	TSharedPtr<FOnlineSessionSettings> ActiveSessionSettings;
	TSharedPtr<FOnlineSessionSearch> ActiveSessionSearch;
	TArray<FOnlineSessionSearchResult> RankedSearchResults;
	TArray<FOnlineSessionSearchResult> RankedUnassignedSearchResults;

	UPROPERTY(Transient)
	FPHMatchResultsSnapshot CachedMatchResults;

	UPROPERTY(Transient)
	EPHPlayerRole CachedResultsLocalRole = EPHPlayerRole::Unassigned;

	UPROPERTY(Transient)
	FString CachedResultsLocalPlayerName;

	UPROPERTY(Transient)
	int32 CachedResultsLocalPlayerId = INDEX_NONE;
	int32 NextJoinIndex = 0;
	int32 SearchVisibleHunterLobbyCount = 0;
	int32 SearchAssignedHunterLobbyCount = 0;
	int32 SearchUnassignedHunterLobbyCount = 0;
	int32 SearchJoinableUnassignedHunterLobbyCount = 0;
	FString LastJoinFailure;
	EPHSearchPass ActiveSearchPass = EPHSearchPass::DedicatedServer;
	bool bAnySearchPassSucceeded = false;
	bool bAutoJoinFirstResult = true;
	bool bAwaitingHostTravel = false;
	bool bAwaitingJoinedTravel = false;
	bool bWaitingForHunterSlot = false;
#if !UE_BUILD_SHIPPING
	bool bDirectConnectAttempted = false;
	FString PendingDirectConnectEndpoint;
#endif
	EPHMatchmakingPreference PendingPreference = EPHMatchmakingPreference::Prop;
	EPHPendingAfterDestroy PendingAfterDestroy = EPHPendingAfterDestroy::None;
	bool bQuitRequested = false;
	bool bQuitSessionFinished = false;
	bool bQuitGatewayFinished = false;

	FDelegateHandle CreateSessionDelegateHandle;
	FDelegateHandle FindSessionsDelegateHandle;
	FDelegateHandle JoinSessionDelegateHandle;
	FDelegateHandle DestroySessionDelegateHandle;
	FDelegateHandle PostLoadMapDelegateHandle;
	FTimerHandle JoinedTravelConfirmationTimer;
	double JoinedTravelConfirmationDeadlineSeconds = 0.0;
};
