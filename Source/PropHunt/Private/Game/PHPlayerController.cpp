#include "Game/PHPlayerController.h"

#include "Characters/PHHunterCharacter.h"
#include "Characters/PHPropCharacter.h"
#include "Camera/CameraTypes.h"
#include "Components/InputComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Game/PHGameState.h"
#include "Game/PHGameMode.h"
#include "Game/PHFrontendGameMode.h"
#include "Game/PHPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/PHSessionSubsystem.h"
#include "Gameplay/Capture/PHRetentionPoint.h"
#include "Gameplay/Characters/PHHumanPrototypeSelector.h"
#include "Gameplay/Objectives/PHObjectiveActor.h"
#include "Gameplay/Escape/PHExitGate.h"
#include "Gameplay/Transformation/PHPropFormDataAsset.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"
#include "UI/PHHumanStaminaWidget.h"
#include "UI/PHInGameMenuWidget.h"
#include "UI/PHMatchResultsWidget.h"
#include "UI/PHMatchmakingWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHObjectiveSmoke, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogPHCaptureSmoke, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogPHExitGateSmoke, Log, All);

namespace
{
APHRetentionPoint* FindFirstRetentionPoint(UWorld* World)
{
	if (World == nullptr)
	{
		return nullptr;
	}

	TActorIterator<APHRetentionPoint> PointIterator(World);
	return PointIterator ? *PointIterator : nullptr;
}

#if !UE_BUILD_SHIPPING
bool PlacePropAtExitGateControlBox(APHPropCharacter& Prop, APHExitGate& Gate)
{
	UWorld* World = Prop.GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const FVector InteractionPoint = Gate.GetInteractionPoint();
	FVector Outward = InteractionPoint - Gate.GetActorLocation();
	Outward.Z = 0.0f;
	Outward = Outward.GetSafeNormal();
	if (Outward.IsNearlyZero())
	{
		Outward = Gate.GetActorRightVector();
		Outward.Z = 0.0f;
		Outward.Normalize();
	}

	const FVector Directions[] =
	{
		Outward,
		-Outward,
		FVector::CrossProduct(FVector::UpVector, Outward).GetSafeNormal(),
		-FVector::CrossProduct(FVector::UpVector, Outward).GetSafeNormal()
	};
	const float Distances[] = { 115.0f, 170.0f };
	const float CapsuleHalfHeight = Prop.GetCapsuleComponent() != nullptr
		? Prop.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 88.0f;

	for (const float Distance : Distances)
	{
		for (const FVector& Direction : Directions)
		{
			FVector Candidate = InteractionPoint + Direction * Distance;
			FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(PHExitGateSmokeFloor), true, &Prop);
			FloorQuery.AddIgnoredActor(&Gate);
			FHitResult FloorHit;
			if (World->LineTraceSingleByChannel(
				FloorHit,
				Candidate + FVector(0.0f, 0.0f, 400.0f),
				Candidate - FVector(0.0f, 0.0f, 600.0f),
				ECC_Visibility,
				FloorQuery))
			{
				Candidate.Z = FloorHit.ImpactPoint.Z + CapsuleHalfHeight;
			}
			else
			{
				Candidate.Z = InteractionPoint.Z - 45.0f;
			}

			Prop.SetActorLocation(Candidate, false, nullptr, ETeleportType::TeleportPhysics);
			if (UCharacterMovementComponent* Movement = Prop.GetCharacterMovement())
			{
				Movement->StopMovementImmediately();
			}
			Prop.ForceNetUpdate();
			if (Gate.CanPropInteract(Prop))
			{
				return true;
			}
		}
	}

	return false;
}
#endif
}

APHPlayerController::APHPlayerController()
{
	bShowMouseCursor = false;
}

void APHPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		EnsureLocalHumanStaminaWidget();
		EnsureLocalMatchmakingWidget();
		EnsureLocalMatchResultsWidget();

#if !UE_BUILD_SHIPPING
		if (FParse::Param(FCommandLine::Get(), TEXT("PHResultsPreview")))
		{
			SeedMatchResultsPreview();
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("PHObjectiveSmoke")))
		{
			GetWorldTimerManager().SetTimerForNextTick(this, &APHPlayerController::TryRunObjectiveSmoke);
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("PHObjectiveRegressionSmokeHost")))
		{
			GetWorldTimerManager().SetTimerForNextTick(this, &APHPlayerController::TryRunObjectiveRegressionSmoke);
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("PHSpectatorSmokeHost")))
		{
			GetWorldTimerManager().SetTimerForNextTick(this, &APHPlayerController::TryRunSpectatorSmokeHost);
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("PHExitGateSmokeHost")))
		{
			GetWorldTimerManager().SetTimerForNextTick(this, &APHPlayerController::TryRunExitGateSmokeHost);
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("PHCaptureSmokeHost")))
		{
			GetWorldTimerManager().SetTimerForNextTick(this, &APHPlayerController::TryRunCaptureSmokeHost);
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("PHCaptureSmokeVictim")))
		{
			RequestedCaptureSmokeRole = 1;
			GetWorldTimerManager().SetTimerForNextTick(this, &APHPlayerController::TryRegisterCaptureSmokeRole);
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("PHCaptureSmokeRescuer")))
		{
			RequestedCaptureSmokeRole = 2;
			GetWorldTimerManager().SetTimerForNextTick(this, &APHPlayerController::TryRegisterCaptureSmokeRole);
		}
#endif

	}

#if !UE_BUILD_SHIPPING
	if (HasAuthority() && !IsLocalController()
		&& (FParse::Param(FCommandLine::Get(), TEXT("PHSpectatorSmokeHost"))
			|| FParse::Param(FCommandLine::Get(), TEXT("PHExitGateSmokeHost"))))
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("PHExitGateSmokeHost")))
		{
			GetWorldTimerManager().SetTimerForNextTick(this, &APHPlayerController::TryRunExitGateSmokeHost);
		}
		else
		{
			GetWorldTimerManager().SetTimerForNextTick(this, &APHPlayerController::TryRunSpectatorSmokeHost);
		}
	}
#endif
}

void APHPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent != nullptr)
	{
		InputComponent->BindAction(TEXT("ToggleMenu"), IE_Pressed, this, &APHPlayerController::ToggleInGameMenu);
		InputComponent->BindAction(TEXT("LobbyReady"), IE_Pressed, this, &APHPlayerController::ToggleLobbyReady);
		FInputActionBinding& SpectateBinding = InputComponent->BindAction(
			TEXT("SpectateNext"), IE_Pressed, this, &APHPlayerController::SpectateNextSurvivor);
		// Space is also the possessed pawn's Jump action. The handler is already gated by spectator state,
		// so let a live pawn receive the same key event.
		SpectateBinding.bConsumeInput = false;
	}
}

void APHPlayerController::ToggleLobbyReady()
{
	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	const APHPlayerState* PHPlayerState = GetPlayerState<APHPlayerState>();
	if (!IsLocalController() || GameState == nullptr || PHPlayerState == nullptr
		|| GameState->GetMatchPhase() != EPHMatchPhase::Lobby
		|| GameState->PlayerArray.Num() != 2
		|| PHPlayerState->GetPlayerRole() != EPHPlayerRole::Unassigned)
	{
		return;
	}

	ServerSetLobbyReady(!PHPlayerState->IsLobbyReady());
}

void APHPlayerController::ServerSetLobbyReady_Implementation(const bool bReady)
{
	if (!HasAuthority() || GetWorld() == nullptr)
	{
		return;
	}

	const double NowSeconds = GetWorld()->GetTimeSeconds();
	if (LastLobbyReadyRequestTimeSeconds >= 0.0
		&& NowSeconds - LastLobbyReadyRequestTimeSeconds < 0.25)
	{
		return;
	}
	// Rate-limit every request, including invalid hostile requests, before the
	// more detailed lobby-state validation below.
	LastLobbyReadyRequestTimeSeconds = NowSeconds;

	APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	APHPlayerState* PHPlayerState = GetPlayerState<APHPlayerState>();
	if (GetNetMode() != NM_DedicatedServer || GameState == nullptr || PHPlayerState == nullptr
		|| GameState->GetMatchPhase() != EPHMatchPhase::Lobby
		|| GameState->PlayerArray.Num() != 2
		|| PHPlayerState->GetPlayerRole() != EPHPlayerRole::Unassigned)
	{
		return;
	}

	PHPlayerState->SetLobbyReady(bReady);
	if (APHGameMode* GameMode = GetWorld()->GetAuthGameMode<APHGameMode>())
	{
		GameMode->NotifyLobbyReadyStateChanged();
	}
}

#if UE_BUILD_SHIPPING
void APHPlayerController::ServerRegisterCaptureSmokeRole_Implementation(uint8 SmokeRole)
{
}

void APHPlayerController::ClientPrepareCaptureSmokeVictim_Implementation()
{
}

void APHPlayerController::ClientVerifyCaptureSmokeCameraAndSound_Implementation()
{
}

void APHPlayerController::ServerReportCaptureSmokeClientChecks_Implementation(
	bool bJumpSucceeded, bool bCameraSucceeded, bool bSoundSucceeded)
{
}

void APHPlayerController::ClientBeginCaptureSmokeRescue_Implementation()
{
}

void APHPlayerController::ClientBeginCaptureSmokeStruggle_Implementation(uint8 InputCount)
{
}

void APHPlayerController::ClientVerifySpectatorSmoke_Implementation()
{
}

void APHPlayerController::ClientVerifyExitGateSmoke_Implementation(
	APHExitGate* FirstGate, APHExitGate* SecondGate, bool bExpectOpen)
{
}
#endif

void APHPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SpectatedProp.IsValid())
	{
		SpectatedProp->SetLocallySpectated(false);
	}
	if (MatchmakingWidget != nullptr)
	{
		MatchmakingWidget->RemoveFromParent();
		MatchmakingWidget = nullptr;
	}
	if (HumanStaminaWidget != nullptr)
	{
		HumanStaminaWidget->RemoveFromParent();
		HumanStaminaWidget = nullptr;
	}
	if (MatchResultsWidget != nullptr)
	{
		MatchResultsWidget->RemoveFromParent();
		MatchResultsWidget = nullptr;
	}
	if (InGameMenuWidget != nullptr)
	{
		InGameMenuWidget->RemoveFromParent();
		InGameMenuWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void APHPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	NotifyControlledPawnChanged();
}

void APHPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	NotifyControlledPawnChanged();
}

EPHPlayerRole APHPlayerController::GetControlledPlayerRole() const
{
	const APHPlayerState* PHPlayerState = GetPlayerState<APHPlayerState>();
	return PHPlayerState != nullptr ? PHPlayerState->GetPlayerRole() : EPHPlayerRole::Unassigned;
}

void APHPlayerController::DismissLocalMatchmakingWidget()
{
	if (!IsLocalController())
	{
		return;
	}

	if (MatchmakingWidget != nullptr)
	{
		UPHMatchmakingWidget* WidgetToRemove = MatchmakingWidget;
		MatchmakingWidget = nullptr;
		WidgetToRemove->RemoveFromParent();
	}

	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
}

void APHPlayerController::ToggleInGameMenu()
{
	if (!IsLocalController())
	{
		return;
	}

	if (InGameMenuWidget != nullptr)
	{
		CloseInGameMenu();
		return;
	}

	InGameMenuWidget = CreateWidget<UPHInGameMenuWidget>(this, UPHInGameMenuWidget::StaticClass());
	if (InGameMenuWidget == nullptr)
	{
		return;
	}

	InGameMenuWidget->AddToViewport(200);
	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(InGameMenuWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
}

void APHPlayerController::CloseInGameMenu()
{
	if (!IsLocalController())
	{
		return;
	}

	if (InGameMenuWidget != nullptr)
	{
		UPHInGameMenuWidget* WidgetToRemove = InGameMenuWidget;
		InGameMenuWidget = nullptr;
		WidgetToRemove->RemoveFromParent();
	}

	if (MatchmakingWidget != nullptr && MatchmakingWidget->IsInViewport())
	{
		bShowMouseCursor = true;
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(MatchmakingWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
	}
	else
	{
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());
	}
}

void APHPlayerController::ClientReturnToLobbyAfterMatch_Implementation(
	const FPHMatchResultsSnapshot& FinalResults,
	const EPHPlayerRole LocalPlayerRole,
	const FString& LocalPlayerName,
	const int32 LocalPlayerId)
{
	if (!IsLocalController() || bFinalResultsTransitionStarted || !FinalResults.bFinalized)
	{
		return;
	}
	bFinalResultsTransitionStarted = true;

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UPHSessionSubsystem* SessionSubsystem = GameInstance->GetSubsystem<UPHSessionSubsystem>())
		{
			SessionSubsystem->CacheMatchResults(
				FinalResults, LocalPlayerRole, LocalPlayerName, LocalPlayerId);
			UE_LOG(LogPHCaptureSmoke, Log, TEXT("Final snapshot received; leaving the match map for the dedicated results map."));
			SessionSubsystem->LeaveSessionAndShowResults();
		}
	}
}

void APHPlayerController::HandleReplicatedFinalResults(const FPHMatchResultsSnapshot& FinalResults)
{
	if (!IsLocalController() || bFinalResultsTransitionStarted || !FinalResults.bFinalized)
	{
		return;
	}

	const APHPlayerState* LocalPlayerState = GetPlayerState<APHPlayerState>();
	ClientReturnToLobbyAfterMatch_Implementation(
		FinalResults,
		LocalPlayerState != nullptr ? LocalPlayerState->GetPlayerRole() : EPHPlayerRole::Unassigned,
		LocalPlayerState != nullptr ? LocalPlayerState->GetPlayerName() : FString(),
		LocalPlayerState != nullptr ? LocalPlayerState->GetPlayerId() : INDEX_NONE);
}

void APHPlayerController::NotifyControlledPawnChanged()
{
	EnsureLocalHumanStaminaWidget();
	EnsureLocalMatchResultsWidget();
	BP_OnControlledPawnChanged(GetPawn(), GetControlledPlayerRole());
	if (APHPropCharacter* PropCharacter = Cast<APHPropCharacter>(GetPawn()))
	{
		HandlePropCaptureStateChanged(*PropCharacter);
	}
}

void APHPlayerController::HandlePropCaptureStateChanged(APHPropCharacter& ChangedProp)
{
	if (!IsLocalController())
	{
		return;
	}

	APHPropCharacter* OwnedProp = Cast<APHPropCharacter>(GetPawn());
	const bool bOwnedPropTerminal = OwnedProp != nullptr
		&& PHCaptureFlow::IsTerminal(OwnedProp->GetCaptureState());
	if (!bOwnedPropTerminal)
	{
		if (bSpectatingProps)
		{
			bSpectatingProps = false;
			if (SpectatedProp.IsValid())
			{
				SpectatedProp->SetLocallySpectated(false);
			}
			SpectatedProp.Reset();
			SetIgnoreMoveInput(false);
			SetIgnoreLookInput(false);
			if (OwnedProp != nullptr)
			{
				SetViewTarget(OwnedProp);
			}
		}
		return;
	}

	const bool bEnteringSpectator = !bSpectatingProps;
	bSpectatingProps = true;
	if (bEnteringSpectator)
	{
		SetIgnoreMoveInput(true);
		SetIgnoreLookInput(true);
		if (InGameMenuWidget == nullptr)
		{
			bShowMouseCursor = false;
			SetInputMode(FInputModeGameOnly());
		}
	}

	const bool bCurrentTargetBecameTerminal = SpectatedProp.Get() == &ChangedProp
		&& PHCaptureFlow::IsTerminal(ChangedProp.GetCaptureState());
	RefreshSpectatorTarget(bCurrentTargetBecameTerminal);
	UE_LOG(LogPHCaptureSmoke, Log, TEXT("Inactive survivor spectator target: %s."),
		SpectatedProp.IsValid() ? *SpectatedProp->GetName() : TEXT("none"));
}

void APHPlayerController::SpectateNextSurvivor()
{
	if (bSpectatingProps)
	{
		RefreshSpectatorTarget(true);
	}
}

void APHPlayerController::RefreshSpectatorTarget(const bool bAdvance)
{
	if (!IsLocalController() || !bSpectatingProps || GetWorld() == nullptr)
	{
		return;
	}

	const APHPropCharacter* OwnedProp = Cast<APHPropCharacter>(GetPawn());
	TArray<APHPropCharacter*> Candidates;
	for (TActorIterator<APHPropCharacter> PropIterator(GetWorld()); PropIterator; ++PropIterator)
	{
		APHPropCharacter* Candidate = *PropIterator;
		const APHPlayerState* CandidatePlayerState = Candidate != nullptr
			? Candidate->GetPlayerState<APHPlayerState>()
			: nullptr;
		if (Candidate != nullptr && Candidate != OwnedProp
			&& !PHCaptureFlow::IsTerminal(Candidate->GetCaptureState())
			&& CandidatePlayerState != nullptr
			&& CandidatePlayerState->GetPlayerRole() == EPHPlayerRole::Prop)
		{
			Candidates.Add(Candidate);
		}
	}

	Candidates.Sort([](const APHPropCharacter& Left, const APHPropCharacter& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});

	if (Candidates.IsEmpty())
	{
		if (SpectatedProp.IsValid())
		{
			SpectatedProp->SetLocallySpectated(false);
		}
		SpectatedProp.Reset();
		if (OwnedProp != nullptr)
		{
			SetViewTarget(const_cast<APHPropCharacter*>(OwnedProp));
		}
		return;
	}

	int32 CandidateIndex = Candidates.IndexOfByKey(SpectatedProp.Get());
	if (CandidateIndex == INDEX_NONE)
	{
		CandidateIndex = 0;
	}
	else if (bAdvance)
	{
		CandidateIndex = (CandidateIndex + 1) % Candidates.Num();
	}

	APHPropCharacter* NewTarget = Candidates[CandidateIndex];
	if (SpectatedProp.Get() != NewTarget || GetViewTarget() != NewTarget)
	{
		if (SpectatedProp.IsValid() && SpectatedProp.Get() != NewTarget)
		{
			SpectatedProp->SetLocallySpectated(false);
		}
		SpectatedProp = NewTarget;
		NewTarget->SetLocallySpectated(true);
		SetViewTargetWithBlend(NewTarget, 0.25f, VTBlend_Cubic);
	}
}

void APHPlayerController::EnsureLocalMatchmakingWidget()
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UPHSessionSubsystem* Sessions = GameInstance != nullptr
		? GameInstance->GetSubsystem<UPHSessionSubsystem>()
		: nullptr;
	if (!IsLocalController() || MatchmakingWidget != nullptr || GetNetMode() != NM_Standalone
		|| (Sessions != nullptr && Sessions->IsCurrentWorldResultsMap())
		|| GetLocalPlayer() == nullptr || GetWorld() == nullptr || GetWorld()->GetGameViewport() == nullptr)
	{
		return;
	}

	TSubclassOf<UPHMatchmakingWidget> WidgetClass = MatchmakingWidgetClass.LoadSynchronous();
	if (WidgetClass == nullptr)
	{
		WidgetClass = UPHMatchmakingWidget::StaticClass();
	}
	MatchmakingWidget = CreateWidget<UPHMatchmakingWidget>(this, WidgetClass);
	if (MatchmakingWidget != nullptr)
	{
		MatchmakingWidget->AddToViewport(100);
	}
}

void APHPlayerController::EnsureLocalHumanStaminaWidget()
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UPHSessionSubsystem* Sessions = GameInstance != nullptr
		? GameInstance->GetSubsystem<UPHSessionSubsystem>()
		: nullptr;
	if (!IsLocalController() || HumanStaminaWidget != nullptr || GetLocalPlayer() == nullptr
		|| (Sessions != nullptr && Sessions->IsCurrentWorldFrontendMap())
		|| GetWorld() == nullptr || GetWorld()->GetGameViewport() == nullptr)
	{
		return;
	}

	HumanStaminaWidget = CreateWidget<UPHHumanStaminaWidget>(this, UPHHumanStaminaWidget::StaticClass());
	if (HumanStaminaWidget != nullptr)
	{
		HumanStaminaWidget->AddToViewport(10);
	}
}

void APHPlayerController::EnsureLocalMatchResultsWidget()
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UPHSessionSubsystem* Sessions = GameInstance != nullptr
		? GameInstance->GetSubsystem<UPHSessionSubsystem>()
		: nullptr;
	if (!IsLocalController() || MatchResultsWidget != nullptr || GetLocalPlayer() == nullptr
		|| Sessions == nullptr || !Sessions->IsCurrentWorldResultsMap()
		|| GetWorld() == nullptr || GetWorld()->GetGameViewport() == nullptr)
	{
		return;
	}

	MatchResultsWidget = CreateWidget<UPHMatchResultsWidget>(this, UPHMatchResultsWidget::StaticClass());
	if (MatchResultsWidget != nullptr)
	{
		MatchResultsWidget->AddToViewport(150);
		bShowMouseCursor = true;
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(MatchResultsWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
	}
}

#if !UE_BUILD_SHIPPING
void APHPlayerController::SeedMatchResultsPreview()
{
	if (!IsLocalController() || GetNetMode() != NM_Standalone)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UPHSessionSubsystem* Sessions = GameInstance != nullptr
		? GameInstance->GetSubsystem<UPHSessionSubsystem>()
		: nullptr;
	if (Sessions == nullptr || !Sessions->IsCurrentWorldResultsMap())
	{
		return;
	}

	FPHMatchResultsSnapshot Snapshot;
	Snapshot.bFinalized = true;
	Snapshot.MatchSeed = 20260718;
	Snapshot.EndReason = EPHMatchEndReason::AllPropsGone;
	Snapshot.CompletedObjectiveCount = 3;

	FPHMatchResultRow& Hunter = Snapshot.PlayerRows.AddDefaulted_GetRef();
	Hunter.PlayerId = 101;
	Hunter.PlayerName = TEXT("CHASSEUR");
	Hunter.PlayerRole = EPHPlayerRole::Hunter;
	Hunter.Outcome = EPHMatchPlayerOutcome::Hunter;
	Hunter.Stats.HunterDowns = 5;
	Hunter.Stats.HunterRetentions = 3;
	Hunter.Stats.HunterEliminations = 2;
	Hunter.TotalScore = 7250;
	Hunter.GrantedScore = 7250;

	FPHMatchResultRow& FirstProp = Snapshot.PlayerRows.AddDefaulted_GetRef();
	FirstProp.PlayerId = 102;
	FirstProp.PlayerName = TEXT("SURVIVANT ALPHA");
	FirstProp.PlayerRole = EPHPlayerRole::Prop;
	FirstProp.Outcome = EPHMatchPlayerOutcome::Eliminated;
	FirstProp.Stats.ObjectivesCompleted = 2;
	FirstProp.Stats.AlliesRescued = 1;
	FirstProp.TotalScore = 2750;
	FirstProp.GrantedScore = 2750;

	FPHMatchResultRow& SecondProp = Snapshot.PlayerRows.AddDefaulted_GetRef();
	SecondProp.PlayerId = 103;
	SecondProp.PlayerName = TEXT("SURVIVANT BRAVO");
	SecondProp.PlayerRole = EPHPlayerRole::Prop;
	SecondProp.Outcome = EPHMatchPlayerOutcome::Disconnected;
	SecondProp.Stats.ObjectivesCompleted = 1;
	SecondProp.TotalScore = 1000;
	SecondProp.GrantedScore = 0;

	Sessions->CacheMatchResults(Snapshot, EPHPlayerRole::Hunter, Hunter.PlayerName, Hunter.PlayerId);
	if (APHFrontendGameMode* FrontendGameMode = GetWorld() != nullptr
		? GetWorld()->GetAuthGameMode<APHFrontendGameMode>()
		: nullptr)
	{
		FrontendGameMode->RefreshResultsPresentation();
	}
	UE_LOG(LogPHCaptureSmoke, Log, TEXT("Seeded non-shipping post-match results preview."));
	if (FParse::Param(FCommandLine::Get(), TEXT("PHResultsPreviewAutoContinue")))
	{
		FTimerHandle PreviewContinueTimer;
		GetWorldTimerManager().SetTimer(
			PreviewContinueTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (UGameInstance* PreviewGameInstance = GetGameInstance())
				{
					if (UPHSessionSubsystem* PreviewSessions = PreviewGameInstance->GetSubsystem<UPHSessionSubsystem>())
					{
						UE_LOG(LogPHCaptureSmoke, Log, TEXT("Automatically continuing results preview to the lobby."));
						PreviewSessions->ContinueFromResultsToLobby();
					}
				}
			}),
			1.5f,
			false);
	}
}

void APHPlayerController::TryRunObjectiveSmoke()
{
	APHPropCharacter* PropCharacter = Cast<APHPropCharacter>(GetPawn());
	const APHGameState* PHGameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	if (!IsLocalController() || PropCharacter == nullptr || GetControlledPlayerRole() != EPHPlayerRole::Prop
		|| PHGameState == nullptr || PHGameState->GetMatchPhase() != EPHMatchPhase::Hunt)
	{
		if (++ObjectiveSmokeRetryCount <= 80)
		{
			GetWorldTimerManager().SetTimer(
				ObjectiveSmokeRetryTimer, this, &APHPlayerController::TryRunObjectiveSmoke, 0.5f, false);
		}
		else
		{
			UE_LOG(LogPHObjectiveSmoke, Error, TEXT("Objective smoke timed out before a local Prop reached the Hunt phase."));
		}
		return;
	}

	UE_LOG(LogPHObjectiveSmoke, Log, TEXT("Objective smoke starting on local Prop %s."), *PropCharacter->GetName());
	PropCharacter->RequestStartObjectiveInteraction();
	GetWorldTimerManager().SetTimer(
		ObjectiveSmokeFinishTimer, this, &APHPlayerController::FinishObjectiveSmoke, 9.0f, false);
}

void APHPlayerController::FinishObjectiveSmoke()
{
	if (APHPropCharacter* PropCharacter = Cast<APHPropCharacter>(GetPawn()))
	{
		PropCharacter->RequestStopObjectiveInteraction();
	}
	GetWorldTimerManager().SetTimer(ObjectiveSmokeFinishTimer, this, &APHPlayerController::ReportObjectiveSmoke, 1.0f, false);
}

void APHPlayerController::ReportObjectiveSmoke()
{
	const APHGameState* PHGameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	const int32 CompletedCount = PHGameState != nullptr ? PHGameState->GetCompletedObjectiveCount() : -1;
	const EPHMatchPhase MatchPhase = PHGameState != nullptr ? PHGameState->GetMatchPhase() : EPHMatchPhase::Lobby;
	if (CompletedCount >= 1)
	{
		UE_LOG(LogPHObjectiveSmoke, Log, TEXT("Objective smoke finished with completed count %d and phase %d."),
			CompletedCount, static_cast<int32>(MatchPhase));
	}
	else
	{
		UE_LOG(LogPHObjectiveSmoke, Error, TEXT("Objective smoke finished with completed count %d and phase %d."),
			CompletedCount, static_cast<int32>(MatchPhase));
	}
}

void APHPlayerController::TryRunObjectiveRegressionSmoke()
{
	APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn());
	const APHGameState* PHGameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	APHPropCharacter* PropCharacter = nullptr;
	APHObjectiveActor* Objective = nullptr;
	if (GetWorld() != nullptr)
	{
		for (TActorIterator<APHPropCharacter> PropIterator(GetWorld()); PropIterator; ++PropIterator)
		{
			if (IsValid(*PropIterator) && !(*PropIterator)->IsLocallyControlled())
			{
				PropCharacter = *PropIterator;
				break;
			}
		}
		for (TActorIterator<APHObjectiveActor> ObjectiveIterator(GetWorld()); ObjectiveIterator; ++ObjectiveIterator)
		{
			if (IsValid(*ObjectiveIterator) && (*ObjectiveIterator)->IsObjectiveActive())
			{
				Objective = *ObjectiveIterator;
				break;
			}
		}
	}
	if (!IsLocalController() || Hunter == nullptr || PropCharacter == nullptr || Objective == nullptr
		|| PHGameState == nullptr || PHGameState->GetMatchPhase() != EPHMatchPhase::Hunt)
	{
		if (++ObjectiveSmokeRetryCount <= 480)
		{
			GetWorldTimerManager().SetTimer(
				ObjectiveSmokeRetryTimer, this, &APHPlayerController::TryRunObjectiveRegressionSmoke, 0.5f, false);
		}
		else
		{
			UE_LOG(LogPHObjectiveSmoke, Error,
				TEXT("Objective regression smoke timed out before Hunter, remote Prop, objective and Hunt were ready."));
		}
		return;
	}

	FVector PropLocation = Objective->GetActorLocation() - FVector(150.0f, 0.0f, 0.0f);
	PropLocation.Z = 100.0f;
	PropCharacter->SetActorLocation(PropLocation, false);
	PropCharacter->SetActorRotation(FRotator::ZeroRotator);
	ObjectiveRegressionTarget = Objective;
	const bool bStarted = Objective->ServerTryBeginInteraction(*PropCharacter);
	UE_LOG(LogPHObjectiveSmoke, Log,
		TEXT("Objective regression smoke began partial server repair: result=%s."),
		bStarted ? TEXT("success") : TEXT("failure"));
	if (!bStarted)
	{
		UE_LOG(LogPHObjectiveSmoke, Error, TEXT("Objective regression smoke could not start its server repair."));
		return;
	}
	GetWorldTimerManager().SetTimer(
		ObjectiveSmokeFinishTimer, this, &APHPlayerController::FinishObjectiveRegressionInteraction, 3.0f, false);
}

void APHPlayerController::FinishObjectiveRegressionInteraction()
{
	APHObjectiveActor* Objective = ObjectiveRegressionTarget.Get();
	APHPropCharacter* PropCharacter = nullptr;
	if (GetWorld() != nullptr)
	{
		for (TActorIterator<APHPropCharacter> PropIterator(GetWorld()); PropIterator; ++PropIterator)
		{
			if (IsValid(*PropIterator) && !(*PropIterator)->IsLocallyControlled())
			{
				PropCharacter = *PropIterator;
				break;
			}
		}
	}
	if (Objective == nullptr || PropCharacter == nullptr)
	{
		UE_LOG(LogPHObjectiveSmoke, Error, TEXT("Objective regression smoke lost its target or remote Prop."));
		return;
	}

	Objective->ServerEndInteraction(*PropCharacter);
	ObjectiveRegressionProgressBeforeHit = Objective->GetObjectiveProgress();
	PropCharacter->SetActorLocation(Objective->GetActorLocation() + FVector(800.0f, 0.0f, 100.0f), false);
	if (APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn()))
	{
		FVector HunterLocation = Objective->GetActorLocation() - FVector(150.0f, 0.0f, 0.0f);
		HunterLocation.Z = 100.0f;
		Hunter->SetActorLocation(HunterLocation, false);
		const FRotator AimRotation = (Objective->GetInteractionPoint() - Hunter->GetPawnViewLocation()).Rotation();
		Hunter->SetActorRotation(FRotator(0.0f, AimRotation.Yaw, 0.0f));
		SetControlRotation(AimRotation);
		Hunter->RequestMeleeAttack();
	}
	GetWorldTimerManager().SetTimer(
		ObjectiveSmokeFinishTimer, this, &APHPlayerController::ReportObjectiveRegressionSmoke, 0.35f, false);
}

void APHPlayerController::ReportObjectiveRegressionSmoke()
{
	const APHObjectiveActor* Objective = ObjectiveRegressionTarget.Get();
	const float ProgressAfterHit = Objective != nullptr ? Objective->GetObjectiveProgress() : -1.0f;
	const float ExpectedProgress = Objective != nullptr
		? FMath::Max(0.0f, ObjectiveRegressionProgressBeforeHit - Objective->GetHunterMeleeRegressionFraction())
		: -1.0f;
	TArray<UStaticMeshComponent*> ObjectiveMeshes;
	if (Objective != nullptr)
	{
		Objective->GetComponents(ObjectiveMeshes);
	}
	const bool bNoWorldProgressPresentation = Objective != nullptr
		&& Objective->FindComponentByClass<UWidgetComponent>() == nullptr
		&& ObjectiveMeshes.Num() == 1;
	const bool bSucceeded = ObjectiveRegressionProgressBeforeHit >= 0.20f
		&& FMath::IsNearlyEqual(ProgressAfterHit, ExpectedProgress, 0.02f)
		&& bNoWorldProgressPresentation;
	UE_LOG(LogPHObjectiveSmoke, Log,
		TEXT("Objective regression smoke result: progress=%.1f%%->%.1f%% expected=%.1f%% world_progress=%s result=%s."),
		ObjectiveRegressionProgressBeforeHit * 100.0f,
		ProgressAfterHit * 100.0f,
		ExpectedProgress * 100.0f,
		bNoWorldProgressPresentation ? TEXT("absent") : TEXT("present"),
		bSucceeded ? TEXT("success") : TEXT("failure"));
	if (!bSucceeded)
	{
		UE_LOG(LogPHObjectiveSmoke, Error, TEXT("Objective regression smoke failed its runtime assertions."));
	}
}

void APHPlayerController::ServerRegisterCaptureSmokeRole_Implementation(const uint8 SmokeRole)
{
	if (SmokeRole == 1 || SmokeRole == 2)
	{
		CaptureSmokeRole = SmokeRole;
		UE_LOG(LogPHCaptureSmoke, Log, TEXT("Registered capture smoke role %d for %s."),
			static_cast<int32>(SmokeRole), *GetName());
	}
}

void APHPlayerController::TryRegisterCaptureSmokeRole()
{
	if (!IsLocalController() || RequestedCaptureSmokeRole == 0)
	{
		return;
	}

	ServerRegisterCaptureSmokeRole(RequestedCaptureSmokeRole);
}

APHPlayerController* APHPlayerController::FindCaptureSmokeController(const uint8 SmokeRole) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	for (TActorIterator<APHPlayerController> ControllerIterator(World); ControllerIterator; ++ControllerIterator)
	{
		APHPlayerController* Candidate = *ControllerIterator;
		if (IsValid(Candidate) && Candidate->CaptureSmokeRole == SmokeRole)
		{
			return Candidate;
		}
	}
	return nullptr;
}

void APHPlayerController::TryRunCaptureSmokeHost()
{
	APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn());
	APHPlayerController* VictimController = FindCaptureSmokeController(1);
	APHPlayerController* RescuerController = FindCaptureSmokeController(2);
	APHPropCharacter* Victim = VictimController != nullptr ? Cast<APHPropCharacter>(VictimController->GetPawn()) : nullptr;
	APHPropCharacter* Rescuer = RescuerController != nullptr ? Cast<APHPropCharacter>(RescuerController->GetPawn()) : nullptr;
	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	if (!IsLocalController() || Hunter == nullptr || Victim == nullptr || Rescuer == nullptr
		|| GameState == nullptr || GameState->GetMatchPhase() != EPHMatchPhase::Hunt)
	{
		if (++CaptureSmokeRetryCount <= 480)
		{
			GetWorldTimerManager().SetTimer(
				CaptureSmokeTimer, this, &APHPlayerController::TryRunCaptureSmokeHost, 0.5f, false);
		}
		else
		{
			UE_LOG(LogPHCaptureSmoke, Error, TEXT("Capture smoke timed out waiting for Hunter, two Props and Hunt."));
		}
		return;
	}

	CaptureSmokeVictim = Victim;
	APHHumanPrototypeSelector* HumanPrototypeSelector = nullptr;
	TActorIterator<APHHumanPrototypeSelector> SelectorIterator(GetWorld());
	if (SelectorIterator)
	{
		HumanPrototypeSelector = *SelectorIterator;
	}
	if (HumanPrototypeSelector != nullptr)
	{
		Victim->SetActorLocation(
			HumanPrototypeSelector->GetActorLocation() + FVector::UpVector * 100.0f,
			false);
		const bool bSwitchedPrototype = HumanPrototypeSelector->ServerTryUse(*Victim);
		if (bSwitchedPrototype)
		{
			UE_LOG(LogPHCaptureSmoke, Log,
				TEXT("Capture smoke survivor selector: switched=yes prototype=%s."),
				*Victim->GetActiveHumanPrototypeName().ToString());
		}
		else
		{
			UE_LOG(LogPHCaptureSmoke, Error,
				TEXT("Capture smoke survivor selector: switched=no prototype=%s."),
				*Victim->GetActiveHumanPrototypeName().ToString());
		}
	}
	else
	{
		UE_LOG(LogPHCaptureSmoke, Error, TEXT("Capture smoke could not find the human prototype selector."));
	}

	Hunter->SetActorLocation(FVector(-350.0f, 0.0f, 100.0f), false);
	Hunter->SetActorRotation(FRotator::ZeroRotator);
	SetControlRotation(FRotator::ZeroRotator);
	Victim->SetActorLocation(FVector(-200.0f, 0.0f, 100.0f), false);
	Victim->SetActorRotation(FRotator::ZeroRotator);
	VictimController->SetControlRotation(FRotator::ZeroRotator);
	Rescuer->SetActorLocation(FVector(450.0f, 200.0f, 100.0f), false);
	Rescuer->SetActorRotation(FRotator(0.0f, -90.0f, 0.0f));
	RescuerController->SetControlRotation(FRotator(0.0f, -90.0f, 0.0f));
	VictimController->ClientPrepareCaptureSmokeVictim();
	CaptureSmokeHunterJumpStartZ = Hunter->GetActorLocation().Z;
	InputKey(FInputKeyEventArgs(
		nullptr,
		INPUTDEVICEID_NONE,
		EKeys::SpaceBar,
		IE_Pressed,
		FPlatformTime::Cycles64()));
	UE_LOG(LogPHCaptureSmoke, Log,
		TEXT("Capture smoke positioned Hunter, free victim and rescuer, then injected Space on Hunter and victim."));
	GetWorldTimerManager().SetTimer(
		CaptureSmokeInputTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			InputKey(FInputKeyEventArgs(
				nullptr,
				INPUTDEVICEID_NONE,
				EKeys::SpaceBar,
				IE_Released,
				FPlatformTime::Cycles64()));
			GetWorldTimerManager().SetTimer(
				CaptureSmokeInputTimer, this, &APHPlayerController::RunCaptureSmokeVerifyHunterJump, 0.12f, false);
		}),
		0.08f,
		false);
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokePrepareCameraAndSound, 1.2f, false);
}

void APHPlayerController::ClientPrepareCaptureSmokeVictim_Implementation()
{
	SetControlRotation(FRotator::ZeroRotator);
	const APHPropCharacter* PropCharacter = Cast<APHPropCharacter>(GetPawn());
	CaptureSmokeClientJumpStartZ = PropCharacter != nullptr ? PropCharacter->GetActorLocation().Z : 0.0f;
	InputKey(FInputKeyEventArgs(
		nullptr,
		INPUTDEVICEID_NONE,
		EKeys::SpaceBar,
		IE_Pressed,
		FPlatformTime::Cycles64()));
	GetWorldTimerManager().SetTimer(
		CaptureSmokeInputTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			InputKey(FInputKeyEventArgs(
				nullptr,
				INPUTDEVICEID_NONE,
				EKeys::SpaceBar,
				IE_Released,
				FPlatformTime::Cycles64()));
			GetWorldTimerManager().SetTimer(
				CaptureSmokeInputTimer,
				FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					const APHPropCharacter* LocalProp = Cast<APHPropCharacter>(GetPawn());
					const float CurrentZ = LocalProp != nullptr ? LocalProp->GetActorLocation().Z : CaptureSmokeClientJumpStartZ;
					const float VerticalSpeed = LocalProp != nullptr && LocalProp->GetVelocity().Z > 0.0f
						? LocalProp->GetVelocity().Z
						: 0.0f;
					bCaptureSmokeClientJumpSucceeded = LocalProp != nullptr
						&& (CurrentZ >= CaptureSmokeClientJumpStartZ + 5.0f || VerticalSpeed >= 50.0f);
					UE_LOG(LogPHCaptureSmoke, Log,
						TEXT("Capture smoke remote survivor Space jump: z=%.1f->%.1f vz=%.1f result=%s."),
						CaptureSmokeClientJumpStartZ, CurrentZ, VerticalSpeed,
						bCaptureSmokeClientJumpSucceeded ? TEXT("success") : TEXT("failure"));
				}),
				0.12f,
				false);
		}),
		0.08f,
		false);
}

void APHPlayerController::RunCaptureSmokeVerifyHunterJump()
{
	const APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn());
	const float CurrentZ = Hunter != nullptr ? Hunter->GetActorLocation().Z : CaptureSmokeHunterJumpStartZ;
	const float VerticalSpeed = Hunter != nullptr && Hunter->GetVelocity().Z > 0.0f
		? Hunter->GetVelocity().Z
		: 0.0f;
	bCaptureSmokeHunterJumpSucceeded = Hunter != nullptr
		&& (CurrentZ >= CaptureSmokeHunterJumpStartZ + 5.0f || VerticalSpeed >= 50.0f);
	UE_LOG(LogPHCaptureSmoke, Log,
		TEXT("Capture smoke Hunter Space jump: z=%.1f->%.1f vz=%.1f result=%s."),
		CaptureSmokeHunterJumpStartZ, CurrentZ, VerticalSpeed,
		bCaptureSmokeHunterJumpSucceeded ? TEXT("success") : TEXT("failure"));
}

void APHPlayerController::RunCaptureSmokePrepareCameraAndSound()
{
	APHPlayerController* VictimController = FindCaptureSmokeController(1);
	APHPropCharacter* Victim = VictimController != nullptr ? Cast<APHPropCharacter>(VictimController->GetPawn()) : nullptr;
	UPHPropFormDataAsset* ToyBrick = LoadObject<UPHPropFormDataAsset>(
		nullptr,
		TEXT("/Game/PropHunt/Data/Transformation/DA_PH_PropForm_ToyBrick.DA_PH_PropForm_ToyBrick"));
	bCaptureSmokeToyBrickSucceeded = Victim != nullptr && ToyBrick != nullptr
		&& Victim->ForcePropFormForSmoke(*ToyBrick);
	UE_LOG(LogPHCaptureSmoke, Log,
		TEXT("Capture smoke applied the small ToyBrick form for camera validation: result=%s."),
		bCaptureSmokeToyBrickSucceeded ? TEXT("success") : TEXT("failure"));
	GetWorldTimerManager().SetTimer(
		CaptureSmokeInputTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (APHPlayerController* SynchronizedVictimController = FindCaptureSmokeController(1))
			{
				SynchronizedVictimController->ClientVerifyCaptureSmokeCameraAndSound();
			}
		}),
		0.5f,
		false);
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokeMelee, 1.3f, false);
}

void APHPlayerController::ClientVerifyCaptureSmokeCameraAndSound_Implementation()
{
	APHPropCharacter* LocalProp = Cast<APHPropCharacter>(GetPawn());
	FMinimalViewInfo CameraView;
	if (LocalProp != nullptr)
	{
		LocalProp->CalcCamera(0.0f, CameraView);
	}

	FHitResult GroundHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHCaptureSmokeCamera), false, LocalProp);
	const bool bGroundFound = GetWorld() != nullptr && LocalProp != nullptr
		&& GetWorld()->LineTraceSingleByChannel(
			GroundHit,
			CameraView.Location + FVector::UpVector * 100.0f,
			CameraView.Location - FVector::UpVector * 2000.0f,
			ECC_Camera,
			QueryParams);
	const float Clearance = bGroundFound ? CameraView.Location.Z - GroundHit.ImpactPoint.Z : -1.0f;
	const UPHPropFormDataAsset* ActiveForm = LocalProp != nullptr ? LocalProp->GetActivePropForm() : nullptr;
	const bool bCameraSucceeded = ActiveForm != nullptr && ActiveForm->FormId == FName(TEXT("ToyBrick"))
		&& bGroundFound && Clearance + 1.0f >= LocalProp->GetMinimumCameraGroundClearance();
	UE_LOG(LogPHCaptureSmoke, Log,
		TEXT("Capture smoke small-prop camera: form=%s view-z=%.1f ground-z=%.1f clearance=%.1fcm minimum=%.1fcm result=%s."),
		ActiveForm != nullptr ? *ActiveForm->FormId.ToString() : TEXT("None"),
		CameraView.Location.Z,
		bGroundFound ? GroundHit.ImpactPoint.Z : -1.0f,
		Clearance,
		LocalProp != nullptr ? LocalProp->GetMinimumCameraGroundClearance() : -1.0f,
		bCameraSucceeded ? TEXT("success") : TEXT("failure"));

	const int32 PlaybackCountBefore = LocalProp != nullptr ? LocalProp->GetSoundEmoteSmokePlaybackCount() : 0;
	if (LocalProp != nullptr)
	{
		LocalProp->RequestSoundEmoteForSmoke(3);
		LocalProp->RequestSoundEmoteForSmoke(3);
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeInputTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, bCameraSucceeded, PlaybackCountBefore]()
		{
			const APHPropCharacter* SynchronizedProp = Cast<APHPropCharacter>(GetPawn());
			const int32 PlaybackCountAfter = SynchronizedProp != nullptr
				? SynchronizedProp->GetSoundEmoteSmokePlaybackCount()
				: PlaybackCountBefore;
			const bool bSoundSucceeded = PlaybackCountAfter == PlaybackCountBefore + 1;
			UE_LOG(LogPHCaptureSmoke, Log,
				TEXT("Capture smoke remote spatial sound multicast and cooldown: playbacks=%d->%d result=%s."),
				PlaybackCountBefore, PlaybackCountAfter,
				bSoundSucceeded ? TEXT("success") : TEXT("failure"));
			ServerReportCaptureSmokeClientChecks(
				bCaptureSmokeClientJumpSucceeded,
				bCameraSucceeded,
				bSoundSucceeded);
		}),
		0.4f,
		false);
}

void APHPlayerController::ServerReportCaptureSmokeClientChecks_Implementation(
	const bool bJumpSucceeded,
	const bool bCameraSucceeded,
	const bool bSoundSucceeded)
{
	bCaptureSmokeVictimJumpSucceeded = bJumpSucceeded;
	bCaptureSmokeCameraSucceeded = bCameraSucceeded;
	bCaptureSmokeSoundSucceeded = bSoundSucceeded;
	UE_LOG(LogPHCaptureSmoke, Log,
		TEXT("Capture smoke received remote checks: jump=%s camera=%s sound=%s."),
		bJumpSucceeded ? TEXT("success") : TEXT("failure"),
		bCameraSucceeded ? TEXT("success") : TEXT("failure"),
		bSoundSucceeded ? TEXT("success") : TEXT("failure"));
}

void APHPlayerController::RunCaptureSmokeMelee()
{
	if (APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn()))
	{
		if (APHPropCharacter* Victim = CaptureSmokeVictim.Get())
		{
			FVector VictimLocation = Victim->GetActorLocation();
			VictimLocation.Z = 100.0f;
			Victim->SetActorLocation(VictimLocation, false);
			Hunter->SetActorLocation(VictimLocation - FVector(90.0f, 0.0f, 0.0f), false);
			const FRotator AimRotation = (
				Victim->GetMeleeLineOfSightPoint(Hunter->GetPawnViewLocation()) - Hunter->GetPawnViewLocation()).Rotation();
			Hunter->SetActorRotation(FRotator(0.0f, AimRotation.Yaw, 0.0f));
			SetControlRotation(AimRotation);
		}
		Hunter->RequestMeleeAttack();
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokeCarry, 0.35f, false);
}

void APHPlayerController::RunCaptureSmokeCarry()
{
	if (APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn()))
	{
		if (CaptureSmokeVictim != nullptr)
		{
			Hunter->SetActorLocation(
				CaptureSmokeVictim->GetActorLocation() - FVector(75.0f, 0.0f, 0.0f),
				false);
			Hunter->SetActorRotation(FRotator::ZeroRotator);
			SetControlRotation(FRotator::ZeroRotator);
		}
		Hunter->RequestCaptureInteraction();
		if (Hunter->IsUsingCarryThirdPersonCamera())
		{
			UE_LOG(LogPHCaptureSmoke, Log, TEXT("Capture smoke Hunter carry view: mode=TPS anchor=%s."),
				*Hunter->GetCarryAnchorOffset().ToCompactString());
		}
		else
		{
			UE_LOG(LogPHCaptureSmoke, Error, TEXT("Capture smoke Hunter carry view: mode=FPS anchor=%s."),
				*Hunter->GetCarryAnchorOffset().ToCompactString());
		}
		InputKey(FInputKeyEventArgs(
			nullptr,
			INPUTDEVICEID_NONE,
			EKeys::W,
			IE_Pressed,
			FPlatformTime::Cycles64()));
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokeVerifyCarryMove, 0.35f, false);
}

void APHPlayerController::RunCaptureSmokeVerifyCarryMove()
{
	if (APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn()))
	{
		APHPropCharacter* Victim = CaptureSmokeVictim.Get();
		bCaptureSmokeCarryMoveSucceeded = Victim != nullptr
			&& Victim->IsPlayingCarriedMoveAnimation();
		InputKey(FInputKeyEventArgs(
			nullptr,
			INPUTDEVICEID_NONE,
			EKeys::W,
			IE_Released,
			FPlatformTime::Cycles64()));
		if (bCaptureSmokeCarryMoveSucceeded)
		{
			UE_LOG(LogPHCaptureSmoke, Log,
				TEXT("Capture smoke synchronized carried move animation: result=success."));
		}
		else
		{
			UE_LOG(LogPHCaptureSmoke, Error,
				TEXT("Capture smoke synchronized carried move animation: result=failure."));
		}
		if (UCharacterMovementComponent* Movement = Hunter->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		CaptureSmokeHunterLocationBeforeStruggle = Hunter->GetActorLocation();
		if (APHPlayerController* VictimController = FindCaptureSmokeController(1))
		{
			VictimController->ClientBeginCaptureSmokeStruggle(15);
		}
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokeVerifyFreeShove, 2.2f, false);
}

void APHPlayerController::RunCaptureSmokeVerifyFreeShove()
{
	if (APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn()))
	{
		const FVector ShoveDelta = Hunter->GetActorLocation() - CaptureSmokeHunterLocationBeforeStruggle;
		const float LateralDistance = FMath::Abs(FVector::DotProduct(
			ShoveDelta, Hunter->GetActorRightVector()));
		bCaptureSmokeFreeShoveSucceeded = LateralDistance >= 50.0f;
		if (bCaptureSmokeFreeShoveSucceeded)
		{
			UE_LOG(LogPHCaptureSmoke, Log,
				TEXT("Capture smoke free carry struggle shove: lateral=%.1fcm result=success."),
				LateralDistance);
		}
		else
		{
			UE_LOG(LogPHCaptureSmoke, Error,
				TEXT("Capture smoke free carry struggle shove: lateral=%.1fcm result=failure."),
				LateralDistance);
		}

		if (APHPropCharacter* Victim = CaptureSmokeVictim.Get())
		{
			CaptureSmokeRecoveryBeforeDrop = Victim->GetDownedRecoveryProgressNormalized();
			CaptureSmokeStruggleBeforeDrop = Victim->GetCarryStruggleProgressNormalized();
			Hunter->RequestCaptureInteraction();
		}
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokeVerifyDropProgress, 0.35f, false);
}

void APHPlayerController::RunCaptureSmokeVerifyDropProgress()
{
	APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn());
	APHPropCharacter* Victim = CaptureSmokeVictim.Get();
	if (Hunter != nullptr && Victim != nullptr)
	{
		const float RecoveryAfterDrop = Victim->GetDownedRecoveryProgressNormalized();
		const float StruggleAfterDrop = Victim->GetCarryStruggleProgressNormalized();
		bCaptureSmokeDropProgressSucceeded = Victim->GetCaptureState() == EPHPropCaptureState::Downed
			&& RecoveryAfterDrop + KINDA_SMALL_NUMBER
				>= FMath::Min(1.0f, CaptureSmokeRecoveryBeforeDrop + Victim->GetCarryDropRecoveryBonus())
			&& StruggleAfterDrop + KINDA_SMALL_NUMBER >= CaptureSmokeStruggleBeforeDrop;
		UE_LOG(LogPHCaptureSmoke,
			Log,
			TEXT("Capture smoke drop progress: recovery %.3f->%.3f struggle %.3f->%.3f result=%s."),
			CaptureSmokeRecoveryBeforeDrop,
			RecoveryAfterDrop,
			CaptureSmokeStruggleBeforeDrop,
			StruggleAfterDrop,
			bCaptureSmokeDropProgressSucceeded ? TEXT("success") : TEXT("failure"));

		CaptureSmokeRecoveryBeforeRepick = RecoveryAfterDrop;
		CaptureSmokeStruggleBeforeRepick = StruggleAfterDrop;
		Hunter->SetActorLocation(Victim->GetActorLocation() - FVector(75.0f, 0.0f, 0.0f), false);
		Hunter->SetActorRotation(FRotator::ZeroRotator);
		SetControlRotation(FRotator::ZeroRotator);
		Hunter->RequestCaptureInteraction();
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokeVerifyRepickProgress, 0.35f, false);
}

void APHPlayerController::RunCaptureSmokeVerifyRepickProgress()
{
	APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn());
	APHPropCharacter* Victim = CaptureSmokeVictim.Get();
	if (Hunter != nullptr && Victim != nullptr)
	{
		const float RecoveryAfterRepick = Victim->GetDownedRecoveryProgressNormalized();
		const float StruggleAfterRepick = Victim->GetCarryStruggleProgressNormalized();
		bCaptureSmokeRepickProgressSucceeded = Victim->GetCaptureState() == EPHPropCaptureState::Carried
			&& FMath::IsNearlyEqual(RecoveryAfterRepick, CaptureSmokeRecoveryBeforeRepick, 0.01f)
			&& FMath::IsNearlyEqual(StruggleAfterRepick, CaptureSmokeStruggleBeforeRepick, 0.01f);
		UE_LOG(LogPHCaptureSmoke,
			Log,
			TEXT("Capture smoke repick progress: recovery %.3f->%.3f struggle %.3f->%.3f result=%s."),
			CaptureSmokeRecoveryBeforeRepick,
			RecoveryAfterRepick,
			CaptureSmokeStruggleBeforeRepick,
			StruggleAfterRepick,
			bCaptureSmokeRepickProgressSucceeded ? TEXT("success") : TEXT("failure"));

		APHRetentionPoint* RetentionPoint = FindFirstRetentionPoint(GetWorld());
		if (RetentionPoint != nullptr)
		{
			const FVector TestLocation = RetentionPoint->GetActorLocation() + FVector(-150.0f, 0.0f, 100.0f);
			Hunter->SetActorLocation(TestLocation, false);
			const FRotator AimRotation = (RetentionPoint->GetInteractionPoint() - Hunter->GetPawnViewLocation()).Rotation();
			Hunter->SetActorRotation(FRotator(0.0f, AimRotation.Yaw, 0.0f));
			SetControlRotation(AimRotation);
		}
		CaptureSmokeHunterLocationNearRetention = Hunter->GetActorLocation();
		if (APHPlayerController* VictimController = FindCaptureSmokeController(1))
		{
			VictimController->ClientBeginCaptureSmokeStruggle(15);
		}
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokeRetain, 2.2f, false);
}

void APHPlayerController::RunCaptureSmokeRetain()
{
	if (APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn()))
	{
		const float ProtectedDistance = FVector::Dist2D(
			Hunter->GetActorLocation(), CaptureSmokeHunterLocationNearRetention);
		bCaptureSmokeProtectedShoveSucceeded = ProtectedDistance <= 10.0f;
		if (bCaptureSmokeProtectedShoveSucceeded)
		{
			UE_LOG(LogPHCaptureSmoke, Log,
				TEXT("Capture smoke protected carry struggle shove: displacement=%.1fcm result=success."),
				ProtectedDistance);
		}
		else
		{
			UE_LOG(LogPHCaptureSmoke, Error,
				TEXT("Capture smoke protected carry struggle shove: displacement=%.1fcm result=failure."),
				ProtectedDistance);
		}
		Hunter->RequestCaptureInteraction();
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokeRelease, 2.0f, false);
}

void APHPlayerController::RunCaptureSmokeRelease()
{
	if (APHPlayerController* RescuerController = FindCaptureSmokeController(2))
	{
		APHRetentionPoint* RetentionPoint = FindFirstRetentionPoint(GetWorld());
		if (APHPropCharacter* Rescuer = Cast<APHPropCharacter>(RescuerController->GetPawn());
			Rescuer != nullptr && RetentionPoint != nullptr)
		{
			Rescuer->SetActorLocation(
				RetentionPoint->GetActorLocation() + FVector(0.0f, 200.0f, 100.0f),
				false);
			Rescuer->SetActorRotation(FRotator(0.0f, -90.0f, 0.0f));
			RescuerController->SetControlRotation(FRotator(0.0f, -90.0f, 0.0f));
		}
		GetWorldTimerManager().SetTimer(
			CaptureSmokeInputTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (APHPlayerController* SynchronizedRescuerController = FindCaptureSmokeController(2))
				{
					SynchronizedRescuerController->ClientBeginCaptureSmokeRescue();
				}
			}),
			0.5f,
			false);
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::ReportCaptureSmokeHost, 3.75f, false);
}

void APHPlayerController::ClientBeginCaptureSmokeRescue_Implementation()
{
	SetControlRotation(FRotator(0.0f, -90.0f, 0.0f));
	InputKey(FInputKeyEventArgs(
		nullptr,
		INPUTDEVICEID_NONE,
		EKeys::LeftMouseButton,
		IE_Pressed,
		FPlatformTime::Cycles64()));
	UE_LOG(LogPHCaptureSmoke, Log, TEXT("Rescuer held the survivor contextual left-click for the 2s release."));
	GetWorldTimerManager().SetTimer(
		CaptureSmokeInputTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			InputKey(FInputKeyEventArgs(
				nullptr,
				INPUTDEVICEID_NONE,
				EKeys::LeftMouseButton,
				IE_Released,
				FPlatformTime::Cycles64()));
		}),
		2.2f,
		false);
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::ReportCaptureSmokeClient, 1.0f, false);
}

void APHPlayerController::ClientBeginCaptureSmokeStruggle_Implementation(const uint8 InputCount)
{
	CaptureSmokeStruggleInputsRemaining = InputCount;
	GetWorldTimerManager().SetTimerForNextTick(
		this, &APHPlayerController::RunCaptureSmokeStruggleInput);
}

void APHPlayerController::RunCaptureSmokeStruggleInput()
{
	if (!IsLocalController() || CaptureSmokeStruggleInputsRemaining == 0)
	{
		return;
	}

	if (CaptureSmokeLastInjectedDirection != 0)
	{
		const FKey PreviousKey = CaptureSmokeLastInjectedDirection < 0 ? EKeys::Q : EKeys::D;
		InputKey(FInputKeyEventArgs(
			nullptr,
			INPUTDEVICEID_NONE,
			PreviousKey,
			IE_Released,
			FPlatformTime::Cycles64()));
	}

	CaptureSmokeLastInjectedDirection = CaptureSmokeLastInjectedDirection <= 0 ? 1 : -1;
	const FKey CurrentKey = CaptureSmokeLastInjectedDirection < 0 ? EKeys::Q : EKeys::D;
	InputKey(FInputKeyEventArgs(
		nullptr,
		INPUTDEVICEID_NONE,
		CurrentKey,
		IE_Pressed,
		FPlatformTime::Cycles64()));
	--CaptureSmokeStruggleInputsRemaining;

	if (CaptureSmokeStruggleInputsRemaining > 0)
	{
		GetWorldTimerManager().SetTimer(
			CaptureSmokeInputTimer,
			this,
			&APHPlayerController::RunCaptureSmokeStruggleInput,
			0.12f,
			false);
	}
	else
	{
		GetWorldTimerManager().SetTimer(
			CaptureSmokeInputTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				const FKey LastKey = CaptureSmokeLastInjectedDirection < 0 ? EKeys::Q : EKeys::D;
				InputKey(FInputKeyEventArgs(
					nullptr,
					INPUTDEVICEID_NONE,
					LastKey,
					IE_Released,
					FPlatformTime::Cycles64()));
				UE_LOG(LogPHCaptureSmoke, Log, TEXT("Victim injected alternating Q-D struggle input."));
			}),
			0.06f,
			false);
	}
}

void APHPlayerController::ReportCaptureSmokeHost()
{
	const APHPropCharacter* Victim = CaptureSmokeVictim.Get();
	APHRetentionPoint* RetentionPoint = FindFirstRetentionPoint(GetWorld());
	const APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn());
	const APHPlayerController* VictimController = FindCaptureSmokeController(1);
	const bool bServerSoundCooldownSucceeded = Victim != nullptr
		&& Victim->GetSoundEmoteSmokeAcceptedCount() == 1;
	const bool bSucceeded = Victim != nullptr
		&& Victim->GetCaptureState() == EPHPropCaptureState::Grace
		&& Victim->GetRetentionCount() == 1
		&& Victim->GetActiveHumanPrototypeName() == FName(TEXT("Jun"))
		&& bCaptureSmokeCarryMoveSucceeded
		&& bCaptureSmokeFreeShoveSucceeded
		&& bCaptureSmokeDropProgressSucceeded
		&& bCaptureSmokeRepickProgressSucceeded
		&& bCaptureSmokeProtectedShoveSucceeded
		&& bCaptureSmokeHunterJumpSucceeded
		&& VictimController != nullptr && VictimController->bCaptureSmokeVictimJumpSucceeded
		&& VictimController->bCaptureSmokeCameraSucceeded
		&& VictimController->bCaptureSmokeSoundSucceeded
		&& bCaptureSmokeToyBrickSucceeded
		&& bServerSoundCooldownSucceeded
		&& Hunter != nullptr && Hunter->GetCarriedProp() == nullptr
		&& RetentionPoint != nullptr && RetentionPoint->GetRetainedProp() == nullptr;
	if (bSucceeded)
	{
		UE_LOG(LogPHCaptureSmoke, Log,
			TEXT("Capture smoke host result: state=%d retention=%d prototype=%s hunter-jump=yes survivor-jump=yes small-camera=yes spatial-sound=yes sound-cooldown=yes toy-brick=yes carry-move=yes free-shove=yes drop-progress=yes repick-progress=yes protected-shove=yes carried=no retained=no."),
			static_cast<int32>(Victim->GetCaptureState()),
			Victim->GetRetentionCount(),
			*Victim->GetActiveHumanPrototypeName().ToString());
	}
	else
	{
		UE_LOG(LogPHCaptureSmoke, Error,
			TEXT("Capture smoke host result: state=%d retention=%d prototype=%s hunter-jump=%s survivor-jump=%s small-camera=%s spatial-sound=%s sound-cooldown=%s toy-brick=%s carry-move=%s free-shove=%s drop-progress=%s repick-progress=%s protected-shove=%s carried=%s retained=%s."),
			Victim != nullptr ? static_cast<int32>(Victim->GetCaptureState()) : -1,
			Victim != nullptr ? Victim->GetRetentionCount() : -1,
			Victim != nullptr ? *Victim->GetActiveHumanPrototypeName().ToString() : TEXT("None"),
			bCaptureSmokeHunterJumpSucceeded ? TEXT("yes") : TEXT("no"),
			VictimController != nullptr && VictimController->bCaptureSmokeVictimJumpSucceeded ? TEXT("yes") : TEXT("no"),
			VictimController != nullptr && VictimController->bCaptureSmokeCameraSucceeded ? TEXT("yes") : TEXT("no"),
			VictimController != nullptr && VictimController->bCaptureSmokeSoundSucceeded ? TEXT("yes") : TEXT("no"),
			bServerSoundCooldownSucceeded ? TEXT("yes") : TEXT("no"),
			bCaptureSmokeToyBrickSucceeded ? TEXT("yes") : TEXT("no"),
			bCaptureSmokeCarryMoveSucceeded ? TEXT("yes") : TEXT("no"),
			bCaptureSmokeFreeShoveSucceeded ? TEXT("yes") : TEXT("no"),
			bCaptureSmokeDropProgressSucceeded ? TEXT("yes") : TEXT("no"),
			bCaptureSmokeRepickProgressSucceeded ? TEXT("yes") : TEXT("no"),
			bCaptureSmokeProtectedShoveSucceeded ? TEXT("yes") : TEXT("no"),
			Hunter != nullptr && Hunter->GetCarriedProp() != nullptr ? TEXT("yes") : TEXT("no"),
			RetentionPoint != nullptr && RetentionPoint->GetRetainedProp() != nullptr ? TEXT("yes") : TEXT("no"));
	}
	if (bSucceeded)
	{
		GetWorldTimerManager().SetTimer(
			CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokeMemento, 0.75f, false);
	}
}

void APHPlayerController::RunCaptureSmokeMemento()
{
	APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn());
	APHPropCharacter* Victim = CaptureSmokeVictim.Get();
	UWorld* World = GetWorld();
	if (Hunter == nullptr || Victim == nullptr || World == nullptr)
	{
		UE_LOG(LogPHCaptureSmoke, Error, TEXT("Memento smoke could not resolve its Hunter, victim or world."));
		return;
	}

	Victim->PrepareMementoForSmoke(Hunter->GetMementoMinimumRetentionCount());
	FVector VictimLocation = Victim->GetActorLocation();
	VictimLocation.Z = 100.0f;
	Victim->SetActorLocation(VictimLocation, false);

	auto FindWallDirection = [World, Hunter, Victim](FVector& OutDirection, FVector& OutImpactPoint)
	{
		const FVector TraceStart = Victim->GetActorLocation() + FVector::UpVector * 55.0f;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PHCaptureSmokeMementoWall), false, Hunter);
		QueryParams.AddIgnoredActor(Hunter);
		QueryParams.AddIgnoredActor(Victim);
		for (int32 DirectionIndex = 0; DirectionIndex < 12; ++DirectionIndex)
		{
			const FVector Direction = FRotator(0.0f, DirectionIndex * 30.0f, 0.0f).Vector();
			FHitResult WallHit;
			if (World->LineTraceSingleByChannel(
				WallHit,
				TraceStart,
				TraceStart + Direction * Hunter->GetMementoWallSearchDistance(),
				ECC_Visibility,
				QueryParams)
				&& FMath::Abs(WallHit.ImpactNormal.Z) <= 0.70f)
			{
				OutDirection = Direction;
				OutImpactPoint = WallHit.ImpactPoint;
				return true;
			}
		}
		return false;
	};

	FVector WallDirection = FVector::ZeroVector;
	FVector WallImpactPoint = FVector::ZeroVector;
	if (!FindWallDirection(WallDirection, WallImpactPoint))
	{
		Victim->SetActorLocation(FVector(0.0f, 0.0f, 100.0f), false);
		if (!FindWallDirection(WallDirection, WallImpactPoint))
		{
			UE_LOG(LogPHCaptureSmoke, Error, TEXT("Memento smoke could not find a vertical wall within range."));
			return;
		}
	}
	if (FVector::Dist2D(Victim->GetActorLocation(), WallImpactPoint) < 250.0f)
	{
		FVector SpacedLocation = WallImpactPoint - WallDirection * 400.0f;
		SpacedLocation.Z = 100.0f;
		Victim->SetActorLocation(SpacedLocation, false);
	}

	VictimLocation = Victim->GetActorLocation();
	Hunter->SetActorLocation(VictimLocation - WallDirection * 110.0f, false);
	const FRotator AimRotation = (
		Victim->GetMeleeLineOfSightPoint(Hunter->GetPawnViewLocation()) - Hunter->GetPawnViewLocation()).Rotation();
	Hunter->SetActorRotation(FRotator(0.0f, AimRotation.Yaw, 0.0f));
	SetControlRotation(AimRotation);
	CaptureSmokeMementoStartLocation = VictimLocation;
	Hunter->RequestMemento();
	UE_LOG(LogPHCaptureSmoke, Log,
		TEXT("Memento smoke requested against a real map wall: start=%s direction=%s retention=%d."),
		*CaptureSmokeMementoStartLocation.ToCompactString(),
		*WallDirection.ToCompactString(),
		Victim->GetRetentionCount());
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::ReportCaptureSmokeMemento, 3.0f, false);
}

void APHPlayerController::ReportCaptureSmokeMemento()
{
	const APHPropCharacter* Victim = CaptureSmokeVictim.Get();
	const float TravelDistance = Victim != nullptr
		? FVector::Dist(Victim->GetActorLocation(), CaptureSmokeMementoStartLocation)
		: 0.0f;
	const bool bSucceeded = Victim != nullptr
		&& Victim->GetCaptureState() == EPHPropCaptureState::Eliminated
		&& !Victim->IsMementoInProgress()
		&& TravelDistance >= 100.0f;
	UE_LOG(LogPHCaptureSmoke, Log,
		TEXT("Memento smoke result: state=%d in-progress=%s travel=%.1fcm result=%s."),
		Victim != nullptr ? static_cast<int32>(Victim->GetCaptureState()) : -1,
		Victim != nullptr && Victim->IsMementoInProgress() ? TEXT("yes") : TEXT("no"),
		TravelDistance,
		bSucceeded ? TEXT("success") : TEXT("failure"));
	if (!bSucceeded)
	{
		UE_LOG(LogPHCaptureSmoke, Error, TEXT("Memento smoke failed its runtime assertions."));
	}
}

void APHPlayerController::ReportCaptureSmokeClient()
{
	const APHPropCharacter* LocalProp = Cast<APHPropCharacter>(GetPawn());
	UE_LOG(LogPHCaptureSmoke, Log, TEXT("Capture smoke client role %d local state=%d."),
		static_cast<int32>(RequestedCaptureSmokeRole),
		LocalProp != nullptr ? static_cast<int32>(LocalProp->GetCaptureState()) : -1);
}

void APHPlayerController::TryRunSpectatorSmokeHost()
{
	if (!HasAuthority())
	{
		return;
	}

	APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn());
	if (GetPawn() != nullptr && Hunter == nullptr)
	{
		// A dedicated server sees the smoke flag from every remote controller.
		// Only the authoritative Hunter controller must orchestrate the scenario.
		return;
	}

	APHPlayerController* VictimController = FindCaptureSmokeController(1);
	APHPlayerController* RescuerController = FindCaptureSmokeController(2);
	APHPropCharacter* Victim = VictimController != nullptr ? Cast<APHPropCharacter>(VictimController->GetPawn()) : nullptr;
	APHPropCharacter* Rescuer = RescuerController != nullptr ? Cast<APHPropCharacter>(RescuerController->GetPawn()) : nullptr;
	APHRetentionPoint* RetentionPoint = FindFirstRetentionPoint(GetWorld());
	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	if (Hunter == nullptr
		|| Victim == nullptr || Rescuer == nullptr || RetentionPoint == nullptr
		|| GameState == nullptr || GameState->GetMatchPhase() != EPHMatchPhase::Hunt)
	{
		if (++CaptureSmokeRetryCount <= 480)
		{
			GetWorldTimerManager().SetTimer(
				CaptureSmokeTimer, this, &APHPlayerController::TryRunSpectatorSmokeHost, 0.5f, false);
		}
		else
		{
			UE_LOG(LogPHCaptureSmoke, Error,
				TEXT("Spectator smoke timed out waiting for Hunter, two Props, retention point and Hunt."));
		}
		return;
	}

	CaptureSmokeVictim = Victim;
	// Give the surviving player a non-trivial view rotation before eliminating the victim.
	// The client smoke later checks that the spectator receives this exact remote view,
	// rather than merely checking that the correct Pawn became the view target.
	RescuerController->ClientSetRotation(FRotator(23.0f, 127.0f, 0.0f), true);
	Victim->ForceFinalRetentionForSmoke(*RetentionPoint);
	UE_LOG(LogPHCaptureSmoke, Log, TEXT("Spectator smoke forced the victim's final retention."));
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::VerifySpectatorSmokeHost, 1.5f, false);
}

void APHPlayerController::VerifySpectatorSmokeHost()
{
	const APHPropCharacter* Victim = CaptureSmokeVictim.Get();
	APHPlayerController* RescuerController = FindCaptureSmokeController(2);
	const APHPropCharacter* Rescuer = RescuerController != nullptr
		? Cast<APHPropCharacter>(RescuerController->GetPawn())
		: nullptr;
	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	const bool bSucceeded = Victim != nullptr
		&& Victim->GetCaptureState() == EPHPropCaptureState::Eliminated
		&& Victim->GetAttachParentActor() == nullptr
		&& Victim->GetRetentionPoint() == nullptr
		&& Rescuer != nullptr
		&& Rescuer->GetCaptureState() != EPHPropCaptureState::Eliminated
		&& GameState != nullptr
		&& GameState->GetMatchPhase() == EPHMatchPhase::Hunt;
	UE_LOG(LogPHCaptureSmoke, Log,
		TEXT("Spectator smoke victim elimination: eliminated=%s attached=%s retained=%s survivor-active=%s phase=%d result=%s."),
		Victim != nullptr && Victim->GetCaptureState() == EPHPropCaptureState::Eliminated ? TEXT("yes") : TEXT("no"),
		Victim != nullptr && Victim->GetAttachParentActor() != nullptr ? TEXT("yes") : TEXT("no"),
		Victim != nullptr && Victim->GetRetentionPoint() != nullptr ? TEXT("yes") : TEXT("no"),
		Rescuer != nullptr && Rescuer->GetCaptureState() != EPHPropCaptureState::Eliminated ? TEXT("yes") : TEXT("no"),
		GameState != nullptr ? static_cast<int32>(GameState->GetMatchPhase()) : -1,
		bSucceeded ? TEXT("success") : TEXT("failure"));

	if (APHPlayerController* VictimController = FindCaptureSmokeController(1))
	{
		VictimController->ClientVerifySpectatorSmoke();
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::FinishSpectatorSmokeHost, 1.5f, false);
}

void APHPlayerController::ClientVerifySpectatorSmoke_Implementation()
{
	InputKey(FInputKeyEventArgs(
		nullptr,
		INPUTDEVICEID_NONE,
		EKeys::Escape,
		IE_Pressed,
		FPlatformTime::Cycles64()));
	GetWorldTimerManager().SetTimer(
		CaptureSmokeInputTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const bool bMenuOpened = InGameMenuWidget != nullptr && InGameMenuWidget->IsInViewport();
			const bool bCameraOnSurvivor = IsSpectatingSurvivor()
				&& SpectatedProp.IsValid()
				&& GetViewTarget() == SpectatedProp.Get();
			const FRotator SpectatorView = SpectatedProp.IsValid()
				? SpectatedProp->GetViewRotation()
				: FRotator::ZeroRotator;
			const bool bRemoteViewMatches = bCameraOnSurvivor
				&& FMath::Abs(FMath::FindDeltaAngleDegrees(SpectatorView.Pitch, 23.0f)) <= 3.0f
				&& FMath::Abs(FMath::FindDeltaAngleDegrees(SpectatorView.Yaw, 127.0f)) <= 3.0f;
			UE_LOG(LogPHCaptureSmoke, Log,
				TEXT("Spectator smoke client: camera=%s target=%s view=(pitch=%.1f yaw=%.1f) remote-view=%s escape-menu=%s result=%s."),
				bCameraOnSurvivor ? TEXT("survivor") : TEXT("invalid"),
				SpectatedProp.IsValid() ? *SpectatedProp->GetName() : TEXT("none"),
				SpectatorView.Pitch,
				SpectatorView.Yaw,
				bRemoteViewMatches ? TEXT("match") : TEXT("mismatch"),
				bMenuOpened ? TEXT("open") : TEXT("closed"),
				bMenuOpened && bCameraOnSurvivor && bRemoteViewMatches ? TEXT("success") : TEXT("failure"));
			if (bMenuOpened)
			{
				CloseInGameMenu();
			}
		}),
		0.2f,
		false);
}

void APHPlayerController::FinishSpectatorSmokeHost()
{
	APHPlayerController* RescuerController = FindCaptureSmokeController(2);
	APHPropCharacter* Rescuer = RescuerController != nullptr ? Cast<APHPropCharacter>(RescuerController->GetPawn()) : nullptr;
	APHRetentionPoint* RetentionPoint = FindFirstRetentionPoint(GetWorld());
	if (Rescuer != nullptr && RetentionPoint != nullptr)
	{
		Rescuer->ForceFinalRetentionForSmoke(*RetentionPoint);
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::ReportSpectatorSmokeHost, 1.5f, false);
}

void APHPlayerController::ReportSpectatorSmokeHost()
{
	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	const bool bSucceeded = GameState != nullptr
		&& GameState->GetMatchPhase() == EPHMatchPhase::Results
		&& GameState->GetMatchEndReason() == EPHMatchEndReason::AllPropsGone;
	UE_LOG(LogPHCaptureSmoke, Log,
		TEXT("Spectator smoke last survivor: phase=%d reason=%d result=%s."),
		GameState != nullptr ? static_cast<int32>(GameState->GetMatchPhase()) : -1,
		GameState != nullptr ? static_cast<int32>(GameState->GetMatchEndReason()) : -1,
		bSucceeded ? TEXT("success") : TEXT("failure"));
}

void APHPlayerController::TryRunExitGateSmokeHost()
{
	if (!HasAuthority())
	{
		return;
	}

	APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn());
	if (GetPawn() != nullptr && Hunter == nullptr)
	{
		return;
	}

	APHPlayerController* FirstPropController = FindCaptureSmokeController(1);
	APHPlayerController* SecondPropController = FindCaptureSmokeController(2);
	APHPropCharacter* FirstProp = FirstPropController != nullptr
		? Cast<APHPropCharacter>(FirstPropController->GetPawn())
		: nullptr;
	APHPropCharacter* SecondProp = SecondPropController != nullptr
		? Cast<APHPropCharacter>(SecondPropController->GetPawn())
		: nullptr;
	APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;

	TArray<APHExitGate*> Gates;
	if (GetWorld() != nullptr)
	{
		for (TActorIterator<APHExitGate> GateIterator(GetWorld()); GateIterator; ++GateIterator)
		{
			if (IsValid(*GateIterator))
			{
				Gates.Add(*GateIterator);
			}
		}
	}
	Gates.Sort([](const APHExitGate& Left, const APHExitGate& Right)
	{
		return Left.GetName() < Right.GetName();
	});

	if (Hunter == nullptr || FirstProp == nullptr || SecondProp == nullptr
		|| GameState == nullptr || GameState->GetMatchPhase() != EPHMatchPhase::Hunt || Gates.Num() != 2)
	{
		if (++CaptureSmokeRetryCount <= 480)
		{
			GetWorldTimerManager().SetTimer(
				CaptureSmokeTimer, this, &APHPlayerController::TryRunExitGateSmokeHost, 0.5f, false);
		}
		else
		{
			UE_LOG(LogPHExitGateSmoke, Error,
				TEXT("Exit gate smoke timed out: hunter=%s prop-a=%s prop-b=%s phase=%d gates=%d."),
				Hunter != nullptr ? TEXT("yes") : TEXT("no"),
				FirstProp != nullptr ? TEXT("yes") : TEXT("no"),
				SecondProp != nullptr ? TEXT("yes") : TEXT("no"),
				GameState != nullptr ? static_cast<int32>(GameState->GetMatchPhase()) : -1,
				Gates.Num());
		}
		return;
	}

	APHGameMode* GameMode = GetWorld()->GetAuthGameMode<APHGameMode>();
	if (GameMode == nullptr)
	{
		UE_LOG(LogPHExitGateSmoke, Error, TEXT("Exit gate smoke has no authoritative game mode."));
		return;
	}
	GameMode->ForceEscapePhaseForSmoke();

	const bool bPlacedFirst = PlacePropAtExitGateControlBox(*FirstProp, *Gates[0]);
	const bool bPlacedSecond = PlacePropAtExitGateControlBox(*SecondProp, *Gates[1]);
	const bool bStartedFirst = bPlacedFirst && Gates[0]->ServerTryBeginInteraction(*FirstProp);
	const bool bStartedSecond = bPlacedSecond && Gates[1]->ServerTryBeginInteraction(*SecondProp);
	if (!bStartedFirst || !bStartedSecond)
	{
		UE_LOG(LogPHExitGateSmoke, Error,
			TEXT("Exit gate smoke could not start both interactions: placed=(%s,%s) started=(%s,%s)."),
			bPlacedFirst ? TEXT("yes") : TEXT("no"), bPlacedSecond ? TEXT("yes") : TEXT("no"),
			bStartedFirst ? TEXT("yes") : TEXT("no"), bStartedSecond ? TEXT("yes") : TEXT("no"));
		return;
	}

	CaptureSmokeVictim = FirstProp;
	ExitGateSmokeSecondProp = SecondProp;
	ExitGateSmokeFirstGate = Gates[0];
	ExitGateSmokeSecondGate = Gates[1];
	UE_LOG(LogPHExitGateSmoke, Log,
		TEXT("Exit gate smoke started two maintained interactions: gates=(%s,%s) duration=(%.1f,%.1f)."),
		*Gates[0]->GetName(), *Gates[1]->GetName(),
		Gates[0]->GetOpenDurationSeconds(), Gates[1]->GetOpenDurationSeconds());
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::VerifyExitGateSmokeMidpoint, 10.0f, false);
}

void APHPlayerController::VerifyExitGateSmokeMidpoint()
{
	APHExitGate* FirstGate = ExitGateSmokeFirstGate.Get();
	APHExitGate* SecondGate = ExitGateSmokeSecondGate.Get();
	const float FirstProgress = FirstGate != nullptr ? FirstGate->GetOpenProgress() : -1.0f;
	const float SecondProgress = SecondGate != nullptr ? SecondGate->GetOpenProgress() : -1.0f;
	const bool bSucceeded = FirstGate != nullptr && SecondGate != nullptr
		&& FirstGate->IsGateEnabled() && SecondGate->IsGateEnabled()
		&& !FirstGate->IsGateOpen() && !SecondGate->IsGateOpen()
		&& FirstProgress >= 0.40f && FirstProgress <= 0.65f
		&& SecondProgress >= 0.40f && SecondProgress <= 0.65f;
	UE_LOG(LogPHExitGateSmoke, Log,
		TEXT("Exit gate smoke midpoint: enabled=(%s,%s) open=(%s,%s) progress=(%.3f,%.3f) result=%s."),
		FirstGate != nullptr && FirstGate->IsGateEnabled() ? TEXT("yes") : TEXT("no"),
		SecondGate != nullptr && SecondGate->IsGateEnabled() ? TEXT("yes") : TEXT("no"),
		FirstGate != nullptr && FirstGate->IsGateOpen() ? TEXT("yes") : TEXT("no"),
		SecondGate != nullptr && SecondGate->IsGateOpen() ? TEXT("yes") : TEXT("no"),
		FirstProgress, SecondProgress, bSucceeded ? TEXT("success") : TEXT("failure"));

	for (uint8 SmokeRole = 1; SmokeRole <= 2; ++SmokeRole)
	{
		if (APHPlayerController* PropController = FindCaptureSmokeController(SmokeRole))
		{
			PropController->ClientVerifyExitGateSmoke(FirstGate, SecondGate, false);
		}
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::VerifyExitGateSmokeOpened, 11.5f, false);
}

void APHPlayerController::VerifyExitGateSmokeOpened()
{
	APHExitGate* FirstGate = ExitGateSmokeFirstGate.Get();
	APHExitGate* SecondGate = ExitGateSmokeSecondGate.Get();
	const bool bSucceeded = FirstGate != nullptr && SecondGate != nullptr
		&& FirstGate->IsGateOpen() && SecondGate->IsGateOpen()
		&& FMath::IsNearlyEqual(FirstGate->GetOpenProgress(), 1.0f)
		&& FMath::IsNearlyEqual(SecondGate->GetOpenProgress(), 1.0f);
	UE_LOG(LogPHExitGateSmoke, Log,
		TEXT("Exit gate smoke opening: open=(%s,%s) progress=(%.3f,%.3f) result=%s."),
		FirstGate != nullptr && FirstGate->IsGateOpen() ? TEXT("yes") : TEXT("no"),
		SecondGate != nullptr && SecondGate->IsGateOpen() ? TEXT("yes") : TEXT("no"),
		FirstGate != nullptr ? FirstGate->GetOpenProgress() : -1.0f,
		SecondGate != nullptr ? SecondGate->GetOpenProgress() : -1.0f,
		bSucceeded ? TEXT("success") : TEXT("failure"));

	for (uint8 SmokeRole = 1; SmokeRole <= 2; ++SmokeRole)
	{
		if (APHPlayerController* PropController = FindCaptureSmokeController(SmokeRole))
		{
			PropController->ClientVerifyExitGateSmoke(FirstGate, SecondGate, true);
		}
	}

	APHPropCharacter* FirstProp = CaptureSmokeVictim.Get();
	if (FirstProp != nullptr && FirstGate != nullptr)
	{
		FirstProp->SetActorLocation(
			FirstGate->GetExitVolumeLocationForSmoke(), false, nullptr, ETeleportType::TeleportPhysics);
		FirstProp->ForceNetUpdate();
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::VerifyFirstExitGateSmokeEscape, 1.0f, false);
}

void APHPlayerController::ClientVerifyExitGateSmoke_Implementation(
	APHExitGate* FirstGate, APHExitGate* SecondGate, const bool bExpectOpen)
{
	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	const bool bStateMatches = FirstGate != nullptr && SecondGate != nullptr
		&& FirstGate->IsGateEnabled() && SecondGate->IsGateEnabled()
		&& FirstGate->IsGateOpen() == bExpectOpen
		&& SecondGate->IsGateOpen() == bExpectOpen
		&& (!bExpectOpen || (FMath::IsNearlyEqual(FirstGate->GetOpenProgress(), 1.0f)
			&& FMath::IsNearlyEqual(SecondGate->GetOpenProgress(), 1.0f)))
		&& GameState != nullptr && GameState->GetMatchPhase() == EPHMatchPhase::Escape;
	UE_LOG(LogPHExitGateSmoke, Log,
		TEXT("Exit gate smoke client replication: expected-open=%s actual-open=(%s,%s) progress=(%.3f,%.3f) phase=%d result=%s."),
		bExpectOpen ? TEXT("yes") : TEXT("no"),
		FirstGate != nullptr && FirstGate->IsGateOpen() ? TEXT("yes") : TEXT("no"),
		SecondGate != nullptr && SecondGate->IsGateOpen() ? TEXT("yes") : TEXT("no"),
		FirstGate != nullptr ? FirstGate->GetOpenProgress() : -1.0f,
		SecondGate != nullptr ? SecondGate->GetOpenProgress() : -1.0f,
		GameState != nullptr ? static_cast<int32>(GameState->GetMatchPhase()) : -1,
		bStateMatches ? TEXT("success") : TEXT("failure"));
}

void APHPlayerController::VerifyFirstExitGateSmokeEscape()
{
	APHPropCharacter* FirstProp = CaptureSmokeVictim.Get();
	APHPropCharacter* SecondProp = ExitGateSmokeSecondProp.Get();
	APHExitGate* SecondGate = ExitGateSmokeSecondGate.Get();
	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	const bool bSucceeded = FirstProp != nullptr
		&& FirstProp->GetCaptureState() == EPHPropCaptureState::Escaped
		&& SecondProp != nullptr && !PHCaptureFlow::IsTerminal(SecondProp->GetCaptureState())
		&& GameState != nullptr && GameState->GetMatchPhase() == EPHMatchPhase::Escape
		&& GameState->GetMatchEndReason() == EPHMatchEndReason::None;
	UE_LOG(LogPHExitGateSmoke, Log,
		TEXT("Exit gate smoke first overlap: first-state=%d second-state=%d phase=%d reason=%d result=%s."),
		FirstProp != nullptr ? static_cast<int32>(FirstProp->GetCaptureState()) : -1,
		SecondProp != nullptr ? static_cast<int32>(SecondProp->GetCaptureState()) : -1,
		GameState != nullptr ? static_cast<int32>(GameState->GetMatchPhase()) : -1,
		GameState != nullptr ? static_cast<int32>(GameState->GetMatchEndReason()) : -1,
		bSucceeded ? TEXT("success") : TEXT("failure"));

	if (SecondProp != nullptr && SecondGate != nullptr)
	{
		SecondProp->SetActorLocation(
			SecondGate->GetExitVolumeLocationForSmoke(), false, nullptr, ETeleportType::TeleportPhysics);
		SecondProp->ForceNetUpdate();
	}
	const bool bSecondSucceeded = SecondProp != nullptr
		&& SecondProp->GetCaptureState() == EPHPropCaptureState::Escaped
		&& GameState != nullptr && GameState->GetMatchPhase() == EPHMatchPhase::Results
		&& GameState->GetMatchEndReason() == EPHMatchEndReason::AtLeastOnePropEscaped;
	UE_LOG(LogPHExitGateSmoke, Log,
		TEXT("Exit gate smoke second overlap: second-state=%d phase=%d reason=%d result=%s."),
		SecondProp != nullptr ? static_cast<int32>(SecondProp->GetCaptureState()) : -1,
		GameState != nullptr ? static_cast<int32>(GameState->GetMatchPhase()) : -1,
		GameState != nullptr ? static_cast<int32>(GameState->GetMatchEndReason()) : -1,
		bSecondSucceeded ? TEXT("success") : TEXT("failure"));
}
#endif
