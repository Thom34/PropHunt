#include "UI/PHHumanStaminaWidget.h"

#include "Characters/PHPropCharacter.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "EngineUtils.h"
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

	ObjectiveSummaryLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("ObjectiveSummaryLabel"));
	ObjectiveSummaryLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.82f, 0.20f, 1.0f)));
	ObjectiveSummaryLabel->SetJustification(ETextJustify::Center);
	if (UCanvasPanelSlot* ObjectiveSlot = RootPanel->AddChildToCanvas(ObjectiveSummaryLabel))
	{
		ObjectiveSlot->SetAnchors(FAnchors(0.5f, 0.0f));
		ObjectiveSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		ObjectiveSlot->SetPosition(FVector2D(0.0f, 58.0f));
		ObjectiveSlot->SetSize(FVector2D(620.0f, 28.0f));
	}

	UHorizontalBox* RosterPanel = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("RosterPanel"));
	if (UCanvasPanelSlot* RosterSlot = RootPanel->AddChildToCanvas(RosterPanel))
	{
		RosterSlot->SetAnchors(FAnchors(0.0f, 0.0f));
		RosterSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		RosterSlot->SetPosition(FVector2D(18.0f, 18.0f));
		RosterSlot->SetSize(FVector2D(640.0f, 78.0f));
	}
	for (int32 SlotIndex = 0; SlotIndex < 5; ++SlotIndex)
	{
		USizeBox* SlotSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SlotSize->SetWidthOverride(122.0f);
		SlotSize->SetHeightOverride(72.0f);
		RosterPanel->AddChildToHorizontalBox(SlotSize);

		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Card->SetPadding(FMargin(4.0f));
		Card->SetBrushColor(FLinearColor(0.04f, 0.05f, 0.07f, 0.88f));
		SlotSize->SetContent(Card);
		RosterCards.Add(Card);

		UVerticalBox* CardContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Card->SetContent(CardContent);

		UTextBlock* PortraitLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		PortraitLabel->SetJustification(ETextJustify::Center);
		PortraitLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		FSlateFontInfo PortraitFont = PortraitLabel->GetFont();
		PortraitFont.Size = 23;
		PortraitLabel->SetFont(PortraitFont);
		CardContent->AddChildToVerticalBox(PortraitLabel);
		RosterPortraitLabels.Add(PortraitLabel);

		UTextBlock* StatusLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		StatusLabel->SetJustification(ETextJustify::Center);
		StatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.88f, 0.92f, 1.0f)));
		FSlateFontInfo StatusFont = StatusLabel->GetFont();
		StatusFont.Size = 9;
		StatusLabel->SetFont(StatusFont);
		CardContent->AddChildToVerticalBox(StatusLabel);
		RosterStatusLabels.Add(StatusLabel);
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

void UPHHumanStaminaWidget::UpdateRosterPresentation(const APHGameState* GameState)
{
	TArray<const APHPlayerState*> Players;
	if (GameState != nullptr)
	{
		for (const APlayerState* BasePlayerState : GameState->PlayerArray)
		{
			if (const APHPlayerState* PlayerState = Cast<APHPlayerState>(BasePlayerState))
			{
				Players.Add(PlayerState);
			}
		}
	}
	Players.Sort([](const APHPlayerState& Left, const APHPlayerState& Right)
	{
		if (Left.GetPlayerId() != Right.GetPlayerId())
		{
			return Left.GetPlayerId() < Right.GetPlayerId();
		}
		return Left.GetPlayerName() < Right.GetPlayerName();
	});

	const int32 ExpectedPlayers = GameState != nullptr ? GameState->GetExpectedPlayerCount() : 0;
	const int32 VisibleSlotCount = FMath::Clamp(FMath::Max(1, FMath::Max(ExpectedPlayers, Players.Num())), 1, 5);
	for (int32 SlotIndex = 0; SlotIndex < RosterCards.Num(); ++SlotIndex)
	{
		const bool bVisible = SlotIndex < VisibleSlotCount;
		RosterCards[SlotIndex]->SetRenderOpacity(bVisible ? 1.0f : 0.0f);
		if (!bVisible)
		{
			continue;
		}

		if (!Players.IsValidIndex(SlotIndex))
		{
			RosterCards[SlotIndex]->SetBrushColor(FLinearColor(0.04f, 0.05f, 0.07f, 0.72f));
			RosterPortraitLabels[SlotIndex]->SetText(FText::FromString(TEXT("?")));
			RosterStatusLabels[SlotIndex]->SetText(FText::FromString(TEXT("PLACE LIBRE\nEN ATTENTE")));
			continue;
		}

		const APHPlayerState* PlayerState = Players[SlotIndex];
		const FString PlayerName = PlayerState->GetPlayerName().IsEmpty()
			? FString(TEXT("JOUEUR"))
			: PlayerState->GetPlayerName();
		const FString Portrait = PlayerName.Left(1).ToUpper();
		FString RoleLabel;
		FLinearColor CardColor;
		switch (PlayerState->GetPlayerRole())
		{
		case EPHPlayerRole::Hunter:
			RoleLabel = TEXT("TUEUR");
			CardColor = FLinearColor(0.32f, 0.04f, 0.04f, 0.92f);
			break;
		case EPHPlayerRole::Prop:
			RoleLabel = TEXT("SURVIVANT");
			CardColor = FLinearColor(0.03f, 0.16f, 0.28f, 0.92f);
			break;
		case EPHPlayerRole::Unassigned:
		default:
			RoleLabel = PlayerState->IsLobbyReady() ? TEXT("PRET") : TEXT("EN ATTENTE");
			CardColor = PlayerState->IsLobbyReady()
				? FLinearColor(0.05f, 0.28f, 0.12f, 0.92f)
				: FLinearColor(0.08f, 0.09f, 0.12f, 0.92f);
			break;
		}

		RosterCards[SlotIndex]->SetBrushColor(CardColor);
		RosterPortraitLabels[SlotIndex]->SetText(FText::FromString(Portrait));
		RosterStatusLabels[SlotIndex]->SetText(FText::FromString(FString::Printf(
			TEXT("%s\n%s%s"),
			*PlayerName.Left(12).ToUpper(),
			*RoleLabel,
			PlayerState->IsLobbyReady() && PlayerState->GetPlayerRole() != EPHPlayerRole::Unassigned
				? TEXT(" | PRET")
				: TEXT(""))));
	}
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
	UpdateRosterPresentation(GameState);
	const EPHMatchPhase MatchPhase = GameState != nullptr
		? GameState->GetMatchPhase()
		: EPHMatchPhase::Results;
	if (ObjectiveSummaryLabel != nullptr)
	{
		const bool bShowObjectives = GameState != nullptr
			&& (MatchPhase == EPHMatchPhase::Hunt || MatchPhase == EPHMatchPhase::Escape);
		ObjectiveSummaryLabel->SetRenderOpacity(bShowObjectives ? 1.0f : 0.0f);
		if (bShowObjectives)
		{
			const int32 RequiredObjectives = GameState->GetRequiredObjectiveCount();
			const int32 CompletedObjectives = FMath::Clamp(
				GameState->GetCompletedObjectiveCount(), 0, RequiredObjectives);
			const int32 RemainingObjectives = FMath::Max(0, RequiredObjectives - CompletedObjectives);
			ObjectiveSummaryLabel->SetText(FText::FromString(FString::Printf(
				TEXT("OBJECTIFS RESTANTS : %d  |  TERMINES : %d/%d"),
				RemainingObjectives,
				CompletedObjectives,
				RequiredObjectives)));
		}
	}
	const bool bShowMatchTimer = GameState != nullptr && MatchPhase != EPHMatchPhase::Results;
	const bool bIsHunter = PHController != nullptr
		&& PHController->GetControlledPlayerRole() == EPHPlayerRole::Hunter;
	bool bAnyExitGateOpen = false;
	if (GetWorld() != nullptr && MatchPhase == EPHMatchPhase::Escape)
	{
		for (TActorIterator<APHExitGate> GateIterator(GetWorld()); GateIterator; ++GateIterator)
		{
			if (IsValid(*GateIterator) && GateIterator->IsGateOpen())
			{
				bAnyExitGateOpen = true;
				break;
			}
		}
	}
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
				if (bAnyExitGateOpen)
				{
					TimerText = bIsHunter
						? FString::Printf(TEXT("UNE PORTE EST OUVERTE - BLOQUEZ LA FUITE - %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60)
						: FString::Printf(TEXT("PORTE OUVERTE - REJOIGNEZ LA SORTIE - %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60);
				}
				else
				{
					TimerText = bIsHunter
						? FString::Printf(TEXT("PORTES DEVERROUILLEES - EMPECHEZ LEUR OUVERTURE - %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60)
						: FString::Printf(TEXT("PORTES DEVERROUILLEES - OUVREZ-EN UNE - %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60);
				}
				break;
			case EPHMatchPhase::Hunt:
			default:
				TimerText = bIsHunter
					? FString::Printf(TEXT("TRAQUEZ LES SURVIVANTS - %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60)
					: FString::Printf(TEXT("SURVIVEZ ET ECHAPPEZ-VOUS AVANT %02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60);
				break;
			}
			if (MatchPhase == EPHMatchPhase::Lobby && GameState->PlayerArray.Num() == 2
				&& PHController != nullptr
				&& PHController->GetControlledPlayerRole() == EPHPlayerRole::Unassigned)
			{
				int32 ReadyPlayerCount = 0;
				for (const APlayerState* BasePlayerState : GameState->PlayerArray)
				{
					const APHPlayerState* PlayerState = Cast<APHPlayerState>(BasePlayerState);
					ReadyPlayerCount += PlayerState != nullptr && PlayerState->IsLobbyReady() ? 1 : 0;
				}
				const APHPlayerState* LocalPlayerState = PHController->GetPlayerState<APHPlayerState>();
				TimerText += LocalPlayerState != nullptr && LocalPlayerState->IsLobbyReady()
					? FString::Printf(TEXT(" | PRET CONFIRME (%d/2)"), ReadyPlayerCount)
					: FString::Printf(TEXT(" | R : PRET POUR LE 1V1 (%d/2)"), ReadyPlayerCount);
			}
			MatchTimerLabel->SetText(FText::FromString(TimerText));
		}
	}
}
