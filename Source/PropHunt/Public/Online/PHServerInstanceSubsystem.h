#pragma once

#include "CoreMinimal.h"
#include "Game/PHMatchTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"

#include "PHServerInstanceSubsystem.generated.h"

struct FUniqueNetIdRepl;

/**
 * Dedicated-server boundary for NOVA worker identity and admission rosters.
 * All trusted values come from the worker command line or its isolated
 * Saved/Runtime directory, never from a joining client's travel URL.
 */
UCLASS(Config = Game)
class PROPHUNT_API UPHServerInstanceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool IsNOVAReservationRequired() const { return bRequireNOVAReservation; }
	bool IsRuntimeIdentityValid() const { return RuntimeIdentityValidationError.IsEmpty(); }
	const FString& GetRuntimeIdentityValidationError() const { return RuntimeIdentityValidationError; }

	bool IsAdmissionRosterReady(FString* OutError = nullptr) const;
	int32 GetExpectedPlayerCount() const;

	bool ResolveAuthorizedRole(
		const FUniqueNetIdRepl& UniqueId,
		EPHPlayerRole& OutRole,
		FString& OutError) const;

	void StartRuntimeHeartbeat(const FString& MapName, int32 MaxPlayers);

private:
	bool LoadAdmissionRoster(
		int32& OutExpectedPlayers,
		const FString& IdentityKey,
		bool bRequireIdentity,
		EPHPlayerRole& OutRole,
		FString& OutError) const;
	void TickRuntimeHeartbeat();
	void WriteRuntimeHeartbeat(bool bStopping = false);
	bool ResolveSteamConnectAddress(FString& OutConnectAddress) const;

	FString ServerInstanceId;
	FString RunId;
	FString MatchId;
	FString RuntimeIdentityValidationError;
	FString AdmissionRosterFilePath;
	FString HeartbeatFilePath;
	FString HeartbeatMapName;

	UPROPERTY(Config)
	FString NOVAProjectBuildId = TEXT("0.1.1907001");

	FTimerHandle HeartbeatTimerHandle;
	uint64 HeartbeatSequence = 0;
	int32 HeartbeatGamePort = 7777;
	int32 HeartbeatQueryPort = 27015;
	int32 HeartbeatMaxPlayers = 5;
	int32 HeartbeatEmptyShutdownSeconds = 0;
	FDateTime HeartbeatEmptySinceUtc;
	bool bDedicatedServer = false;
	bool bRequireNOVAReservation = false;
	bool bHeartbeatStarted = false;
	bool bEmptyShutdownRequested = false;
};
