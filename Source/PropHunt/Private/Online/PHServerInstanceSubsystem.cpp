#include "Online/PHServerInstanceSubsystem.h"

#include "Online/PHServerInstanceContract.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHServerInstance, Log, All);

void UPHServerInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const FPHServerRuntimeIdentity Identity = PHServerInstanceContract::ResolveCommandLine(
		FCommandLine::Get(),
		IsRunningDedicatedServer(),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	ServerInstanceId = Identity.ServerInstanceId;
	RunId = Identity.RunId;
	MatchId = Identity.MatchId;
	RuntimeIdentityValidationError = Identity.ValidationError;
	bDedicatedServer = Identity.bDedicatedServer;
	bRequireNOVAReservation = Identity.bRequireNOVAReservation;
	if (PHServerInstanceContract::ShouldRequireNOVAReservation(
		bDedicatedServer,
		UE_BUILD_SHIPPING != 0)
		&& !bRequireNOVAReservation)
	{
		RuntimeIdentityValidationError = TEXT("RequireNOVAReservation est obligatoire sur un serveur dedie Shipping");
	}
	AdmissionRosterFilePath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Runtime"), TEXT("admission-roster.json"));
	HeartbeatFilePath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Runtime"), TEXT("worker-heartbeat.json"));

	if (!RuntimeIdentityValidationError.IsEmpty())
	{
		UE_LOG(LogPHServerInstance, Error, TEXT("NOVA worker identity rejected: %s"),
			*RuntimeIdentityValidationError);
		if (bDedicatedServer)
		{
			FPlatformMisc::RequestExitWithStatus(true, 2);
		}
		return;
	}

	if (bRequireNOVAReservation)
	{
		UE_LOG(LogPHServerInstance, Display,
			TEXT("NOVA worker identity ready instance=%s run=%s match=%s roster=%s"),
			*ServerInstanceId, *RunId, *MatchId, *AdmissionRosterFilePath);
	}
	else if (bDedicatedServer)
	{
		UE_LOG(LogPHServerInstance, Warning,
			TEXT("Dedicated development worker is not protected by RequireNOVAReservation."));
	}
}

void UPHServerInstanceSubsystem::Deinitialize()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().ClearTimer(HeartbeatTimerHandle);
	}
	if (bHeartbeatStarted)
	{
		WriteRuntimeHeartbeat(true);
	}
	bHeartbeatStarted = false;
	Super::Deinitialize();
}

bool UPHServerInstanceSubsystem::IsAdmissionRosterReady(FString* OutError) const
{
	if (!bRequireNOVAReservation)
	{
		if (OutError != nullptr)
		{
			OutError->Reset();
		}
		return true;
	}

	int32 ExpectedPlayers = 0;
	EPHPlayerRole IgnoredRole = EPHPlayerRole::Unassigned;
	FString Error;
	const bool bReady = LoadAdmissionRoster(
		ExpectedPlayers, FString(), false, IgnoredRole, Error);
	if (OutError != nullptr)
	{
		*OutError = Error;
	}
	return bReady;
}

int32 UPHServerInstanceSubsystem::GetExpectedPlayerCount() const
{
	if (!bRequireNOVAReservation)
	{
		return 0;
	}
	int32 ExpectedPlayers = 0;
	EPHPlayerRole IgnoredRole = EPHPlayerRole::Unassigned;
	FString Error;
	return LoadAdmissionRoster(
		ExpectedPlayers, FString(), false, IgnoredRole, Error)
		? ExpectedPlayers
		: 0;
}

bool UPHServerInstanceSubsystem::ResolveAuthorizedRole(
	const FUniqueNetIdRepl& UniqueId,
	EPHPlayerRole& OutRole,
	FString& OutError) const
{
	OutRole = EPHPlayerRole::Unassigned;
	OutError.Reset();
	if (!bRequireNOVAReservation)
	{
		OutError = TEXT("NOVA reservation is not enabled for this worker");
		return false;
	}
	if (!UniqueId.IsValid())
	{
		OutError = TEXT("identite Steam authentifiee requise");
		return false;
	}

	const FString IdentityKey = FString::Printf(
		TEXT("%s:%s"), *UniqueId.GetType().ToString(), *UniqueId.ToString());
	int32 ExpectedPlayers = 0;
	return LoadAdmissionRoster(
		ExpectedPlayers, IdentityKey, true, OutRole, OutError);
}

bool UPHServerInstanceSubsystem::LoadAdmissionRoster(
	int32& OutExpectedPlayers,
	const FString& IdentityKey,
	const bool bRequireIdentity,
	EPHPlayerRole& OutRole,
	FString& OutError) const
{
	OutExpectedPlayers = 0;
	OutRole = EPHPlayerRole::Unassigned;
	OutError.Reset();
	if (!bDedicatedServer || !bRequireNOVAReservation || !IsRuntimeIdentityValid())
	{
		OutError = TEXT("worker NOVA non initialise");
		return false;
	}

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *AdmissionRosterFilePath))
	{
		OutError = TEXT("roster NOVA indisponible");
		return false;
	}

	FPHServerRuntimeIdentity ExpectedIdentity;
	ExpectedIdentity.ServerInstanceId = ServerInstanceId;
	ExpectedIdentity.RunId = RunId;
	ExpectedIdentity.MatchId = MatchId;
	ExpectedIdentity.bDedicatedServer = bDedicatedServer;
	ExpectedIdentity.bRequireNOVAReservation = bRequireNOVAReservation;
	FPHAdmissionRosterSnapshot Roster;
	if (!PHServerInstanceContract::ParseAdmissionRoster(
		Json, ExpectedIdentity, FDateTime::UtcNow(), Roster, OutError))
	{
		return false;
	}

	OutExpectedPlayers = Roster.ExpectedPlayers;
	return !bRequireIdentity
		|| PHServerInstanceContract::ResolveAuthorizedRole(
			Roster, IdentityKey, OutRole, OutError);
}

void UPHServerInstanceSubsystem::StartRuntimeHeartbeat(
	const FString& MapName,
	const int32 MaxPlayers)
{
	if (!bDedicatedServer || !bRequireNOVAReservation || !IsRuntimeIdentityValid())
	{
		return;
	}
	if (bHeartbeatStarted)
	{
		WriteRuntimeHeartbeat();
		return;
	}
	HeartbeatMapName = MapName;
	int32 AllocatedMaxPlayers = MaxPlayers;
	FParse::Value(FCommandLine::Get(), TEXT("MaxPlayers="), AllocatedMaxPlayers);
	HeartbeatMaxPlayers = FMath::Clamp(
		AllocatedMaxPlayers, 2, PHServerInstanceContract::MaximumRosterPlayers);
	FParse::Value(FCommandLine::Get(), TEXT("Port="), HeartbeatGamePort);
	FParse::Value(FCommandLine::Get(), TEXT("QueryPort="), HeartbeatQueryPort);
	FParse::Value(
		FCommandLine::Get(), TEXT("EmptyShutdownSeconds="), HeartbeatEmptyShutdownSeconds);
	HeartbeatEmptyShutdownSeconds = FMath::Clamp(HeartbeatEmptyShutdownSeconds, 0, 86400);
	HeartbeatSequence = 0;
	HeartbeatEmptySinceUtc = FDateTime();
	bEmptyShutdownRequested = false;
	bHeartbeatStarted = true;
	WriteRuntimeHeartbeat();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().SetTimer(
			HeartbeatTimerHandle,
			this,
			&UPHServerInstanceSubsystem::TickRuntimeHeartbeat,
			1.0f,
			true);
	}
}

void UPHServerInstanceSubsystem::TickRuntimeHeartbeat()
{
	WriteRuntimeHeartbeat();
}

void UPHServerInstanceSubsystem::WriteRuntimeHeartbeat(const bool bStopping)
{
	if (!bHeartbeatStarted || HeartbeatFilePath.IsEmpty())
	{
		return;
	}

	FString SteamConnectAddress;
	const bool bSteamReady = ResolveSteamConnectAddress(SteamConnectAddress);
	const bool bRosterReady = IsAdmissionRosterReady();
	int32 PlayerCount = 0;
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UWorld* World = GameInstance->GetWorld())
		{
			const FString CurrentMapName = World->GetOutermost()->GetName();
			if (!CurrentMapName.IsEmpty())
			{
				HeartbeatMapName = CurrentMapName;
			}
			if (const AGameStateBase* CurrentGameState = World->GetGameState())
			{
				PlayerCount = CurrentGameState->PlayerArray.Num();
			}
		}
	}
	const FDateTime NowUtc = FDateTime::UtcNow();
	if (PlayerCount > 0 || HeartbeatEmptyShutdownSeconds <= 0)
	{
		HeartbeatEmptySinceUtc = FDateTime();
	}
	else if (HeartbeatEmptySinceUtc.GetTicks() <= 0)
	{
		HeartbeatEmptySinceUtc = NowUtc;
	}
	else if (!bStopping
		&& (NowUtc - HeartbeatEmptySinceUtc).GetTotalSeconds() >= HeartbeatEmptyShutdownSeconds)
	{
		bEmptyShutdownRequested = true;
	}
	const bool bEffectiveStopping = bStopping || bEmptyShutdownRequested;
	const FString State = bEffectiveStopping
		? TEXT("stopping")
		: bRosterReady && bSteamReady
			? PlayerCount > 0 ? TEXT("active") : TEXT("ready")
			: TEXT("starting");

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), 1);
	Root->SetStringField(TEXT("state"), State);
	Root->SetStringField(TEXT("server_instance_id"), ServerInstanceId);
	Root->SetStringField(TEXT("run_id"), RunId);
	Root->SetStringField(TEXT("world_id"), MatchId);
	Root->SetStringField(TEXT("map"), HeartbeatMapName);
	Root->SetStringField(TEXT("mode"), TEXT("PropHunt"));
	Root->SetStringField(TEXT("build_id"), NOVAProjectBuildId);
	Root->SetNumberField(TEXT("game_port"), HeartbeatGamePort);
	Root->SetNumberField(TEXT("query_port"), HeartbeatQueryPort);
	Root->SetNumberField(TEXT("max_players"), HeartbeatMaxPlayers);
	Root->SetNumberField(TEXT("player_count"), PlayerCount);
	Root->SetNumberField(TEXT("pid"), static_cast<double>(FPlatformProcess::GetCurrentProcessId()));
	Root->SetNumberField(TEXT("sequence"), static_cast<double>(++HeartbeatSequence));
	Root->SetStringField(TEXT("updated_utc"), NowUtc.ToIso8601());
	Root->SetNumberField(TEXT("empty_shutdown_seconds"), HeartbeatEmptyShutdownSeconds);
	Root->SetStringField(
		TEXT("empty_since_utc"),
		HeartbeatEmptySinceUtc.GetTicks() > 0 ? HeartbeatEmptySinceUtc.ToIso8601() : FString());
	Root->SetStringField(
		TEXT("shutdown_deadline_utc"),
		HeartbeatEmptySinceUtc.GetTicks() > 0 && HeartbeatEmptyShutdownSeconds > 0
			? (HeartbeatEmptySinceUtc + FTimespan::FromSeconds(HeartbeatEmptyShutdownSeconds)).ToIso8601()
			: FString());
	Root->SetStringField(
		TEXT("shutdown_reason"), bEmptyShutdownRequested ? TEXT("empty_timeout") : TEXT(""));
	Root->SetStringField(TEXT("visibility"), TEXT("private"));
	Root->SetStringField(TEXT("backfill_policy"), TEXT("NoBackfill"));
	Root->SetBoolField(TEXT("admission_roster_ready"), bRosterReady);
	Root->SetBoolField(TEXT("backfill_requested"), false);
	Root->SetNumberField(TEXT("open_backfill_slots"), 0);
	Root->SetBoolField(TEXT("steam_expected"), true);
	Root->SetBoolField(TEXT("steam_session_ready"), bSteamReady);
	Root->SetStringField(TEXT("steam_connect_address"), SteamConnectAddress);

	FString Payload;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return;
	}
	Payload.AppendChar(TEXT('\n'));
	IFileManager& FileManager = IFileManager::Get();
	const FString Directory = FPaths::GetPath(HeartbeatFilePath);
	const FString TempPath = FString::Printf(
		TEXT("%s.tmp-%u"), *HeartbeatFilePath, FPlatformProcess::GetCurrentProcessId());
	const bool bSaved = FileManager.MakeDirectory(*Directory, true)
		&& FFileHelper::SaveStringToFile(
			Payload, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		&& FileManager.Move(*HeartbeatFilePath, *TempPath, true, true, false, true);
	if (!bSaved)
	{
		FileManager.Delete(*TempPath, false, true, true);
		UE_LOG(LogPHServerInstance, Error, TEXT("NOVA heartbeat write failed."));
	}
	else if (bEmptyShutdownRequested)
	{
		UE_LOG(LogPHServerInstance, Display,
			TEXT("NOVA worker empty timeout reached after %d seconds."),
			HeartbeatEmptyShutdownSeconds);
		FPlatformMisc::RequestExit(false);
	}
}

bool UPHServerInstanceSubsystem::ResolveSteamConnectAddress(FString& OutConnectAddress) const
{
	OutConnectAddress.Reset();
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance != nullptr ? GameInstance->GetWorld() : nullptr;
	IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(World);
	if (OnlineSubsystem == nullptr || OnlineSubsystem->GetSubsystemName() != FName(TEXT("STEAM")))
	{
		return false;
	}
	const IOnlineSessionPtr Sessions = OnlineSubsystem->GetSessionInterface();
	if (!Sessions.IsValid() || Sessions->GetNamedSession(NAME_GameSession) == nullptr)
	{
		return false;
	}
	FString ResolvedAddress;
	if (!Sessions->GetResolvedConnectString(
		NAME_GameSession, ResolvedAddress, NAME_GamePort))
	{
		return false;
	}
	const FString ExpectedSuffix = FString::Printf(TEXT(":%d"), HeartbeatGamePort);
	if (!ResolvedAddress.StartsWith(TEXT("steam."), ESearchCase::CaseSensitive)
		|| !ResolvedAddress.EndsWith(ExpectedSuffix, ESearchCase::CaseSensitive))
	{
		return false;
	}
	OutConnectAddress = MoveTemp(ResolvedAddress);
	return true;
}
