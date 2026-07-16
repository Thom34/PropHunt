#include "Game/PHGameMode.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Characters/PHHunterCharacter.h"
#include "Characters/PHPropCharacter.h"
#include "Game/PHGameState.h"
#include "Game/PHMatchRulesDataAsset.h"
#include "Game/PHPlayerController.h"
#include "Game/PHPlayerState.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Objectives/PHObjectiveActor.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHMatch, Log, All);

APHGameMode::APHGameMode()
	: bMatchFlowHasStarted(false)
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
		if (APHPlayerState* NewPlayerState = NewPlayer->GetPlayerState<APHPlayerState>())
		{
			AssignRole(*NewPlayerState);
		}
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

void APHGameMode::Logout(AController* Exiting)
{
	EPHPlayerRole DepartingRole = EPHPlayerRole::Unassigned;
	if (Exiting != nullptr)
	{
		if (const APHPlayerState* DepartingPlayerState = Exiting->GetPlayerState<APHPlayerState>())
		{
			DepartingRole = DepartingPlayerState->GetPlayerRole();
		}
	}

	const int32 RemainingPropPlayerCount = PHMatchFlow::GetRemainingRoleCountAfterDeparture(
		CountPlayersWithRole(EPHPlayerRole::Prop), DepartingRole, EPHPlayerRole::Prop);

	Super::Logout(Exiting);

	const APHGameState* PHGameState = GetPHGameState();
	if (PHGameState == nullptr || PHGameState->GetMatchPhase() == EPHMatchPhase::Lobby || PHGameState->GetMatchPhase() == EPHMatchPhase::Results)
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
	if (CountPlayersWithRole(EPHPlayerRole::Hunter) == 1
		&& CountPlayersWithRole(EPHPlayerRole::Prop) >= MinimumPropPlayers)
	{
		bMatchFlowHasStarted = true;
		BeginPhase(EPHMatchPhase::Preparation);
	}
}

void APHGameMode::AssignRole(APHPlayerState& NewPlayerState)
{
	if (NewPlayerState.GetPlayerRole() != EPHPlayerRole::Unassigned)
	{
		return;
	}

	const EPHPlayerRole NewRole = CountPlayersWithRole(EPHPlayerRole::Hunter) == 0
		? EPHPlayerRole::Hunter
		: EPHPlayerRole::Prop;

	NewPlayerState.SetPlayerRole(NewRole);
	UE_LOG(LogPHMatch, Log, TEXT("Assigned role %s to %s."),
		NewRole == EPHPlayerRole::Hunter ? TEXT("Hunter") : TEXT("Prop"),
		*NewPlayerState.GetPlayerName());
}

void APHGameMode::BeginPhase(const EPHMatchPhase NewPhase)
{
	APHGameState* PHGameState = GetPHGameState();
	if (PHGameState == nullptr)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(PhaseTimerHandle);

	const UPHMatchRulesDataAsset* Rules = GetMatchRules();
	float PhaseDuration = 0.0f;
	switch (NewPhase)
	{
	case EPHMatchPhase::Preparation:
		PhaseDuration = Rules->PreparationDuration;
		PHGameState->SetMatchSeed(FMath::Rand());
		PHGameState->SetCompletedObjectiveCount(0);
		PHGameState->SetMatchEndReason(EPHMatchEndReason::None);
		PrepareObjectivesForMatch();
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
		PhaseDuration = Rules->ResultsDuration;
		break;
	case EPHMatchPhase::Lobby:
		bMatchFlowHasStarted = false;
		break;
	case EPHMatchPhase::Escape:
	default:
		break;
	}

	PHGameState->SetMatchPhase(NewPhase, PhaseDuration);
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

	BeginPhase(PHMatchFlow::GetNextTimedPhase(CurrentPhase));
}

void APHGameMode::FinishMatch(const EPHMatchEndReason EndReason)
{
	if (APHGameState* PHGameState = GetPHGameState())
	{
		PHGameState->SetMatchEndReason(EndReason);
		BeginPhase(EPHMatchPhase::Results);
	}
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
