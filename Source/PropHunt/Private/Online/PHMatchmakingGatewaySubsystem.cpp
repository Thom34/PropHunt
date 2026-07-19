#include "Online/PHMatchmakingGatewaySubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Online/PHMatchmakingGatewayContract.h"
#include "OnlineSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHMatchmakingGateway, Log, All);

namespace
{
const FName SteamSubsystemName(TEXT("STEAM"));
const FString SteamGatewayTokenType(TEXT("WebAPI:PropHuntGateway"));

FString ResolveGatewayBaseUrl(const FString& ConfiguredBaseUrl)
{
#if UE_BUILD_SHIPPING
	return TEXT("https://gateway.meteothom.com");
#else
	return ConfiguredBaseUrl;
#endif
}

bool IsSafeAccessToken(const FString& Token)
{
	if (Token.IsEmpty() || Token.Len() > 8192)
	{
		return false;
	}
	for (const TCHAR Character : Token)
	{
		if (FChar::IsWhitespace(Character) || Character == TEXT('\r') || Character == TEXT('\n'))
		{
			return false;
		}
	}
	return true;
}

FString NormalizeTicketPath(const FString& RawPath)
{
	FString Path = RawPath.TrimStartAndEnd();
	if (Path.IsEmpty())
	{
		return TEXT("/v1/prophunt/tickets");
	}
	if (!Path.StartsWith(TEXT("/")))
	{
		Path.InsertAt(0, TEXT('/'));
	}
	while (Path.EndsWith(TEXT("/")))
	{
		Path.LeftChopInline(1);
	}
	return Path;
}
}

void UPHMatchmakingGatewaySubsystem::Deinitialize()
{
	RequestGeneration = RequestGeneration == MAX_uint64 ? 1 : RequestGeneration + 1;
	ClearHttpAndTickers();
	ClearSensitiveState();
	Super::Deinitialize();
}

bool UPHMatchmakingGatewaySubsystem::RequestTicket(
	const EPHMatchmakingPreference Preference,
	const int32 LocalUserNum)
{
	return BeginTicketAuthentication(Preference, LocalUserNum, FString());
}

bool UPHMatchmakingGatewaySubsystem::RequestTicketFromLobbyInvite(
	const FString& InviteSecret,
	const int32 LocalUserNum)
{
	if (!PHMatchmakingGatewayContract::IsSafeLobbyInviteSecret(InviteSecret))
	{
		FinishFailure(TEXT("L'invitation de lobby Steam est invalide."));
		return false;
	}
	return BeginTicketAuthentication(EPHMatchmakingPreference::Random, LocalUserNum, InviteSecret);
}

bool UPHMatchmakingGatewaySubsystem::BeginTicketAuthentication(
	const EPHMatchmakingPreference Preference,
	const int32 LocalUserNum,
	const FString& LobbyInviteSecret)
{
	if (IsRunningDedicatedServer())
	{
		FinishFailure(TEXT("Un serveur dédié ne peut pas créer un ticket joueur."));
		return false;
	}
	if (IsBusy())
	{
		OnFinished.Broadcast(false, TEXT("Un ticket matchmaking est déjà actif."));
		return false;
	}
	if (PHMatchmakingGatewayContract::RolePreferenceToString(Preference) == nullptr)
	{
		FinishFailure(TEXT("La préférence de rôle est invalide."));
		return false;
	}
	if (LocalUserNum < 0 || LocalUserNum >= MAX_LOCAL_PLAYERS)
	{
		FinishFailure(TEXT("L'utilisateur local Steam est invalide."));
		return false;
	}

	FString NormalizedGatewayUrl;
	FString UrlError;
	if (!PHMatchmakingGatewayContract::NormalizeGatewayBaseUrl(
		ResolveGatewayBaseUrl(GatewayBaseUrl),
		UE_BUILD_SHIPPING != 0,
		NormalizedGatewayUrl,
		UrlError))
	{
		FinishFailure(UrlError);
		return false;
	}

	IOnlineSubsystem* SteamSubsystem = IOnlineSubsystem::Get(SteamSubsystemName);
	const IOnlineIdentityPtr Identity = SteamSubsystem ? SteamSubsystem->GetIdentityInterface() : nullptr;
	if (!Identity.IsValid() || Identity->GetLoginStatus(LocalUserNum) != ELoginStatus::LoggedIn)
	{
		FinishFailure(TEXT("Steam doit être connecté pour rejoindre le matchmaking NOVA."));
		return false;
	}

	ClearHttpAndTickers();
	ClearSensitiveState();
	ClearLobbySnapshot();
	State = EPHGatewayMatchmakingState::Idle;
	RequestGeneration = RequestGeneration == MAX_uint64 ? 1 : RequestGeneration + 1;
	const uint64 CallbackGeneration = RequestGeneration;
	PendingPreference = Preference;
	PendingLobbyInviteSecret = LobbyInviteSecret;
	PendingGatewayBaseUrl = NormalizedGatewayUrl;
	PendingTicketPath = NormalizeTicketPath(TicketPath);
	bSteamAuthTokenRequestInProgress = true;
	SetState(EPHGatewayMatchmakingState::Authenticating, TEXT("Authentification Steam auprès de la gateway..."));
	SteamAuthTimeoutTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this,
			&UPHMatchmakingGatewaySubsystem::HandleSteamAuthTimeout,
			CallbackGeneration),
		FMath::Clamp(RequestTimeoutSeconds, 5.0f, 60.0f));
	Identity->GetLinkedAccountAuthToken(
		LocalUserNum,
		SteamGatewayTokenType,
		IOnlineIdentity::FOnGetLinkedAccountAuthTokenCompleteDelegate::CreateUObject(
			this,
			&UPHMatchmakingGatewaySubsystem::HandleSteamAuthToken,
			CallbackGeneration,
			LocalUserNum,
			Preference));
	return true;
}

void UPHMatchmakingGatewaySubsystem::HandleSteamAuthToken(
	const int32 CallbackLocalUserNum,
	const bool bWasSuccessful,
	const FExternalAuthToken& AuthToken,
	const uint64 CallbackGeneration,
	const int32 ExpectedLocalUserNum,
	const EPHMatchmakingPreference ExpectedPreference)
{
	if (CallbackGeneration != RequestGeneration || !bSteamAuthTokenRequestInProgress)
	{
		return;
	}
	if (SteamAuthTimeoutTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SteamAuthTimeoutTickerHandle);
		SteamAuthTimeoutTickerHandle.Reset();
	}
	bSteamAuthTokenRequestInProgress = false;
	if (CallbackLocalUserNum != ExpectedLocalUserNum || !bWasSuccessful
		|| !AuthToken.HasTokenString() || !IsSafeAccessToken(AuthToken.TokenString))
	{
		FinishFailure(TEXT("Steam n'a pas fourni de ticket d'authentification valide."));
		return;
	}
	StartTicketRequest(ExpectedPreference, AuthToken.TokenString);
}

bool UPHMatchmakingGatewaySubsystem::HandleSteamAuthTimeout(
	const float DeltaSeconds,
	const uint64 CallbackGeneration)
{
	(void)DeltaSeconds;
	SteamAuthTimeoutTickerHandle.Reset();
	if (CallbackGeneration == RequestGeneration && bSteamAuthTokenRequestInProgress)
	{
		FinishFailure(TEXT("Steam n'a pas fourni de ticket d'authentification dans le délai autorisé."));
	}
	return false;
}

bool UPHMatchmakingGatewaySubsystem::StartTicketRequest(
	const EPHMatchmakingPreference Preference,
	const FString& PlayerAccessToken)
{
	const TCHAR* RolePreference = PHMatchmakingGatewayContract::RolePreferenceToString(Preference);
	if (RolePreference == nullptr || !IsSafeAccessToken(PlayerAccessToken))
	{
		FinishFailure(TEXT("Le ticket ou la préférence matchmaking est invalide."));
		return false;
	}

	TicketId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	PendingPlayerAccessToken = PlayerAccessToken;
	PendingDeadlineUtc = FDateTime::UtcNow()
		+ FTimespan::FromSeconds(FMath::Clamp(MatchmakingTimeoutSeconds, 30.0f, 1800.0f));
	const int32 ProtocolVersion = ResolveProtocolVersion();

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("schema"), PHMatchmakingGatewayContract::SchemaVersion);
	Body->SetStringField(TEXT("ticket_id"), TicketId);
	Body->SetNumberField(TEXT("protocol_version"), ProtocolVersion);
	Body->SetStringField(TEXT("build_id"), ResolveBuildId());
	if (PendingLobbyInviteSecret.IsEmpty())
	{
		Body->SetStringField(TEXT("role_preference"), RolePreference);
	}
	else
	{
		Body->SetStringField(TEXT("invite_secret"), PendingLobbyInviteSecret);
	}

	FString BodyJson;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyJson);
	if (!FJsonSerializer::Serialize(Body, Writer))
	{
		FinishFailure(TEXT("La requête matchmaking n'a pas pu être sérialisée."));
		return false;
	}

	ActiveRequest = FHttpModule::Get().CreateRequest();
	ActiveRequest->SetURL(PendingGatewayBaseUrl + PendingTicketPath
		+ (PendingLobbyInviteSecret.IsEmpty() ? FString() : TEXT("/join-invite")));
	ActiveRequest->SetVerb(TEXT("POST"));
	ActiveRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	ActiveRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ActiveRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *PendingPlayerAccessToken));
	ActiveRequest->SetHeader(TEXT("Idempotency-Key"), TicketId);
	ActiveRequest->SetContentAsString(BodyJson);
	ActiveRequest->SetTimeout(FMath::Clamp(RequestTimeoutSeconds, 5.0f, 60.0f));
	ActiveRequest->OnProcessRequestComplete().BindUObject(
		this,
		&UPHMatchmakingGatewaySubsystem::HandleTicketResponse,
		RequestGeneration,
		TicketId,
		false,
		false);
	SetState(EPHGatewayMatchmakingState::Queued, TEXT("Ticket créé. Recherche d'une WaitingRoom dédiée..."));
	if (!ActiveRequest->ProcessRequest())
	{
		ActiveRequest.Reset();
		FinishFailure(TEXT("La requête matchmaking n'a pas pu démarrer."));
		return false;
	}
	UE_LOG(LogPHMatchmakingGateway, Display,
		TEXT("NOVA matchmaking ticket requested ticket_id=%s preference=%s invited=%d protocol=%d"),
		*TicketId,
		RolePreference,
		PendingLobbyInviteSecret.IsEmpty() ? 0 : 1,
		ProtocolVersion);
	PendingLobbyInviteSecret.Reset();
	return true;
}

bool UPHMatchmakingGatewaySubsystem::RequestLobbyInviteToken(
	FPHLobbyInviteTokenCallback Callback)
{
	if (!Callback.IsBound() || !bHasLobbySnapshot || bLobbyLocked || TicketId.IsEmpty()
		|| PendingPlayerAccessToken.IsEmpty() || PendingGatewayBaseUrl.IsEmpty()
		|| PendingTicketPath.IsEmpty() || LobbyInviteRequest.IsValid())
	{
		Callback.ExecuteIfBound(false, FString(),
			TEXT("Entre d'abord dans un salon encore ouvert avant d'inviter un ami."));
		return false;
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("schema"), PHMatchmakingGatewayContract::SchemaVersion);
	Body->SetStringField(TEXT("build_id"), ResolveBuildId());
	FString BodyJson;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyJson);
	if (!FJsonSerializer::Serialize(Body, Writer))
	{
		Callback.Execute(false, FString(), TEXT("La requête d'invitation n'a pas pu être sérialisée."));
		return false;
	}

	LobbyInviteTokenCallback = MoveTemp(Callback);
	LobbyInviteRequest = FHttpModule::Get().CreateRequest();
	LobbyInviteRequest->SetURL(PendingGatewayBaseUrl + PendingTicketPath
		+ TEXT("/") + TicketId + TEXT("/invite"));
	LobbyInviteRequest->SetVerb(TEXT("POST"));
	LobbyInviteRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	LobbyInviteRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	LobbyInviteRequest->SetHeader(
		TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *PendingPlayerAccessToken));
	LobbyInviteRequest->SetContentAsString(BodyJson);
	LobbyInviteRequest->SetTimeout(FMath::Clamp(RequestTimeoutSeconds, 5.0f, 30.0f));
	LobbyInviteRequest->OnProcessRequestComplete().BindUObject(
		this, &UPHMatchmakingGatewaySubsystem::HandleLobbyInviteTokenResponse);
	if (!LobbyInviteRequest->ProcessRequest())
	{
		LobbyInviteRequest.Reset();
		FPHLobbyInviteTokenCallback FailedCallback = MoveTemp(LobbyInviteTokenCallback);
		FailedCallback.ExecuteIfBound(false, FString(), TEXT("La demande d'invitation n'a pas pu démarrer."));
		return false;
	}
	return true;
}

void UPHMatchmakingGatewaySubsystem::HandleLobbyInviteTokenResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	const bool bConnectedSuccessfully)
{
	if (Request != LobbyInviteRequest)
	{
		return;
	}
	LobbyInviteRequest.Reset();
	FPHLobbyInviteTokenCallback Callback = MoveTemp(LobbyInviteTokenCallback);
	if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 201)
	{
		Callback.ExecuteIfBound(false, FString(), TEXT("La gateway a refusé l'invitation de lobby."));
		return;
	}
	FString InviteSecret;
	FDateTime ExpiresUtc;
	FString Error;
	if (!PHMatchmakingGatewayContract::ParseLobbyInviteResponse(
		Response->GetContentAsString(), ResolveProtocolVersion(), ResolveBuildId(), FDateTime::UtcNow(),
		InviteSecret, ExpiresUtc, Error))
	{
		Callback.ExecuteIfBound(false, FString(), Error);
		return;
	}
	Callback.ExecuteIfBound(true, InviteSecret, TEXT("Invitation de lobby prête."));
}

void UPHMatchmakingGatewaySubsystem::HandleTicketResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	const bool bConnectedSuccessfully,
	const uint64 CallbackGeneration,
	const FString ExpectedTicketId,
	const bool bWasPollRequest,
	const bool bWasCancelRequest)
{
	if (CallbackGeneration != RequestGeneration || Request != ActiveRequest)
	{
		return;
	}
	ActiveRequest.Reset();
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		FinishFailure(TEXT("La gateway matchmaking PropHunt ne répond pas."));
		return;
	}
	const int32 ExpectedStatus = bWasPollRequest || bWasCancelRequest ? 200 : 202;
	if (Response->GetResponseCode() != ExpectedStatus)
	{
		FinishFailure(FString::Printf(
			TEXT("La gateway matchmaking a refusé la requête (HTTP %d)."),
			Response->GetResponseCode()));
		return;
	}
	if (bWasCancelRequest)
	{
		ClearSensitiveState();
		ClearLobbySnapshot();
		TicketId.Reset();
		SetState(EPHGatewayMatchmakingState::Idle, TEXT("Ticket matchmaking annulé."));
		OnFinished.Broadcast(true, TEXT("Ticket matchmaking annulé."));
		return;
	}

	FPHGatewayTicket Ticket;
	FString Error;
	if (!PHMatchmakingGatewayContract::ParseTicketStatus(
		Response->GetContentAsString(),
		ExpectedTicketId,
		ResolveProtocolVersion(),
		ResolveBuildId(),
		FDateTime::UtcNow(),
		Ticket,
		Error))
	{
		FinishFailure(Error);
		return;
	}
	if (Ticket.Status == EPHGatewayTicketStatus::Pending)
	{
		UpdateLobbySnapshot(Ticket);
		if (!Ticket.bHasLobby)
		{
			SetState(EPHGatewayMatchmakingState::Allocating,
				TEXT("En attente du roster et du worker dédié NOVA..."));
		}
		else if (Ticket.LobbyPhase == TEXT("waiting"))
		{
			SetState(EPHGatewayMatchmakingState::Queued,
				Ticket.AssignedRole == EPHPlayerRole::Hunter
					? TEXT("Salon créé. En attente d'au moins un Survivant...")
					: TEXT("Salon créé. En attente du Tueur, qui reste invisible aux Survivants..."));
		}
		else if (Ticket.LobbyPhase == TEXT("countdown"))
		{
			SetState(EPHGatewayMatchmakingState::Queued, FString::Printf(
				TEXT("Roster minimum atteint. Départ dans %d s — le salon reste ouvert."),
				GetLobbyCountdownSeconds()));
		}
		else if (Ticket.LobbyPhase == TEXT("locked"))
		{
			SetState(EPHGatewayMatchmakingState::Allocating, FString::Printf(
				TEXT("Salon verrouillé. Worker NOVA en préparation — départ dans %d s."),
				GetLobbyCountdownSeconds()));
		}
		else
		{
			SetState(EPHGatewayMatchmakingState::Allocating,
				TEXT("Compte à rebours terminé. Le serveur de jeu finit de démarrer..."));
		}
		SchedulePoll(Ticket.RetryAfterSeconds);
		return;
	}
	if (Ticket.Status == EPHGatewayTicketStatus::Failed
		|| Ticket.Status == EPHGatewayTicketStatus::Cancelled)
	{
		FinishFailure(Ticket.FailureMessage);
		return;
	}

	ClearSensitiveState();
	SetState(EPHGatewayMatchmakingState::Connecting,
		Ticket.AssignedRole == EPHPlayerRole::Hunter
			? TEXT("Réservation Tueur prête. Connexion à la WaitingRoom dédiée...")
			: TEXT("Réservation Survivant prête. Connexion à la WaitingRoom dédiée..."));
	TravelToReadyTicket(Ticket);
}

void UPHMatchmakingGatewaySubsystem::StartPollRequest()
{
	if (PendingPlayerAccessToken.IsEmpty() || TicketId.IsEmpty() || ActiveRequest.IsValid())
	{
		FinishFailure(TEXT("L'état local du ticket matchmaking est invalide."));
		return;
	}
	if (FDateTime::UtcNow() >= PendingDeadlineUtc)
	{
		FinishFailure(TEXT("Le matchmaking NOVA n'a pas abouti dans le délai autorisé."));
		return;
	}
	ActiveRequest = FHttpModule::Get().CreateRequest();
	ActiveRequest->SetURL(PendingGatewayBaseUrl + PendingTicketPath + TEXT("/") + TicketId);
	ActiveRequest->SetVerb(TEXT("GET"));
	ActiveRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	ActiveRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *PendingPlayerAccessToken));
	ActiveRequest->SetTimeout(FMath::Clamp(RequestTimeoutSeconds, 5.0f, 60.0f));
	ActiveRequest->OnProcessRequestComplete().BindUObject(
		this,
		&UPHMatchmakingGatewaySubsystem::HandleTicketResponse,
		RequestGeneration,
		TicketId,
		true,
		false);
	if (!ActiveRequest->ProcessRequest())
	{
		ActiveRequest.Reset();
		FinishFailure(TEXT("Le polling matchmaking n'a pas pu démarrer."));
	}
}

void UPHMatchmakingGatewaySubsystem::SchedulePoll(const float DelaySeconds)
{
	if (PollTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PollTickerHandle);
		PollTickerHandle.Reset();
	}
	PollTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this,
			&UPHMatchmakingGatewaySubsystem::HandlePollTicker,
			RequestGeneration),
		FMath::Clamp(DelaySeconds, 0.25f, 5.0f));
}

bool UPHMatchmakingGatewaySubsystem::HandlePollTicker(
	const float DeltaSeconds,
	const uint64 CallbackGeneration)
{
	(void)DeltaSeconds;
	PollTickerHandle.Reset();
	if (CallbackGeneration == RequestGeneration)
	{
		StartPollRequest();
	}
	return false;
}

void UPHMatchmakingGatewaySubsystem::CancelTicket()
{
	if (!IsBusy() && State != EPHGatewayMatchmakingState::Failed)
	{
		return;
	}
	RequestGeneration = RequestGeneration == MAX_uint64 ? 1 : RequestGeneration + 1;
	ClearHttpAndTickers();
	if (TicketId.IsEmpty() || PendingPlayerAccessToken.IsEmpty())
	{
		ClearSensitiveState();
		ClearLobbySnapshot();
		TicketId.Reset();
		SetState(EPHGatewayMatchmakingState::Idle, TEXT("Recherche annulée."));
		OnFinished.Broadcast(true, TEXT("Recherche annulée."));
		return;
	}
	StartCancelRequest();
}

void UPHMatchmakingGatewaySubsystem::CompleteMatchAndReset()
{
	const FString CompletedTicketId = TicketId;
	FString NormalizedGatewayUrl;
	FString UrlError;
	const bool bCanNotifyGateway = !CompletedTicketId.IsEmpty()
		&& PHMatchmakingGatewayContract::NormalizeGatewayBaseUrl(
			ResolveGatewayBaseUrl(GatewayBaseUrl),
			UE_BUILD_SHIPPING != 0,
			NormalizedGatewayUrl,
			UrlError);

	RequestGeneration = RequestGeneration == MAX_uint64 ? 1 : RequestGeneration + 1;
	ClearHttpAndTickers();
	ClearSensitiveState();
	ClearLobbySnapshot();
	TicketId.Reset();
	PendingPreference = EPHMatchmakingPreference::Random;
	SetState(EPHGatewayMatchmakingState::Idle, TEXT("Prêt pour une nouvelle partie."));

	if (!bCanNotifyGateway)
	{
		return;
	}

	IOnlineSubsystem* SteamSubsystem = IOnlineSubsystem::Get(SteamSubsystemName);
	const IOnlineIdentityPtr Identity = SteamSubsystem ? SteamSubsystem->GetIdentityInterface() : nullptr;
	if (!Identity.IsValid() || Identity->GetLoginStatus(0) != ELoginStatus::LoggedIn)
	{
		UE_LOG(LogPHMatchmakingGateway, Warning,
			TEXT("Ready reservation could not be completed because Steam is unavailable; local state was reset."));
		return;
	}

	const FString CompletionUrl = NormalizedGatewayUrl + NormalizeTicketPath(TicketPath)
		+ TEXT("/") + CompletedTicketId + TEXT("/complete");
	Identity->GetLinkedAccountAuthToken(
		0,
		SteamGatewayTokenType,
		IOnlineIdentity::FOnGetLinkedAccountAuthTokenCompleteDelegate::CreateUObject(
			this,
			&UPHMatchmakingGatewaySubsystem::HandleCompletionAuthToken,
			CompletionUrl));
}

void UPHMatchmakingGatewaySubsystem::HandleCompletionAuthToken(
	const int32 CallbackLocalUserNum,
	const bool bWasSuccessful,
	const FExternalAuthToken& AuthToken,
	FString CompletionUrl)
{
	if (CallbackLocalUserNum != 0 || !bWasSuccessful
		|| !AuthToken.HasTokenString() || !IsSafeAccessToken(AuthToken.TokenString))
	{
		UE_LOG(LogPHMatchmakingGateway, Warning,
			TEXT("Ready reservation completion authentication failed; the local frontend remains reset."));
		return;
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("schema"), PHMatchmakingGatewayContract::SchemaVersion);
	FString BodyJson;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyJson);
	if (!FJsonSerializer::Serialize(Body, Writer))
	{
		return;
	}

	FHttpRequestPtr CompletionRequest = FHttpModule::Get().CreateRequest();
	CompletionRequest->SetURL(CompletionUrl);
	CompletionRequest->SetVerb(TEXT("POST"));
	CompletionRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	CompletionRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	CompletionRequest->SetHeader(
		TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AuthToken.TokenString));
	CompletionRequest->SetContentAsString(BodyJson);
	CompletionRequest->SetTimeout(FMath::Clamp(RequestTimeoutSeconds, 5.0f, 15.0f));
	CompletionRequest->ProcessRequest();
}

void UPHMatchmakingGatewaySubsystem::StartCancelRequest()
{
	SetState(EPHGatewayMatchmakingState::Cancelling, TEXT("Annulation du ticket matchmaking..."));
	ActiveRequest = FHttpModule::Get().CreateRequest();
	ActiveRequest->SetURL(PendingGatewayBaseUrl + PendingTicketPath + TEXT("/") + TicketId);
	ActiveRequest->SetVerb(TEXT("DELETE"));
	ActiveRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	ActiveRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *PendingPlayerAccessToken));
	ActiveRequest->SetTimeout(FMath::Clamp(RequestTimeoutSeconds, 5.0f, 60.0f));
	ActiveRequest->OnProcessRequestComplete().BindUObject(
		this,
		&UPHMatchmakingGatewaySubsystem::HandleTicketResponse,
		RequestGeneration,
		TicketId,
		false,
		true);
	if (!ActiveRequest->ProcessRequest())
	{
		ActiveRequest.Reset();
		FinishFailure(TEXT("L'annulation du ticket matchmaking n'a pas pu démarrer."));
	}
}

void UPHMatchmakingGatewaySubsystem::TravelToReadyTicket(const FPHGatewayTicket& ReadyTicket)
{
	APlayerController* PlayerController = GetGameInstance() != nullptr
		? GetGameInstance()->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	if (PlayerController == nullptr || !PlayerController->IsLocalController())
	{
		FinishFailure(TEXT("Aucun contrôleur local n'est disponible pour rejoindre la WaitingRoom."));
		return;
	}
	UE_LOG(LogPHMatchmakingGateway, Display,
		TEXT("Travelling to NOVA reservation ticket_id=%s match_id=%s role=%s"),
		*ReadyTicket.TicketId,
		*ReadyTicket.MatchId,
		ReadyTicket.AssignedRole == EPHPlayerRole::Hunter ? TEXT("hunter") : TEXT("prop"));
	PlayerController->ClientTravel(ReadyTicket.ConnectAddress, TRAVEL_Absolute);
	OnFinished.Broadcast(true, TEXT("Réservation NOVA prête. Connexion au serveur dédié."));
}

void UPHMatchmakingGatewaySubsystem::SetState(
	const EPHGatewayMatchmakingState NewState,
	const FString& Message)
{
	State = NewState;
	OnStateChanged.Broadcast(State, Message);
}

void UPHMatchmakingGatewaySubsystem::FinishFailure(const FString& Message)
{
	ClearHttpAndTickers();
	BestEffortCancelActiveTicket();
	ClearSensitiveState();
	ClearLobbySnapshot();
	SetState(EPHGatewayMatchmakingState::Failed, Message);
	UE_LOG(LogPHMatchmakingGateway, Warning, TEXT("NOVA matchmaking failed: %s"), *Message);
	OnFinished.Broadcast(false, Message);
}

void UPHMatchmakingGatewaySubsystem::BestEffortCancelActiveTicket()
{
	if (TicketId.IsEmpty() || PendingPlayerAccessToken.IsEmpty()
		|| PendingGatewayBaseUrl.IsEmpty() || PendingTicketPath.IsEmpty())
	{
		return;
	}
	FHttpRequestPtr CleanupRequest = FHttpModule::Get().CreateRequest();
	CleanupRequest->SetURL(PendingGatewayBaseUrl + PendingTicketPath + TEXT("/") + TicketId);
	CleanupRequest->SetVerb(TEXT("DELETE"));
	CleanupRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	CleanupRequest->SetHeader(
		TEXT("Authorization"),
		FString::Printf(TEXT("Bearer %s"), *PendingPlayerAccessToken));
	CleanupRequest->SetTimeout(FMath::Clamp(RequestTimeoutSeconds, 5.0f, 15.0f));
	CleanupRequest->ProcessRequest();
}

void UPHMatchmakingGatewaySubsystem::ClearHttpAndTickers()
{
	if (SteamAuthTimeoutTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SteamAuthTimeoutTickerHandle);
		SteamAuthTimeoutTickerHandle.Reset();
	}
	if (PollTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PollTickerHandle);
		PollTickerHandle.Reset();
	}
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->OnProcessRequestComplete().Unbind();
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	if (LobbyInviteRequest.IsValid())
	{
		LobbyInviteRequest->OnProcessRequestComplete().Unbind();
		LobbyInviteRequest->CancelRequest();
		LobbyInviteRequest.Reset();
	}
	LobbyInviteTokenCallback.Unbind();
}

void UPHMatchmakingGatewaySubsystem::ClearSensitiveState()
{
	bSteamAuthTokenRequestInProgress = false;
	PendingGatewayBaseUrl.Reset();
	PendingTicketPath.Reset();
	PendingPlayerAccessToken.Reset();
	PendingLobbyInviteSecret.Reset();
	PendingDeadlineUtc = FDateTime();
}

void UPHMatchmakingGatewaySubsystem::UpdateLobbySnapshot(const FPHGatewayTicket& Ticket)
{
	bHasLobbySnapshot = Ticket.bHasLobby;
	LobbyAssignedRole = Ticket.AssignedRole;
	LobbyPhase = Ticket.LobbyPhase;
	LobbyId = Ticket.LobbyId;
	LobbySurvivorsPresent = Ticket.SurvivorsPresent;
	LobbySurvivorSlots = Ticket.SurvivorSlots;
	AssignedSurvivorSlot = Ticket.AssignedSurvivorSlot;
	OccupiedSurvivorSlots = Ticket.OccupiedSurvivorSlots;
	LobbyCountdownEndsUtc = Ticket.LobbyCountdownEndsUtc;
	bLobbyLocked = Ticket.bLobbyLocked;
}

void UPHMatchmakingGatewaySubsystem::ClearLobbySnapshot()
{
	bHasLobbySnapshot = false;
	LobbyAssignedRole = EPHPlayerRole::Unassigned;
	LobbyPhase.Reset();
	LobbyId.Reset();
	LobbySurvivorsPresent = 0;
	LobbySurvivorSlots = 4;
	AssignedSurvivorSlot = 0;
	OccupiedSurvivorSlots.Reset();
	LobbyCountdownEndsUtc = FDateTime();
	bLobbyLocked = false;
}

int32 UPHMatchmakingGatewaySubsystem::GetLobbyCountdownSeconds() const
{
	if (!bHasLobbySnapshot || LobbyCountdownEndsUtc.GetTicks() <= 0)
	{
		return 0;
	}
	return FMath::Max(0, FMath::CeilToInt(
		(LobbyCountdownEndsUtc - FDateTime::UtcNow()).GetTotalSeconds()));
}

int32 UPHMatchmakingGatewaySubsystem::ResolveProtocolVersion() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UPHSessionSubsystem* Sessions = GameInstance != nullptr
		? GameInstance->GetSubsystem<UPHSessionSubsystem>() : nullptr;
	return Sessions != nullptr ? Sessions->GetProtocolVersion() : 0;
}

FString UPHMatchmakingGatewaySubsystem::ResolveBuildId() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UPHSessionSubsystem* Sessions = GameInstance != nullptr
		? GameInstance->GetSubsystem<UPHSessionSubsystem>()
		: nullptr;
	return Sessions != nullptr ? Sessions->GetReleaseVersion() : FString();
}
