#include "Online/PHServerInstanceContract.h"

#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool IsAsciiAlphaNumeric(const TCHAR Character)
	{
		return (Character >= TEXT('a') && Character <= TEXT('z'))
			|| (Character >= TEXT('A') && Character <= TEXT('Z'))
			|| (Character >= TEXT('0') && Character <= TEXT('9'));
	}

	bool TryReadInteger(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* Field,
		int32& OutValue)
	{
		double Number = 0.0;
		if (!Object.IsValid() || !Object->TryGetNumberField(Field, Number)
			|| !FMath::IsFinite(Number)
			|| Number != FMath::RoundToDouble(Number)
			|| Number < static_cast<double>(MIN_int32)
			|| Number > static_cast<double>(MAX_int32))
		{
			return false;
		}
		OutValue = static_cast<int32>(Number);
		return true;
	}

	bool TryParseRole(const FString& Value, EPHPlayerRole& OutRole)
	{
		if (Value.Equals(TEXT("hunter"), ESearchCase::IgnoreCase))
		{
			OutRole = EPHPlayerRole::Hunter;
			return true;
		}
		if (Value.Equals(TEXT("prop"), ESearchCase::IgnoreCase))
		{
			OutRole = EPHPlayerRole::Prop;
			return true;
		}
		return false;
	}
}

bool PHServerInstanceContract::IsValidIdentifier(const FString& Value)
{
	if (Value.IsEmpty() || Value.Len() > 64
		|| !IsAsciiAlphaNumeric(Value[0])
		|| !IsAsciiAlphaNumeric(Value[Value.Len() - 1])
		|| Value.Contains(TEXT("..")))
	{
		return false;
	}

	for (const TCHAR Character : Value)
	{
		if (!IsAsciiAlphaNumeric(Character)
			&& Character != TEXT('-')
			&& Character != TEXT('_')
			&& Character != TEXT('.'))
		{
			return false;
		}
	}
	return true;
}

bool PHServerInstanceContract::ShouldRequireNOVAReservation(
	const bool bDedicatedServer,
	const bool bShippingBuild)
{
	return bDedicatedServer && bShippingBuild;
}

FPHServerRuntimeIdentity PHServerInstanceContract::ResolveCommandLine(
	const TCHAR* CommandLine,
	const bool bDedicatedServer,
	const FString& GeneratedSuffix)
{
	FPHServerRuntimeIdentity Result;
	Result.bDedicatedServer = bDedicatedServer;
	Result.bRequireNOVAReservation = FParse::Param(CommandLine, TEXT("RequireNOVAReservation"));

	bool bServerInstanceExplicit = FParse::Value(
		CommandLine, TEXT("ServerInstanceId="), Result.ServerInstanceId);
	bool bRunExplicit = FParse::Value(CommandLine, TEXT("RunId="), Result.RunId);
	bool bMatchExplicit = FParse::Value(CommandLine, TEXT("MatchId="), Result.MatchId);
	Result.ServerInstanceId.TrimStartAndEndInline();
	Result.RunId.TrimStartAndEndInline();
	Result.MatchId.TrimStartAndEndInline();

	FString SafeSuffix = GeneratedSuffix;
	SafeSuffix.ReplaceInline(TEXT("-"), TEXT(""));
	SafeSuffix = SafeSuffix.Left(32);
	if (!bServerInstanceExplicit)
	{
		Result.ServerInstanceId = FString::Printf(TEXT("local-%s"), *SafeSuffix.Left(20));
	}
	if (!bRunExplicit)
	{
		Result.RunId = FString::Printf(TEXT("run-%s"), *SafeSuffix);
	}
	if (!bMatchExplicit)
	{
		Result.MatchId = FString::Printf(TEXT("match-%s"), *SafeSuffix);
	}

	TArray<FString> Errors;
	if (!IsValidIdentifier(Result.ServerInstanceId))
	{
		Errors.Add(TEXT("ServerInstanceId invalide"));
	}
	if (!IsValidIdentifier(Result.RunId))
	{
		Errors.Add(TEXT("RunId invalide"));
	}
	if (!IsValidIdentifier(Result.MatchId))
	{
		Errors.Add(TEXT("MatchId invalide"));
	}
	if (Result.bRequireNOVAReservation)
	{
		if (!bDedicatedServer)
		{
			Errors.Add(TEXT("RequireNOVAReservation exige un serveur dedie"));
		}
		if (!bServerInstanceExplicit || !bRunExplicit || !bMatchExplicit)
		{
			Errors.Add(TEXT("identite NOVA explicite incomplete"));
		}
		if (FParse::Param(CommandLine, TEXT("NoSteam")))
		{
			Errors.Add(TEXT("RequireNOVAReservation exige Steam"));
		}
	}

	Result.ValidationError = FString::Join(Errors, TEXT("; "));
	return Result;
}

bool PHServerInstanceContract::ParseAdmissionRoster(
	const FString& Json,
	const FPHServerRuntimeIdentity& ExpectedIdentity,
	const FDateTime& NowUtc,
	FPHAdmissionRosterSnapshot& OutRoster,
	FString& OutError)
{
	OutRoster = FPHAdmissionRosterSnapshot();
	OutError.Reset();
	if (!ExpectedIdentity.IsValid() || Json.IsEmpty() || Json.Len() > 65536)
	{
		OutError = TEXT("roster NOVA absent ou trop volumineux");
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("roster NOVA JSON invalide");
		return false;
	}

	FString ExpiresAt;
	const TArray<TSharedPtr<FJsonValue>>* Players = nullptr;
	if (!TryReadInteger(Root, TEXT("schema"), OutRoster.SchemaVersion)
		|| OutRoster.SchemaVersion != 1
		|| !TryReadInteger(Root, TEXT("protocol_version"), OutRoster.ProtocolVersion)
		|| OutRoster.ProtocolVersion != ProtocolVersion
		|| !Root->TryGetStringField(TEXT("build_id"), OutRoster.BuildId)
		|| OutRoster.BuildId != BuildId
		|| !Root->TryGetStringField(TEXT("server_instance_id"), OutRoster.ServerInstanceId)
		|| OutRoster.ServerInstanceId != ExpectedIdentity.ServerInstanceId
		|| !Root->TryGetStringField(TEXT("run_id"), OutRoster.RunId)
		|| OutRoster.RunId != ExpectedIdentity.RunId
		|| !Root->TryGetStringField(TEXT("match_id"), OutRoster.MatchId)
		|| OutRoster.MatchId != ExpectedIdentity.MatchId
		|| !Root->TryGetStringField(TEXT("reservation_id"), OutRoster.ReservationId)
		|| !IsValidIdentifier(OutRoster.ReservationId)
		|| !Root->TryGetStringField(TEXT("expires_at"), ExpiresAt)
		|| !FDateTime::ParseIso8601(*ExpiresAt, OutRoster.ExpiresAtUtc)
		|| OutRoster.ExpiresAtUtc <= NowUtc
		|| OutRoster.ExpiresAtUtc > NowUtc + FTimespan::FromHours(1.0)
		|| !TryReadInteger(Root, TEXT("expected_players"), OutRoster.ExpectedPlayers)
		|| OutRoster.ExpectedPlayers < MinimumRosterPlayers
		|| OutRoster.ExpectedPlayers > MaximumRosterPlayers
		|| !Root->TryGetArrayField(TEXT("players"), Players)
		|| Players == nullptr
		|| Players->Num() != OutRoster.ExpectedPlayers)
	{
		OutError = TEXT("roster NOVA incompatible avec ce worker P9");
		return false;
	}

	TSet<FString> SeenIdentities;
	int32 HunterCount = 0;
	for (const TSharedPtr<FJsonValue>& Value : *Players)
	{
		const TSharedPtr<FJsonObject> Player = Value.IsValid() ? Value->AsObject() : nullptr;
		FPHAdmissionRosterEntry Entry;
		FString Role;
		if (!Player.IsValid()
			|| !Player->TryGetStringField(TEXT("identity"), Entry.IdentityKey)
			|| Entry.IdentityKey.Len() < 7
			|| Entry.IdentityKey.Len() > 128
			|| !Entry.IdentityKey.StartsWith(TEXT("STEAM:"), ESearchCase::IgnoreCase)
			|| !Player->TryGetStringField(TEXT("role"), Role)
			|| !TryParseRole(Role, Entry.Role))
		{
			OutError = TEXT("entree joueur NOVA invalide");
			return false;
		}

		const FString CanonicalIdentity = Entry.IdentityKey.ToUpper();
		if (SeenIdentities.Contains(CanonicalIdentity))
		{
			OutError = TEXT("identite Steam dupliquee dans le roster NOVA");
			return false;
		}
		SeenIdentities.Add(CanonicalIdentity);
		HunterCount += Entry.Role == EPHPlayerRole::Hunter ? 1 : 0;
		OutRoster.Players.Add(MoveTemp(Entry));
	}

	const int32 PropCount = OutRoster.Players.Num() - HunterCount;
	if (HunterCount != 1 || PropCount != OutRoster.ExpectedPlayers - 1)
	{
		OutError = TEXT("le roster NOVA doit contenir exactement un Hunter et le reste en Props");
		OutRoster = FPHAdmissionRosterSnapshot();
		return false;
	}
	return true;
}

bool PHServerInstanceContract::ResolveAuthorizedRole(
	const FPHAdmissionRosterSnapshot& Roster,
	const FString& IdentityKey,
	EPHPlayerRole& OutRole,
	FString& OutError)
{
	OutRole = EPHPlayerRole::Unassigned;
	OutError.Reset();
	if (IdentityKey.IsEmpty())
	{
		OutError = TEXT("identite Steam authentifiee requise");
		return false;
	}
	for (const FPHAdmissionRosterEntry& Entry : Roster.Players)
	{
		if (Entry.IdentityKey.Equals(IdentityKey, ESearchCase::IgnoreCase))
		{
			OutRole = Entry.Role;
			return OutRole == EPHPlayerRole::Hunter || OutRole == EPHPlayerRole::Prop;
		}
	}
	OutError = TEXT("joueur absent du roster NOVA de cette instance");
	return false;
}
