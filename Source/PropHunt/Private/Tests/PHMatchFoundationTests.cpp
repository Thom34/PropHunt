#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"

#include "Camera/CameraActor.h"
#include "Characters/PHHunterCharacter.h"
#include "Characters/PHPropCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/Level.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Game/PHMatchRulesDataAsset.h"
#include "Game/PHMatchTypes.h"
#include "Game/PHPlayerState.h"
#include "Gameplay/Transformation/PHPropFormDataAsset.h"
#include "Gameplay/Transformation/PHPropTransformTarget.h"
#include "Gameplay/Capture/PHCaptureTypes.h"
#include "Gameplay/Capture/PHRetentionPoint.h"
#include "Gameplay/Characters/PHHumanPrototypeSelector.h"
#include "Gameplay/Escape/PHExitGate.h"
#include "Gameplay/Physics/PHPhysicsPropDataAsset.h"
#include "Gameplay/Physics/PHPhysicsPropPresentation.h"
#include "Gameplay/Physics/PHPhysicsPropPrototype.h"
#include "Gameplay/Objectives/PHObjectiveActor.h"
#include "Gameplay/PHCollisionChannels.h"
#include "PhysicsEngine/BodySetup.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "InputCoreTypes.h"
#include "Materials/Material.h"
#include "Online/PHMatchmakingGatewayContract.h"
#include "Online/PHServerInstanceContract.h"
#include "Online/PHSessionSubsystem.h"
#include "UI/PHInGameMenuWidget.h"
#include "UI/PHMatchmakingWidget.h"
#include "UI/PHHumanStaminaWidget.h"
#include "UObject/SoftObjectPath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
bool HasAxisMapping(const UInputSettings& InputSettings, const FName AxisName, const FKey Key, const float Scale)
{
	return InputSettings.GetAxisMappings().ContainsByPredicate(
		[AxisName, Key, Scale](const FInputAxisKeyMapping& Mapping)
		{
			return Mapping.AxisName == AxisName
				&& Mapping.Key == Key
				&& FMath::IsNearlyEqual(Mapping.Scale, Scale);
		});
}

bool HasActionMapping(const UInputSettings& InputSettings, const FName ActionName, const FKey Key)
{
	return InputSettings.GetActionMappings().ContainsByPredicate(
		[ActionName, Key](const FInputActionKeyMapping& Mapping)
		{
			return Mapping.ActionName == ActionName && Mapping.Key == Key;
		});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHSteamLobbyPriorityTest,
	"PropHunt.Steam.Matchmaking.LobbyPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHSteamLobbyPriorityTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("A compatible open Hunter lobby is accepted"),
		PHMatchmakingPolicy::IsCompatibleLobby(3, 1, TEXT("H"), TEXT("Lobby"), 1));
	TestFalse(TEXT("A full Hunter lobby is rejected"),
		PHMatchmakingPolicy::IsCompatibleLobby(0, 1, TEXT("H"), TEXT("Lobby"), 1));
	TestFalse(TEXT("An incompatible protocol is rejected"),
		PHMatchmakingPolicy::IsCompatibleLobby(3, 2, TEXT("H"), TEXT("Lobby"), 1));
	TestFalse(TEXT("A lobby already in match is rejected"),
		PHMatchmakingPolicy::IsCompatibleLobby(3, 1, TEXT("H"), TEXT("Match"), 1));

	TArray<FPHLobbyPriority> Priorities;
	Priorities.Add({ 300, 20, 0, TEXT("newer-low-ping") });
	Priorities.Add({ 100, 80, 1, TEXT("oldest") });
	Priorities.Add({ 200, 40, 2, TEXT("middle") });
	Priorities.Add({ 0, 5, 3, TEXT("legacy-without-time") });
	PHMatchmakingPolicy::SortLobbyPriorities(Priorities);

	TestEqual(TEXT("The oldest advertised lobby is proposed first"), Priorities[0].SessionId, FString(TEXT("oldest")));
	TestEqual(TEXT("The next oldest lobby follows"), Priorities[1].SessionId, FString(TEXT("middle")));
	TestEqual(TEXT("A newer lobby remains behind older available lobbies"), Priorities[2].SessionId, FString(TEXT("newer-low-ping")));
	TestEqual(TEXT("A lobby without a creation timestamp is a last-resort fallback"), Priorities[3].SessionId, FString(TEXT("legacy-without-time")));

	TestEqual(TEXT("Dedicated-only Hunter waits for NOVA instead of creating a listen lobby"),
		PHMatchmakingPolicy::DecideAfterSearch(EPHMatchmakingPreference::Hunter, 1, 1, 2),
		EPHMatchmakingSearchDecision::WaitForAvailableSlot);
	TestEqual(TEXT("Hunter waits instead of being forced to Prop when two lobbies already exist"),
		PHMatchmakingPolicy::DecideAfterSearch(EPHMatchmakingPreference::Hunter, 2, 1, 2),
		EPHMatchmakingSearchDecision::WaitForAvailableSlot);
	TestEqual(TEXT("Hunter waits rather than creating a third lobby when both existing lobbies are full"),
		PHMatchmakingPolicy::DecideAfterSearch(EPHMatchmakingPreference::Hunter, 2, 0, 2),
		EPHMatchmakingSearchDecision::WaitForAvailableSlot);
	TestEqual(TEXT("Hunter joins an unassigned dedicated server instead of creating a listen lobby"),
		PHMatchmakingPolicy::DecideAfterSearch(EPHMatchmakingPreference::Hunter, 1, 1, 2, 1),
		EPHMatchmakingSearchDecision::JoinHunterLobby);
	TestEqual(TEXT("Random prefers filling an existing lobby as Prop"),
		PHMatchmakingPolicy::DecideAfterSearch(EPHMatchmakingPreference::Random, 1, 1, 2),
		EPHMatchmakingSearchDecision::JoinPropLobby);
	TestEqual(TEXT("Random never creates a listen lobby when no dedicated worker exists"),
		PHMatchmakingPolicy::DecideAfterSearch(EPHMatchmakingPreference::Random, 0, 0, 2),
		EPHMatchmakingSearchDecision::WaitForAvailableSlot);

	TestEqual(TEXT("The listen-server local controller is the Hunter"),
		PHMatchmakingPolicy::ResolveListenServerRole(true, false),
		EPHPlayerRole::Hunter);
	TestEqual(TEXT("A remote Steam client is always a Prop"),
		PHMatchmakingPolicy::ResolveListenServerRole(false, false),
		EPHPlayerRole::Prop);
	TestEqual(TEXT("A second local controller cannot become another Hunter"),
		PHMatchmakingPolicy::ResolveListenServerRole(true, true),
		EPHPlayerRole::Prop);
	TestEqual(TEXT("A dedicated client stays unassigned until the roster is locked"),
		PHMatchmakingPolicy::ResolveAuthoritativeServerRole(false, true, false),
		EPHPlayerRole::Unassigned);
	TestEqual(TEXT("Dedicated role assignment never depends on connection order"),
		PHMatchmakingPolicy::ResolveAuthoritativeServerRole(false, true, true),
		EPHPlayerRole::Unassigned);

	TSet<int32> SelectedHunterIndexes;
	for (int32 Seed = 1; Seed <= 256; ++Seed)
	{
		const int32 HunterIndex = PHMatchmakingPolicy::ChooseRandomHunterIndex(3, Seed);
		TestTrue(TEXT("A server-selected Hunter index stays inside the roster"), HunterIndex >= 0 && HunterIndex < 3);
		SelectedHunterIndexes.Add(HunterIndex);
	}
	TestEqual(TEXT("Server-side random selection can choose every member of a three-player roster"),
		SelectedHunterIndexes.Num(), 3);
	TestEqual(TEXT("An empty roster has no Hunter candidate"),
		PHMatchmakingPolicy::ChooseRandomHunterIndex(0, 1234), INDEX_NONE);

	const FPHSessionHostingPolicy ListenPolicy = PHMatchmakingPolicy::ResolveSessionHostingPolicy(false);
	TestFalse(TEXT("Listen hosting is not marked dedicated"), ListenPolicy.bIsDedicated);
	TestTrue(TEXT("Listen hosting uses Steam presence"), ListenPolicy.bUsesPresence);
	TestTrue(TEXT("Listen hosting uses Steam lobbies"), ListenPolicy.bUseLobbies);
	const FPHSessionHostingPolicy DedicatedPolicy = PHMatchmakingPolicy::ResolveSessionHostingPolicy(true);
	TestTrue(TEXT("Dedicated hosting is marked dedicated"), DedicatedPolicy.bIsDedicated);
	TestFalse(TEXT("Dedicated hosting does not require Steam presence"), DedicatedPolicy.bUsesPresence);
	TestFalse(TEXT("Dedicated hosting does not require a local-player lobby"), DedicatedPolicy.bUseLobbies);

	TestTrue(TEXT("A loaded client world with an open server connection confirms travel"),
		PHMatchmakingPolicy::IsJoinedClientConnection(true, true, true));
	TestFalse(TEXT("A standalone map load is not a successful lobby connection"),
		PHMatchmakingPolicy::IsJoinedClientConnection(false, false, false));
	TestFalse(TEXT("A client world without a server connection is not connected"),
		PHMatchmakingPolicy::IsJoinedClientConnection(true, false, false));

	TestTrue(TEXT("An enabled Steam session interface can publish a dedicated server"),
		PHMatchmakingPolicy::IsDedicatedSteamBackendAvailable(TEXT("STEAM"), true, true));
	TestFalse(TEXT("The NULL fallback cannot claim dedicated Steam publication"),
		PHMatchmakingPolicy::IsDedicatedSteamBackendAvailable(TEXT("NULL"), true, true));
	TestFalse(TEXT("A disabled Steam backend cannot claim dedicated publication"),
		PHMatchmakingPolicy::IsDedicatedSteamBackendAvailable(TEXT("STEAM"), false, true));
	TestFalse(TEXT("Steam without a session interface cannot claim dedicated publication"),
		PHMatchmakingPolicy::IsDedicatedSteamBackendAvailable(TEXT("STEAM"), true, false));

	TestTrue(TEXT("Two ready players can explicitly launch the dedicated 1v1 test"),
		PHMatchmakingPolicy::ShouldLaunchReadyTwoPlayerTest(2, 2, 1, true, true));
	TestFalse(TEXT("One ready player cannot launch the dedicated 1v1 test"),
		PHMatchmakingPolicy::ShouldLaunchReadyTwoPlayerTest(2, 1, 1, true, true));
	TestFalse(TEXT("A third connected player keeps the normal joinable countdown"),
		PHMatchmakingPolicy::ShouldLaunchReadyTwoPlayerTest(3, 3, 1, true, true));
	TestFalse(TEXT("The ready shortcut cannot bypass a public two-survivor minimum"),
		PHMatchmakingPolicy::ShouldLaunchReadyTwoPlayerTest(2, 2, 2, true, true));
	TestFalse(TEXT("The ready shortcut cannot run outside a dedicated Lobby"),
		PHMatchmakingPolicy::ShouldLaunchReadyTwoPlayerTest(2, 2, 1, false, true)
			|| PHMatchmakingPolicy::ShouldLaunchReadyTwoPlayerTest(2, 2, 1, true, false));
	TestTrue(TEXT("A waiting-room roster rejects late admission after launch is locked"),
		PHMatchmakingPolicy::IsRosterAdmissionLocked(true, true, false));
	TestTrue(TEXT("A gameplay world rejects admission while its seamless roster is arriving"),
		PHMatchmakingPolicy::IsRosterAdmissionLocked(false, false, true));
	TestFalse(TEXT("An unlocked waiting room remains joinable"),
		PHMatchmakingPolicy::IsRosterAdmissionLocked(true, false, false));

	TestTrue(TEXT("The non-Shipping smoke-only direct-connect validator accepts an explicit IPv4 and port"),
		PHMatchmakingPolicy::IsValidDirectConnectEndpoint(TEXT("127.0.0.1:7846")));
	TestTrue(TEXT("A public VPS IPv4 and valid high port are accepted"),
		PHMatchmakingPolicy::IsValidDirectConnectEndpoint(TEXT("37.59.57.1:65535")));
	TestFalse(TEXT("A direct-connect endpoint cannot inject URL options"),
		PHMatchmakingPolicy::IsValidDirectConnectEndpoint(TEXT("127.0.0.1:7846?game=/Script/Other")));
	TestFalse(TEXT("A direct-connect endpoint rejects hostnames and missing ports"),
		PHMatchmakingPolicy::IsValidDirectConnectEndpoint(TEXT("localhost:7846"))
			|| PHMatchmakingPolicy::IsValidDirectConnectEndpoint(TEXT("127.0.0.1")));
	TestFalse(TEXT("A direct-connect endpoint rejects invalid address and port ranges"),
		PHMatchmakingPolicy::IsValidDirectConnectEndpoint(TEXT("256.0.0.1:7846"))
			|| PHMatchmakingPolicy::IsValidDirectConnectEndpoint(TEXT("0.0.0.0:7846"))
			|| PHMatchmakingPolicy::IsValidDirectConnectEndpoint(TEXT("255.255.255.255:7846"))
			|| PHMatchmakingPolicy::IsValidDirectConnectEndpoint(TEXT("127.0.0.1:0"))
			|| PHMatchmakingPolicy::IsValidDirectConnectEndpoint(TEXT("127.0.0.1:65536")));

	APHPlayerState* TravelSourceState = NewObject<APHPlayerState>();
	APHPlayerState* TravelDestinationState = NewObject<APHPlayerState>();
	TestNotNull(TEXT("Seamless travel source PlayerState exists"), TravelSourceState);
	TestNotNull(TEXT("Seamless travel destination PlayerState exists"), TravelDestinationState);
	if (TravelSourceState != nullptr && TravelDestinationState != nullptr)
	{
		TravelSourceState->SetPlayerRole(EPHPlayerRole::Hunter);
		TravelSourceState->SetLobbyReady(true);
		TravelSourceState->CopyProperties(TravelDestinationState);
		TestEqual(TEXT("Seamless travel preserves the server-selected role"),
			TravelDestinationState->GetPlayerRole(), EPHPlayerRole::Hunter);
		TestFalse(TEXT("Seamless travel clears the transient lobby-ready flag"),
			TravelDestinationState->IsLobbyReady());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHSteamMatchmakingDefaultsTest,
	"PropHunt.Steam.Matchmaking.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHSteamMatchmakingDefaultsTest::RunTest(const FString& Parameters)
{
	const UPHSessionSubsystem* SessionDefaults = GetDefault<UPHSessionSubsystem>();
	const UPHMatchmakingWidget* MatchmakingWidgetDefaults = GetDefault<UPHMatchmakingWidget>();
	TestNotNull(TEXT("Steam session subsystem defaults exist"), SessionDefaults);
	TestNotNull(TEXT("Native matchmaking menu exists"), MatchmakingWidgetDefaults);
	if (SessionDefaults != nullptr)
	{
		TestEqual(TEXT("Steam network protocol isolates incompatible beta builds"),
			SessionDefaults->GetProtocolVersion(), 91);
		TestEqual(TEXT("Client and dedicated server share the exact release identifier"),
			SessionDefaults->GetReleaseVersion(), FString(TEXT("0.1.1907001")));
		TestTrue(TEXT("Dedicated servers publish their session automatically"),
			SessionDefaults->GetAutoCreateDedicatedSession());
		TestTrue(TEXT("Steam client travel confirmation has a non-zero grace period"),
			SessionDefaults->GetJoinedTravelConfirmationTimeoutSeconds() >= 1.0f);
	}

	TArray<FString> NetDriverDefinitions;
	GConfig->GetArray(TEXT("/Script/Engine.GameEngine"), TEXT("NetDriverDefinitions"), NetDriverDefinitions, GEngineIni);
	TestTrue(TEXT("Packaged games use SteamSockets instead of resolving steam IDs through IpNetDriver"),
		NetDriverDefinitions.ContainsByPredicate([](const FString& Definition)
		{
			return Definition.Contains(TEXT("/Script/SteamSockets.SteamSocketsNetDriver"));
		}));
	TestFalse(TEXT("P9 has no IpNetDriver fallback in the packaged GameNetDriver"),
		NetDriverDefinitions.ContainsByPredicate([](const FString& Definition)
		{
			return Definition.Contains(TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"));
		}));
	return SessionDefaults != nullptr && MatchmakingWidgetDefaults != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHNOVAGatewayContractTest,
	"PropHunt.NOVA.Matchmaking.GatewayContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHNOVAGatewayContractTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Hunter preference is explicit"),
		FString(PHMatchmakingGatewayContract::RolePreferenceToString(EPHMatchmakingPreference::Hunter)),
		FString(TEXT("hunter")));
	TestEqual(TEXT("Survivor preference is explicit"),
		FString(PHMatchmakingGatewayContract::RolePreferenceToString(EPHMatchmakingPreference::Prop)),
		FString(TEXT("prop")));
	TestEqual(TEXT("Random preference remains one ticket"),
		FString(PHMatchmakingGatewayContract::RolePreferenceToString(EPHMatchmakingPreference::Random)),
		FString(TEXT("random")));

	FString Normalized;
	FString Error;
	TestTrue(TEXT("Shipping accepts the pinned HTTPS gateway"),
		PHMatchmakingGatewayContract::NormalizeGatewayBaseUrl(
			TEXT("https://gateway.meteothom.com/"), true, Normalized, Error));
	TestEqual(TEXT("Gateway origin is normalized"), Normalized,
		FString(TEXT("https://gateway.meteothom.com")));
	TestFalse(TEXT("Shipping rejects clear-text public gateways"),
		PHMatchmakingGatewayContract::NormalizeGatewayBaseUrl(
			TEXT("http://gateway.meteothom.com"), true, Normalized, Error));
	TestTrue(TEXT("Development accepts a loopback gateway mock"),
		PHMatchmakingGatewayContract::NormalizeGatewayBaseUrl(
			TEXT("http://127.0.0.1:8789"), false, Normalized, Error));
	TestTrue(TEXT("SteamSockets destination is accepted"),
		PHMatchmakingGatewayContract::NormalizeConnectAddress(
			TEXT("steam.76561198000000000:7836"), Normalized, Error));
	TestFalse(TEXT("A direct IP destination cannot bypass SteamSockets"),
		PHMatchmakingGatewayContract::NormalizeConnectAddress(
			TEXT("127.0.0.1:7836"), Normalized, Error));
	TestFalse(TEXT("A gateway destination can never inject listen-server options"),
		PHMatchmakingGatewayContract::NormalizeConnectAddress(
			TEXT("127.0.0.1:7836?listen"), Normalized, Error));

	FString PublicErrorCode;
	FString PublicErrorMessage;
	TestTrue(TEXT("The client accepts the bounded update-required error envelope"),
		PHMatchmakingGatewayContract::ParsePublicErrorResponse(
			TEXT("{\"error\":{\"code\":\"client_update_required\",\"message\":\"Une mise à jour du jeu est requise.\"}}"),
			PublicErrorCode,
			PublicErrorMessage));
	TestEqual(TEXT("The update-required code remains machine-readable"),
		PublicErrorCode, FString(TEXT("client_update_required")));
	TestFalse(TEXT("Multiline gateway errors are rejected"),
		PHMatchmakingGatewayContract::ParsePublicErrorResponse(
			TEXT("{\"error\":{\"code\":\"client_update_required\",\"message\":\"unsafe\\nmessage\"}}"),
			PublicErrorCode,
			PublicErrorMessage));

	FPHGatewayTicket Ticket;
	const FString PendingJson =
		TEXT("{\"schema\":1,\"protocol_version\":91,\"build_id\":\"0.1.1907001\",\"ticket_id\":\"ticket-12345678\",\"state\":\"queued\",\"retry_after_ms\":500}");
	TestTrue(TEXT("A queued NOVA ticket is accepted"),
		PHMatchmakingGatewayContract::ParseTicketStatus(
			PendingJson, TEXT("ticket-12345678"), 91, TEXT("0.1.1907001"), FDateTime::UtcNow(), Ticket, Error));
	TestEqual(TEXT("Queued ticket remains pending"), Ticket.Status, EPHGatewayTicketStatus::Pending);

	const FDateTime NowUtc = FDateTime::UtcNow();
	const FString LobbyJson = FString::Printf(
		TEXT("{\"schema\":1,\"protocol_version\":91,\"build_id\":\"0.1.1907001\",\"ticket_id\":\"ticket-12345678\",\"state\":\"queued\",")
		TEXT("\"retry_after_ms\":500,\"lobby\":{\"lobby_id\":\"world-12345678\",\"phase\":\"countdown\",")
		TEXT("\"assigned_role\":\"prop\",\"survivors_present\":2,\"survivor_slots\":4,")
		TEXT("\"occupied_survivor_slots\":[1,3],\"assigned_survivor_slot\":3,\"locked\":false,")
		TEXT("\"countdown_ends_utc\":\"%s\"}}"),
		*(NowUtc + FTimespan::FromMinutes(1.0)).ToIso8601());
	TestTrue(TEXT("A P9 staging lobby snapshot is accepted"),
		PHMatchmakingGatewayContract::ParseTicketStatus(
			LobbyJson, TEXT("ticket-12345678"), 91, TEXT("0.1.1907001"), NowUtc, Ticket, Error));
	TestTrue(TEXT("The client exposes the lobby presentation"), Ticket.bHasLobby);
	TestEqual(TEXT("Only Survivor occupancy is exposed"), Ticket.SurvivorsPresent, 2);
	TestEqual(TEXT("The private role remains authoritative"), Ticket.AssignedRole, EPHPlayerRole::Prop);
	TestEqual(TEXT("The gateway assigns this Survivor podium"), Ticket.AssignedSurvivorSlot, 3);
	TestTrue(TEXT("A vacated podium remains visibly empty"),
		Ticket.OccupiedSurvivorSlots == TArray<int32>({1, 3}));

	const FString ReadyJson = FString::Printf(
		TEXT("{\"schema\":1,\"protocol_version\":91,\"build_id\":\"0.1.1907001\",\"ticket_id\":\"ticket-12345678\",\"state\":\"ready\",")
		TEXT("\"match_id\":\"match-12345678\",\"reservation_id\":\"reservation-12345678\",")
		TEXT("\"assigned_role\":\"prop\",\"connect_address\":\"steam.76561198000000000:7836\",")
		TEXT("\"expires_utc\":\"%s\"}"),
		*(NowUtc + FTimespan::FromMinutes(5.0)).ToIso8601());
	TestTrue(TEXT("A bound ready reservation is accepted"),
		PHMatchmakingGatewayContract::ParseTicketStatus(
			ReadyJson, TEXT("ticket-12345678"), 91, TEXT("0.1.1907001"), NowUtc, Ticket, Error));
	TestEqual(TEXT("Backend role is preserved"), Ticket.AssignedRole, EPHPlayerRole::Prop);
	TestFalse(TEXT("A response from another release cannot enter the P9.1 client"),
		PHMatchmakingGatewayContract::ParseTicketStatus(
			PendingJson, TEXT("ticket-12345678"), 91, TEXT("0.1.1907000"), NowUtc, Ticket, Error));

	FString GatewayUrl;
	FString TicketPath;
	GConfig->GetString(TEXT("/Script/PropHunt.PHMatchmakingGatewaySubsystem"),
		TEXT("GatewayBaseUrl"), GatewayUrl, GGameIni);
	GConfig->GetString(TEXT("/Script/PropHunt.PHMatchmakingGatewaySubsystem"),
		TEXT("TicketPath"), TicketPath, GGameIni);
	TestEqual(TEXT("Client uses the public gateway origin"), GatewayUrl,
		FString(TEXT("https://gateway.meteothom.com")));
	TestEqual(TEXT("Client uses the PropHunt ticket route"), TicketPath,
		FString(TEXT("/v1/prophunt/tickets")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHNOVAServerAdmissionContractTest,
	"PropHunt.NOVA.Matchmaking.ServerAdmissionContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHNOVAServerAdmissionContractTest::RunTest(const FString& Parameters)
{
	const FPHServerRuntimeIdentity Identity = PHServerInstanceContract::ResolveCommandLine(
		TEXT("-RequireNOVAReservation -ServerInstanceId=worker-8 -RunId=run-8 -MatchId=match-8"),
		true,
		TEXT("unused"));
	TestTrue(TEXT("A dedicated worker accepts complete immutable NOVA identity"), Identity.IsValid());
	TestTrue(TEXT("NOVA admission is explicitly required"), Identity.bRequireNOVAReservation);
	TestTrue(TEXT("A dedicated Shipping worker must fail closed without a NOVA reservation"),
		PHServerInstanceContract::ShouldRequireNOVAReservation(true, true));
	TestFalse(TEXT("A Development worker may still run bounded local diagnostics without NOVA"),
		PHServerInstanceContract::ShouldRequireNOVAReservation(true, false));
	TestFalse(TEXT("A Shipping client is not treated as a worker"),
		PHServerInstanceContract::ShouldRequireNOVAReservation(false, true));
	const FPHServerRuntimeIdentity MissingMatch = PHServerInstanceContract::ResolveCommandLine(
		TEXT("-RequireNOVAReservation -ServerInstanceId=worker-8 -RunId=run-8"),
		true,
		TEXT("fallback"));
	TestFalse(TEXT("A NOVA worker rejects generated match identity"), MissingMatch.IsValid());

	const FDateTime NowUtc = FDateTime::UtcNow();
	const FString RosterJson = FString::Printf(
		TEXT("{\"schema\":1,\"protocol_version\":91,\"build_id\":\"0.1.1907001\",\"server_instance_id\":\"worker-8\",")
		TEXT("\"run_id\":\"run-8\",\"match_id\":\"match-8\",\"reservation_id\":\"reservation-8\",")
		TEXT("\"expires_at\":\"%s\",\"expected_players\":3,\"players\":[")
		TEXT("{\"identity\":\"STEAM:76561198000000001\",\"role\":\"hunter\"},")
		TEXT("{\"identity\":\"STEAM:76561198000000002\",\"role\":\"prop\"},")
		TEXT("{\"identity\":\"STEAM:76561198000000003\",\"role\":\"prop\"}]}"),
		*(NowUtc + FTimespan::FromMinutes(5.0)).ToIso8601());
	FPHAdmissionRosterSnapshot Roster;
	FString Error;
	TestTrue(TEXT("A P9 roster bound to the worker is accepted"),
		PHServerInstanceContract::ParseAdmissionRoster(
			RosterJson, Identity, NowUtc, Roster, Error));
	TestEqual(TEXT("Roster count is authoritative"), Roster.ExpectedPlayers, 3);
	EPHPlayerRole Role = EPHPlayerRole::Unassigned;
	TestTrue(TEXT("Reserved Steam identity is admitted"),
		PHServerInstanceContract::ResolveAuthorizedRole(
			Roster, TEXT("steam:76561198000000001"), Role, Error));
	TestEqual(TEXT("Reserved Hunter role is preserved"), Role, EPHPlayerRole::Hunter);
	TestFalse(TEXT("An unreserved Steam identity is rejected"),
		PHServerInstanceContract::ResolveAuthorizedRole(
			Roster, TEXT("STEAM:76561198000009999"), Role, Error));

	FPHServerRuntimeIdentity WrongRun = Identity;
	WrongRun.RunId = TEXT("run-other");
	TestFalse(TEXT("A roster cannot be replayed on another run"),
		PHServerInstanceContract::ParseAdmissionRoster(
			RosterJson, WrongRun, NowUtc, Roster, Error));
	TestFalse(TEXT("An expired roster is rejected"),
		PHServerInstanceContract::ParseAdmissionRoster(
			RosterJson, Identity, NowUtc + FTimespan::FromMinutes(10.0), Roster, Error));
	const FString WrongBuildRosterJson = RosterJson.Replace(
		TEXT("\"build_id\":\"0.1.1907001\""),
		TEXT("\"build_id\":\"0.1.1907000\""));
	TestFalse(TEXT("A roster built for another release is rejected"),
		PHServerInstanceContract::ParseAdmissionRoster(
			WrongBuildRosterJson, Identity, NowUtc, Roster, Error));
	const FString TwoPlayerRosterJson = FString::Printf(
		TEXT("{\"schema\":1,\"protocol_version\":91,\"build_id\":\"0.1.1907001\",\"server_instance_id\":\"worker-8\",")
		TEXT("\"run_id\":\"run-8\",\"match_id\":\"match-8\",\"reservation_id\":\"reservation-8\",")
		TEXT("\"expires_at\":\"%s\",\"expected_players\":2,\"players\":[")
		TEXT("{\"identity\":\"STEAM:76561198000000001\",\"role\":\"hunter\"},")
		TEXT("{\"identity\":\"STEAM:76561198000000002\",\"role\":\"prop\"}]}"),
		*(NowUtc + FTimespan::FromMinutes(5.0)).ToIso8601());
	TestTrue(TEXT("A private P9 reservation can explicitly request the dedicated 1v1 mode"),
		PHServerInstanceContract::ParseAdmissionRoster(
			TwoPlayerRosterJson, Identity, NowUtc, Roster, Error));
	const FString FourPlayerRosterJson = FString::Printf(
		TEXT("{\"schema\":1,\"protocol_version\":91,\"build_id\":\"0.1.1907001\",\"server_instance_id\":\"worker-8\",")
		TEXT("\"run_id\":\"run-8\",\"match_id\":\"match-8\",\"reservation_id\":\"reservation-8\",")
		TEXT("\"expires_at\":\"%s\",\"expected_players\":4,\"players\":[")
		TEXT("{\"identity\":\"STEAM:76561198000000001\",\"role\":\"hunter\"},")
		TEXT("{\"identity\":\"STEAM:76561198000000002\",\"role\":\"prop\"},")
		TEXT("{\"identity\":\"STEAM:76561198000000003\",\"role\":\"prop\"},")
		TEXT("{\"identity\":\"STEAM:76561198000000004\",\"role\":\"prop\"}]}"),
		*(NowUtc + FTimespan::FromMinutes(5.0)).ToIso8601());
	TestTrue(TEXT("A P9 reservation can scale to one Hunter and three Props"),
		PHServerInstanceContract::ParseAdmissionRoster(
			FourPlayerRosterJson, Identity, NowUtc, Roster, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHKeyboardLayoutMappingsTest,
	"PropHunt.Character.Input.KeyboardLayouts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHKeyboardLayoutMappingsTest::RunTest(const FString& Parameters)
{
	const UInputSettings* InputSettings = GetDefault<UInputSettings>();
	TestNotNull(TEXT("Input settings exist"), InputSettings);
	if (InputSettings == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("QWERTY forward uses W"), HasAxisMapping(*InputSettings, TEXT("MoveForward"), EKeys::W, 1.0f));
	TestTrue(TEXT("AZERTY forward uses Z"), HasAxisMapping(*InputSettings, TEXT("MoveForward"), EKeys::Z, 1.0f));
	TestTrue(TEXT("Backward uses S"), HasAxisMapping(*InputSettings, TEXT("MoveForward"), EKeys::S, -1.0f));
	TestTrue(TEXT("Right uses D"), HasAxisMapping(*InputSettings, TEXT("MoveRight"), EKeys::D, 1.0f));
	TestTrue(TEXT("QWERTY left uses A"), HasAxisMapping(*InputSettings, TEXT("MoveRight"), EKeys::A, -1.0f));
	TestTrue(TEXT("AZERTY left uses Q"), HasAxisMapping(*InputSettings, TEXT("MoveRight"), EKeys::Q, -1.0f));
	TestTrue(TEXT("Gamepad forward uses the left stick"), HasAxisMapping(*InputSettings, TEXT("MoveForward"), EKeys::Gamepad_LeftY, 1.0f));
	TestTrue(TEXT("Gamepad lateral movement uses the left stick"), HasAxisMapping(*InputSettings, TEXT("MoveRight"), EKeys::Gamepad_LeftX, 1.0f));
	TestTrue(TEXT("Survivor primary contextual action uses left click"),
		HasActionMapping(*InputSettings, TEXT("SurvivorInteract"), EKeys::LeftMouseButton));
	TestTrue(TEXT("Survivor contextual interaction has a gamepad trigger"),
		HasActionMapping(*InputSettings, TEXT("SurvivorInteract"), EKeys::Gamepad_RightTrigger));
	TestTrue(TEXT("Hunter contextual interaction uses E"),
		HasActionMapping(*InputSettings, TEXT("HunterInteract"), EKeys::E));
	TestFalse(TEXT("Prop transformation no longer competes with door interaction on E"),
		HasActionMapping(*InputSettings, TEXT("TransformProp"), EKeys::E));
	TestTrue(TEXT("Returning to human form uses right click"),
		HasActionMapping(*InputSettings, TEXT("ResetPropForm"), EKeys::RightMouseButton));
	TestTrue(TEXT("Human sprint uses left shift"), HasActionMapping(*InputSettings, TEXT("SprintHuman"), EKeys::LeftShift));
	TestTrue(TEXT("Physics Prop straighten remains on left shift"),
		HasActionMapping(*InputSettings, TEXT("StraightenProp"), EKeys::LeftShift));
	TestTrue(TEXT("Human prototype selector uses T"), HasActionMapping(*InputSettings, TEXT("SwitchHumanPrototype"), EKeys::T));
	TestTrue(TEXT("Human prototype selector has a gamepad mapping"),
		HasActionMapping(*InputSettings, TEXT("SwitchHumanPrototype"), EKeys::Gamepad_FaceButton_Top));
	TestTrue(TEXT("Escape opens the in-game menu"),
		HasActionMapping(*InputSettings, TEXT("ToggleMenu"), EKeys::Escape));
	TestTrue(TEXT("Eliminated Props can cycle survivor cameras with Space"),
		HasActionMapping(*InputSettings, TEXT("SpectateNext"), EKeys::SpaceBar));
	TestTrue(TEXT("Eliminated Props can cycle survivor cameras with the gamepad"),
		HasActionMapping(*InputSettings, TEXT("SpectateNext"), EKeys::Gamepad_FaceButton_Bottom));
	TestTrue(TEXT("Hunter memento is mapped to F"),
		HasActionMapping(*InputSettings, TEXT("Memento"), EKeys::F));
	TestTrue(TEXT("Spatial sound radial is mapped to X for both roles"),
		HasActionMapping(*InputSettings, TEXT("SoundRadial"), EKeys::X));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHInteractionOutlineAssetTest,
	"PropHunt.Character.Interaction.OutlineAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHInteractionOutlineAssetTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath OutlineMaterialPath(
		TEXT("/Game/PropHunt/UI/Materials/M_PH_InteractableOutline_PP_V2.M_PH_InteractableOutline_PP_V2"));
	const UMaterial* OutlineMaterial = Cast<UMaterial>(OutlineMaterialPath.TryLoad());
	TestNotNull(TEXT("The interactable white-outline post-process material exists"), OutlineMaterial);
	if (OutlineMaterial == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("The outline asset is compiled as a post-process material"),
		OutlineMaterial->MaterialDomain, MD_PostProcess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHCaptureFlowDefaultsTest,
	"PropHunt.Capture.Graybox.Flow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHCaptureFlowDefaultsTest::RunTest(const FString& Parameters)
{
	const APHPropCharacter* PropDefaults = GetDefault<APHPropCharacter>();
	const APHHunterCharacter* HunterDefaults = GetDefault<APHHunterCharacter>();
	const APHRetentionPoint* PointDefaults = GetDefault<APHRetentionPoint>();
	const APHHumanPrototypeSelector* SelectorDefaults = GetDefault<APHHumanPrototypeSelector>();
	const UPHHumanStaminaWidget* StaminaWidgetDefaults = GetDefault<UPHHumanStaminaWidget>();
	TestNotNull(TEXT("Prop defaults exist"), PropDefaults);
	TestNotNull(TEXT("Hunter defaults exist"), HunterDefaults);
	TestNotNull(TEXT("Retention point defaults exist"), PointDefaults);
	TestNotNull(TEXT("Human prototype selector defaults exist"), SelectorDefaults);
	TestNotNull(TEXT("Native human stamina widget exists"), StaminaWidgetDefaults);
	if (PropDefaults == nullptr || HunterDefaults == nullptr || PointDefaults == nullptr
		|| SelectorDefaults == nullptr || StaminaWidgetDefaults == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Prop starts free"), PropDefaults->GetCaptureState(), EPHPropCaptureState::Free);
	TestEqual(TEXT("Prop starts with no retention"), PropDefaults->GetRetentionCount(), 0);
	TestEqual(TEXT("First retention leaves a real rescue window"), PropDefaults->GetFirstRetentionDuration(), 60.0f);
	TestEqual(TEXT("Second retention still leaves a real rescue window"), PropDefaults->GetSecondRetentionDuration(), 60.0f);
	TestEqual(TEXT("Grace duration is exposed"), PropDefaults->GetGraceDuration(), 5.0f);
	TestEqual(TEXT("Downed crawl speed is deliberately slow"), PropDefaults->GetDownedCrawlSpeed(), 150.0f);
	TestEqual(TEXT("Downed self recovery starts at thirty seconds"), PropDefaults->GetDownedRecoveryDuration(), 30.0f);
	TestEqual(TEXT("Being dropped by the Hunter grants five percent recovery"), PropDefaults->GetCarryDropRecoveryBonus(), 0.05f);
	TestTrue(TEXT("The Prop camera target stays above small physical forms"), PropDefaults->GetCameraTargetHeight() >= 95.0f);
	TestTrue(TEXT("The Prop camera enforces a ground clearance"), PropDefaults->GetMinimumCameraGroundClearance() >= 20.0f);
	TestTrue(TEXT("Pickup preserves the current knockdown-cycle progress"),
		PHCaptureFlow::PreservesKnockdownCycleProgress(
			EPHPropCaptureState::Downed,
			EPHPropCaptureState::Carried));
	TestTrue(TEXT("Hunter drop preserves the current knockdown-cycle progress"),
		PHCaptureFlow::PreservesKnockdownCycleProgress(
			EPHPropCaptureState::Carried,
			EPHPropCaptureState::Downed));
	TestEqual(TEXT("Hunter drop adds its recovery bonus instead of replacing progress"),
		PHCaptureFlow::GetRecoveryProgressAfterHunterDrop(0.40f, 0.05f),
		0.45f);
	TestEqual(TEXT("Hunter drop recovery remains normalized"),
		PHCaptureFlow::GetRecoveryProgressAfterHunterDrop(0.98f, 0.05f),
		1.0f);
	TestFalse(TEXT("One retained Prop does not end a match while another Prop remains active"),
		PHCaptureFlow::ShouldAccelerateAllRetainedElimination(2, 1));
	TestTrue(TEXT("Two retained Props start the terminal Hunter-victory countdown"),
		PHCaptureFlow::ShouldAccelerateAllRetainedElimination(2, 2));
	TestTrue(TEXT("The last non-eliminated Prop retained also starts the terminal countdown"),
		PHCaptureFlow::ShouldAccelerateAllRetainedElimination(1, 1));
	TestFalse(TEXT("No active Prop never starts a redundant retention countdown"),
		PHCaptureFlow::ShouldAccelerateAllRetainedElimination(0, 0));
	TestEqual(TEXT("Carry struggle gives the Hunter enough travel time"), PropDefaults->GetCarryStruggleRequiredAlternations(), 72);
	TestEqual(TEXT("Carry struggle produces a visible lateral step"), PropDefaults->GetCarryStruggleHunterShoveDistance(), 90.0f);
	TestEqual(TEXT("Lateral struggle step is spread over a short readable duration"), HunterDefaults->GetCarryStruggleShoveDuration(), 0.35f);
	TestEqual(TEXT("Hunter capture interaction range is exposed"), HunterDefaults->GetCaptureInteractionDistance(), 225.0f);
	TestEqual(TEXT("Memento requires two prior retentions by default"), HunterDefaults->GetMementoMinimumRetentionCount(), 2);
	TestEqual(TEXT("Memento searches far enough for a visible wall"), HunterDefaults->GetMementoWallSearchDistance(), 1200.0f);
	TestEqual(TEXT("Hunter exposes four sound-emote radial choices"), HunterDefaults->GetSoundEmoteCount(), 4);
	TestEqual(TEXT("Survivor exposes four sound-emote radial choices"), PropDefaults->GetSoundEmoteCount(), 4);
	TestTrue(TEXT("Sound-emote server cooldown prevents spam"), HunterDefaults->GetSoundEmoteCooldownSeconds() >= 2.0f);
	TestEqual(TEXT("Character capsules do not collapse third-person cameras"),
		HunterDefaults->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Camera), ECR_Ignore);
	TestEqual(TEXT("Survivor capsules do not collapse third-person cameras"),
		PropDefaults->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Camera), ECR_Ignore);
	TestEqual(TEXT("Synchronized carry pair shares the Hunter root origin"),
		HunterDefaults->GetCarryAnchorOffset(), FVector::ZeroVector);
	TestTrue(TEXT("Hunter starts in FPS when not carrying"), !HunterDefaults->IsUsingCarryThirdPersonCamera());
	TestTrue(TEXT("Hunter carry camera is a real shoulder TPS view"), HunterDefaults->GetCarryCameraArmLength() >= 250.0f);
	TestTrue(TEXT("Survivor starts in human FPS"), !PropDefaults->IsUsingPropThirdPersonCamera());
	TestEqual(TEXT("Human stamina starts with a readable full-scale maximum"), PropDefaults->GetMaximumHumanStamina(), 100.0f);
	TestTrue(TEXT("Human sprint is faster than normal movement"), PropDefaults->GetHumanSprintSpeed() > 500.0f);
	TestTrue(TEXT("Human sprint drains faster than it recharges"),
		PropDefaults->GetHumanSprintDrainPerSecond() > PropDefaults->GetHumanStaminaRechargePerSecond());
	TestEqual(TEXT("Human jump consumes part of the stamina bar"), PropDefaults->GetHumanJumpStaminaCost(), 20.0f);
	TestEqual(TEXT("Retention point interaction range is exposed"), PointDefaults->GetInteractionDistance(), 225.0f);
	TestEqual(TEXT("Retention rescue requires a short hold"), PointDefaults->GetReleaseDuration(), 2.0f);
	TestEqual(TEXT("Retention rescue starts empty"), PointDefaults->GetReleaseProgressNormalized(), 0.0f);
	TestNull(TEXT("Retention rescue starts without a rescuer"), PointDefaults->GetReleaseRescuer());
	TestTrue(TEXT("Human prototype selector replicates"), SelectorDefaults->GetIsReplicated());
	TestEqual(TEXT("Human prototype selector is reachable near the authored starts"), SelectorDefaults->GetInteractionDistance(), 350.0f);
	TestTrue(TEXT("A valid melee hit downs a free Prop"),
		PHCaptureFlow::CanMeleeHitDown(EPHPropCaptureState::Free));
	TestFalse(TEXT("Grace prevents an immediate melee redown"),
		PHCaptureFlow::CanMeleeHitDown(EPHPropCaptureState::Grace));
	TestFalse(TEXT("An already downed Prop cannot be downed twice"),
		PHCaptureFlow::CanMeleeHitDown(EPHPropCaptureState::Downed));
	TestFalse(TEXT("Second retention is not immediate elimination"),
		PHCaptureFlow::ShouldEliminateOnRetention(2, 3));
	TestTrue(TEXT("Third retention eliminates in the graybox rule"),
		PHCaptureFlow::ShouldEliminateOnRetention(3, 3));
	TestEqual(TEXT("First retention uses the normal timer"),
		PHCaptureFlow::GetRetentionDuration(1, 60.0f, 60.0f), 60.0f);
	TestEqual(TEXT("Second retention uses the shorter timer"),
		PHCaptureFlow::GetRetentionDuration(2, 60.0f, 60.0f), 60.0f);
	const float PickupMinimumAimDot = FMath::Cos(FMath::DegreesToRadians(35.0f));
	TestTrue(TEXT("A downed target below the camera remains selectable when horizontally in front"),
		PHCaptureFlow::IsDownedPickupDirectionAllowed(
			FVector::ForwardVector,
			FVector(75.0f, 0.0f, -130.0f),
			PickupMinimumAimDot));
	TestFalse(TEXT("A close downed target behind the Hunter is not selected"),
		PHCaptureFlow::IsDownedPickupDirectionAllowed(
			FVector::ForwardVector,
			FVector(-35.0f, 0.0f, -130.0f),
			PickupMinimumAimDot));
	TestTrue(TEXT("A downed target directly beneath the Hunter remains selectable"),
		PHCaptureFlow::IsDownedPickupDirectionAllowed(
			FVector::ForwardVector,
			FVector(0.0f, 0.0f, -130.0f),
			PickupMinimumAimDot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHPropnightReferenceAssetsTest,
	"PropHunt.Character.PropnightReferenceAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHPropnightReferenceAssetsTest::RunTest(const FString& Parameters)
{
	const USkeletalMesh* IsaacMesh = Cast<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/PropHunt/Characters/Survivor/Prototype/PropnightReference/Isaac/SK_PH_Survivor_Isaac_Ref.SK_PH_Survivor_Isaac_Ref")).TryLoad());
	const USkeletalMesh* JunMesh = Cast<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/PropHunt/Characters/Survivor/Prototype/PropnightReference/Jun/SK_PH_Survivor_Jun_Ref.SK_PH_Survivor_Jun_Ref")).TryLoad());
	const USkeletalMesh* SamuraiMesh = Cast<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/PropHunt/Characters/Hunter/Prototype/PropnightReference/Samurai/SK_PH_Hunter_Samurai_Ref.SK_PH_Hunter_Samurai_Ref")).TryLoad());
	const USkeletalMesh* MaddyMesh = Cast<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/PropHunt/Characters/Hunter/Prototype/PropnightReference/Maddy/SK_PH_Hunter_Maddy_Ref.SK_PH_Hunter_Maddy_Ref")).TryLoad());
	const UAnimSequence* JunCrawl = Cast<UAnimSequence>(FSoftObjectPath(
		TEXT("/Game/PropHunt/Characters/Survivor/Prototype/PropnightReference/Jun/A_PH_Jun_Crawl_Forward.A_PH_Jun_Crawl_Forward")).TryLoad());
	const UAnimSequence* MaddyCarry = Cast<UAnimSequence>(FSoftObjectPath(
		TEXT("/Game/PropHunt/Characters/Hunter/Prototype/PropnightReference/Maddy/A_PH_Maddy_Carry_Walk.A_PH_Maddy_Carry_Walk")).TryLoad());
	const UClass* PropBlueprintClass = FSoftClassPath(
		TEXT("/Game/PropHunt/Characters/Props/BP_PH_PropCharacter.BP_PH_PropCharacter_C"))
		.TryLoadClass<APHPropCharacter>();
	const APHPropCharacter* ConfiguredPropDefaults = PropBlueprintClass != nullptr
		? Cast<APHPropCharacter>(PropBlueprintClass->GetDefaultObject())
		: nullptr;

	TestNotNull(TEXT("Isaac reference mesh exists"), IsaacMesh);
	TestNotNull(TEXT("Jun reference mesh exists"), JunMesh);
	TestNotNull(TEXT("Samurai reference mesh exists"), SamuraiMesh);
	TestNotNull(TEXT("Maddy reference mesh exists"), MaddyMesh);
	TestNotNull(TEXT("Jun crawl animation exists"), JunCrawl);
	TestNotNull(TEXT("Maddy carry animation exists"), MaddyCarry);
	TestNotNull(TEXT("Configured Prop Blueprint class exists"), PropBlueprintClass);
	TestNotNull(TEXT("Configured Prop Blueprint defaults exist"), ConfiguredPropDefaults);
	if (IsaacMesh == nullptr || JunMesh == nullptr || SamuraiMesh == nullptr || MaddyMesh == nullptr)
	{
		return false;
	}

	TestNotEqual(TEXT("Jun and Isaac retain distinct source Skeletons"),
		JunMesh->GetSkeleton(), IsaacMesh->GetSkeleton());
	TestNotEqual(TEXT("Maddy and Samurai retain distinct source Skeletons"),
		MaddyMesh->GetSkeleton(), SamuraiMesh->GetSkeleton());
	if (JunCrawl != nullptr)
	{
		TestTrue(TEXT("Jun crawl uses the Jun Skeleton"),
			JunCrawl->GetSkeleton() == JunMesh->GetSkeleton());
	}
	if (MaddyCarry != nullptr)
	{
		TestTrue(TEXT("Maddy carry uses the Maddy Skeleton"),
			MaddyCarry->GetSkeleton() == MaddyMesh->GetSkeleton());
	}
	if (ConfiguredPropDefaults != nullptr)
	{
		TestTrue(TEXT("Configured Prop exposes the alternative Jun prototype"),
			ConfiguredPropDefaults->HasAlternativeHumanPrototype());
		TestEqual(TEXT("Configured Prop starts on Isaac"),
			ConfiguredPropDefaults->GetActiveHumanPrototypeName(), FName(TEXT("Isaac")));
		TestNotNull(TEXT("Isaac has a synchronized carried move animation"),
			ConfiguredPropDefaults->GetPrimaryHumanCarriedMoveAnimation());
		TestNotNull(TEXT("Jun has a carried move animation ready for its future paired Hunter"),
			ConfiguredPropDefaults->GetAlternativeHumanCarriedMoveAnimation());
		TestEqual(TEXT("Isaac synchronized pair shares the Hunter actor origin"),
			ConfiguredPropDefaults->GetPrimaryCarryAttachmentOffset(), FVector::ZeroVector);
		TestEqual(TEXT("Jun paired animation also starts from the shared actor origin"),
			ConfiguredPropDefaults->GetAlternativeCarryAttachmentOffset(), FVector::ZeroVector);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHCaptureAuthoredMapTest,
	"PropHunt.Capture.Graybox.AuthoredMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHCaptureAuthoredMapTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath MapPath(TEXT("/Game/PropHunt/Tests/L_PH_CaptureTest.L_PH_CaptureTest"));
	const UWorld* CaptureWorld = Cast<UWorld>(MapPath.TryLoad());
	TestNotNull(TEXT("Capture test map exists"), CaptureWorld);
	if (CaptureWorld == nullptr || CaptureWorld->PersistentLevel == nullptr)
	{
		return false;
	}

	int32 ObjectiveCount = 0;
	int32 RetentionPointCount = 0;
	int32 PlayerStartCount = 0;
	int32 HumanPrototypeSelectorCount = 0;
	int32 TransformTargetCount = 0;
	int32 ExitGateCount = 0;
	FVector HumanPrototypeSelectorLocation = FVector::ZeroVector;
	TArray<FVector> PlayerStartLocations;
	for (const AActor* Actor : CaptureWorld->PersistentLevel->Actors)
	{
		ObjectiveCount += IsValid(Actor) && Actor->IsA<APHObjectiveActor>() ? 1 : 0;
		RetentionPointCount += IsValid(Actor) && Actor->IsA<APHRetentionPoint>() ? 1 : 0;
		PlayerStartCount += IsValid(Actor) && Actor->IsA<APlayerStart>() ? 1 : 0;
		HumanPrototypeSelectorCount += IsValid(Actor) && Actor->IsA<APHHumanPrototypeSelector>() ? 1 : 0;
		TransformTargetCount += IsValid(Actor) && Actor->IsA<APHPropTransformTarget>() ? 1 : 0;
		ExitGateCount += IsValid(Actor) && Actor->IsA<APHExitGate>() ? 1 : 0;
		if (IsValid(Actor) && Actor->IsA<APlayerStart>())
		{
			PlayerStartLocations.Add(Actor->GetActorLocation());
		}
		if (IsValid(Actor) && Actor->IsA<APHHumanPrototypeSelector>())
		{
			HumanPrototypeSelectorLocation = Actor->GetActorLocation();
		}
	}

	TestEqual(TEXT("Five authored objectives keep normal match rules valid"), ObjectiveCount, 5);
	TestEqual(TEXT("Two authored retention points cover the expanded arena"), RetentionPointCount, 2);
	TestEqual(TEXT("Hunter, victim and rescuer have authored starts"), PlayerStartCount, 3);
	TestEqual(TEXT("One authored Isaac and Jun selector is present"), HumanPrototypeSelectorCount, 1);
	TestEqual(TEXT("The three baseline and three textured fun transformation targets are present"), TransformTargetCount, 6);
	TestEqual(TEXT("Two authored exit gates serve the arena"), ExitGateCount, 2);
	if (HumanPrototypeSelectorCount == 1 && PlayerStartLocations.Num() > 0)
	{
		float ClosestStartDistanceSquared = MAX_flt;
		for (const FVector& PlayerStartLocation : PlayerStartLocations)
		{
			ClosestStartDistanceSquared = FMath::Min(
				ClosestStartDistanceSquared,
				FVector::DistSquared(PlayerStartLocation, HumanPrototypeSelectorLocation));
		}
		TestTrue(TEXT("Isaac and Jun selector is within interaction range of a player start"),
			ClosestStartDistanceSquared <= FMath::Square(350.0f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHExitGateDefaultsTest,
	"PropHunt.Escape.ExitGate.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHExitGateDefaultsTest::RunTest(const FString& Parameters)
{
	const APHExitGate* GateDefaults = GetDefault<APHExitGate>();
	TestNotNull(TEXT("Exit gate defaults exist"), GateDefaults);
	if (GateDefaults == nullptr)
	{
		return false;
	}
	TestTrue(TEXT("Exit gate replicates"), GateDefaults->GetIsReplicated());
	TestEqual(TEXT("Opening a gate takes twenty seconds"), GateDefaults->GetOpenDurationSeconds(), 20.0f);
	TestEqual(TEXT("Gate interaction uses the short contextual range"), GateDefaults->GetInteractionDistance(), 225.0f);
	TestTrue(TEXT("Standing on the offset control box is always inside the interaction range"),
		GateDefaults->IsWithinInteractionRange(GateDefaults->GetInteractionPoint()));
	TestFalse(TEXT("Gate is disabled before objectives complete"), GateDefaults->IsGateEnabled());
	TestFalse(TEXT("Gate is closed before objectives complete"), GateDefaults->IsGateOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHObjectiveDefaultsTest,
	"PropHunt.Objective.Graybox.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHObjectiveDefaultsTest::RunTest(const FString& Parameters)
{
	const APHObjectiveActor* ObjectiveDefaults = GetDefault<APHObjectiveActor>();
	TestNotNull(TEXT("Objective class defaults exist"), ObjectiveDefaults);
	if (ObjectiveDefaults == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Objective actor replicates"), ObjectiveDefaults->GetIsReplicated());
	TestEqual(TEXT("Objective duration supports cooperative play without becoming instant"), ObjectiveDefaults->GetInteractionDurationSeconds(), 24.0f);
	TestEqual(TEXT("Graybox objective range is exposed"), ObjectiveDefaults->GetInteractionDistance(), 225.0f);
	TestEqual(TEXT("First objective preserves partial progress"), ObjectiveDefaults->GetInterruptionPolicy(), EPHObjectiveInterruptionPolicy::Preserve);
	TestEqual(TEXT("Hunter melee removes ten percentage points from a repaired objective"),
		ObjectiveDefaults->GetHunterMeleeRegressionFraction(), 0.10f);
	TestEqual(TEXT("Objective starts with no progress"), ObjectiveDefaults->GetObjectiveProgress(), 0.0f);
	TestFalse(TEXT("Objective CDO is not completed"), ObjectiveDefaults->IsObjectiveCompleted());
	TestEqual(TEXT("Two Props contribute one and a half workers by default"),
		PHObjectiveFlow::GetContributionMultiplier(2, 0.5f), 1.5f);
	TestEqual(TEXT("One Prop advances one twenty-fourth per second"),
		PHObjectiveFlow::AdvanceProgress(0.0f, 1.0f, 24.0f, 1, 0.5f), 1.0f / 24.0f);
	TestEqual(TEXT("Hunter melee regression is discrete and server bounded"),
		PHObjectiveFlow::ApplyHunterMeleeRegression(0.55f, 0.10f), 0.45f);
	TestEqual(TEXT("Hunter melee regression cannot go below zero"),
		PHObjectiveFlow::ApplyHunterMeleeRegression(0.05f, 0.10f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHObjectiveAuthoredMapTest,
	"PropHunt.Objective.Graybox.AuthoredMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHObjectiveAuthoredMapTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath MapPath(TEXT("/Game/PropHunt/Tests/L_PH_ObjectiveTest.L_PH_ObjectiveTest"));
	const UWorld* ObjectiveWorld = Cast<UWorld>(MapPath.TryLoad());
	TestNotNull(TEXT("Objective test map exists"), ObjectiveWorld);
	if (ObjectiveWorld == nullptr || ObjectiveWorld->PersistentLevel == nullptr)
	{
		return false;
	}

	int32 ObjectiveCount = 0;
	int32 PlayerStartCount = 0;
	for (const AActor* Actor : ObjectiveWorld->PersistentLevel->Actors)
	{
		ObjectiveCount += IsValid(Actor) && Actor->IsA<APHObjectiveActor>() ? 1 : 0;
		PlayerStartCount += IsValid(Actor) && Actor->IsA<APlayerStart>() ? 1 : 0;
	}

	TestEqual(TEXT("Five authored objective anchors are present"), ObjectiveCount, 5);
	TestEqual(TEXT("Hunter and Prop have separate authored starts"), PlayerStartCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHHunterMeleeDefaultsTest,
	"PropHunt.Character.Melee.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHHunterMeleeDefaultsTest::RunTest(const FString& Parameters)
{
	const APHHunterCharacter* HunterDefaults = GetDefault<APHHunterCharacter>();
	TestNotNull(TEXT("Hunter class defaults exist"), HunterDefaults);
	if (HunterDefaults == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Melee range starts at the graybox value"), HunterDefaults->GetMeleeRange(), 175.0f);
	TestEqual(TEXT("Melee box width starts at the tolerant graybox value"), HunterDefaults->GetMeleeWidth(), 90.0f);
	TestEqual(TEXT("Melee vertical tolerance starts at the graybox value"), HunterDefaults->GetMeleeVerticalTolerance(), 70.0f);
	TestEqual(TEXT("Melee recovery starts at the graybox value"), HunterDefaults->GetMeleeRecoverySeconds(), 0.8f);
	TestEqual(TEXT("Hunter movement capsule ignores other Pawns"),
		HunterDefaults->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
	TestEqual(TEXT("Hunter movement capsule ignores Prop query hitboxes"),
		HunterDefaults->GetCapsuleComponent()->GetCollisionResponseToChannel(PHCollision::PropHitbox), ECR_Ignore);
	TestEqual(TEXT("Hunter movement capsule cannot become an invisible step"),
		HunterDefaults->GetCapsuleComponent()->CanCharacterStepUpOn, ECB_No);
	TestEqual(TEXT("No attack has been confirmed on the CDO"), HunterDefaults->GetMeleeAttackSequence(), 0);
	TestFalse(TEXT("The CDO does not report a hit"), HunterDefaults->DidLastMeleeAttackHitProp());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHPropTransformationDefaultsTest,
	"PropHunt.Character.Transformation.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHPropTransformationDefaultsTest::RunTest(const FString& Parameters)
{
	const APHPropCharacter* PropDefaults = GetDefault<APHPropCharacter>();
	TestNotNull(TEXT("Prop class defaults exist"), PropDefaults);
	if (PropDefaults == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Transformation requires approaching the target"), PropDefaults->GetTransformationDistance(), 250.0f);
	TestEqual(TEXT("Transformation cooldown is exposed with its graybox value"), PropDefaults->GetTransformationCooldownSeconds(), 1.0f);
	TestEqual(TEXT("Transformation targeting cone starts at the TPS graybox value"), PropDefaults->GetTransformationTargetingHalfAngleDegrees(), 12.0f);
	TestEqual(TEXT("Server placement clearance is bounded to 25 cm by default"), PropDefaults->GetMaximumPlacementAdjustment(), 25.0f);
	TestEqual(TEXT("Prop movement capsule ignores other Pawns"),
		PropDefaults->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
	TestEqual(TEXT("Prop movement capsule no longer answers melee visibility traces"),
		PropDefaults->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Visibility), ECR_Ignore);
	TestEqual(TEXT("Prop movement capsule cannot become an invisible step"),
		PropDefaults->GetCapsuleComponent()->CanCharacterStepUpOn, ECB_No);
	TestEqual(TEXT("Native CDO does not silently authorize forms"), PropDefaults->GetAllowedPropFormCount(), 0);
	TestEqual(TEXT("No transformation result exists on the CDO"), PropDefaults->GetTransformationSequence(), 0);
	TestEqual(TEXT("No form is active on the CDO"), PropDefaults->GetActivePropForm(), static_cast<const UPHPropFormDataAsset*>(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHPropTransformationAssetsTest,
	"PropHunt.Character.Transformation.AuthoredForms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHPropTransformationAssetsTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath CratePath(TEXT("/Game/PropHunt/Data/Transformation/DA_PH_PropForm_Crate_Test.DA_PH_PropForm_Crate_Test"));
	const FSoftObjectPath BarrelPath(TEXT("/Game/PropHunt/Data/Transformation/DA_PH_PropForm_Barrel_Test.DA_PH_PropForm_Barrel_Test"));
	const FSoftObjectPath ChickenPath(TEXT("/Game/PropHunt/Data/Transformation/DA_PH_PropForm_ScreamingChicken.DA_PH_PropForm_ScreamingChicken"));
	const FSoftObjectPath ToyBoxPath(TEXT("/Game/PropHunt/Data/Transformation/DA_PH_PropForm_ToyBox.DA_PH_PropForm_ToyBox"));
	const FSoftObjectPath ToyDrumPath(TEXT("/Game/PropHunt/Data/Transformation/DA_PH_PropForm_ToyDrum.DA_PH_PropForm_ToyDrum"));
	const FSoftObjectPath ToyBrickPath(TEXT("/Game/PropHunt/Data/Transformation/DA_PH_PropForm_ToyBrick.DA_PH_PropForm_ToyBrick"));
	const UPHPropFormDataAsset* Crate = Cast<UPHPropFormDataAsset>(CratePath.TryLoad());
	const UPHPropFormDataAsset* Barrel = Cast<UPHPropFormDataAsset>(BarrelPath.TryLoad());
	const UPHPropFormDataAsset* Chicken = Cast<UPHPropFormDataAsset>(ChickenPath.TryLoad());
	const UPHPropFormDataAsset* ToyBox = Cast<UPHPropFormDataAsset>(ToyBoxPath.TryLoad());
	const UPHPropFormDataAsset* ToyDrum = Cast<UPHPropFormDataAsset>(ToyDrumPath.TryLoad());
	const UPHPropFormDataAsset* ToyBrick = Cast<UPHPropFormDataAsset>(ToyBrickPath.TryLoad());
	TestNotNull(TEXT("Authored crate form exists"), Crate);
	TestNotNull(TEXT("Authored barrel form exists"), Barrel);
	TestNotNull(TEXT("Authored Screaming Chicken form exists"), Chicken);
	TestNotNull(TEXT("Textured Toy Box form exists"), ToyBox);
	TestNotNull(TEXT("Textured Toy Drum form exists"), ToyDrum);
	TestNotNull(TEXT("Textured small Toy Brick form exists"), ToyBrick);
	if (Crate == nullptr || Barrel == nullptr || Chicken == nullptr
		|| ToyBox == nullptr || ToyDrum == nullptr || ToyBrick == nullptr)
	{
		return false;
	}

	FText ValidationError;
	TestTrue(TEXT("Crate form is valid and references a production cooked mesh"), Crate->HasValidDefinition(&ValidationError));
	if (!ValidationError.IsEmpty())
	{
		AddError(ValidationError.ToString());
	}
	ValidationError = FText::GetEmpty();
	TestTrue(TEXT("Barrel form is valid and references a production cooked mesh"), Barrel->HasValidDefinition(&ValidationError));
	if (!ValidationError.IsEmpty())
	{
		AddError(ValidationError.ToString());
	}
	ValidationError = FText::GetEmpty();
	TestTrue(TEXT("Screaming Chicken form is valid and references its cooked mesh"), Chicken->HasValidDefinition(&ValidationError));
	if (!ValidationError.IsEmpty())
	{
		AddError(ValidationError.ToString());
	}
	for (const UPHPropFormDataAsset* FunForm : { ToyBox, ToyDrum, ToyBrick })
	{
		ValidationError = FText::GetEmpty();
		TestTrue(*FString::Printf(TEXT("%s is a valid textured cooked form"), *FunForm->FormId.ToString()),
			FunForm->HasValidDefinition(&ValidationError));
		if (!ValidationError.IsEmpty())
		{
			AddError(ValidationError.ToString());
		}
		TestNotNull(*FString::Printf(TEXT("%s has a material"), *FunForm->FormId.ToString()),
			FunForm->StaticMesh != nullptr ? FunForm->StaticMesh->GetMaterial(0) : nullptr);
		if (FunForm->StaticMesh != nullptr && FunForm->StaticMesh->GetMaterial(0) != nullptr)
		{
			TestTrue(*FString::Printf(TEXT("%s uses the authored FunProps material"), *FunForm->FormId.ToString()),
				FunForm->StaticMesh->GetMaterial(0)->GetPathName().StartsWith(
					TEXT("/Game/PropHunt/Gameplay/FunProps/Materials/")));
		}
	}
	TestNotEqual(TEXT("The two test forms have distinct stable identifiers"), Crate->FormId, Barrel->FormId);
	TestNotEqual(TEXT("The Screaming Chicken has a distinct stable identifier"), Chicken->FormId, Barrel->FormId);
	TestEqual(TEXT("Crate uses its authored box hitbox"), Crate->HitboxShape, EPHPropHitboxShape::Box);
	TestTrue(TEXT("Crate box hitbox has positive authored dimensions"), Crate->HitboxBoxHalfExtents.GetMin() > 0.0f);
	TestEqual(TEXT("Barrel uses its authored capsule hitbox"), Barrel->HitboxShape, EPHPropHitboxShape::Capsule);
	for (const UPHPropFormDataAsset* PhysicsForm : { Crate, Barrel, Chicken, ToyBox, ToyDrum, ToyBrick })
	{
		const UBodySetup* BodySetup = PhysicsForm->StaticMesh != nullptr ? PhysicsForm->StaticMesh->GetBodySetup() : nullptr;
		TestNotNull(*FString::Printf(TEXT("%s has a BodySetup for playable physics"), *PhysicsForm->FormId.ToString()), BodySetup);
		if (BodySetup != nullptr)
		{
			TestTrue(*FString::Printf(TEXT("%s has simple collision for Chaos"), *PhysicsForm->FormId.ToString()),
				BodySetup->CollisionTraceFlag != CTF_UseComplexAsSimple
				&& (BodySetup->AggGeom.GetElementCount() > 0));
		}
	}
	TestTrue(TEXT("Barrel capsule hitbox has valid authored dimensions"),
		Barrel->HitboxCapsuleRadius > 0.0f && Barrel->HitboxCapsuleHalfHeight >= Barrel->HitboxCapsuleRadius);
	TestTrue(TEXT("Toy Brick remains a genuinely small camera-regression form"),
		ToyBrick->CapsuleHalfHeight <= 30.0f && ToyBrick->HitboxBoxHalfExtents.Z <= 21.0f);

	const FSoftClassPath PropBlueprintClassPath(TEXT("/Game/PropHunt/Characters/Props/BP_PH_PropCharacter.BP_PH_PropCharacter_C"));
	UClass* PropBlueprintClass = PropBlueprintClassPath.TryLoadClass<APHPropCharacter>();
	TestNotNull(TEXT("Prop Blueprint class exists"), PropBlueprintClass);
	const APHPropCharacter* BlueprintDefaults = PropBlueprintClass != nullptr
		? Cast<APHPropCharacter>(PropBlueprintClass->GetDefaultObject())
		: nullptr;
	TestNotNull(TEXT("Prop Blueprint defaults exist"), BlueprintDefaults);
	if (BlueprintDefaults != nullptr)
	{
		TestEqual(TEXT("Configured Prop uses the close transformation range"), BlueprintDefaults->GetTransformationDistance(), 250.0f);
		TestEqual(TEXT("Configured Prop first retention leaves a rescue window"), BlueprintDefaults->GetFirstRetentionDuration(), 60.0f);
		TestEqual(TEXT("Configured Prop second retention leaves a rescue window"), BlueprintDefaults->GetSecondRetentionDuration(), 60.0f);
		TestEqual(TEXT("Exactly six authored forms are explicitly allowlisted"), BlueprintDefaults->GetAllowedPropFormCount(), 6);
		TestTrue(TEXT("Crate is explicitly allowed"), BlueprintDefaults->IsPropFormAllowed(Crate));
		TestTrue(TEXT("Barrel is explicitly allowed"), BlueprintDefaults->IsPropFormAllowed(Barrel));
		TestTrue(TEXT("Screaming Chicken is explicitly allowed"), BlueprintDefaults->IsPropFormAllowed(Chicken));
		TestTrue(TEXT("Toy Box is explicitly allowed"), BlueprintDefaults->IsPropFormAllowed(ToyBox));
		TestTrue(TEXT("Toy Drum is explicitly allowed"), BlueprintDefaults->IsPropFormAllowed(ToyDrum));
		TestTrue(TEXT("Toy Brick is explicitly allowed"), BlueprintDefaults->IsPropFormAllowed(ToyBrick));
		TestNotNull(TEXT("Playable forms reuse the validated Physics Prop definition"), BlueprintDefaults->GetPhysicsPropDefinition());
		if (BlueprintDefaults->GetPhysicsPropDefinition() != nullptr)
		{
			TestEqual(TEXT("Only the matching playable Chicken form inherits the distinctive impact sound"),
				Chicken->FormId, BlueprintDefaults->GetPhysicsPropDefinition()->PropId);
		}
		USceneComponent* PresentationRoot = BlueprintDefaults->GetPropPresentationRoot();
		TestNotNull(TEXT("Playable Props have a dedicated smoothed presentation root"), PresentationRoot);
		if (PresentationRoot != nullptr)
		{
			TestEqual(TEXT("The presentation root remains attached to the authoritative capsule"),
				PresentationRoot->GetAttachParent(),
				static_cast<USceneComponent*>(BlueprintDefaults->GetCapsuleComponent()));
			USpringArmComponent* CameraBoom = BlueprintDefaults->GetPropCameraBoom();
			UStaticMeshComponent* PresentationMesh = BlueprintDefaults->GetPropPresentationMesh();
			TestNotNull(TEXT("Playable Props retain their third-person camera boom"), CameraBoom);
			TestNotNull(TEXT("Playable Props retain their visual mesh"), PresentationMesh);
			if (CameraBoom != nullptr)
			{
				TestEqual(TEXT("The third-person camera follows the smoothed presentation root"),
					CameraBoom->GetAttachParent(), PresentationRoot);
			}
			if (PresentationMesh != nullptr)
			{
				TestEqual(TEXT("The visible Prop follows the same smoothed presentation root as the camera"),
					PresentationMesh->GetAttachParent(), PresentationRoot);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHPhysicsPropPrototypeDefaultsTest,
	"PropHunt.PhysicsProp.Prototype.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHPhysicsPropPrototypeDefaultsTest::RunTest(const FString& Parameters)
{
	const APHPhysicsPropPrototype* PrototypeDefaults = GetDefault<APHPhysicsPropPrototype>();
	TestNotNull(TEXT("Physics Prop prototype class defaults exist"), PrototypeDefaults);
	if (PrototypeDefaults == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Physics Prop prototype replicates"), PrototypeDefaults->GetIsReplicated());
	TestTrue(TEXT("Physics Prop prototype replicates movement"), PrototypeDefaults->IsReplicatingMovement());
	TestEqual(TEXT("Physics Prop prototype starts at the reconstructed 30 Hz"), PrototypeDefaults->GetNetUpdateFrequency(), 30.0f);
	TestNotNull(TEXT("Physics Prop prototype has a physical mesh"), PrototypeDefaults->GetPhysicsMesh());
	if (PrototypeDefaults->GetPhysicsMesh() != nullptr)
	{
		TestEqual(TEXT("Physics mesh ignores Pawn movement capsules"),
			PrototypeDefaults->GetPhysicsMesh()->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
		TestEqual(TEXT("Physics mesh cannot become a character step"),
			PrototypeDefaults->GetPhysicsMesh()->CanCharacterStepUpOn, ECB_No);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHPhysicsPropPresentationSmoothingTest,
	"PropHunt.PhysicsProp.NetworkPresentation.Smoothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHPhysicsPropPresentationSmoothingTest::RunTest(const FString& Parameters)
{
	FPHPhysicsPropPresentationSettings Settings;
	FPHPhysicsPropPresentationState State;
	const FVector LinearVelocity(600.0f, 0.0f, 0.0f);
	const FVector AngularVelocity(0.0f, 180.0f, 0.0f);
	const float ClientDeltaSeconds = 1.0f / 120.0f;
	const int32 ClientFramesPerSnapshot = 4;
	double ClientTimeSeconds = 0.0;
	FVector NetworkLocation = FVector::ZeroVector;
	FQuat NetworkRotation = FQuat::Identity;
	FVector PreviousVisualLocation = FVector::ZeroVector;
	float MaximumVisualFrameStep = 0.0f;
	int32 MovingVisualFrames = 0;

	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		if (Frame % ClientFramesPerSnapshot == 0)
		{
			NetworkLocation = FVector(LinearVelocity.X * ClientTimeSeconds, 0.0f, 0.0f);
			NetworkRotation = FQuat(
				FVector::YAxisVector,
				FMath::DegreesToRadians(AngularVelocity.Y * ClientTimeSeconds));
		}

		PHPhysicsPropPresentation::Advance(
			State,
			NetworkLocation,
			NetworkRotation,
			LinearVelocity,
			AngularVelocity,
			ClientTimeSeconds,
			ClientDeltaSeconds,
			Settings);

		if (Frame > 0)
		{
			const float FrameStep = FVector::Distance(State.Location, PreviousVisualLocation);
			MaximumVisualFrameStep = FMath::Max(MaximumVisualFrameStep, FrameStep);
			MovingVisualFrames += FrameStep > 0.01f ? 1 : 0;
		}
		PreviousVisualLocation = State.Location;
		ClientTimeSeconds += ClientDeltaSeconds;
	}

	const float RawThirtyHertzStep = LinearVelocity.X / 30.0f;
	const float ExpectedLocation = LinearVelocity.X * static_cast<float>(ClientTimeSeconds - ClientDeltaSeconds);
	TestTrue(TEXT("A 30 Hz stream produces movement on almost every 120 FPS presentation frame"),
		MovingVisualFrames >= 115);
	TestTrue(TEXT("No smoothed presentation frame reproduces the full raw 30 Hz position step"),
		MaximumVisualFrameStep < RawThirtyHertzStep * 0.5f);
	TestTrue(TEXT("Bounded extrapolation keeps the visual Prop close to the authoritative trajectory"),
		FMath::Abs(State.Location.X - ExpectedLocation) < 40.0f);
	TestTrue(TEXT("Angular velocity extrapolation advances the rolling Prop between snapshots"),
		State.Rotation.AngularDistance(FQuat::Identity) > FMath::DegreesToRadians(90.0f));

	const FVector TeleportLocation(1000.0f, 250.0f, 75.0f);
	PHPhysicsPropPresentation::Advance(
		State,
		TeleportLocation,
		FQuat::Identity,
		FVector::ZeroVector,
		FVector::ZeroVector,
		ClientTimeSeconds,
		ClientDeltaSeconds,
		Settings);
	TestTrue(TEXT("A real teleport bypasses interpolation instead of dragging the camera across the map"),
		State.Location.Equals(TeleportLocation, 0.01f));

	const double VelocityChangeTime = ClientTimeSeconds + 0.11;
	PHPhysicsPropPresentation::Advance(
		State,
		TeleportLocation,
		FQuat::Identity,
		LinearVelocity,
		FVector::ZeroVector,
		VelocityChangeTime,
		ClientDeltaSeconds,
		Settings);
	TestTrue(TEXT("A new velocity snapshot does not extrapolate from the age of the previous resting pose"),
		State.Location.Equals(TeleportLocation, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHPhysicsPropJumpImpulseTest,
	"PropHunt.PhysicsProp.Movement.JumpImpulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHPhysicsPropJumpImpulseTest::RunTest(const FString& Parameters)
{
	const FVector CurrentVelocity(100.0f, 50.0f, -200.0f);
	const FVector TargetVelocity = PHPhysicsPropPresentation::ComputeJumpTargetVelocity(
		CurrentVelocity,
		FVector::ForwardVector,
		480.0f,
		225.0f,
		628.0f);
	TestTrue(TEXT("Every accepted jump restores the authored upward velocity"),
		FMath::IsNearlyEqual(TargetVelocity.Z, 480.0f));
	TestTrue(TEXT("Jump adds the authored directional burst without erasing lateral momentum"),
		TargetVelocity.Equals(FVector(325.0f, 50.0f, 480.0f), 0.01f));

	const FVector NoInputTarget = PHPhysicsPropPresentation::ComputeJumpTargetVelocity(
		CurrentVelocity,
		FVector::ZeroVector,
		480.0f,
		225.0f,
		628.0f);
	TestTrue(TEXT("A neutral jump preserves horizontal momentum"),
		NoInputTarget.Equals(FVector(100.0f, 50.0f, 480.0f), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHPhysicsPropGroundContactPresentationTest,
	"PropHunt.PhysicsProp.NetworkPresentation.GroundContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHPhysicsPropGroundContactPresentationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A grounded visual is lowered onto the replicated floor"),
		PHPhysicsPropPresentation::ComputeGroundContactCorrection(true, 112.0f, 100.0f, 25.0f),
		-12.0f);
	TestEqual(TEXT("An airborne visual is never magnetized to the floor"),
		PHPhysicsPropPresentation::ComputeGroundContactCorrection(false, 112.0f, 100.0f, 25.0f),
		0.0f);
	TestEqual(TEXT("A correction beyond the authored probe budget is rejected"),
		PHPhysicsPropPresentation::ComputeGroundContactCorrection(true, 140.0f, 100.0f, 25.0f),
		0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHScreamingChickenAssetTest,
	"PropHunt.PhysicsProp.ScreamingChicken.Asset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHScreamingChickenAssetTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath DefinitionPath(TEXT("/Game/PropHunt/Data/Physics/DA_PH_PhysicsProp_ScreamingChicken.DA_PH_PhysicsProp_ScreamingChicken"));
	const UPHPhysicsPropDataAsset* Definition = Cast<UPHPhysicsPropDataAsset>(DefinitionPath.TryLoad());
	TestNotNull(TEXT("Screaming Chicken definition exists"), Definition);
	if (Definition == nullptr)
	{
		return false;
	}

	FText ValidationError;
	TestTrue(TEXT("Screaming Chicken definition is valid"), Definition->HasValidDefinition(&ValidationError));
	if (!ValidationError.IsEmpty())
	{
		AddError(ValidationError.ToString());
	}
	TestEqual(TEXT("Screaming Chicken is the first stable Physics Prop id"), Definition->PropId, FName(TEXT("ScreamingChicken")));
	TestTrue(TEXT("Reconstructed mass is preserved"), FMath::IsNearlyEqual(Definition->MassOverrideKilograms, 8.3912f, 0.001f));
	TestEqual(TEXT("Reconstructed impulse-per-mass threshold is preserved"), Definition->ImpactImpulsePerMassThreshold, 50.0f);
	TestNotNull(TEXT("Screaming Chicken impact sound is cooked and configured"), Definition->ImpactSound.Get());
	TestEqual(TEXT("Screaming Chicken impact sound cooldown is preserved"), Definition->ImpactSoundCooldownSeconds, 0.12f);
	TestEqual(TEXT("Chicken impact sound keeps a close full-volume radius"), Definition->ImpactSoundInnerRadius, 150.0f);
	TestEqual(TEXT("Chicken impact sound is spatially bounded"), Definition->ImpactSoundFalloffDistance, 1800.0f);
	TestTrue(TEXT("Chicken impact sound uses world occlusion"), Definition->bImpactSoundOcclusion);
	TestEqual(TEXT("Occluded chicken impacts remain audible but reduced"),
		Definition->ImpactSoundOcclusionVolumeAttenuation, 0.35f);
	TestEqual(TEXT("Reconstructed network frequency is preserved"), Definition->NetworkUpdateFrequency, 30.0f);
	TestEqual(TEXT("Network location presentation uses the tuned smoothing speed"),
		Definition->NetworkVisualLocationSmoothingSpeed, 24.0f);
	TestEqual(TEXT("Network rotation presentation uses the tuned smoothing speed"),
		Definition->NetworkVisualRotationSmoothingSpeed, 24.0f);
	TestEqual(TEXT("Network prediction remains bounded to one short packet gap"),
		Definition->NetworkVisualMaximumExtrapolationSeconds, 0.10f);
	TestEqual(TEXT("Large authoritative corrections still snap immediately"),
		Definition->NetworkVisualTeleportDistance, 200.0f);
	TestEqual(TEXT("Reconstructed free linear damping is preserved"), Definition->FreeLinearDamping, 0.01f);
	TestTrue(TEXT("Chaos angular assists are enabled after the faithful locomotion slid excessively"), Definition->bUseExperimentalAngularAssists);
	TestEqual(TEXT("Playable prototype uses the selected assisted torque"), Definition->MovementTorqueDegrees, 900000.0f);
	TestEqual(TEXT("Measured Physics Prop horizontal cap is preserved"), Definition->MaximumHorizontalSpeed, 628.0f);
	TestEqual(TEXT("Playable prototype interpolates its arcade velocity"), Definition->MovementVelocityInterpSpeed, 8.0f);
	TestEqual(TEXT("Playable prototype brakes horizontal drift"), Definition->MovementStopInterpSpeed, 12.0f);
	TestEqual(TEXT("Measured Physics Prop angular velocity cap is preserved"), Definition->MaximumAngularVelocityDegrees, 550.0f);
	TestEqual(TEXT("No speculative movement hop is enabled"), Definition->MovementHopImpulse, 0.0f);
	TestEqual(TEXT("Measured Physics Prop jump velocity is preserved"), Definition->JumpVelocity, 480.0f);
	TestEqual(TEXT("Jump adds a forward burst"), Definition->JumpHorizontalBoostVelocity, 225.0f);
	TestEqual(TEXT("Jump burst respects the measured horizontal cap"), Definition->MaximumJumpHorizontalSpeed, 628.0f);
	TestEqual(TEXT("Playable prototype exposes a double jump"), Definition->MaximumJumpCount, 2);
	TestEqual(TEXT("Playable prototype has a responsive jump cooldown"), Definition->JumpCooldownSeconds, 0.18f);
	TestEqual(TEXT("Playable prototype validates ground proximity"), Definition->GroundProbeDistance, 25.0f);
	TestEqual(TEXT("Reconstructed straighten damping is preserved"), Definition->StraightenAngularDamping, 10000.0f);
	TestEqual(TEXT("Reconstructed straighten target speed is preserved"), Definition->StraightenTargetInterpSpeed, 5.0f);
	TestEqual(TEXT("Reconstructed blocked rotation multiplier is preserved"), Definition->BlockedRotationMultiplier, 0.02f);
	TestTrue(TEXT("Measured high-speed Physics Prop uses CCD"), Definition->bUseContinuousCollisionDetection);

	TestNotNull(TEXT("Screaming Chicken Static Mesh exists"), Definition->StaticMesh.Get());
	if (Definition->StaticMesh != nullptr)
	{
		TestEqual(TEXT("Screaming Chicken imports all four visual LODs"), Definition->StaticMesh->GetNumLODs(), 4);
		const UBodySetup* BodySetup = Definition->StaticMesh->GetBodySetup();
		TestNotNull(TEXT("Screaming Chicken has a simple collision BodySetup"), BodySetup);
		if (BodySetup != nullptr)
		{
			TestEqual(TEXT("Screaming Chicken retains exactly two authored convex hulls"), BodySetup->AggGeom.ConvexElems.Num(), 2);
			TestTrue(TEXT("Screaming Chicken does not use complex-as-simple collision"),
				BodySetup->CollisionTraceFlag != CTF_UseComplexAsSimple);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHDefaultMatchRulesTest,
	"PropHunt.Match.Foundation.DefaultRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHDefaultMatchRulesTest::RunTest(const FString& Parameters)
{
	const UPHMatchRulesDataAsset* Rules = GetDefault<UPHMatchRulesDataAsset>();
	TestNotNull(TEXT("Default match rules exist"), Rules);
	if (Rules == nullptr)
	{
		return false;
	}

	FText ValidationError;
	TestTrue(TEXT("Default match rules are internally valid"), Rules->HasValidRules(&ValidationError));
	if (!ValidationError.IsEmpty())
	{
		AddError(ValidationError.ToString());
	}

	TestEqual(TEXT("Reference active objective count"), Rules->ActiveObjectiveCount, 5);
	TestEqual(TEXT("Reference required objective count"), Rules->RequiredObjectiveCount, 4);
	TestEqual(TEXT("MVP supports at most four Props"), Rules->MaximumPropPlayers, 4);
	TestEqual(TEXT("Lobby remains joinable for one minute after the first Prop arrives"), Rules->LobbyWaitDuration, 60.0f);
	TestEqual(TEXT("A complete roster remains joinable for at least fifteen seconds"), Rules->LobbyReadyCountdownDuration, 15.0f);
	TestEqual(TEXT("An incomplete seamless roster recovers after thirty seconds"), Rules->RosterTravelTimeoutDuration, 30.0f);
	TestEqual(TEXT("Reference Hunt lasts fifteen minutes"), Rules->HuntDuration, 900.0f);
	TestEqual(TEXT("Escape phase allows three minutes to open and reach a gate"), Rules->EscapeDuration, 180.0f);
	TestEqual(TEXT("All remaining Props retained die within five seconds"),
		Rules->AllPropsRetainedEliminationDelay, 5.0f);
	TestEqual(TEXT("Objective score is configurable"), Rules->ObjectiveScore, 1000);
	TestEqual(TEXT("Rescue score is configurable"), Rules->RescueScore, 750);
	TestEqual(TEXT("Hunter down score is configurable"), Rules->HunterDownScore, 500);
	TestEqual(TEXT("Hunter retention score is configurable"), Rules->HunterRetentionScore, 750);
	TestEqual(TEXT("Hunter elimination score is configurable"), Rules->HunterEliminationScore, 1500);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHSeparatedPlayerStartsTest,
	"PropHunt.Match.Foundation.SeparatedPlayerStarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHSeparatedPlayerStartsTest::RunTest(const FString& Parameters)
{
	const TArray<FVector> Starts = {
		FVector(0.0, 0.0, 100.0),
		FVector(0.0, 350.0, 100.0),
		FVector(0.0, 700.0, 100.0)
	};
	TestEqual(TEXT("The first controller gets the deterministic first start"),
		PHMatchFlow::ChooseMostSeparatedStartIndex(Starts, {}), 0);
	TestEqual(TEXT("The second controller gets the farthest start instead of stacking"),
		PHMatchFlow::ChooseMostSeparatedStartIndex(Starts, {Starts[0]}), 2);
	TestEqual(TEXT("The third controller gets the remaining middle start"),
		PHMatchFlow::ChooseMostSeparatedStartIndex(Starts, {Starts[0], Starts[2]}), 1);
	TestEqual(TEXT("No authored start returns no selection"),
		PHMatchFlow::ChooseMostSeparatedStartIndex({}, {Starts[0]}), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHMatchRulesAssetTest,
	"PropHunt.Match.Foundation.ConfiguredRulesAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHMatchRulesAssetTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath RulesAssetPath(TEXT("/Game/PropHunt/Data/DA_PH_MatchRules_Default.DA_PH_MatchRules_Default"));
	const UPHMatchRulesDataAsset* Rules = Cast<UPHMatchRulesDataAsset>(RulesAssetPath.TryLoad());
	TestNotNull(TEXT("Configured match rules Data Asset exists"), Rules);
	if (Rules == nullptr)
	{
		return false;
	}

	FText ValidationError;
	TestTrue(TEXT("Configured match rules Data Asset is valid"), Rules->HasValidRules(&ValidationError));
	if (!ValidationError.IsEmpty())
	{
		AddError(ValidationError.ToString());
	}

	TestEqual(TEXT("Configured minimum Prop count"), Rules->MinimumPropPlayers, 1);
	TestEqual(TEXT("Configured maximum Prop count"), Rules->MaximumPropPlayers, 4);
	TestEqual(TEXT("Configured lobby join window"), Rules->LobbyWaitDuration, 60.0f);
	TestEqual(TEXT("Configured complete-roster countdown"), Rules->LobbyReadyCountdownDuration, 15.0f);
	TestEqual(TEXT("Configured seamless roster timeout"), Rules->RosterTravelTimeoutDuration, 30.0f);
	TestEqual(TEXT("Configured preparation duration"), Rules->PreparationDuration, 10.0f);
	TestEqual(TEXT("Configured hunt duration"), Rules->HuntDuration, 900.0f);
	TestEqual(TEXT("Configured escape duration"), Rules->EscapeDuration, 180.0f);
	TestEqual(TEXT("Configured terminal retention delay"), Rules->AllPropsRetainedEliminationDelay, 5.0f);
	TestEqual(TEXT("Configured active objective count"), Rules->ActiveObjectiveCount, 5);
	TestEqual(TEXT("Configured required objective count"), Rules->RequiredObjectiveCount, 4);
	TestEqual(TEXT("Configured objective score"), Rules->ObjectiveScore, 1000);
	TestEqual(TEXT("Configured rescue score"), Rules->RescueScore, 750);
	TestEqual(TEXT("Configured Hunter down score"), Rules->HunterDownScore, 500);
	TestEqual(TEXT("Configured Hunter retention score"), Rules->HunterRetentionScore, 750);
	TestEqual(TEXT("Configured Hunter elimination score"), Rules->HunterEliminationScore, 1500);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHMatchResultsScoreTest,
	"PropHunt.Match.Results.WeightedScore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHMatchResultsScoreTest::RunTest(const FString& Parameters)
{
	FPHPlayerMatchStats Stats;
	Stats.ObjectivesCompleted = 2;
	Stats.AlliesRescued = 1;
	Stats.HunterDowns = 3;
	Stats.HunterRetentions = 2;
	Stats.HunterEliminations = 1;
	TestEqual(
		TEXT("Every authoritative raw stat contributes its configured weight"),
		PHMatchResults::CalculateTotalScore(Stats, 1000, 750, 500, 750, 1500),
		7250);

	Stats.ObjectivesCompleted = -4;
	Stats.AlliesRescued = -1;
	Stats.HunterDowns = 0;
	Stats.HunterRetentions = 0;
	Stats.HunterEliminations = 0;
	TestEqual(
		TEXT("Invalid negative counters and weights cannot reduce or invert the score"),
		PHMatchResults::CalculateTotalScore(Stats, 1000, 750, -500, -750, -1500),
		0);

	Stats.ObjectivesCompleted = MAX_int32;
	TestEqual(
		TEXT("Weighted score saturates instead of overflowing"),
		PHMatchResults::CalculateTotalScore(Stats, MAX_int32, 0, 0, 0, 0),
		MAX_int32);

	TestEqual(
		TEXT("A connected player keeps the score as an eligible progression gain"),
		PHMatchResults::CalculateGrantedScore(1000, EPHMatchPlayerOutcome::Survived),
		1000);
	TestEqual(
		TEXT("A disconnected player keeps participation visible but receives no progression gain"),
		PHMatchResults::CalculateGrantedScore(1000, EPHMatchPlayerOutcome::Disconnected),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHMatchResultsCacheTest,
	"PropHunt.Match.Results.TravelCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHMatchResultsCacheTest::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UPHSessionSubsystem* Sessions = GameInstance != nullptr
		? NewObject<UPHSessionSubsystem>(GameInstance)
		: nullptr;
	TestNotNull(TEXT("Session subsystem can own the travel cache"), Sessions);
	if (Sessions == nullptr)
	{
		return false;
	}

	FPHMatchResultsSnapshot Snapshot;
	Snapshot.bFinalized = true;
	Snapshot.MatchSeed = 12345;
	Snapshot.EndReason = EPHMatchEndReason::AllPropsGone;
	FPHMatchResultRow& Row = Snapshot.PlayerRows.AddDefaulted_GetRef();
	Row.PlayerId = 42;
	Row.PlayerName = TEXT("LOCAL_TEST_PLAYER");
	Row.PlayerRole = EPHPlayerRole::Hunter;
	Row.Outcome = EPHMatchPlayerOutcome::Hunter;
	Row.TotalScore = 1500;
	Row.GrantedScore = 1500;

	Sessions->CacheMatchResults(Snapshot, EPHPlayerRole::Hunter, Row.PlayerName, Row.PlayerId);
	TestTrue(TEXT("A finalized authoritative snapshot is cached"), Sessions->HasCachedMatchResults());
	TestEqual(TEXT("Cached match seed survives local travel"), Sessions->GetCachedMatchResults().MatchSeed, 12345);
	TestEqual(TEXT("Cached local role survives local travel"), Sessions->GetCachedResultsLocalRole(), EPHPlayerRole::Hunter);
	TestEqual(TEXT("Cached local player id identifies duplicate display names"), Sessions->GetCachedResultsLocalPlayerId(), 42);

	Sessions->ClearCachedMatchResults();
	TestFalse(TEXT("Starting another queue can clear the stale result snapshot"), Sessions->HasCachedMatchResults());
	TestEqual(TEXT("Clearing also resets the local player id"), Sessions->GetCachedResultsLocalPlayerId(), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHPostMatchMapsTest,
	"PropHunt.Match.Results.DedicatedMaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHPostMatchMapsTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath ResultsMapPath(TEXT("/Game/PropHunt/Maps/L_PH_Results.L_PH_Results"));
	const FSoftObjectPath LobbyMapPath(TEXT("/Game/PropHunt/Maps/L_PH_Lobby.L_PH_Lobby"));
	const FSoftObjectPath IntroMapPath(TEXT("/Game/PropHunt/Tests/L_PH_Intro.L_PH_Intro"));
	const FSoftObjectPath WaitingRoomMapPath(TEXT("/Game/PropHunt/Maps/L_PH_WaitingRoom.L_PH_WaitingRoom"));
	TestNotNull(TEXT("Dedicated post-match results map exists"), ResultsMapPath.TryLoad());
	TestNotNull(TEXT("Separate matchmaking lobby map exists"), LobbyMapPath.TryLoad());
	UWorld* IntroWorld = Cast<UWorld>(IntroMapPath.TryLoad());
	TestNotNull(TEXT("Current P9 social lobby map exists"), IntroWorld);
	if (IntroWorld != nullptr && IntroWorld->PersistentLevel != nullptr)
	{
		const AWorldSettings* IntroSettings = IntroWorld->GetWorldSettings();
		TestNotNull(TEXT("P9 lobby has WorldSettings"), IntroSettings);
		if (IntroSettings != nullptr)
		{
			TestTrue(TEXT("P9 lobby uses the no-Pawn frontend GameMode"),
				IntroSettings->DefaultGameMode != nullptr
					&& IntroSettings->DefaultGameMode->GetName().Contains(TEXT("PHFrontendGameMode")));
		}
		int32 CameraCount = 0;
		int32 SurvivorPreviewMarkers = 0;
		int32 HunterPreviewMarkers = 0;
		int32 StageActors = 0;
		for (const AActor* Actor : IntroWorld->PersistentLevel->Actors)
		{
			if (!IsValid(Actor))
			{
				continue;
			}
			CameraCount += Cast<ACameraActor>(Actor) != nullptr ? 1 : 0;
			SurvivorPreviewMarkers += Actor->ActorHasTag(TEXT("PHLobbySurvivorPreview")) ? 1 : 0;
			HunterPreviewMarkers += Actor->ActorHasTag(TEXT("PHLobbyHunterPreview")) ? 1 : 0;
			StageActors += Actor->ActorHasTag(TEXT("PHLobbyStage")) ? 1 : 0;
		}
		TestTrue(TEXT("P9 lobby has a fixed authored camera"), CameraCount > 0);
		TestEqual(TEXT("P9 lobby exposes four authored Survivor preview slots"), SurvivorPreviewMarkers, 4);
		TestEqual(TEXT("P9 lobby exposes one private Hunter preview slot"), HunterPreviewMarkers, 1);
		TestTrue(TEXT("P9 lobby contains visible authored stage geometry"), StageActors >= 7);
	}
	UWorld* WaitingRoomWorld = Cast<UWorld>(WaitingRoomMapPath.TryLoad());
	TestNotNull(TEXT("Separate dedicated waiting room map exists"), WaitingRoomWorld);
	if (WaitingRoomWorld != nullptr)
	{
		const AWorldSettings* WaitingRoomSettings = WaitingRoomWorld->GetWorldSettings();
		TestNotNull(TEXT("Waiting room has WorldSettings"), WaitingRoomSettings);
		if (WaitingRoomSettings != nullptr)
		{
			TestTrue(TEXT("Waiting room uses the match GameMode"),
				WaitingRoomSettings->DefaultGameMode != nullptr
					&& WaitingRoomSettings->DefaultGameMode->GetName().Contains(TEXT("BP_PH_GameMode")));
		}
		int32 WaitingRoomCameraCount = 0;
		if (WaitingRoomWorld->PersistentLevel != nullptr)
		{
			for (const AActor* Actor : WaitingRoomWorld->PersistentLevel->Actors)
			{
				WaitingRoomCameraCount += Cast<ACameraActor>(Actor) != nullptr ? 1 : 0;
			}
		}
		TestTrue(TEXT("Waiting room provides a fixed camera for no-Pawn clients"), WaitingRoomCameraCount > 0);
	}

	FString ConfiguredResultsMap;
	FString ConfiguredLobbyMap;
	FString ConfiguredServerDefaultMap;
	FString ConfiguredMatchMap;
	GConfig->GetString(
		TEXT("/Script/PropHunt.PHSessionSubsystem"),
		TEXT("ResultsMap"),
		ConfiguredResultsMap,
		GGameIni);
	GConfig->GetString(
		TEXT("/Script/PropHunt.PHSessionSubsystem"),
		TEXT("ReturnMap"),
		ConfiguredLobbyMap,
		GGameIni);
	GConfig->GetString(
		TEXT("/Script/EngineSettings.GameMapsSettings"),
		TEXT("ServerDefaultMap"),
		ConfiguredServerDefaultMap,
		GEngineIni);
	GConfig->GetString(
		TEXT("/Script/PropHunt.PHSessionSubsystem"),
		TEXT("MatchMap"),
		ConfiguredMatchMap,
		GGameIni);
	TestEqual(TEXT("Normal match completion targets the results map"),
		ConfiguredResultsMap, FString(TEXT("/Game/PropHunt/Maps/L_PH_Results")));
	TestEqual(TEXT("Continue from results restores the fresh matchmaking menu"),
		ConfiguredLobbyMap, FString(TEXT("/Game/PropHunt/Tests/L_PH_Intro")));
	TestNotEqual(TEXT("Results and matchmaking never share the same map"),
		ConfiguredResultsMap, ConfiguredLobbyMap);
	TestEqual(TEXT("A dedicated server starts in the separate waiting room scene"),
		ConfiguredServerDefaultMap, FString(TEXT("/Game/PropHunt/Maps/L_PH_WaitingRoom")));
	TestFalse(TEXT("ServerDefaultMap is a cookable package path without URL options"),
		ConfiguredServerDefaultMap.Contains(TEXT("?")));
	TestNotEqual(TEXT("The dedicated waiting room is not the gameplay map"),
		ConfiguredServerDefaultMap, ConfiguredMatchMap);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPHMatchPhaseFlowTest,
	"PropHunt.Match.Foundation.TimedPhaseFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPHMatchPhaseFlowTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Preparation advances to Hunt"), PHMatchFlow::GetNextTimedPhase(EPHMatchPhase::Preparation), EPHMatchPhase::Hunt);
	TestEqual(TEXT("Hunt timeout advances to Results"), PHMatchFlow::GetNextTimedPhase(EPHMatchPhase::Hunt), EPHMatchPhase::Results);
	TestEqual(TEXT("Results return to Lobby"), PHMatchFlow::GetNextTimedPhase(EPHMatchPhase::Results), EPHMatchPhase::Lobby);
	TestFalse(TEXT("Three objectives do not open a four-objective exit"), PHMatchFlow::ShouldOpenEscape(3, 4));
	TestTrue(TEXT("Four objectives open a four-objective exit"), PHMatchFlow::ShouldOpenEscape(4, 4));
	TestEqual(TEXT("Departing last Prop leaves no Props"),
		PHMatchFlow::GetRemainingRoleCountAfterDeparture(1, EPHPlayerRole::Prop, EPHPlayerRole::Prop), 0);
	TestEqual(TEXT("Departing Hunter does not change Prop count"),
		PHMatchFlow::GetRemainingRoleCountAfterDeparture(2, EPHPlayerRole::Hunter, EPHPlayerRole::Prop), 2);
	return true;
}

#endif
