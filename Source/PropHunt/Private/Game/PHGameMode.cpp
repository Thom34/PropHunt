#include "Game/PHGameMode.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Characters/PHHunterCharacter.h"
#include "Characters/PHPropCharacter.h"
#include "Game/PHGameState.h"
#include "Game/PHMatchRulesDataAsset.h"
#include "Game/PHPlayerController.h"
#include "Game/PHPlayerState.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Gameplay/Objectives/PHObjectiveActor.h"
#include "Gameplay/Escape/PHExitGate.h"
#include "Kismet/GameplayStatics.h"
#include "Online/PHSessionSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHMatch, Log, All);

APHGameMode::APHGameMode()
	: bMatchFlowHasStarted(false)
	, bLobbyReadyCountdownActive(false)
	, ExpectedPlayerCount(0)
	, EscapedPropCount(0)
#if !UE_BUILD_SHIPPING
	, TestMinimumPropPlayersOverride(0)
#endif
{
	GameStateClass = APHGameState::StaticClass();
	PlayerStateClass = APHPlayerState::StaticClass();
	PlayerControllerClass = APHPlayerController::StaticClass();
	HunterPawnClass = APHHunterCharacter::StaticClass();
	PropPawnClass = APHPropCharacter::StaticClass();
	DefaultPawnClass = APHPropCharacter::StaticClass();
	bStartPlayersAsSpectators = false;

	static ConstructorHelpers::FObjectFinder<UPHMatchRulesDataAsset> DefaultRulesAsset(
		TEXT("/Game/PropHunt/Data/DA_PH_MatchRules_Default.DA_PH_MatchRules_Default"));
	if (DefaultRulesAsset.Succeeded())
	{
		MatchRules = DefaultRulesAsset.Object;
	}
}

void APHGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	FText RulesValidationError;
	if (!GetMatchRules()->HasValidRules(&RulesValidationError))
	{
		UE_LOG(LogPHMatch, Error, TEXT("Configured match rules are invalid: %s. Falling back to C++ defaults."),
			*RulesValidationError.ToString());
		MatchRules = nullptr;
	}

	if (GameSession != nullptr)
	{
		GameSession->MaxPlayers = GetMatchRules()->MaximumPropPlayers + 1;
	}

	const FString ExpectedPlayersOption = UGameplayStatics::ParseOption(Options, TEXT("PHExpectedPlayers"));
	if (!ExpectedPlayersOption.IsEmpty())
	{
		ExpectedPlayerCount = FMath::Clamp(
			FCString::Atoi(*ExpectedPlayersOption),
			GetMatchRules()->MinimumPropPlayers + 1,
			GetMatchRules()->MaximumPropPlayers + 1);
		UE_LOG(LogPHMatch, Log, TEXT("Reserved roster expects %d player(s)."), ExpectedPlayerCount);
	}

#if !UE_BUILD_SHIPPING
	const FString MinimumPropsOption = UGameplayStatics::ParseOption(Options, TEXT("PHTestMinimumProps"));
	if (!MinimumPropsOption.IsEmpty())
	{
		TestMinimumPropPlayersOverride = FMath::Clamp(
			FCString::Atoi(*MinimumPropsOption), 0, GetMatchRules()->MaximumPropPlayers);
	}
#endif
}

void APHGameMode::StartPlay()
{
	Super::StartPlay();

	APHGameState* PHGameState = GetPHGameState();
	if (PHGameState != nullptr)
	{
		PHGameState->SetMatchPhase(EPHMatchPhase::Lobby, 0.0f);
		PHGameState->SetMatchEndReason(EPHMatchEndReason::None);
		PHGameState->SetExpectedPlayerCount(ExpectedPlayerCount);
	}

	TryStartMatchFlow();
}

void APHGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	const APHGameState* PHGameState = GetPHGameState();
	if (PHGameState != nullptr && PHGameState->GetMatchPhase() != EPHMatchPhase::Lobby)
	{
		ErrorMessage = TEXT("MatchAlreadyStarted");
		return;
	}

	const int32 ConnectedPlayerCount = GameState != nullptr ? GameState->PlayerArray.Num() : 0;
	if (ConnectedPlayerCount >= GetMatchRules()->MaximumPropPlayers + 1)
	{
		ErrorMessage = TEXT("MatchFull");
	}
}

void APHGameMode::PostLogin(APlayerController* NewPlayer)
{
	if (NewPlayer != nullptr)
	{
		AssignRole(*NewPlayer);
	}

	Super::PostLogin(NewPlayer);

	TryStartMatchFlow();
}

UClass* APHGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	const APHPlayerState* PHPlayerState = InController != nullptr ? InController->GetPlayerState<APHPlayerState>() : nullptr;
	if (PHPlayerState != nullptr && PHPlayerState->GetPlayerRole() == EPHPlayerRole::Hunter)
	{
		return HunterPawnClass != nullptr ? HunterPawnClass.Get() : APHHunterCharacter::StaticClass();
	}

	return PropPawnClass != nullptr ? PropPawnClass.Get() : APHPropCharacter::StaticClass();
}

AActor* APHGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	TArray<APlayerStart*> CandidateStarts;
	for (TActorIterator<APlayerStart> StartIterator(GetWorld()); StartIterator; ++StartIterator)
	{
		if (IsValid(*StartIterator))
		{
			CandidateStarts.Add(*StartIterator);
		}
	}
	CandidateStarts.Sort([](const APlayerStart& Left, const APlayerStart& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});
	if (CandidateStarts.IsEmpty())
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	TArray<FVector> StartLocations;
	StartLocations.Reserve(CandidateStarts.Num());
	for (const APlayerStart* Candidate : CandidateStarts)
	{
		StartLocations.Add(Candidate->GetActorLocation());
	}

	TArray<FVector> OccupiedLocations;
	for (TActorIterator<APawn> PawnIterator(GetWorld()); PawnIterator; ++PawnIterator)
	{
		const APawn* ExistingPawn = *PawnIterator;
		if (IsValid(ExistingPawn) && ExistingPawn->GetController() != Player)
		{
			OccupiedLocations.Add(ExistingPawn->GetActorLocation());
		}
	}

	const int32 StartIndex = PHMatchFlow::ChooseMostSeparatedStartIndex(StartLocations, OccupiedLocations);
	if (!CandidateStarts.IsValidIndex(StartIndex))
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	APlayerStart* ChosenStart = CandidateStarts[StartIndex];
	UE_LOG(LogPHMatch, Log, TEXT("Selected separated PlayerStart %s for %s (%d existing Pawn(s))."),
		*ChosenStart->GetName(), Player != nullptr ? *Player->GetName() : TEXT("unknown controller"),
		OccupiedLocations.Num());
	return ChosenStart;
}

void APHGameMode::Logout(AController* Exiting)
{
	EPHPlayerRole DepartingRole = EPHPlayerRole::Unassigned;
	TWeakObjectPtr<APHPropCharacter> DepartingPropPawn;
	const EPHMatchPhase DepartingPhase = GetPHGameState() != nullptr
		? GetPHGameState()->GetMatchPhase()
		: EPHMatchPhase::Lobby;
	if (Exiting != nullptr)
	{
		if (const APHPlayerState* DepartingPlayerState = Exiting->GetPlayerState<APHPlayerState>())
		{
			DepartingRole = DepartingPlayerState->GetPlayerRole();
			if (DepartingRole != EPHPlayerRole::Unassigned
				&& DepartingPhase != EPHMatchPhase::Lobby
				&& DepartingPhase != EPHMatchPhase::Results)
			{
				FPHMatchResultRow& DepartedRow = DepartedResultRows.AddDefaulted_GetRef();
				DepartedRow.PlayerId = DepartingPlayerState->GetPlayerId();
				DepartedRow.PlayerName = DepartingPlayerState->GetPlayerName().IsEmpty()
					? FString(TEXT("JOUEUR"))
					: DepartingPlayerState->GetPlayerName();
				DepartedRow.PlayerRole = DepartingRole;
				DepartedRow.Outcome = EPHMatchPlayerOutcome::Disconnected;
				DepartedRow.Stats = DepartingPlayerState->GetMatchStats();
			}
		}
		DepartingPropPawn = Cast<APHPropCharacter>(Exiting->GetPawn());
	}

	const int32 RemainingPropPlayerCount = PHMatchFlow::GetRemainingRoleCountAfterDeparture(
		CountPlayersWithRole(EPHPlayerRole::Prop), DepartingRole, EPHPlayerRole::Prop);

	Super::Logout(Exiting);

	const APHGameState* PHGameState = GetPHGameState();
	if (PHGameState == nullptr)
	{
		return;
	}
	if (PHGameState->GetMatchPhase() == EPHMatchPhase::Lobby)
	{
		if (DepartingRole == EPHPlayerRole::Hunter)
		{
			if (UGameInstance* GameInstance = GetGameInstance())
			{
				if (UPHSessionSubsystem* SessionSubsystem = GameInstance->GetSubsystem<UPHSessionSubsystem>())
				{
					SessionSubsystem->SetHunterAssigned(false);
				}
			}
		}
		TryStartMatchFlow();
		return;
	}
	if (PHGameState->GetMatchPhase() == EPHMatchPhase::Results)
	{
		return;
	}

	if (DepartingRole == EPHPlayerRole::Hunter)
	{
		FinishMatch(EPHMatchEndReason::HunterDisconnected);
	}
	else if (DepartingRole == EPHPlayerRole::Prop && RemainingPropPlayerCount == 0)
	{
		FinishMatch(EPHMatchEndReason::AllPropsGone);
	}
	else if (DepartingRole == EPHPlayerRole::Prop)
	{
		EvaluateAllRemainingPropsRetained(DepartingPropPawn.Get());
	}
}

const UPHMatchRulesDataAsset* APHGameMode::GetMatchRules() const
{
	return MatchRules != nullptr ? MatchRules.Get() : GetDefault<UPHMatchRulesDataAsset>();
}

void APHGameMode::NotifyObjectiveCompleted(APHObjectiveActor& Objective)
{
	APHGameState* PHGameState = GetPHGameState();
	if (!HasAuthority() || PHGameState == nullptr || PHGameState->GetMatchPhase() != EPHMatchPhase::Hunt
		|| !Objective.IsObjectiveActive() || CompletedObjectives.Contains(&Objective))
	{
		return;
	}

	CompletedObjectives.Add(&Objective);
	const int32 CompletedCount = CompletedObjectives.Num();
	PHGameState->SetCompletedObjectiveCount(CompletedCount);
	UE_LOG(LogPHMatch, Log, TEXT("Objective %s registered (%d/%d required)."),
		*Objective.GetName(), CompletedCount, GetMatchRules()->RequiredObjectiveCount);

	if (PHMatchFlow::ShouldOpenEscape(CompletedCount, GetMatchRules()->RequiredObjectiveCount))
	{
		BeginPhase(EPHMatchPhase::Escape);
	}
}

void APHGameMode::NotifyPropEliminated(APHPropCharacter& EliminatedProp)
{
	const APHGameState* PHGameState = GetPHGameState();
	if (!HasAuthority() || PHGameState == nullptr
		|| (PHGameState->GetMatchPhase() != EPHMatchPhase::Hunt
			&& PHGameState->GetMatchPhase() != EPHMatchPhase::Escape))
	{
		return;
	}

	for (APlayerState* PlayerState : PHGameState->PlayerArray)
	{
		if (APHPlayerState* HunterPlayerState = Cast<APHPlayerState>(PlayerState);
			HunterPlayerState != nullptr && HunterPlayerState->GetPlayerRole() == EPHPlayerRole::Hunter)
		{
			HunterPlayerState->RecordHunterElimination();
			break;
		}
	}

	int32 ActivePropCount = 0;
	for (TActorIterator<APHPropCharacter> PropIterator(GetWorld()); PropIterator; ++PropIterator)
	{
		const APHPropCharacter* Candidate = *PropIterator;
		if (Candidate != nullptr && !PHCaptureFlow::IsTerminal(Candidate->GetCaptureState()))
		{
			++ActivePropCount;
		}
	}

	UE_LOG(LogPHMatch, Log, TEXT("Prop %s eliminated; %d active Prop(s) remain."),
		*EliminatedProp.GetName(), ActivePropCount);
	if (ActivePropCount == 0)
	{
		FinishMatch(EscapedPropCount > 0
			? EPHMatchEndReason::AtLeastOnePropEscaped
			: EPHMatchEndReason::AllPropsGone);
	}
	else
	{
		EvaluateAllRemainingPropsRetained();
	}
}

void APHGameMode::NotifyPropRetained(APHPropCharacter& RetainedProp)
{
	if (!HasAuthority() || RetainedProp.GetCaptureState() != EPHPropCaptureState::Retained)
	{
		return;
	}
	EvaluateAllRemainingPropsRetained();
}

void APHGameMode::NotifyPropEscaped(APHPropCharacter& EscapedProp)
{
	const APHGameState* PHGameState = GetPHGameState();
	if (!HasAuthority() || PHGameState == nullptr || PHGameState->GetMatchPhase() != EPHMatchPhase::Escape)
	{
		return;
	}
	EscapedPropCount = FMath::Min(MAX_int32, EscapedPropCount + 1);
	int32 ActivePropCount = 0;
	for (TActorIterator<APHPropCharacter> PropIterator(GetWorld()); PropIterator; ++PropIterator)
	{
		if (IsValid(*PropIterator) && !PHCaptureFlow::IsTerminal(PropIterator->GetCaptureState()))
		{
			++ActivePropCount;
		}
	}
	UE_LOG(LogPHMatch, Log, TEXT("Prop %s escaped; %d active Prop(s) remain."),
		*EscapedProp.GetName(), ActivePropCount);
	if (ActivePropCount == 0)
	{
		FinishMatch(EPHMatchEndReason::AtLeastOnePropEscaped);
	}
	else
	{
		EvaluateAllRemainingPropsRetained();
	}
}

void APHGameMode::EvaluateAllRemainingPropsRetained(const APHPropCharacter* IgnoredProp)
{
	const APHGameState* PHGameState = GetPHGameState();
	if (!HasAuthority() || PHGameState == nullptr
		|| (PHGameState->GetMatchPhase() != EPHMatchPhase::Hunt
			&& PHGameState->GetMatchPhase() != EPHMatchPhase::Escape))
	{
		return;
	}

	TArray<APHPropCharacter*> ActiveProps;
	int32 RetainedPropCount = 0;
	for (TActorIterator<APHPropCharacter> PropIterator(GetWorld()); PropIterator; ++PropIterator)
	{
		APHPropCharacter* Candidate = *PropIterator;
		if (!IsValid(Candidate) || Candidate == IgnoredProp
			|| PHCaptureFlow::IsTerminal(Candidate->GetCaptureState()))
		{
			continue;
		}
		ActiveProps.Add(Candidate);
		RetainedPropCount += Candidate->GetCaptureState() == EPHPropCaptureState::Retained ? 1 : 0;
	}

	if (!PHCaptureFlow::ShouldAccelerateAllRetainedElimination(ActiveProps.Num(), RetainedPropCount))
	{
		return;
	}

	const float EliminationDelay = FMath::Clamp(
		GetMatchRules()->AllPropsRetainedEliminationDelay,
		0.1f,
		5.0f);
	UE_LOG(LogPHMatch, Log,
		TEXT("All %d remaining Prop(s) are retained; Hunter victory countdown clamped to %.1f seconds."),
		ActiveProps.Num(), EliminationDelay);
	for (APHPropCharacter* RetainedProp : ActiveProps)
	{
		RetainedProp->ServerClampRetainedEliminationDelay(EliminationDelay);
	}
}

void APHGameMode::TryStartMatchFlow()
{
	if (bMatchFlowHasStarted || !HasAuthority())
	{
		return;
	}

	const APHGameState* PHGameState = GetPHGameState();
	const UPHMatchRulesDataAsset* Rules = GetMatchRules();
	if (PHGameState == nullptr || PHGameState->GetMatchPhase() != EPHMatchPhase::Lobby)
	{
		return;
	}

	int32 MinimumPropPlayers = Rules->MinimumPropPlayers;
#if !UE_BUILD_SHIPPING
	if (TestMinimumPropPlayersOverride > 0)
	{
		MinimumPropPlayers = TestMinimumPropPlayersOverride;
	}
#endif
	const bool bEnoughPlayers = CountPlayersWithRole(EPHPlayerRole::Hunter) == 1
		&& CountPlayersWithRole(EPHPlayerRole::Prop) >= MinimumPropPlayers;
	if (!bEnoughPlayers)
	{
		CancelLobbyWait();
		return;
	}

	const int32 ConnectedPlayerCount = CountPlayersWithRole(EPHPlayerRole::Hunter)
		+ CountPlayersWithRole(EPHPlayerRole::Prop);
	const bool bReservedRosterComplete = ExpectedPlayerCount > 0
		&& ConnectedPlayerCount >= ExpectedPlayerCount;
	const bool bServerCapacityReached = ExpectedPlayerCount == 0
		&& ConnectedPlayerCount >= Rules->MaximumPropPlayers + 1;
	const bool bReadyToLaunch = bReservedRosterComplete || bServerCapacityReached;
	const float LobbyWaitDuration = FMath::Max(
		0.0f,
		bReadyToLaunch ? Rules->LobbyReadyCountdownDuration : Rules->LobbyWaitDuration);

	if (GetWorldTimerManager().IsTimerActive(LobbyWaitTimerHandle)
		&& bLobbyReadyCountdownActive == bReadyToLaunch)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(LobbyWaitTimerHandle);
	bLobbyReadyCountdownActive = bReadyToLaunch;
	if (LobbyWaitDuration <= 0.0f)
	{
		FinishLobbyWait();
		return;
	}

	if (APHGameState* MutableGameState = GetPHGameState())
	{
		MutableGameState->SetMatchPhase(EPHMatchPhase::Lobby, LobbyWaitDuration);
	}
	GetWorldTimerManager().SetTimer(
		LobbyWaitTimerHandle,
		this,
		&APHGameMode::FinishLobbyWait,
		LobbyWaitDuration,
		false);
	if (bReadyToLaunch)
	{
		UE_LOG(LogPHMatch, Log,
			TEXT("Reserved roster complete (%d/%d); visible launch countdown started for %.1f seconds."),
			ConnectedPlayerCount,
			ExpectedPlayerCount > 0 ? ExpectedPlayerCount : Rules->MaximumPropPlayers + 1,
			LobbyWaitDuration);
	}
	else
	{
		UE_LOG(LogPHMatch, Log,
			TEXT("Minimum roster reached (%d connected); safety join timeout started for %.1f seconds."),
			ConnectedPlayerCount,
			LobbyWaitDuration);
	}
}

void APHGameMode::FinishLobbyWait()
{
	GetWorldTimerManager().ClearTimer(LobbyWaitTimerHandle);
	bLobbyReadyCountdownActive = false;
	if (bMatchFlowHasStarted || !HasAuthority())
	{
		return;
	}

	const UPHMatchRulesDataAsset* Rules = GetMatchRules();
	int32 MinimumPropPlayers = Rules->MinimumPropPlayers;
#if !UE_BUILD_SHIPPING
	if (TestMinimumPropPlayersOverride > 0)
	{
		MinimumPropPlayers = TestMinimumPropPlayersOverride;
	}
#endif
	const APHGameState* PHGameState = GetPHGameState();
	if (PHGameState == nullptr || PHGameState->GetMatchPhase() != EPHMatchPhase::Lobby
		|| CountPlayersWithRole(EPHPlayerRole::Hunter) != 1
		|| CountPlayersWithRole(EPHPlayerRole::Prop) < MinimumPropPlayers)
	{
		CancelLobbyWait();
		return;
	}

	bMatchFlowHasStarted = true;
	BeginPhase(EPHMatchPhase::Preparation);
}

void APHGameMode::CancelLobbyWait()
{
	const bool bWasWaiting = GetWorldTimerManager().IsTimerActive(LobbyWaitTimerHandle);
	GetWorldTimerManager().ClearTimer(LobbyWaitTimerHandle);
	bLobbyReadyCountdownActive = false;
	if (APHGameState* PHGameState = GetPHGameState())
	{
		if (PHGameState->GetMatchPhase() == EPHMatchPhase::Lobby && PHGameState->GetRemainingPhaseTime() > 0.0f)
		{
			PHGameState->SetMatchPhase(EPHMatchPhase::Lobby, 0.0f);
		}
	}
	if (bWasWaiting)
	{
		UE_LOG(LogPHMatch, Log, TEXT("Lobby launch window cancelled because the minimum player requirement is no longer met."));
	}
}

void APHGameMode::AssignRole(APlayerController& NewPlayer)
{
	APHPlayerState* NewPlayerState = NewPlayer.GetPlayerState<APHPlayerState>();
	if (NewPlayerState == nullptr)
	{
		UE_LOG(LogPHMatch, Error, TEXT("Cannot assign a role to %s without APHPlayerState."), *NewPlayer.GetName());
		return;
	}

	if (NewPlayerState->GetPlayerRole() != EPHPlayerRole::Unassigned)
	{
		return;
	}

	const bool bHunterAlreadyAssigned = CountPlayersWithRole(EPHPlayerRole::Hunter) > 0;
	const bool bDedicatedServer = GetNetMode() == NM_DedicatedServer;
	const EPHPlayerRole NewRole = PHMatchmakingPolicy::ResolveAuthoritativeServerRole(
		NewPlayer.IsLocalController(),
		bDedicatedServer,
		bHunterAlreadyAssigned);

	NewPlayerState->SetPlayerRole(NewRole);
	if (NewRole == EPHPlayerRole::Hunter)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UPHSessionSubsystem* SessionSubsystem = GameInstance->GetSubsystem<UPHSessionSubsystem>())
			{
				SessionSubsystem->SetHunterAssigned(true);
			}
		}
	}
	UE_LOG(LogPHMatch, Log, TEXT("Assigned role %s to %s (local=%s, dedicated=%s, hunter-already-assigned=%s)."),
		NewRole == EPHPlayerRole::Hunter ? TEXT("Hunter") : TEXT("Prop"),
		*NewPlayerState->GetPlayerName(),
		NewPlayer.IsLocalController() ? TEXT("true") : TEXT("false"),
		bDedicatedServer ? TEXT("true") : TEXT("false"),
		bHunterAlreadyAssigned ? TEXT("true") : TEXT("false"));
}

void APHGameMode::BeginPhase(const EPHMatchPhase NewPhase)
{
	APHGameState* PHGameState = GetPHGameState();
	if (PHGameState == nullptr)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(PhaseTimerHandle);
	if (NewPhase != EPHMatchPhase::Lobby)
	{
		GetWorldTimerManager().ClearTimer(LobbyWaitTimerHandle);
	}

	const UPHMatchRulesDataAsset* Rules = GetMatchRules();
	float PhaseDuration = 0.0f;
	switch (NewPhase)
	{
	case EPHMatchPhase::Preparation:
		PhaseDuration = Rules->PreparationDuration;
		DepartedResultRows.Reset();
		EscapedPropCount = 0;
		PHGameState->SetMatchSeed(FMath::Rand());
		PHGameState->SetCompletedObjectiveCount(0);
		PHGameState->SetMatchEndReason(EPHMatchEndReason::None);
		PHGameState->ClearMatchResultsSnapshot();
		for (APlayerState* PlayerState : PHGameState->PlayerArray)
		{
			if (APHPlayerState* PHPlayerState = Cast<APHPlayerState>(PlayerState))
			{
				PHPlayerState->ResetMatchStats();
			}
		}
		PrepareObjectivesForMatch();
		PrepareExitGatesForMatch();
		for (TActorIterator<APHPropCharacter> PropIterator(GetWorld()); PropIterator; ++PropIterator)
		{
			if (IsValid(*PropIterator))
			{
				PropIterator->ResetCaptureForMatch();
			}
		}
		break;
	case EPHMatchPhase::Hunt:
		PhaseDuration = Rules->HuntDuration;
		break;
	case EPHMatchPhase::Results:
		// The detailed result screen is a separate local frontend map. Keep the
		// replicated phase transition, then dispatch the immutable snapshot on
		// the next world tick instead of showing a duplicate in-match panel.
		break;
	case EPHMatchPhase::Lobby:
		bMatchFlowHasStarted = false;
		for (TActorIterator<APHPropCharacter> PropIterator(GetWorld()); PropIterator; ++PropIterator)
		{
			if (IsValid(*PropIterator))
			{
				PropIterator->ResetCaptureForMatch();
			}
		}
		break;
	case EPHMatchPhase::Escape:
		PhaseDuration = Rules->EscapeDuration;
		SetExitGatesEnabled(true);
		break;
	default:
		break;
	}

	PHGameState->SetMatchPhase(NewPhase, PhaseDuration);
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UPHSessionSubsystem* SessionSubsystem = GameInstance->GetSubsystem<UPHSessionSubsystem>())
		{
			SessionSubsystem->SetHunterLobbyJoinable(NewPhase == EPHMatchPhase::Lobby);
		}
	}
	UE_LOG(LogPHMatch, Log, TEXT("Match phase changed to %d for %.1f seconds (seed %d)."),
		static_cast<int32>(NewPhase), PhaseDuration, PHGameState->GetMatchSeed());

	if (PhaseDuration > 0.0f)
	{
		GetWorldTimerManager().SetTimer(PhaseTimerHandle, this, &APHGameMode::AdvanceTimedPhase, PhaseDuration, false);
	}
	else if (NewPhase != EPHMatchPhase::Lobby && NewPhase != EPHMatchPhase::Escape)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &APHGameMode::AdvanceTimedPhase);
	}
}

#if !UE_BUILD_SHIPPING
void APHGameMode::ForceEscapePhaseForSmoke()
{
	if (HasAuthority())
	{
		BeginPhase(EPHMatchPhase::Escape);
	}
}
#endif

void APHGameMode::PrepareObjectivesForMatch()
{
	CompletedObjectives.Reset();

	TArray<APHObjectiveActor*> Objectives;
	for (TActorIterator<APHObjectiveActor> ObjectiveIterator(GetWorld()); ObjectiveIterator; ++ObjectiveIterator)
	{
		if (IsValid(*ObjectiveIterator))
		{
			Objectives.Add(*ObjectiveIterator);
		}
	}

	Objectives.Sort([](const APHObjectiveActor& Left, const APHObjectiveActor& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});

	FRandomStream RandomStream(GetPHGameState() != nullptr ? GetPHGameState()->GetMatchSeed() : 0);
	for (int32 Index = Objectives.Num() - 1; Index > 0; --Index)
	{
		const int32 SwapIndex = RandomStream.RandRange(0, Index);
		Objectives.Swap(Index, SwapIndex);
	}

	const int32 ActiveCount = FMath::Min(Objectives.Num(), GetMatchRules()->ActiveObjectiveCount);
	for (int32 Index = 0; Index < Objectives.Num(); ++Index)
	{
		Objectives[Index]->ResetForMatch(Index < ActiveCount);
	}

	if (ActiveCount < GetMatchRules()->RequiredObjectiveCount)
	{
		UE_LOG(LogPHMatch, Warning, TEXT("Only %d authored objectives are active, but %d are required."),
			ActiveCount, GetMatchRules()->RequiredObjectiveCount);
	}
}

void APHGameMode::PrepareExitGatesForMatch()
{
	int32 GateCount = 0;
	for (TActorIterator<APHExitGate> GateIterator(GetWorld()); GateIterator; ++GateIterator)
	{
		if (IsValid(*GateIterator))
		{
			GateIterator->ResetForMatch();
			++GateCount;
		}
	}
	if (GateCount != 2)
	{
		UE_LOG(LogPHMatch, Warning, TEXT("Expected exactly 2 authored exit gates, found %d."), GateCount);
	}
}

void APHGameMode::SetExitGatesEnabled(const bool bEnabled)
{
	for (TActorIterator<APHExitGate> GateIterator(GetWorld()); GateIterator; ++GateIterator)
	{
		if (IsValid(*GateIterator))
		{
			GateIterator->SetGateEnabled(bEnabled);
		}
	}
}

void APHGameMode::AdvanceTimedPhase()
{
	const APHGameState* PHGameState = GetPHGameState();
	if (PHGameState == nullptr)
	{
		return;
	}

	const EPHMatchPhase CurrentPhase = PHGameState->GetMatchPhase();
	if (CurrentPhase == EPHMatchPhase::Hunt || CurrentPhase == EPHMatchPhase::Escape)
	{
		FinishMatch(EPHMatchEndReason::TimeExpired);
		return;
	}
	if (CurrentPhase == EPHMatchPhase::Results)
	{
		ReturnPlayersToLobbyAfterResults();
		return;
	}

	BeginPhase(PHMatchFlow::GetNextTimedPhase(CurrentPhase));
}

void APHGameMode::ReturnPlayersToLobbyAfterResults()
{
	if (!HasAuthority())
	{
		return;
	}

	TArray<TObjectPtr<APHPlayerController>> Controllers;
	for (FConstPlayerControllerIterator ControllerIterator = GetWorld()->GetPlayerControllerIterator(); ControllerIterator; ++ControllerIterator)
	{
		if (APHPlayerController* PlayerController = Cast<APHPlayerController>(ControllerIterator->Get()))
		{
			Controllers.Add(PlayerController);
		}
	}

	const FPHMatchResultsSnapshot FinalResults = GetPHGameState() != nullptr
		? GetPHGameState()->GetMatchResultsSnapshot()
		: FPHMatchResultsSnapshot();

	// Reset the reusable dedicated worker first. Remote clients then leave this
	// gameplay world independently for the configured local lobby map.
	BeginPhase(EPHMatchPhase::Lobby);
	for (APHPlayerController* PlayerController : Controllers)
	{
		if (IsValid(PlayerController))
		{
			const APHPlayerState* LocalPlayerState = PlayerController->GetPlayerState<APHPlayerState>();
			PlayerController->ClientReturnToLobbyAfterMatch(
				FinalResults,
				LocalPlayerState != nullptr ? LocalPlayerState->GetPlayerRole() : EPHPlayerRole::Unassigned,
				LocalPlayerState != nullptr ? LocalPlayerState->GetPlayerName() : FString(),
				LocalPlayerState != nullptr ? LocalPlayerState->GetPlayerId() : INDEX_NONE);
		}
	}

	UE_LOG(LogPHMatch, Log, TEXT("Final results ready; dispatched %d player(s) directly to the dedicated results map."), Controllers.Num());
}

void APHGameMode::FinishMatch(const EPHMatchEndReason EndReason)
{
	if (APHGameState* PHGameState = GetPHGameState())
	{
		PHGameState->SetMatchEndReason(EndReason);
		BuildAndPublishMatchResults(EndReason);
		BeginPhase(EPHMatchPhase::Results);
	}
}

void APHGameMode::BuildAndPublishMatchResults(const EPHMatchEndReason EndReason)
{
	APHGameState* PHGameState = GetPHGameState();
	const UPHMatchRulesDataAsset* Rules = GetMatchRules();
	if (!HasAuthority() || PHGameState == nullptr || Rules == nullptr)
	{
		return;
	}

	FPHMatchResultsSnapshot Snapshot;
	Snapshot.bFinalized = true;
	Snapshot.MatchSeed = PHGameState->GetMatchSeed();
	Snapshot.EndReason = EndReason;
	Snapshot.CompletedObjectiveCount = PHGameState->GetCompletedObjectiveCount();
	Snapshot.PlayerRows = DepartedResultRows;

	for (const APlayerState* PlayerState : PHGameState->PlayerArray)
	{
		const APHPlayerState* PHPlayerState = Cast<APHPlayerState>(PlayerState);
		if (PHPlayerState == nullptr || PHPlayerState->GetPlayerRole() == EPHPlayerRole::Unassigned)
		{
			continue;
		}

		FPHMatchResultRow& Row = Snapshot.PlayerRows.AddDefaulted_GetRef();
		Row.PlayerId = PHPlayerState->GetPlayerId();
		Row.PlayerName = PHPlayerState->GetPlayerName().IsEmpty()
			? FString(TEXT("JOUEUR"))
			: PHPlayerState->GetPlayerName();
		Row.PlayerRole = PHPlayerState->GetPlayerRole();
		Row.Stats = PHPlayerState->GetMatchStats();

		if (Row.PlayerRole == EPHPlayerRole::Hunter)
		{
			Row.Outcome = EPHMatchPlayerOutcome::Hunter;
		}
		else
		{
			const APHPropCharacter* PropCharacter = Cast<APHPropCharacter>(PHPlayerState->GetPawn());
			const bool bEliminated = PropCharacter != nullptr
				&& PropCharacter->GetCaptureState() == EPHPropCaptureState::Eliminated;
			Row.Outcome = bEliminated
				? EPHMatchPlayerOutcome::Eliminated
				: EPHMatchPlayerOutcome::Survived;
		}

	}

	for (FPHMatchResultRow& Row : Snapshot.PlayerRows)
	{
		Row.TotalScore = PHMatchResults::CalculateTotalScore(
			Row.Stats,
			Rules->ObjectiveScore,
			Rules->RescueScore,
			Rules->HunterDownScore,
			Rules->HunterRetentionScore,
			Rules->HunterEliminationScore);
		Row.GrantedScore = PHMatchResults::CalculateGrantedScore(Row.TotalScore, Row.Outcome);
	}

	Snapshot.PlayerRows.Sort([](const FPHMatchResultRow& Left, const FPHMatchResultRow& Right)
	{
		if (Left.PlayerRole != Right.PlayerRole)
		{
			return Left.PlayerRole == EPHPlayerRole::Hunter;
		}
		if (Left.TotalScore != Right.TotalScore)
		{
			return Left.TotalScore > Right.TotalScore;
		}
		return Left.PlayerName < Right.PlayerName;
	});

	PHGameState->SetMatchResultsSnapshot(Snapshot);
	UE_LOG(LogPHMatch, Log,
		TEXT("Finalized authoritative match results: reason=%d objectives=%d players=%d departed=%d."),
		static_cast<int32>(EndReason), Snapshot.CompletedObjectiveCount,
		Snapshot.PlayerRows.Num(), DepartedResultRows.Num());
}

int32 APHGameMode::CountPlayersWithRole(const EPHPlayerRole DesiredRole) const
{
	int32 Count = 0;
	if (GameState == nullptr)
	{
		return Count;
	}

	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		const APHPlayerState* PHPlayerState = Cast<APHPlayerState>(PlayerState);
		if (PHPlayerState != nullptr && PHPlayerState->GetPlayerRole() == DesiredRole)
		{
			++Count;
		}
	}

	return Count;
}

APHGameState* APHGameMode::GetPHGameState() const
{
	return GetGameState<APHGameState>();
}
