#pragma once

#include "CoreMinimal.h"
#include "Game/PHMatchTypes.h"

struct FPHServerRuntimeIdentity
{
	FString ServerInstanceId;
	FString RunId;
	FString MatchId;
	FString ValidationError;
	bool bDedicatedServer = false;
	bool bRequireNOVAReservation = false;

	bool IsValid() const { return ValidationError.IsEmpty(); }
};

struct FPHAdmissionRosterEntry
{
	FString IdentityKey;
	EPHPlayerRole Role = EPHPlayerRole::Unassigned;
};

struct FPHAdmissionRosterSnapshot
{
	int32 SchemaVersion = 1;
	int32 ProtocolVersion = 0;
	FString BuildId;
	FString ServerInstanceId;
	FString RunId;
	FString MatchId;
	FString ReservationId;
	FDateTime ExpiresAtUtc;
	int32 ExpectedPlayers = 0;
	TArray<FPHAdmissionRosterEntry> Players;
};

namespace PHServerInstanceContract
{
	inline constexpr int32 ProtocolVersion = 91;
	inline constexpr const TCHAR* BuildId = TEXT("0.1.1907002");
	inline constexpr int32 MinimumRosterPlayers = 2;
	inline constexpr int32 MaximumRosterPlayers = 5;

	PROPHUNT_API bool IsValidIdentifier(const FString& Value);
	PROPHUNT_API bool ShouldRequireNOVAReservation(bool bDedicatedServer, bool bShippingBuild);

	PROPHUNT_API FPHServerRuntimeIdentity ResolveCommandLine(
		const TCHAR* CommandLine,
		bool bDedicatedServer,
		const FString& GeneratedSuffix);

	PROPHUNT_API bool ParseAdmissionRoster(
		const FString& Json,
		const FPHServerRuntimeIdentity& ExpectedIdentity,
		const FDateTime& NowUtc,
		FPHAdmissionRosterSnapshot& OutRoster,
		FString& OutError);

	PROPHUNT_API bool ResolveAuthorizedRole(
		const FPHAdmissionRosterSnapshot& Roster,
		const FString& IdentityKey,
		EPHPlayerRole& OutRole,
		FString& OutError);
}
