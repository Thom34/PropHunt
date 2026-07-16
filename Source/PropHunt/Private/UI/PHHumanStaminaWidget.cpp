#include "UI/PHHumanStaminaWidget.h"

#include "Characters/PHPropCharacter.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Game/PHGameState.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Objectives/PHObjectiveActor.h"

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
		TimerSlot->SetSize(FVector2D(240.0f, 32.0f));
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
	const bool bShowHumanStamina = PropCharacter != nullptr
		&& PropCharacter->GetActivePropForm() == nullptr
		&& PropCharacter->GetCaptureState() != EPHPropCaptureState::Downed
		&& PropCharacter->GetCaptureState() != EPHPropCaptureState::Carried
		&& PropCharacter->GetCaptureState() != EPHPropCaptureState::Retained
		&& PropCharacter->GetCaptureState() != EPHPropCaptureState::Eliminated;

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
	const bool bShowDownedRecovery = CaptureState == EPHPropCaptureState::Downed;
	const bool bShowCarryStruggle = CaptureState == EPHPropCaptureState::Carried;
	const bool bShowRetentionRescue = PropCharacter != nullptr
		&& PropCharacter->IsReleasingRetainedProp();
	const APHObjectiveActor* ActiveObjective = PropCharacter != nullptr
		? PropCharacter->GetActiveObjective()
		: nullptr;
	const bool bShowObjectiveProgress = ActiveObjective != nullptr;
	const bool bShowCaptureProgress = bShowDownedRecovery || bShowCarryStruggle
		|| bShowRetentionRescue || bShowObjectiveProgress;
	const float CaptureProgress = PropCharacter == nullptr
		? 0.0f
		: (bShowRetentionRescue
			? PropCharacter->GetRetentionRescueProgressNormalized()
			: bShowCarryStruggle
			? PropCharacter->GetCarryStruggleProgressNormalized()
			: bShowDownedRecovery
			? PropCharacter->GetDownedRecoveryProgressNormalized()
			: ActiveObjective != nullptr
			? ActiveObjective->GetObjectiveProgress()
			: 0.0f);
	if (CaptureProgressBar != nullptr)
	{
		CaptureProgressBar->SetRenderOpacity(bShowCaptureProgress ? 1.0f : 0.0f);
		CaptureProgressBar->SetPercent(CaptureProgress);
	}
	if (CaptureProgressLabel != nullptr)
	{
		CaptureProgressLabel->SetRenderOpacity(bShowCaptureProgress ? 1.0f : 0.0f);
		if (bShowRetentionRescue)
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
				? FString::Printf(TEXT("RELEVEMENT  +%d ALLIE(S)"), HelperCount)
				: FString(TEXT("RELEVEMENT"));
			CaptureProgressLabel->SetText(FText::FromString(RecoveryLabel));
		}
		else if (bShowObjectiveProgress)
		{
			CaptureProgressLabel->SetText(FText::FromString(TEXT("OBJECTIF - MAINTIENS CLIC")));
		}
	}

	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	const bool bShowMatchTimer = GameState != nullptr
		&& GameState->GetMatchPhase() == EPHMatchPhase::Hunt;
	if (MatchTimerLabel != nullptr)
	{
		MatchTimerLabel->SetRenderOpacity(bShowMatchTimer ? 1.0f : 0.0f);
		if (bShowMatchTimer)
		{
			const int32 RemainingSeconds = FMath::CeilToInt(GameState->GetRemainingPhaseTime());
			MatchTimerLabel->SetText(FText::FromString(FString::Printf(
				TEXT("%02d:%02d"),
				RemainingSeconds / 60,
				RemainingSeconds % 60)));
		}
	}
}
