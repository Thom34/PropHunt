#include "UI/PHInGameMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Game/PHPlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Online/PHSessionSubsystem.h"

namespace
{
UTextBlock* AddMenuButtonLabel(UWidgetTree& WidgetTree, UButton& Button, const TCHAR* Name, const TCHAR* Text)
{
	UTextBlock* Label = WidgetTree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Label->SetText(FText::FromString(Text));
	Label->SetJustification(ETextJustify::Center);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Button.AddChild(Label);
	return Label;
}
}

void UPHInGameMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree == nullptr)
	{
		return;
	}

	UBorder* RootBorder = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("InGameMenuBackground"));
	RootBorder->SetBrushColor(FLinearColor(0.01f, 0.015f, 0.025f, 0.94f));
	RootBorder->SetPadding(FMargin(96.0f));
	WidgetTree->RootWidget = RootBorder;

	UVerticalBox* Menu = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("InGameMenu"));
	RootBorder->SetContent(Menu);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("InGameMenuTitle"));
	Title->SetText(FText::FromString(TEXT("MENU")));
	Title->SetJustification(ETextJustify::Center);
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.72f, 0.12f, 1.0f)));
	Title->SetFont(FSlateFontInfo(Title->GetFont().FontObject, 34));
	if (UVerticalBoxSlot* TitleSlot = Menu->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 36.0f));
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	}

	ResumeButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("ResumeGameButton"));
	ResumeButton->SetBackgroundColor(FLinearColor(0.08f, 0.36f, 0.18f, 1.0f));
	AddMenuButtonLabel(*WidgetTree, *ResumeButton, TEXT("ResumeGameLabel"), TEXT("REVENIR AU JEU"));
	if (UVerticalBoxSlot* ResumeSlot = Menu->AddChildToVerticalBox(ResumeButton))
	{
		ResumeSlot->SetPadding(FMargin(0.0f, 8.0f));
		ResumeSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	ReturnToLobbyButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("ReturnToLobbyButton"));
	ReturnToLobbyButton->SetBackgroundColor(FLinearColor(0.12f, 0.22f, 0.48f, 1.0f));
	AddMenuButtonLabel(*WidgetTree, *ReturnToLobbyButton, TEXT("ReturnToLobbyLabel"), TEXT("RETOUR A L'ACCUEIL"));
	if (UVerticalBoxSlot* ReturnSlot = Menu->AddChildToVerticalBox(ReturnToLobbyButton))
	{
		ReturnSlot->SetPadding(FMargin(0.0f, 8.0f));
		ReturnSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	QuitButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("QuitGameButton"));
	QuitButton->SetBackgroundColor(FLinearColor(0.55f, 0.08f, 0.08f, 1.0f));
	AddMenuButtonLabel(*WidgetTree, *QuitButton, TEXT("QuitGameLabel"), TEXT("QUITTER LE JEU"));
	if (UVerticalBoxSlot* QuitSlot = Menu->AddChildToVerticalBox(QuitButton))
	{
		QuitSlot->SetPadding(FMargin(0.0f, 8.0f));
		QuitSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	ResumeButton->OnClicked.AddDynamic(this, &UPHInGameMenuWidget::HandleResumeClicked);
	ReturnToLobbyButton->OnClicked.AddDynamic(this, &UPHInGameMenuWidget::HandleReturnToLobbyClicked);
	QuitButton->OnClicked.AddDynamic(this, &UPHInGameMenuWidget::HandleQuitClicked);
}

void UPHInGameMenuWidget::HandleResumeClicked()
{
	if (APHPlayerController* PlayerController = Cast<APHPlayerController>(GetOwningPlayer()))
	{
		PlayerController->CloseInGameMenu();
	}
}

void UPHInGameMenuWidget::HandleReturnToLobbyClicked()
{
	if (APHPlayerController* PlayerController = Cast<APHPlayerController>(GetOwningPlayer()))
	{
		PlayerController->CloseInGameMenu();
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UPHSessionSubsystem* SessionSubsystem = GameInstance->GetSubsystem<UPHSessionSubsystem>())
		{
			SessionSubsystem->LeaveSessionAndReturnToLobby();
		}
	}
}

void UPHInGameMenuWidget::HandleQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
