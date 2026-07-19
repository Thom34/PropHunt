#include "Online/PHMatchmakingGatewayContract.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool IsSafeIdentifier(const FString& Value, const int32 MaxLength = 96)
{
	if (Value.IsEmpty() || Value.Len() > MaxLength)
	{
		return false;
	}
	for (int32 Index = 0; Index < Value.Len(); ++Index)
	{
		const TCHAR Character = Value[Index];
		const bool bAlphaNumeric = (Character >= TEXT('a') && Character <= TEXT('z'))
			|| (Character >= TEXT('A') && Character <= TEXT('Z'))
			|| (Character >= TEXT('0') && Character <= TEXT('9'));
		if (!bAlphaNumeric
			&& (Index == 0
				|| (Character != TEXT('-') && Character != TEXT('_') && Character != TEXT('.'))))
		{
			return false;
		}
	}
	return true;
}

bool IsSafePublicMessage(const FString& Value)
{
	return !Value.IsEmpty() && Value.Len() <= 255
		&& !Value.Contains(TEXT("\r")) && !Value.Contains(TEXT("\n"));
}

bool IsValidPort(const FString& PortText)
{
	if (PortText.IsEmpty() || PortText.Len() > 5 || !PortText.IsNumeric())
	{
		return false;
	}
	const int32 Port = FCString::Atoi(*PortText);
	return Port >= 1 && Port <= 65535;
}

bool IsSafeHost(const FString& Host)
{
	if (Host.IsEmpty() || Host.Len() > 253
		|| Host.StartsWith(TEXT(".")) || Host.EndsWith(TEXT("."))
		|| Host.StartsWith(TEXT("-")) || Host.EndsWith(TEXT("-"))
		|| Host.Contains(TEXT("..")))
	{
		return false;
	}
	for (const TCHAR Character : Host)
	{
		const bool bAlphaNumeric = (Character >= TEXT('a') && Character <= TEXT('z'))
			|| (Character >= TEXT('A') && Character <= TEXT('Z'))
			|| (Character >= TEXT('0') && Character <= TEXT('9'));
		if (!bAlphaNumeric && Character != TEXT('.') && Character != TEXT('-'))
		{
			return false;
		}
	}
	return true;
}

bool IsSteamConnectHost(const FString& Host)
{
	if (!Host.StartsWith(TEXT("steam."), ESearchCase::CaseSensitive))
	{
		return false;
	}
	const FString SteamId = Host.Mid(6);
	return !SteamId.IsEmpty()
		&& SteamId.Len() <= 20
		&& SteamId[0] >= TEXT('1')
		&& SteamId[0] <= TEXT('9')
		&& SteamId.IsNumeric();
}

bool ReadRequiredString(
	const TSharedPtr<FJsonObject>& Root,
	const TCHAR* Field,
	FString& OutValue,
	FString& OutError)
{
	if (!Root->TryGetStringField(Field, OutValue))
	{
		OutError = FString::Printf(TEXT("Réponse matchmaking incomplète : champ %s absent."), Field);
		return false;
	}
	OutValue.TrimStartAndEndInline();
	if (OutValue.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Réponse matchmaking invalide : champ %s vide."), Field);
		return false;
	}
	return true;
}
}

bool PHMatchmakingGatewayContract::NormalizeGatewayBaseUrl(
	const FString& RawUrl,
	const bool bShippingBuild,
	FString& OutNormalizedUrl,
	FString& OutError)
{
	OutNormalizedUrl = RawUrl.TrimStartAndEnd();
	OutError.Reset();
	while (OutNormalizedUrl.EndsWith(TEXT("/")))
	{
		OutNormalizedUrl.LeftChopInline(1);
	}
	if (OutNormalizedUrl.IsEmpty() || OutNormalizedUrl.Len() > 2048
		|| OutNormalizedUrl.Contains(TEXT("?")) || OutNormalizedUrl.Contains(TEXT("#"))
		|| OutNormalizedUrl.Contains(TEXT("@")) || OutNormalizedUrl.Contains(TEXT("\r"))
		|| OutNormalizedUrl.Contains(TEXT("\n")))
	{
		OutError = TEXT("L'URL de la gateway PropHunt est absente ou invalide.");
		return false;
	}
	for (const TCHAR Character : OutNormalizedUrl)
	{
		if (FChar::IsWhitespace(Character))
		{
			OutError = TEXT("L'URL de la gateway PropHunt contient un espace interdit.");
			return false;
		}
	}
	if (OutNormalizedUrl.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		const FString Authority = OutNormalizedUrl.Mid(8);
		FString Host = Authority;
		FString Port;
		if (Authority.Split(TEXT(":"), &Host, &Port) && !IsValidPort(Port))
		{
			OutError = TEXT("Le port HTTPS de la gateway PropHunt est invalide.");
			return false;
		}
		if (!IsSafeHost(Host))
		{
			OutError = TEXT("L'origine HTTPS de la gateway PropHunt est invalide.");
			return false;
		}
		return true;
	}
	if (!bShippingBuild && OutNormalizedUrl.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase))
	{
		const FString Authority = OutNormalizedUrl.Mid(7);
		if (Authority == TEXT("127.0.0.1") || Authority.StartsWith(TEXT("127.0.0.1:"))
			|| Authority.Equals(TEXT("localhost"), ESearchCase::IgnoreCase)
			|| Authority.StartsWith(TEXT("localhost:"), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	OutError = TEXT("La gateway PropHunt exige HTTPS ; HTTP est réservé au loopback hors Shipping.");
	OutNormalizedUrl.Reset();
	return false;
}

bool PHMatchmakingGatewayContract::NormalizeConnectAddress(
	const FString& RawAddress,
	FString& OutNormalizedAddress,
	FString& OutError)
{
	OutNormalizedAddress = RawAddress.TrimStartAndEnd();
	OutError.Reset();
	if (OutNormalizedAddress.IsEmpty() || OutNormalizedAddress.Len() > 255
		|| OutNormalizedAddress.Contains(TEXT("?")) || OutNormalizedAddress.Contains(TEXT("#"))
		|| OutNormalizedAddress.Contains(TEXT("/")) || OutNormalizedAddress.Contains(TEXT("\\"))
		|| OutNormalizedAddress.Contains(TEXT("@")))
	{
		OutError = TEXT("La destination NOVA est absente ou contient des options interdites.");
		return false;
	}
	for (const TCHAR Character : OutNormalizedAddress)
	{
		if (FChar::IsWhitespace(Character) || Character == TEXT('\r') || Character == TEXT('\n'))
		{
			OutError = TEXT("La destination NOVA contient un espace ou un contrôle interdit.");
			return false;
		}
	}
	FString Host;
	FString Port;
	if (!OutNormalizedAddress.Split(TEXT(":"), &Host, &Port, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
		|| !IsSafeHost(Host) || !IsSteamConnectHost(Host) || !IsValidPort(Port))
	{
		OutError = TEXT("La destination NOVA doit être une adresse SteamSockets valide.");
		return false;
	}
	return true;
}

const TCHAR* PHMatchmakingGatewayContract::RolePreferenceToString(
	const EPHMatchmakingPreference Preference)
{
	switch (Preference)
	{
	case EPHMatchmakingPreference::Hunter: return TEXT("hunter");
	case EPHMatchmakingPreference::Prop: return TEXT("prop");
	case EPHMatchmakingPreference::Random: return TEXT("random");
	default: return nullptr;
	}
}

bool PHMatchmakingGatewayContract::IsSafeLobbyInviteSecret(const FString& InviteSecret)
{
	if (InviteSecret.Len() != 43)
	{
		return false;
	}
	for (const TCHAR Character : InviteSecret)
	{
		const bool bAlphaNumeric = FChar::IsAlnum(Character);
		if (!bAlphaNumeric && Character != TEXT('_') && Character != TEXT('-'))
		{
			return false;
		}
	}
	return true;
}

bool PHMatchmakingGatewayContract::ParsePublicErrorResponse(
	const FString& Json,
	FString& OutCode,
	FString& OutMessage)
{
	OutCode.Reset();
	OutMessage.Reset();
	if (Json.IsEmpty() || Json.Len() > MaxResponseBytes)
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
	return Root->TryGetObjectField(TEXT("error"), ErrorObject)
		&& ErrorObject != nullptr
		&& ErrorObject->IsValid()
		&& (*ErrorObject)->TryGetStringField(TEXT("code"), OutCode)
		&& (*ErrorObject)->TryGetStringField(TEXT("message"), OutMessage)
		&& IsSafeIdentifier(OutCode, 64)
		&& IsSafePublicMessage(OutMessage);
}

bool PHMatchmakingGatewayContract::ParseLobbyInviteResponse(
	const FString& Json,
	const int32 ExpectedProtocolVersion,
	const FString& ExpectedBuildId,
	const FDateTime& NowUtc,
	FString& OutInviteSecret,
	FDateTime& OutExpiresUtc,
	FString& OutError)
{
	OutInviteSecret.Reset();
	OutExpiresUtc = FDateTime();
	OutError.Reset();
	if (Json.IsEmpty() || FTCHARToUTF8(*Json).Length() > MaxResponseBytes)
	{
		OutError = TEXT("La réponse d'invitation est vide ou trop volumineuse.");
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("La gateway a renvoyé une invitation JSON invalide.");
		return false;
	}
	double Schema = 0.0;
	double Protocol = 0.0;
	FString BuildId;
	FString ExpiresUtc;
	if (!Root->TryGetNumberField(TEXT("schema"), Schema) || Schema != SchemaVersion
		|| !Root->TryGetNumberField(TEXT("protocol_version"), Protocol)
		|| Protocol != ExpectedProtocolVersion
		|| !Root->TryGetStringField(TEXT("build_id"), BuildId)
		|| BuildId != ExpectedBuildId
		|| !Root->TryGetStringField(TEXT("invite_secret"), OutInviteSecret)
		|| !Root->TryGetStringField(TEXT("expires_utc"), ExpiresUtc)
		|| !IsSafeLobbyInviteSecret(OutInviteSecret)
		|| !FDateTime::ParseIso8601(*ExpiresUtc, OutExpiresUtc)
		|| OutExpiresUtc <= NowUtc
		|| OutExpiresUtc > NowUtc + FTimespan::FromMinutes(10.0))
	{
		OutInviteSecret.Reset();
		OutError = TEXT("La réponse d'invitation de lobby est incompatible ou expirée.");
		return false;
	}
	return true;
}

bool PHMatchmakingGatewayContract::ParseTicketStatus(
	const FString& Json,
	const FString& ExpectedTicketId,
	const int32 ExpectedProtocolVersion,
	const FString& ExpectedBuildId,
	const FDateTime& NowUtc,
	FPHGatewayTicket& OutTicket,
	FString& OutError)
{
	OutTicket = FPHGatewayTicket();
	OutError.Reset();
	if (Json.IsEmpty() || FTCHARToUTF8(*Json).Length() > MaxResponseBytes)
	{
		OutError = TEXT("La réponse matchmaking est vide ou trop volumineuse.");
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("La gateway PropHunt a renvoyé un JSON invalide.");
		return false;
	}
	double Schema = 0.0;
	double Protocol = 0.0;
	FString BuildId;
	FString State;
	if (!Root->TryGetNumberField(TEXT("schema"), Schema) || Schema != SchemaVersion
		|| !Root->TryGetNumberField(TEXT("protocol_version"), Protocol)
		|| Protocol != ExpectedProtocolVersion
		|| !Root->TryGetStringField(TEXT("build_id"), BuildId)
		|| BuildId != ExpectedBuildId
		|| !ReadRequiredString(Root, TEXT("ticket_id"), OutTicket.TicketId, OutError)
		|| !ReadRequiredString(Root, TEXT("state"), State, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Le schéma ou le protocole de la réponse matchmaking est incompatible.");
		}
		return false;
	}
	if (OutTicket.TicketId != ExpectedTicketId || !IsSafeIdentifier(OutTicket.TicketId))
	{
		OutError = TEXT("La réponse ne correspond pas au ticket matchmaking courant.");
		return false;
	}
	if (State == TEXT("queued") || State == TEXT("forming") || State == TEXT("allocating"))
	{
		OutTicket.Status = EPHGatewayTicketStatus::Pending;
		double RetryAfterMs = 500.0;
		if (Root->HasField(TEXT("retry_after_ms"))
			&& !Root->TryGetNumberField(TEXT("retry_after_ms"), RetryAfterMs))
		{
			OutError = TEXT("Le délai de polling matchmaking est invalide.");
			return false;
		}
		if (RetryAfterMs < MinRetryAfterMs || RetryAfterMs > MaxRetryAfterMs
			|| RetryAfterMs != FMath::FloorToDouble(RetryAfterMs))
		{
			OutError = TEXT("Le délai de polling matchmaking est hors limites.");
			return false;
		}
		OutTicket.RetryAfterSeconds = static_cast<float>(RetryAfterMs / 1000.0);
		const TSharedPtr<FJsonObject>* LobbyObject = nullptr;
		if (Root->TryGetObjectField(TEXT("lobby"), LobbyObject))
		{
			FString AssignedRole;
			double SurvivorsPresent = 0.0;
			double SurvivorSlots = 0.0;
			const TArray<TSharedPtr<FJsonValue>>* OccupiedSlotsJson = nullptr;
			if (LobbyObject == nullptr || !LobbyObject->IsValid()
				|| !ReadRequiredString(*LobbyObject, TEXT("lobby_id"), OutTicket.LobbyId, OutError)
				|| !ReadRequiredString(*LobbyObject, TEXT("phase"), OutTicket.LobbyPhase, OutError)
				|| !ReadRequiredString(*LobbyObject, TEXT("assigned_role"), AssignedRole, OutError)
				|| !(*LobbyObject)->TryGetNumberField(TEXT("survivors_present"), SurvivorsPresent)
				|| !(*LobbyObject)->TryGetNumberField(TEXT("survivor_slots"), SurvivorSlots)
				|| !(*LobbyObject)->TryGetBoolField(TEXT("locked"), OutTicket.bLobbyLocked)
				|| !IsSafeIdentifier(OutTicket.LobbyId)
				|| (OutTicket.LobbyPhase != TEXT("waiting")
					&& OutTicket.LobbyPhase != TEXT("countdown")
					&& OutTicket.LobbyPhase != TEXT("locked")
					&& OutTicket.LobbyPhase != TEXT("server_starting"))
				|| SurvivorsPresent != FMath::FloorToDouble(SurvivorsPresent)
				|| SurvivorSlots != FMath::FloorToDouble(SurvivorSlots)
				|| SurvivorsPresent < 0.0 || SurvivorsPresent > 4.0
				|| SurvivorSlots != 4.0)
			{
				if (OutError.IsEmpty()) OutError = TEXT("Le salon d'attente matchmaking est invalide.");
				return false;
			}
			if (AssignedRole == TEXT("hunter")) OutTicket.AssignedRole = EPHPlayerRole::Hunter;
			else if (AssignedRole == TEXT("prop")) OutTicket.AssignedRole = EPHPlayerRole::Prop;
			else
			{
				OutError = TEXT("Le salon a attribué un rôle inconnu.");
				return false;
			}
			OutTicket.SurvivorsPresent = static_cast<int32>(SurvivorsPresent);
			OutTicket.SurvivorSlots = static_cast<int32>(SurvivorSlots);
			OutTicket.OccupiedSurvivorSlots.Reset();
			const bool bHasAuthoritativeSlots =
				(*LobbyObject)->TryGetArrayField(TEXT("occupied_survivor_slots"), OccupiedSlotsJson);
			if (bHasAuthoritativeSlots && OccupiedSlotsJson == nullptr)
			{
				OutError = TEXT("La liste des podiums Survivants est invalide.");
				return false;
			}
			if (bHasAuthoritativeSlots)
			{
				for (const TSharedPtr<FJsonValue>& SlotValue : *OccupiedSlotsJson)
				{
					double SlotNumber = 0.0;
					if (!SlotValue.IsValid() || !SlotValue->TryGetNumber(SlotNumber)
						|| SlotNumber != FMath::FloorToDouble(SlotNumber)
						|| SlotNumber < 1.0 || SlotNumber > SurvivorSlots
						|| OutTicket.OccupiedSurvivorSlots.Contains(static_cast<int32>(SlotNumber)))
					{
						OutError = TEXT("La liste des podiums Survivants est invalide.");
						return false;
					}
					OutTicket.OccupiedSurvivorSlots.Add(static_cast<int32>(SlotNumber));
				}
			}
			else
			{
				// Transitional P9 compatibility: the deployed gateway predates explicit slot IDs.
				for (int32 Slot = 1; Slot <= OutTicket.SurvivorsPresent; ++Slot)
				{
					OutTicket.OccupiedSurvivorSlots.Add(Slot);
				}
			}
			if (OutTicket.OccupiedSurvivorSlots.Num() != OutTicket.SurvivorsPresent)
			{
				OutError = TEXT("Le nombre de podiums Survivants est incohérent.");
				return false;
			}
			OutTicket.OccupiedSurvivorSlots.Sort();
			if (OutTicket.AssignedRole == EPHPlayerRole::Prop)
			{
				double AssignedSlot = 0.0;
				const bool bHasAssignedSlot =
					(*LobbyObject)->TryGetNumberField(TEXT("assigned_survivor_slot"), AssignedSlot);
				if (bHasAssignedSlot
					&& (AssignedSlot != FMath::FloorToDouble(AssignedSlot)
						|| !OutTicket.OccupiedSurvivorSlots.Contains(static_cast<int32>(AssignedSlot))))
				{
					OutError = TEXT("Le podium Survivant attribué est invalide.");
					return false;
				}
				OutTicket.AssignedSurvivorSlot = bHasAssignedSlot
					? static_cast<int32>(AssignedSlot)
					: (OutTicket.OccupiedSurvivorSlots.IsEmpty()
						? 0 : OutTicket.OccupiedSurvivorSlots[0]);
			}
			OutTicket.bHasLobby = true;
			FString CountdownEndsUtc;
			if ((*LobbyObject)->TryGetStringField(TEXT("countdown_ends_utc"), CountdownEndsUtc)
				&& (!FDateTime::ParseIso8601(*CountdownEndsUtc, OutTicket.LobbyCountdownEndsUtc)
					|| OutTicket.LobbyCountdownEndsUtc < NowUtc - FTimespan::FromMinutes(10.0)
					|| OutTicket.LobbyCountdownEndsUtc > NowUtc + FTimespan::FromMinutes(10.0)))
			{
				OutError = TEXT("Le compte à rebours du salon est invalide.");
				return false;
			}
		}
		return true;
	}
	if (State == TEXT("failed") || State == TEXT("cancelled"))
	{
		OutTicket.Status = State == TEXT("cancelled")
			? EPHGatewayTicketStatus::Cancelled : EPHGatewayTicketStatus::Failed;
		const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
		if (!Root->TryGetObjectField(TEXT("error"), ErrorObject) || ErrorObject == nullptr
			|| !ErrorObject->IsValid()
			|| !ReadRequiredString(*ErrorObject, TEXT("code"), OutTicket.FailureCode, OutError)
			|| !ReadRequiredString(*ErrorObject, TEXT("message"), OutTicket.FailureMessage, OutError)
			|| !IsSafeIdentifier(OutTicket.FailureCode, 64)
			|| !IsSafePublicMessage(OutTicket.FailureMessage))
		{
			OutError = TEXT("La gateway PropHunt a renvoyé une erreur non conforme.");
			return false;
		}
		return true;
	}
	if (State != TEXT("ready"))
	{
		OutError = TEXT("L'état du ticket matchmaking est inconnu.");
		return false;
	}
	OutTicket.Status = EPHGatewayTicketStatus::Ready;
	FString AssignedRole;
	FString ExpiresUtc;
	if (!ReadRequiredString(Root, TEXT("match_id"), OutTicket.MatchId, OutError)
		|| !ReadRequiredString(Root, TEXT("reservation_id"), OutTicket.ReservationId, OutError)
		|| !ReadRequiredString(Root, TEXT("assigned_role"), AssignedRole, OutError)
		|| !ReadRequiredString(Root, TEXT("connect_address"), OutTicket.ConnectAddress, OutError)
		|| !ReadRequiredString(Root, TEXT("expires_utc"), ExpiresUtc, OutError)
		|| !IsSafeIdentifier(OutTicket.MatchId) || !IsSafeIdentifier(OutTicket.ReservationId)
		|| !FDateTime::ParseIso8601(*ExpiresUtc, OutTicket.ExpiresUtc))
	{
		if (OutError.IsEmpty()) OutError = TEXT("La réservation matchmaking ready est invalide.");
		return false;
	}
	if (AssignedRole == TEXT("hunter")) OutTicket.AssignedRole = EPHPlayerRole::Hunter;
	else if (AssignedRole == TEXT("prop")) OutTicket.AssignedRole = EPHPlayerRole::Prop;
	else
	{
		OutError = TEXT("La gateway PropHunt a attribué un rôle inconnu.");
		return false;
	}
	FString NormalizedAddress;
	if (!NormalizeConnectAddress(OutTicket.ConnectAddress, NormalizedAddress, OutError))
	{
		return false;
	}
	OutTicket.ConnectAddress = NormalizedAddress;
	if (OutTicket.ExpiresUtc <= NowUtc || OutTicket.ExpiresUtc > NowUtc + FTimespan::FromHours(1.0))
	{
		OutError = TEXT("La réservation matchmaking est expirée ou dépasse la durée autorisée.");
		return false;
	}
	return true;
}
