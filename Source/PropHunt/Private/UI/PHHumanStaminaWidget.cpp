#include "UI/PHHumanStaminaWidget.h"

#include "Characters/PHPropCharacter.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Game/PHGameState.h"
#include "Game/PHPlayerController.h"
#include "Game/PHPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Objectives/PHObjectiveActor.h"
#include "Gameplay/Escape/PHExitGate.h"

void UPHHumanStaminaWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree == nullptr)
	{
		return;
	}

	UCanvasPanel* RootPanel = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("StaminaRoot"));
	WidgetTree->RootWidget = RootPanel;

	StaminaBar = WidgetTree->ConstructWidget<UProgressBar>(
		UProgressBar::StaticClass(), TEXT("HumanStaminaBar"));
	StaminaBar->SetFillColorAndOpacity(FLinearColor(0.15f, 0.75f, 0.30f, 1.0f));
	if (UCanvasPanelSlot* BarSlot = RootPanel->AddChildToCanvas(StaminaBar))
	{
		BarSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		BarSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		BarSlot->SetPosition(FVector2D(0.0f, -42.0f));
		BarSlot->SetSize(FVector2D(280.0f, 18.0f));
	}

	StaminaLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("HumanStaminaLabel"));
	StaminaLabel->SetText(FText::FromString(TEXT("ENDURANCE")));
	StaminaLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	StaminaLabel->SetJustification(ETextJustify::Center);
	if (UCanvasPanelSlot* LabelSlot = RootPanel->AddChildToCanvas(StaminaLabel))
	{
		LabelSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		LabelSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		LabelSlot->SetPosition(FVector2D(0.0f, -64.0f));
		LabelSlot->SetSize(FVector2D(280.0f, 20.0f));
	}

	CaptureProgressBar = WidgetTree->ConstructWidget<UProgressBar>(
		UProgressBar::StaticClass(), TEXT("CaptureProgressBar"));
	CaptureProgressBar->SetFillColorAndOpacity(FLinearColor(0.85f, 0.55f, 0.10f, 1.0f));
	if (UCanvasPanelSlot* BarSlot = RootPanel->AddChildToCanvas(CaptureProgressBar))
	{
		BarSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		BarSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		BarSlot->SetPosition(FVector2D(0.0f, -94.0f));
		BarSlot->SetSize(FVector2D(360.0f, 22.0f));
	}

	DownedRecoveryBar = WidgetTree->ConstructWidget<UProgressBar>(
		UProgressBar::StaticClass(), TEXT("DownedRecoveryBar"));
	DownedRecoveryBar->SetFillColorAndOpacity(FLinearColor(0.10f, 0.72f, 0.62f, 1.0f));
	if (UCanvasPanelSlot* BarSlot = RootPanel->AddChildToCanvas(DownedRecoveryBar))
	{
		BarSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		BarSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		BarSlot->SetPosition(FVector2D(0.0f, -94.0f));
		BarSlot->SetSize(FVector2D(360.0f, 22.0f));
	}

	CarryStruggleBar = WidgetTree->ConstructWidget<UProgressBar>(
		UProgressBar::StaticClass(), TEXT("CarryStruggleBar"));
	CarryStruggleBar->SetFillColorAndOpacity(FLinearColor(0.88f, 0.20f, 0.08f, 1.0f));
	if (UCanvasPanelSlot* BarSlot = RootPanel->AddChildToCanvas(CarryStruggleBar))
	{
		BarSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		BarSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		BarSlot->SetPosition(FVector2D(0.0f, -94.0f));
		BarSlot->SetSize(FVector2D(360.0f, 22.0f));
	}

	CaptureProgressLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("CaptureProgressLabel"));
	CaptureProgressLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	CaptureProgressLabel->SetJustification(ETextJustify::Center);
	if (UCanvasPanelSlot* LabelSlot = RootPanel->AddChildToCanvas(CaptureProgressLabel))
	{
		LabelSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		LabelSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		LabelSlot->SetPosition(FVector2D(0.0f, -120.0f));
		LabelSlot->SetSize(FVector2D(520.0f, 24.0f));
	}

	MatchTimerLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("MatchTimerLabel"));
	MatchTimerLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	MatchTimerLabel->SetJustification(ETextJustify::Center);
	if (UCanvasPanelSlot* TimerSlot = RootPanel->AddChildToCanvas(MatchTimerLabel))
	{
		TimerSlot->SetAnchors(FAnchors(0.5f, 0.0f));
		TimerSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		TimerSlot->SetPosition(FVector2D(0.0f, 24.0f));
		TimerSlot->SetSize(FVector2D(620.0f, 32.0f));
	}

	InteractionPromptLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("InteractionPromptLabel"));
	InteractionPromptLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	InteractionPromptLabel->SetJustification(ETextJustify::Center);
	if (UCanvasPanelSlot* PromptSlot = RootPanel->AddChildToCanvas(InteractionPromptLabel))
	{
		PromptSlot->SetAnchors(FAnchors(0.5f, 0.65f));
		PromptSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PromptSlot->SetPosition(FVector2D::ZeroVector);
		PromptSlot->SetSize(FVector2D(620.0f, 32.0f));
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UPHHumanStaminaWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const APlayerController* OwningController = GetOwningPlayer();
	const APHPropCharacter* PropCharacter = OwningController != nullptr
		? Cast<APHPropCharacter>(OwningController->GetPawn())
		: nullptr;
	if (InteractionPromptLabel != nullptr)
	{
		const FText Prompt = PropCharacter != nullptr
			? PropCharacter->GetPrimaryInteractionPromptText()
			: FText::GetEmpty();
		InteractionPromptLabel->SetText(Prompt);
		InteractionPromptLabel->SetRenderOpacity(Prompt.IsEmpty() ? 0.0f : 1.0f);
	}
	const bool bShowHumanStamina = PropCharacter != nullptr
		&& PropCharacter->GetActivePropForm() == nullptr
		&& PropCharacter->GetCaptureState() != EPHPropCaptureState::Downed
		&& PropCharacter->GetCaptureState() != EPHPropCaptureState::Carried
		&& PropCharacter->GetCaptureState() != EPHPropCaptureState::Retained
		&& !PHCaptureFlow::IsTerminal(PropCharacter->GetCaptureState());

	if (StaminaBar != nullptr)
	{
		StaminaBar->SetRenderOpacity(bShowHumanStamina ? 1.0f : 0.0f);
	}
	if (StaminaLabel != nullptr)
	{
		StaminaLabel->SetRenderOpacity(bShowHumanStamina ? 1.0f : 0.0f);
	}
	if (bShowHumanStamina && StaminaBar != nullptr)
	{
		StaminaBar->SetPercent(PropCharacter->GetHumanStaminaNormalized());
	}

	const EPHPropCaptureState CaptureState = PropCharacter != nullptr
		? PropCharacter->GetCaptureState()
		: EPHPropCaptureState::Free;
	const APHPlayerController* PHController = Cast<APHPlayerController>(OwningController);
	const bool bShowSpectator = PHCaptureFlow::IsTerminal(CaptureState);
	const bool bShowDownedRecovery = CaptureState == EPHPropCaptureState::Downed;
	const bool bShowCarryStruggle = CaptureState == EPHPropCaptureState::Carried;
	const bool bShowRetentionRescue = PropCharacter != nullptr
		&& PropCharacter->IsReleasingRetainedProp();
	const APHObjectiveActor* ActiveObjective = PropCharacter != nullptr
		? PropCharacter->GetActiveObjective()
		: nullptr;
	const APHExitGate* ActiveExitGate = PropCharacter != nullptr
		? PropCharacter->GetActiveExitGate()
		: nullptr;
	const bool bShowObjectiveProgress = ActiveObjective != nullptr;
	const bool bShowExitGateProgress = ActiveExitGate != nullptr;
	const bool bShowGenericProgress = bShowRetentionRescue || bShowObjectiveProgress || bShowExitGateProgress;
	const float CaptureProgress = PropCharacter == nullptr
		? 0.0f
		: (bShowRetentionRescue
			? PropCharacter->GetRetentionRescueProgressNormalized()
			: ActiveExitGate != nullptr
			? ActiveExitGate->GetOpenProgress()
			: ActiveObjective != nullptr
			? ActiveObjective->GetObjectiveProgress()
			: 0.0f);
	if (CaptureProgressBar != nullptr)
	{
		CaptureProgressBar->SetRenderOpacity(bShowGenericProgress ? 1.0f : 0.0f);
		CaptureProgressBar->SetPercent(CaptureProgress);
	}
	if (DownedRecoveryBar != nullptr)
	{
		DownedRecoveryBar->SetRenderOpacity(bShowDownedRecovery ? 1.0f : 0.0f);
		DownedRecoveryBar->SetPercent(PropCharacter != nullptr
			? PropCharacter->GetDownedRecoveryProgressNormalized()
			: 0.0f);
	}
	if (CarryStruggleBar != nullptr)
	{
		CarryStruggleBar->SetRenderOpacity(bShowCarryStruggle ? 1.0f : 0.0f);
		CarryStruggleBar->SetPercent(PropCharacter != nullptr
			? PropCharacter->GetCarryStruggleProgressNormalized()
			: 0.0f);
	}
	if (CaptureProgressLabel != nullptr)
	{
		CaptureProgressLabel->SetRenderOpacity(
			(bShowGenericProgress || bShowDownedRecovery || bShowCarryStruggle || bShowSpectator) ? 1.0f : 0.0f);
		if (bShowSpectator)
		{
			const TCHAR* TerminalPrefix = CaptureState == EPHPropCaptureState::Escaped ? TEXT("ECHAPPE") : TEXT("ELIMINE");
			CaptureProgressLabel->SetText(FText::FromString(
				PHController != nullptr && PHController->IsSpectatingSurvivor()
					? FString::Printf(TEXT("%s - SPECTATEUR | ESPACE : SUIVANT | ECHAP : MENU"), TerminalPrefix)
					: FString::Printf(TEXT("%s - ECHAP : MENU"), TerminalPrefix)));
		}
		else if (bShowRetentionRescue)
		{
			CaptureProgressLabel->SetText(FText::FromString(TEXT("LIBERATION DU PIQUET - MAINTIENS CLIC")));
		}
		else if (bShowCarryStruggle)
		{
			CaptureProgressLabel->SetText(FText::FromString(TEXT("DEBATTRE : ALTERNE GAUCHE / DROITE")));
		}
		else if (bShowDownedRecovery)
		{
			const int32 HelperCount = PropCharacter->GetRecoveryHelperCount();
			const FString RecoveryLabel = HelperCount > 0
				? FString::Printf(TEXT("SOINS / RECUPERATION  +%d ALLIE(S)"), HelperCount)
				: FString(TEXT("SOINS / RECUPERATION"));
			CaptureProgressLabel->SetText(FText::FromString(RecoveryLabel));
		}
		else if (bShowExitGateProgress)
		{
			CaptureProgressLabel->SetText(FText::FromString(TEXT("OUVERTURE DE LA PORTE DE SORTIE")));
		}
		else if (bShowObjectiveProgress)
		{
			CaptureProgressLabel->SetText(FText::FromString(TEXT("OBJECTIF - MAINTIENS CLIC")));
		}
	}

	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	const EPHMatchPhase MatchPhase = GameState != nullptr
		? GameState->GetMatchPhase()
		: EPHMatchPhase::Results;
	const bool bShowMatchTimer = GameState != nullptr && MatchPhase != EPHMatchPhase::Results;
	const bool bIsHunter = PHController != nullptr
		&& PHController->GetControlledPlayerRole() == EPHPlayerRole::Hunter;
	if (MatchTimerLabel != nullptr)
	{
		MatchTimerLabel->SetRenderOpacity(bShowMatchTimer ? 1.0f : 0.0f);
		if (bShowMatchTimer)
		{
			const int32 RemainingSeconds = FMath::CeilToInt(GameState->GetRemainingPhaseTime());
			FString TimerText;
			switch (MatchPhase)
			{
			case EPHMatchPhase::Lobby:
				if (RemainingSeconds > 0)
				{
					TimerText = FString::Printf(TEXT("LANCEMENT DANS %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60);
				}
				else
				{
					const int32 ConnectedPlayers = GameState->PlayerArray.Num();
					const int32 ExpectedPlayers = GameState->GetExpectedPlayerCount();
					TimerText = ExpectedPlayers > 0
						? FString::Printf(TEXT("EN ATTENTE DES JOUEURS - %d/%d"), ConnectedPlayers, ExpectedPlayers)
						: FString::Printf(TEXT("EN ATTENTE DES JOUEURS - %d CONNECTE(S)"), ConnectedPlayers);
				}
				break;
			case EPHMatchPhase::Preparation:
				TimerText = bIsHunter
					? FString::Printf(TEXT("PREPAREZ LA TRAQUE - %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60)
					: FString::Printf(TEXT("CACHEZ-VOUS - TRAQUE DANS %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60);
				break;
			case EPHMatchPhase::Escape:
				TimerText = bIsHunter
					? FString::Printf(TEXT("EMPECHEZ LEUR FUITE - %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60)
					: FString::Printf(TEXT("OUVREZ UNE PORTE ET FUYEZ - %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60);
				break;
			case EPHMatchPhase::Hunt:
			default:
				TimerText = bIsHunter
					? FString::Printf(TEXT("TRAQUEZ LES SURVIVANTS - %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60)
					: FString::Printf(TEXT("SURVIVEZ ET ECHAPPEZ-VOUS AVANT %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60);
				break;
			}
			MatchTimerLabel->SetText(FText::FromString(TimerText));
		}
	}
}
