#include "Game/PHPlayerController.h"

#include "Characters/PHHunterCharacter.h"
#include "Characters/PHPropCharacter.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Game/PHGameState.h"
#include "Game/PHPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Gameplay/Capture/PHRetentionPoint.h"
#include "Gameplay/Characters/PHHumanPrototypeSelector.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"
#include "UI/PHHumanStaminaWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHObjectiveSmoke, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogPHCaptureSmoke, Log, All);

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

#if !UE_BUILD_SHIPPING
		if (FParse::Param(FCommandLine::Get(), TEXT("PHObjectiveSmoke")))
		{
			GetWorldTimerManager().SetTimerForNextTick(this, &APHPlayerController::TryRunObjectiveSmoke);
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("PHCaptureSmokeHost")))
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

#if UE_BUILD_SHIPPING
void APHPlayerController::ServerRegisterCaptureSmokeRole_Implementation(uint8 SmokeRole)
{
}

void APHPlayerController::ClientPrepareCaptureSmokeVictim_Implementation()
{
}

void APHPlayerController::ClientBeginCaptureSmokeRescue_Implementation()
{
}

void APHPlayerController::ClientBeginCaptureSmokeStruggle_Implementation(uint8 InputCount)
{
}
#endif
	}
}

void APHPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HumanStaminaWidget != nullptr)
	{
		HumanStaminaWidget->RemoveFromParent();
		HumanStaminaWidget = nullptr;
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

void APHPlayerController::NotifyControlledPawnChanged()
{
	EnsureLocalHumanStaminaWidget();
	BP_OnControlledPawnChanged(GetPawn(), GetControlledPlayerRole());
}

void APHPlayerController::EnsureLocalHumanStaminaWidget()
{
	if (!IsLocalController() || HumanStaminaWidget != nullptr || GetLocalPlayer() == nullptr
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

#if !UE_BUILD_SHIPPING
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
		if (++CaptureSmokeRetryCount <= 240)
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
	UE_LOG(LogPHCaptureSmoke, Log,
		TEXT("Capture smoke positioned Hunter, free victim and rescuer without objective interaction."));
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokeMelee, 1.0f, false);
}

void APHPlayerController::ClientPrepareCaptureSmokeVictim_Implementation()
{
	SetControlRotation(FRotator::ZeroRotator);
	UE_LOG(LogPHCaptureSmoke, Log, TEXT("Victim ready while free and outside objective interaction."));
}

void APHPlayerController::RunCaptureSmokeMelee()
{
	if (APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn()))
	{
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
			VictimController->ClientBeginCaptureSmokeStruggle(6);
		}
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokeVerifyFreeShove, 1.1f, false);
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

		Hunter->SetActorLocation(FVector(250.0f, 0.0f, 100.0f), false);
		Hunter->SetActorRotation(FRotator::ZeroRotator);
		SetControlRotation(FRotator::ZeroRotator);
		CaptureSmokeHunterLocationNearRetention = Hunter->GetActorLocation();
		if (APHPlayerController* VictimController = FindCaptureSmokeController(1))
		{
			VictimController->ClientBeginCaptureSmokeStruggle(5);
		}
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::RunCaptureSmokeRetain, 0.9f, false);
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
		RescuerController->ClientBeginCaptureSmokeRescue();
	}
	GetWorldTimerManager().SetTimer(
		CaptureSmokeTimer, this, &APHPlayerController::ReportCaptureSmokeHost, 2.75f, false);
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
	APHRetentionPoint* RetentionPoint = nullptr;
	if (GetWorld() != nullptr)
	{
		for (TActorIterator<APHRetentionPoint> PointIterator(GetWorld()); PointIterator; ++PointIterator)
		{
			RetentionPoint = *PointIterator;
			break;
		}
	}
	const APHHunterCharacter* Hunter = Cast<APHHunterCharacter>(GetPawn());
	const bool bSucceeded = Victim != nullptr
		&& Victim->GetCaptureState() == EPHPropCaptureState::Grace
		&& Victim->GetRetentionCount() == 1
		&& Victim->GetActiveHumanPrototypeName() == FName(TEXT("Jun"))
		&& bCaptureSmokeCarryMoveSucceeded
		&& bCaptureSmokeFreeShoveSucceeded
		&& bCaptureSmokeProtectedShoveSucceeded
		&& Hunter != nullptr && Hunter->GetCarriedProp() == nullptr
		&& RetentionPoint != nullptr && RetentionPoint->GetRetainedProp() == nullptr;
	if (bSucceeded)
	{
		UE_LOG(LogPHCaptureSmoke, Log,
			TEXT("Capture smoke host result: state=%d retention=%d prototype=%s carry-move=yes free-shove=yes protected-shove=yes carried=no retained=no."),
			static_cast<int32>(Victim->GetCaptureState()),
			Victim->GetRetentionCount(),
			*Victim->GetActiveHumanPrototypeName().ToString());
	}
	else
	{
		UE_LOG(LogPHCaptureSmoke, Error,
			TEXT("Capture smoke host result: state=%d retention=%d prototype=%s carry-move=%s free-shove=%s protected-shove=%s carried=%s retained=%s."),
			Victim != nullptr ? static_cast<int32>(Victim->GetCaptureState()) : -1,
			Victim != nullptr ? Victim->GetRetentionCount() : -1,
			Victim != nullptr ? *Victim->GetActiveHumanPrototypeName().ToString() : TEXT("None"),
			bCaptureSmokeCarryMoveSucceeded ? TEXT("yes") : TEXT("no"),
			bCaptureSmokeFreeShoveSucceeded ? TEXT("yes") : TEXT("no"),
			bCaptureSmokeProtectedShoveSucceeded ? TEXT("yes") : TEXT("no"),
			Hunter != nullptr && Hunter->GetCarriedProp() != nullptr ? TEXT("yes") : TEXT("no"),
			RetentionPoint != nullptr && RetentionPoint->GetRetainedProp() != nullptr ? TEXT("yes") : TEXT("no"));
	}
}

void APHPlayerController::ReportCaptureSmokeClient()
{
	const APHPropCharacter* LocalProp = Cast<APHPropCharacter>(GetPawn());
	UE_LOG(LogPHCaptureSmoke, Log, TEXT("Capture smoke client role %d local state=%d."),
		static_cast<int32>(RequestedCaptureSmokeRole),
		LocalProp != nullptr ? static_cast<int32>(LocalProp->GetCaptureState()) : -1);
}
#endif
