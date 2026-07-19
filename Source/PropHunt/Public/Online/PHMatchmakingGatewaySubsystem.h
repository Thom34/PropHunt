#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HttpFwd.h"
#include "Online/PHSessionSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "PHMatchmakingGatewaySubsystem.generated.h"

struct FExternalAuthToken;
struct FPHGatewayTicket;
DECLARE_DELEGATE_ThreeParams(
	FPHLobbyInviteTokenCallback,
	bool,
	const FString&,
	const FString&);

UENUM(BlueprintType)
enum class EPHGatewayMatchmakingState : uint8
{
	Idle,
	Authenticating,
	Queued,
	Allocating,
	Connecting,
	Cancelling,
	Failed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FPHGatewayMatchmakingStateChanged,
	EPHGatewayMatchmakingState,
	NewState,
	const FString&,
	StatusMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FPHGatewayMatchmakingFinished,
	bool,
	bSucceeded,
	const FString&,
	Message);

/** Client-only adapter for the public PropHunt matchmaking gateway. */
UCLASS(Config = Game, DefaultConfig)
class PROPHUNT_API UPHMatchmakingGatewaySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Matchmaking")
	bool RequestTicket(EPHMatchmakingPreference Preference, int32 LocalUserNum = 0);

	/** Creates an authenticated ticket directly in the lobby identified by an invite secret. */
	bool RequestTicketFromLobbyInvite(const FString& InviteSecret, int32 LocalUserNum = 0);

	/** Requests a short-lived invite secret for the currently visible waiting lobby. */
	bool RequestLobbyInviteToken(FPHLobbyInviteTokenCallback Callback);

	UFUNCTION(BlueprintCallable, Category = "PropHunt|Matchmaking")
	void CancelTicket();

	/** Releases this client's ready reservation after a match and restores a fresh frontend state. */
	UFUNCTION(BlueprintCallable, Category = "PropHunt|Matchmaking")
	void CompleteMatchAndReset();

	UFUNCTION(BlueprintPure, Category = "PropHunt|Matchmaking")
	EPHGatewayMatchmakingState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Matchmaking")
	bool IsBusy() const { return State != EPHGatewayMatchmakingState::Idle && State != EPHGatewayMatchmakingState::Failed; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Matchmaking")
	FString GetTicketId() const { return TicketId; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Matchmaking")
	bool HasLobbySnapshot() const { return bHasLobbySnapshot; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Matchmaking")
	EPHPlayerRole GetLobbyAssignedRole() const { return LobbyAssignedRole; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Matchmaking")
	FString GetLobbyPhase() const { return LobbyPhase; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Matchmaking")
	int32 GetLobbySurvivorsPresent() const { return LobbySurvivorsPresent; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Matchmaking")
	int32 GetLobbySurvivorSlots() const { return LobbySurvivorSlots; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Matchmaking")
	TArray<int32> GetOccupiedSurvivorSlots() const { return OccupiedSurvivorSlots; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Matchmaking")
	int32 GetAssignedSurvivorSlot() const { return AssignedSurvivorSlot; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Matchmaking")
	int32 GetLobbyCountdownSeconds() const;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Matchmaking")
	bool IsLobbyLocked() const { return bLobbyLocked; }

	FString GetLobbyId() const { return LobbyId; }

	UPROPERTY(BlueprintAssignable, Category = "PropHunt|Matchmaking")
	FPHGatewayMatchmakingStateChanged OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "PropHunt|Matchmaking")
	FPHGatewayMatchmakingFinished OnFinished;

private:
	void HandleSteamAuthToken(
		int32 CallbackLocalUserNum,
		bool bWasSuccessful,
		const FExternalAuthToken& AuthToken,
		uint64 CallbackGeneration,
		int32 ExpectedLocalUserNum,
		EPHMatchmakingPreference ExpectedPreference);
	void HandleCompletionAuthToken(
		int32 CallbackLocalUserNum,
		bool bWasSuccessful,
		const FExternalAuthToken& AuthToken,
		FString CompletionUrl);
	bool HandleSteamAuthTimeout(float DeltaSeconds, uint64 CallbackGeneration);
	bool StartTicketRequest(EPHMatchmakingPreference Preference, const FString& PlayerAccessToken);
	bool BeginTicketAuthentication(
		EPHMatchmakingPreference Preference,
		int32 LocalUserNum,
		const FString& LobbyInviteSecret);
	void HandleLobbyInviteTokenResponse(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bConnectedSuccessfully);
	void HandleTicketResponse(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bConnectedSuccessfully,
		uint64 CallbackGeneration,
		FString ExpectedTicketId,
		bool bWasPollRequest,
		bool bWasCancelRequest);
	void StartPollRequest();
	void StartCancelRequest();
	void SchedulePoll(float DelaySeconds);
	bool HandlePollTicker(float DeltaSeconds, uint64 CallbackGeneration);
	void TravelToReadyTicket(const FPHGatewayTicket& ReadyTicket);
	void SetState(EPHGatewayMatchmakingState NewState, const FString& Message);
	void FinishFailure(const FString& Message);
	void BestEffortCancelActiveTicket();
	void ClearHttpAndTickers();
	void ClearSensitiveState();
	void UpdateLobbySnapshot(const FPHGatewayTicket& Ticket);
	void ClearLobbySnapshot();
	int32 ResolveProtocolVersion() const;
	FString ResolveBuildId() const;

	/** No credential belongs in this value. Shipping is pinned to the public TLS origin. */
	UPROPERTY(Config)
	FString GatewayBaseUrl = TEXT("https://gateway.meteothom.com");

	UPROPERTY(Config)
	FString TicketPath = TEXT("/v1/prophunt/tickets");

	UPROPERTY(Config)
	float RequestTimeoutSeconds = 20.0f;

	UPROPERTY(Config)
	float MatchmakingTimeoutSeconds = 300.0f;

	FHttpRequestPtr ActiveRequest;
	FHttpRequestPtr LobbyInviteRequest;
	FPHLobbyInviteTokenCallback LobbyInviteTokenCallback;
	FTSTicker::FDelegateHandle SteamAuthTimeoutTickerHandle;
	FTSTicker::FDelegateHandle PollTickerHandle;
	uint64 RequestGeneration = 0;
	EPHGatewayMatchmakingState State = EPHGatewayMatchmakingState::Idle;
	EPHMatchmakingPreference PendingPreference = EPHMatchmakingPreference::Random;
	FString TicketId;
	FString PendingGatewayBaseUrl;
	FString PendingTicketPath;
	FString PendingPlayerAccessToken;
	FString PendingLobbyInviteSecret;
	FDateTime PendingDeadlineUtc;
	FDateTime LobbyCountdownEndsUtc;
	EPHPlayerRole LobbyAssignedRole = EPHPlayerRole::Unassigned;
	FString LobbyPhase;
	FString LobbyId;
	int32 LobbySurvivorsPresent = 0;
	int32 LobbySurvivorSlots = 4;
	int32 AssignedSurvivorSlot = 0;
	TArray<int32> OccupiedSurvivorSlots;
	bool bHasLobbySnapshot = false;
	bool bLobbyLocked = false;
	bool bSteamAuthTokenRequestInProgress = false;
};
