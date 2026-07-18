#include "UI/PHMatchResultsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/PHGameState.h"
#include "Game/PHPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Online/PHSessionSubsystem.h"

namespace
{
FString GetRoleText(const EPHPlayerRole Role)
{
	switch (Role)
	{
	case EPHPlayerRole::Hunter:
		return TEXT("TUEUR");
	case EPHPlayerRole::Prop:
		return TEXT("SURVIVANT");
	case EPHPlayerRole::Unassigned:
	default:
		return TEXT("-");
	}
}

FString GetOutcomeText(const EPHMatchPlayerOutcome Outcome)
{
	switch (Outcome)
	{
	case EPHMatchPlayerOutcome::Hunter:
		return TEXT("TUEUR");
	case EPHMatchPlayerOutcome::Survived:
		return TEXT("SURVECU");
	case EPHMatchPlayerOutcome::Eliminated:
		return TEXT("ELIMINE");
	case EPHMatchPlayerOutcome::Disconnected:
	default:
		return TEXT("DECONNECTE");
	}
}

FString GetReasonText(const EPHMatchEndReason EndReason)
{
	switch (EndReason)
	{
	case EPHMatchEndReason::AllPropsGone:
		return TEXT("TOUS LES SURVIVANTS ONT ETE ELIMINES");
	case EPHMatchEndReason::TimeExpired:
		return TEXT("LE TEMPS EST ECOULE");
	case EPHMatchEndReason::HunterDisconnected:
		return TEXT("LE TUEUR S'EST DECONNECTE");
	case EPHMatchEndReason::AtLeastOnePropEscaped:
		return TEXT("AU MOINS UN SURVIVANT S'EST ECHAPPE");
	case EPHMatchEndReason::None:
	default:
		return TEXT("RESULTAT DE LA PARTIE");
	}
}

bool IsVictoryForRole(const EPHPlayerRole Role, const EPHMatchEndReason EndReason)
{
	const bool bHunterVictory = EndReason == EPHMatchEndReason::AllPropsGone;
	const bool bSurvivorVictory = EndReason == EPHMatchEndReason::TimeExpired
		|| EndReason == EPHMatchEndReason::HunterDisconnected
		|| EndReason == EPHMatchEndReason::AtLeastOnePropEscaped;
	return (Role == EPHPlayerRole::Hunter && bHunterVictory)
		|| (Role == EPHPlayerRole::Prop && bSurvivorVictory);
}

uint32 BuildSnapshotSignature(const FPHMatchResultsSnapshot& Snapshot)
{
	uint32 Signature = HashCombine(GetTypeHash(Snapshot.MatchSeed), GetTypeHash(static_cast<uint8>(Snapshot.EndReason)));
	Signature = HashCombine(Signature, GetTypeHash(Snapshot.CompletedObjectiveCount));
	for (const FPHMatchResultRow& Row : Snapshot.PlayerRows)
	{
		Signature = HashCombine(Signature, GetTypeHash(Row.PlayerId));
		Signature = HashCombine(Signature, GetTypeHash(Row.PlayerName));
		Signature = HashCombine(Signature, GetTypeHash(static_cast<uint8>(Row.PlayerRole)));
		Signature = HashCombine(Signature, GetTypeHash(static_cast<uint8>(Row.Outcome)));
		Signature = HashCombine(Signature, GetTypeHash(Row.TotalScore));
		Signature = HashCombine(Signature, GetTypeHash(Row.GrantedScore));
		Signature = HashCombine(Signature, GetTypeHash(Row.Stats.ObjectivesCompleted));
		Signature = HashCombine(Signature, GetTypeHash(Row.Stats.AlliesRescued));
		Signature = HashCombine(Signature, GetTypeHash(Row.Stats.HunterDowns));
		Signature = HashCombine(Signature, GetTypeHash(Row.Stats.HunterRetentions));
		Signature = HashCombine(Signature, GetTypeHash(Row.Stats.HunterEliminations));
	}
	return Signature;
}
}

void UPHMatchResultsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree == nullptr)
	{
		return;
	}

	UCanvasPanel* RootPanel = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("MatchResultsRoot"));
	WidgetTree->RootWidget = RootPanel;

	ResultsPanel = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("MatchResultsPanel"));
	ResultsPanel->SetBrushColor(FLinearColor(0.012f, 0.018f, 0.028f, 0.96f));
	ResultsPanel->SetPadding(FMargin(30.0f, 22.0f));
	ResultsPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* PanelSlot = RootPanel->AddChildToCanvas(ResultsPanel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PanelSlot->SetSize(FVector2D(900.0f, 500.0f));
	}

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("MatchResultsContent"));
	ResultsPanel->SetContent(Content);

	auto AddCenteredLabel = [this, Content](
		const FName Name, const FString& Text, const int32 FontSize,
		const FLinearColor Color, const float BottomPadding) -> UTextBlock*
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Label->SetText(FText::FromString(Text));
		Label->SetJustification(ETextJustify::Center);
		Label->SetColorAndOpacity(FSlateColor(Color));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FontSize;
		Label->SetFont(Font);
		if (UVerticalBoxSlot* Slot = Content->AddChildToVerticalBox(Label))
		{
			Slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, BottomPadding));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Label;
	};

	AddCenteredLabel(TEXT("MatchResultsTitle"), TEXT("FIN DE PARTIE"), 36, FLinearColor::White, 6.0f);
	OutcomeLabel = AddCenteredLabel(
		TEXT("MatchResultsOutcome"), TEXT("RESULTAT"), 28,
		FLinearColor(0.95f, 0.64f, 0.12f, 1.0f), 4.0f);
	ReasonLabel = AddCenteredLabel(
		TEXT("MatchResultsReason"), TEXT("PARTIE TERMINEE"), 17,
		FLinearColor(0.78f, 0.84f, 0.92f, 1.0f), 14.0f);

	PlayerRowsBox = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("MatchResultsPlayerRows"));
	if (UVerticalBoxSlot* RowsSlot = Content->AddChildToVerticalBox(PlayerRowsBox))
	{
		RowsSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
		RowsSlot->SetHorizontalAlignment(HAlign_Center);
	}

	CountdownLabel = AddCenteredLabel(
		TEXT("MatchResultsCountdown"), TEXT("RETOUR AU LOBBY"), 16,
		FLinearColor(0.62f, 0.68f, 0.76f, 1.0f), 8.0f);

	ContinueButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("MatchResultsContinueButton"));
	ContinueButton->SetBackgroundColor(FLinearColor(0.10f, 0.42f, 0.22f, 1.0f));
	ContinueButton->SetVisibility(ESlateVisibility::Collapsed);
	UTextBlock* ContinueLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("MatchResultsContinueLabel"));
	ContinueLabel->SetText(FText::FromString(TEXT("CONTINUER VERS LE LOBBY")));
	ContinueLabel->SetJustification(ETextJustify::Center);
	ContinueLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ContinueButton->AddChild(ContinueLabel);
	ContinueButton->OnClicked.AddDynamic(this, &UPHMatchResultsWidget::HandleContinueClicked);
	if (UVerticalBoxSlot* ButtonSlot = Content->AddChildToVerticalBox(ContinueButton))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	SetVisibility(ESlateVisibility::Visible);
}

void UPHMatchResultsWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	FPHMatchResultsSnapshot Snapshot;
	EPHPlayerRole LocalRole = EPHPlayerRole::Unassigned;
	int32 LocalPlayerId = INDEX_NONE;
	FString LocalPlayerName;
	bool bCachedLobbySnapshot = false;

	const APlayerController* OwningController = GetOwningPlayer();
	const APHGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<APHGameState>() : nullptr;
	if (GameState != nullptr && GameState->GetMatchPhase() == EPHMatchPhase::Results
		&& GameState->HasFinalMatchResults())
	{
		Snapshot = GameState->GetMatchResultsSnapshot();
		if (const APHPlayerState* LocalPlayerState = OwningController != nullptr
			? OwningController->GetPlayerState<APHPlayerState>()
			: nullptr)
		{
			LocalRole = LocalPlayerState->GetPlayerRole();
			LocalPlayerId = LocalPlayerState->GetPlayerId();
			LocalPlayerName = LocalPlayerState->GetPlayerName();
		}
	}
	else if (GetWorld() != nullptr && GetWorld()->GetNetMode() == NM_Standalone)
	{
		if (const UGameInstance* GameInstance = GetGameInstance())
		{
			if (const UPHSessionSubsystem* Sessions = GameInstance->GetSubsystem<UPHSessionSubsystem>();
				Sessions != nullptr && Sessions->IsCurrentWorldResultsMap()
				&& Sessions->HasCachedMatchResults())
			{
				Snapshot = Sessions->GetCachedMatchResults();
				LocalRole = Sessions->GetCachedResultsLocalRole();
				LocalPlayerId = Sessions->GetCachedResultsLocalPlayerId();
				LocalPlayerName = Sessions->GetCachedResultsLocalPlayerName();
				bCachedLobbySnapshot = true;
			}
		}
	}

	if (!Snapshot.bFinalized)
	{
		if (ResultsPanel != nullptr)
		{
			ResultsPanel->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	ResultsPanel->SetVisibility(ESlateVisibility::Visible);
	if (UCanvasPanelSlot* PanelSlot = Cast<UCanvasPanelSlot>(ResultsPanel->Slot))
	{
		PanelSlot->SetSize(bCachedLobbySnapshot
			? FVector2D(900.0f, 500.0f)
			: FVector2D(640.0f, 230.0f));
	}
	const bool bVictory = IsVictoryForRole(LocalRole, Snapshot.EndReason);
	if (OutcomeLabel != nullptr)
	{
		OutcomeLabel->SetText(FText::FromString(bVictory ? TEXT("VICTOIRE") : TEXT("DEFAITE")));
		OutcomeLabel->SetColorAndOpacity(FSlateColor(
			bVictory
				? FLinearColor(0.20f, 0.82f, 0.38f, 1.0f)
				: FLinearColor(0.92f, 0.18f, 0.12f, 1.0f)));
	}
	if (ReasonLabel != nullptr)
	{
		ReasonLabel->SetText(FText::FromString(GetReasonText(Snapshot.EndReason)));
	}

	if (PlayerRowsBox != nullptr)
	{
		PlayerRowsBox->SetVisibility(
			bCachedLobbySnapshot ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	const uint32 SnapshotSignature = BuildSnapshotSignature(Snapshot);
	if (bCachedLobbySnapshot
		&& (!bHasRenderedSnapshot || SnapshotSignature != LastSnapshotSignature))
	{
		RebuildPlayerRows(Snapshot, LocalPlayerId, LocalPlayerName);
		LastSnapshotSignature = SnapshotSignature;
		bHasRenderedSnapshot = true;
	}

	if (ContinueButton != nullptr)
	{
		ContinueButton->SetVisibility(
			bCachedLobbySnapshot ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (CountdownLabel != nullptr)
	{
		const FString CountdownText = bCachedLobbySnapshot
			? FString(TEXT("RESULTATS DE LA DERNIERE PARTIE"))
			: FString::Printf(TEXT("SALLE DES RESULTATS DANS %d s"),
				GameState != nullptr ? FMath::CeilToInt(GameState->GetRemainingPhaseTime()) : 0);
		CountdownLabel->SetText(FText::FromString(CountdownText));
	}
}

void UPHMatchResultsWidget::RebuildPlayerRows(
	const FPHMatchResultsSnapshot& Snapshot,
	const int32 LocalPlayerId,
	const FString& LocalPlayerName)
{
	if (PlayerRowsBox == nullptr || WidgetTree == nullptr)
	{
		return;
	}

	PlayerRowsBox->ClearChildren();
	auto AddRow = [this](
		const FString& Name, const FString& Role, const FString& Outcome,
		const FString& Actions, const FString& Score, const FString& Granted,
		const FLinearColor& Color, const bool bHeader)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		auto AddCell = [this, Row, Color, bHeader](const FString& Text, const float Width)
		{
			USizeBox* Cell = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			Cell->SetWidthOverride(Width);
			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Label->SetText(FText::FromString(Text));
			Label->SetColorAndOpacity(FSlateColor(Color));
			Label->SetJustification(ETextJustify::Center);
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = bHeader ? 13 : 14;
			Label->SetFont(Font);
			Cell->SetContent(Label);
			if (UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Cell))
			{
				Slot->SetPadding(FMargin(2.0f, 3.0f));
				Slot->SetVerticalAlignment(VAlign_Center);
			}
		};

		AddCell(Name, 210.0f);
		AddCell(Role, 100.0f);
		AddCell(Outcome, 120.0f);
		AddCell(Actions, 270.0f);
		AddCell(Score, 90.0f);
		AddCell(Granted, 90.0f);
		if (UVerticalBoxSlot* Slot = PlayerRowsBox->AddChildToVerticalBox(Row))
		{
			Slot->SetHorizontalAlignment(HAlign_Center);
		}
	};

	AddRow(
		TEXT("JOUEUR"), TEXT("ROLE"), TEXT("ETAT"), TEXT("ACTIONS"), TEXT("SCORE"), TEXT("GAIN"),
		FLinearColor(0.65f, 0.72f, 0.82f, 1.0f), true);

	for (const FPHMatchResultRow& Row : Snapshot.PlayerRows)
	{
		const FString Actions = Row.PlayerRole == EPHPlayerRole::Hunter
			? FString::Printf(TEXT("SOL %d  |  PIQUETS %d  |  ELIM. %d"),
				Row.Stats.HunterDowns, Row.Stats.HunterRetentions, Row.Stats.HunterEliminations)
			: FString::Printf(TEXT("OBJECTIFS %d  |  SAUVETAGES %d"),
				Row.Stats.ObjectivesCompleted, Row.Stats.AlliesRescued);
		const bool bLocalRow = LocalPlayerId != INDEX_NONE
			? Row.PlayerId == LocalPlayerId
			: !LocalPlayerName.IsEmpty() && Row.PlayerName == LocalPlayerName;
		AddRow(
			Row.PlayerName,
			GetRoleText(Row.PlayerRole),
			GetOutcomeText(Row.Outcome),
			Actions,
			FString::FromInt(Row.TotalScore),
			FString::FromInt(Row.GrantedScore),
			bLocalRow
				? FLinearColor(0.95f, 0.76f, 0.24f, 1.0f)
				: FLinearColor::White,
			false);
	}
}

void UPHMatchResultsWidget::HandleContinueClicked()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UPHSessionSubsystem* Sessions = GameInstance->GetSubsystem<UPHSessionSubsystem>())
		{
			Sessions->ContinueFromResultsToLobby();
		}
	}
}
