#include "UI/PHMatchmakingWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Game/PHPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"

namespace
{
UTextBlock* AddButtonLabel(UWidgetTree& WidgetTree, UButton& Button, const TCHAR* Name, const TCHAR* Text)
{
	UTextBlock* Label = WidgetTree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Label->SetText(FText::FromString(Text));
	Label->SetJustification(ETextJustify::Center);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Button.AddChild(Label);
	return Label;
}
}

void UPHMatchmakingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree == nullptr)
	{
		return;
	}

	UBorder* RootBorder = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("MatchmakingBackground"));
	RootBorder->SetBrushColor(FLinearColor(0.015f, 0.02f, 0.03f, 0.96f));
	RootBorder->SetPadding(FMargin(72.0f));
	WidgetTree->RootWidget = RootBorder;

	UVerticalBox* Menu = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("MatchmakingMenu"));
	RootBorder->SetContent(Menu);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("MatchmakingTitle"));
	Title->SetText(FText::FromString(TEXT("PROP HUNT - PROTOTYPE STEAM")));
	Title->SetJustification(ETextJustify::Center);
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.72f, 0.12f, 1.0f)));
	Title->SetFont(FSlateFontInfo(Title->GetFont().FontObject, 34));
	if (UVerticalBoxSlot* TitleSlot = Menu->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	}

	UTextBlock* VersionLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("MatchmakingVersion"));
	const UPHSessionSubsystem* SessionDefaults = GetDefault<UPHSessionSubsystem>();
	const int32 DisplayProtocolVersion = SessionDefaults != nullptr
		? SessionDefaults->GetProtocolVersion()
		: 0;
	VersionLabel->SetText(FText::FromString(FString::Printf(
		TEXT("PROTOCOLE RESEAU %d"),
		DisplayProtocolVersion)));
	VersionLabel->SetJustification(ETextJustify::Center);
	VersionLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.68f, 0.76f, 1.0f)));
	if (UVerticalBoxSlot* VersionSlot = Menu->AddChildToVerticalBox(VersionLabel))
	{
		VersionSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 28.0f));
		VersionSlot->SetHorizontalAlignment(HAlign_Center);
	}

	HostButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("HostHunterLobbyButton"));
	HostButton->SetBackgroundColor(FLinearColor(0.55f, 0.10f, 0.08f, 1.0f));
	AddButtonLabel(*WidgetTree, *HostButton, TEXT("HostHunterLobbyLabel"), TEXT("JOUER TUEUR - CREER UN LOBBY"));
	if (UVerticalBoxSlot* HostButtonSlot = Menu->AddChildToVerticalBox(HostButton))
	{
		HostButtonSlot->SetPadding(FMargin(0.0f, 8.0f));
		HostButtonSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	FindButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("FindPropLobbyButton"));
	FindButton->SetBackgroundColor(FLinearColor(0.08f, 0.28f, 0.55f, 1.0f));
	AddButtonLabel(*WidgetTree, *FindButton, TEXT("FindPropLobbyLabel"), TEXT("JOUER PROP - REJOINDRE LE PREMIER LOBBY"));
	if (UVerticalBoxSlot* FindButtonSlot = Menu->AddChildToVerticalBox(FindButton))
	{
		FindButtonSlot->SetPadding(FMargin(0.0f, 8.0f));
		FindButtonSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	RandomButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("FindRandomRoleButton"));
	RandomButton->SetBackgroundColor(FLinearColor(0.28f, 0.12f, 0.48f, 1.0f));
	AddButtonLabel(*WidgetTree, *RandomButton, TEXT("FindRandomRoleLabel"), TEXT("ROLE ALEATOIRE - TUEUR OU SURVIVANT"));
	if (UVerticalBoxSlot* RandomButtonSlot = Menu->AddChildToVerticalBox(RandomButton))
	{
		RandomButtonSlot->SetPadding(FMargin(0.0f, 8.0f));
		RandomButtonSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	QuitButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("QuitMatchmakingButton"));
	QuitButton->SetBackgroundColor(FLinearColor(0.48f, 0.06f, 0.06f, 1.0f));
	AddButtonLabel(*WidgetTree, *QuitButton, TEXT("QuitMatchmakingLabel"), TEXT("QUITTER LE JEU"));
	if (UVerticalBoxSlot* QuitButtonSlot = Menu->AddChildToVerticalBox(QuitButton))
	{
		QuitButtonSlot->SetPadding(FMargin(0.0f, 22.0f, 0.0f, 8.0f));
		QuitButtonSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	StatusLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("MatchmakingStatus"));
	StatusLabel->SetText(FText::FromString(TEXT("Connexion à Steam...")));
	StatusLabel->SetJustification(ETextJustify::Center);
	StatusLabel->SetAutoWrapText(true);
	StatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	if (UVerticalBoxSlot* StatusSlot = Menu->AddChildToVerticalBox(StatusLabel))
	{
		StatusSlot->SetPadding(FMargin(0.0f, 32.0f, 0.0f, 0.0f));
		StatusSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	HostButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleHostClicked);
	FindButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleFindClicked);
	RandomButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleRandomClicked);
	QuitButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleQuitClicked);
}

void UPHMatchmakingWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UPHSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->OnMatchmakingStateChanged.AddDynamic(this, &UPHMatchmakingWidget::HandleStateChanged);
		Sessions->OnLobbySuggested.AddDynamic(this, &UPHMatchmakingWidget::HandleLobbySuggested);
		Sessions->OnAvailabilityChanged.AddDynamic(this, &UPHMatchmakingWidget::HandleAvailabilityChanged);
		Sessions->OnMatchmakingFinished.AddDynamic(this, &UPHMatchmakingWidget::HandleOperationFinished);
		SetStatus(Sessions->IsSteamAvailable()
			? FString::Printf(
				TEXT("Steam connecté. Protocole %d. Choisis Tueur pour héberger ou Prop pour rejoindre."),
				Sessions->GetProtocolVersion())
			: FString::Printf(
				TEXT("Steam indisponible. Protocole %d. Lance l’App 1551300 depuis sa vraie fiche Steam ; sous Linux utilise Proton, pas un raccourci non-Steam."),
				Sessions->GetProtocolVersion()));
		SetButtonsEnabled(Sessions->IsSteamAvailable());
		if (Sessions->IsSteamAvailable())
		{
			RefreshAvailability();
			GetWorld()->GetTimerManager().SetTimer(
				AvailabilityRefreshTimer,
				this,
				&UPHMatchmakingWidget::RefreshAvailability,
				10.0f,
				true);
		}
	}

	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		PlayerController->bShowMouseCursor = true;
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(InputMode);
	}
}

void UPHMatchmakingWidget::NativeDestruct()
{
	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(AvailabilityRefreshTimer);
	}
	if (UPHSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->OnMatchmakingStateChanged.RemoveAll(this);
		Sessions->OnLobbySuggested.RemoveAll(this);
		Sessions->OnAvailabilityChanged.RemoveAll(this);
		Sessions->OnMatchmakingFinished.RemoveAll(this);
	}
	Super::NativeDestruct();
}

void UPHMatchmakingWidget::HandleHostClicked()
{
	SetButtonsEnabled(false);
	SetStatus(TEXT("Création du lobby Hunter..."));
	if (UPHSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->HostHunterLobby();
	}
}

void UPHMatchmakingWidget::HandleFindClicked()
{
	SetButtonsEnabled(false);
	SetStatus(TEXT("Recherche du premier lobby Hunter disponible..."));
	if (UPHSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->FindPropLobby(true);
	}
}

void UPHMatchmakingWidget::HandleRandomClicked()
{
	SetButtonsEnabled(false);
	SetStatus(TEXT("Recherche d’un rôle aléatoire disponible..."));
	if (UPHSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->FindRandomRole();
	}
}

void UPHMatchmakingWidget::HandleQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UPHMatchmakingWidget::HandleStateChanged(const EPHMatchmakingState NewState)
{
	switch (NewState)
	{
	case EPHMatchmakingState::CreatingLobby:
		SetStatus(TEXT("Création du lobby Hunter..."));
		break;
	case EPHMatchmakingState::Searching:
		SetStatus(TEXT("Recherche des lobbies Hunter disponibles..."));
		break;
	case EPHMatchmakingState::Joining:
		SetStatus(TEXT("Connexion au lobby prioritaire..."));
		break;
	case EPHMatchmakingState::Leaving:
		SetStatus(TEXT("Fermeture de la session Steam..."));
		break;
	case EPHMatchmakingState::InSession:
		if (APHPlayerController* PlayerController = Cast<APHPlayerController>(GetOwningPlayer()))
		{
			PlayerController->DismissLocalMatchmakingWidget();
		}
		else
		{
			RemoveFromParent();
		}
		break;
	default:
		break;
	}
}

void UPHMatchmakingWidget::HandleLobbySuggested(const FPHLobbySummary& Lobby)
{
	const FString Host = Lobby.HostName.IsEmpty() ? TEXT("Hunter Steam") : Lobby.HostName;
	SetStatus(FString::Printf(
		TEXT("Lobby prioritaire : %s - %d place(s) libre(s) - ping %d ms"),
		*Host,
		Lobby.OpenSlots,
		Lobby.PingMilliseconds));
}

void UPHMatchmakingWidget::HandleAvailabilityChanged(const FPHMatchmakingAvailability& NewAvailability)
{
	if (NewAvailability.UnassignedHunterLobbies > 0)
	{
		SetStatus(FString::Printf(
			TEXT("%d lobby(s) attend(ent) encore un Tueur : utilise Aléatoire."),
			NewAvailability.UnassignedHunterLobbies));
		if (HostButton != nullptr)
		{
			HostButton->SetIsEnabled(false);
		}
		if (FindButton != nullptr)
		{
			FindButton->SetIsEnabled(false);
		}
		if (RandomButton != nullptr)
		{
			RandomButton->SetIsEnabled(true);
		}
		return;
	}

	if (NewAvailability.AssignedHunterLobbies > 0)
	{
		SetStatus(FString::Printf(
			TEXT("%d partie(s), Tueur déjà attribué. Survivant ou Aléatoire disponible%s."),
			NewAvailability.AssignedHunterLobbies,
			NewAvailability.bCanCreateHunterLobby ? TEXT(" ; une place Tueur reste créable") : TEXT(" ; Tueur indisponible")));
	}
	else
	{
		SetStatus(TEXT("Aucune partie Steam visible. Tueur ou Aléatoire peut créer la première."));
	}
	SetButtonsEnabled(true);
}

void UPHMatchmakingWidget::HandleOperationFinished(const bool bSucceeded, const FString& Message)
{
	SetStatus(Message);
	if (!bSucceeded)
	{
		SetButtonsEnabled(true);
	}
}

void UPHMatchmakingWidget::SetButtonsEnabled(const bool bEnabled)
{
	if (HostButton != nullptr)
	{
		HostButton->SetIsEnabled(bEnabled);
	}
	if (FindButton != nullptr)
	{
		FindButton->SetIsEnabled(bEnabled);
	}
	if (RandomButton != nullptr)
	{
		RandomButton->SetIsEnabled(bEnabled);
	}
}

void UPHMatchmakingWidget::RefreshAvailability()
{
	if (UPHSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->RefreshAvailability();
	}
}

void UPHMatchmakingWidget::SetStatus(const FString& Status)
{
	if (StatusLabel != nullptr)
	{
		StatusLabel->SetText(FText::FromString(Status));
	}
}

UPHSessionSubsystem* UPHMatchmakingWidget::GetSessionSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UPHSessionSubsystem>() : nullptr;
}
