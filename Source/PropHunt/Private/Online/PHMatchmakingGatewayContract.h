#pragma once

#include "CoreMinimal.h"
#include "Online/PHSessionSubsystem.h"

enum class EPHGatewayTicketStatus : uint8
{
	Pending,
	Ready,
	Failed,
	Cancelled
};

struct FPHGatewayTicket
{
	EPHGatewayTicketStatus Status = EPHGatewayTicketStatus::Pending;
	FString TicketId;
	FString MatchId;
	FString ReservationId;
	FString ConnectAddress;
	FString FailureCode;
	FString FailureMessage;
	FString LobbyId;
	FString LobbyPhase;
	EPHPlayerRole AssignedRole = EPHPlayerRole::Unassigned;
	FDateTime ExpiresUtc;
	FDateTime LobbyCountdownEndsUtc;
	int32 SurvivorsPresent = 0;
	int32 SurvivorSlots = 4;
	int32 AssignedSurvivorSlot = 0;
	TArray<int32> OccupiedSurvivorSlots;
	bool bHasLobby = false;
	bool bLobbyLocked = false;
	float RetryAfterSeconds = 0.5f;
};

namespace PHMatchmakingGatewayContract
{
	inline constexpr int32 SchemaVersion = 1;
	inline constexpr int32 MaxResponseBytes = 64 * 1024;
	inline constexpr int32 MinRetryAfterMs = 250;
	inline constexpr int32 MaxRetryAfterMs = 5000;

	PROPHUNT_API bool NormalizeGatewayBaseUrl(
		const FString& RawUrl,
		bool bShippingBuild,
		FString& OutNormalizedUrl,
		FString& OutError);

	PROPHUNT_API bool NormalizeConnectAddress(
		const FString& RawAddress,
		FString& OutNormalizedAddress,
		FString& OutError);

	PROPHUNT_API const TCHAR* RolePreferenceToString(EPHMatchmakingPreference Preference);

	PROPHUNT_API bool IsSafeLobbyInviteSecret(const FString& InviteSecret);

	PROPHUNT_API bool ParseLobbyInviteResponse(
		const FString& Json,
		int32 ExpectedProtocolVersion,
		const FString& ExpectedBuildId,
		const FDateTime& NowUtc,
		FString& OutInviteSecret,
		FDateTime& OutExpiresUtc,
		FString& OutError);

	PROPHUNT_API bool ParseTicketStatus(
		const FString& Json,
		const FString& ExpectedTicketId,
		int32 ExpectedProtocolVersion,
		const FString& ExpectedBuildId,
		const FDateTime& NowUtc,
		FPHGatewayTicket& OutTicket,
		FString& OutError);
}
