#include "UI/PHMatchmakingWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Game/PHPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/PHMatchmakingGatewaySubsystem.h"
#include "Online/PHSessionSubsystem.h"
#include "Online/PHSocialInviteSubsystem.h"
#include "UI/PHSocialFriendButton.h"
#include "Styling/SlateTypes.h"

namespace
{
constexpr double FriendInviteCooldownSeconds = 15.0;

UTextBlock* AddButtonLabel(UWidgetTree& WidgetTree, UButton& Button, const TCHAR* Name, const TCHAR* Text)
{
	UTextBlock* Label = WidgetTree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Label->SetText(FText::FromString(Text));
	Label->SetJustification(ETextJustify::Center);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	FSlateFontInfo Font = Label->GetFont();
	Font.Size = 18;
	Label->SetFont(Font);
	Button.AddChild(Label);
	return Label;
}

void ApplyRoundedButtonStyle(UButton& Button, const FLinearColor& BaseColor)
{
	FButtonStyle Style = Button.GetStyle();
	auto ConfigureBrush = [](FSlateBrush& Brush, const FLinearColor& Color, const float Radius)
	{
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Color);
		Brush.OutlineSettings.CornerRadii = FVector4(Radius);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.Color = FSlateColor(FLinearColor(0.42f, 0.49f, 0.58f, 0.55f));
		Brush.OutlineSettings.Width = 1.0f;
	};
	ConfigureBrush(Style.Normal, BaseColor, 12.0f);
	ConfigureBrush(Style.Hovered, BaseColor * 1.24f, 12.0f);
	ConfigureBrush(Style.Pressed, BaseColor * 0.78f, 12.0f);
	ConfigureBrush(Style.Disabled, FLinearColor(BaseColor.R, BaseColor.G, BaseColor.B, 0.35f), 12.0f);
	Button.SetStyle(Style);
	Button.SetBackgroundColor(FLinearColor::White);
}

void ApplyRoundedBorderStyle(
	UBorder& Border,
	const FLinearColor& BackgroundColor,
	const FLinearColor& OutlineColor,
	const float Radius)
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(BackgroundColor);
	Brush.OutlineSettings.CornerRadii = FVector4(Radius);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.Color = FSlateColor(OutlineColor);
	Brush.OutlineSettings.Width = 1.5f;
	Border.SetBrush(Brush);
}

void ApplyRoundedAvatarBrush(UImage& Image, UTexture2D* Texture)
{
	FSlateBrush Brush;
	Brush.SetResourceObject(Texture);
	Brush.ImageSize = FVector2D(52.0f, 52.0f);
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(FLinearColor::White);
	Brush.OutlineSettings.CornerRadii = FVector4(10.0f);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.Color = FSlateColor(FLinearColor(0.18f, 0.78f, 1.0f, 0.72f));
	Brush.OutlineSettings.Width = 1.0f;
	Image.SetBrush(Brush);
}

UImage* AddButtonIcon(
	UWidgetTree& WidgetTree,
	UButton& Button,
	const TCHAR* Name,
	UTexture2D* Texture,
	const FVector2D Size = FVector2D(54.0f, 54.0f))
{
	UImage* Icon = WidgetTree.ConstructWidget<UImage>(UImage::StaticClass(), Name);
	Icon->SetBrushFromTexture(Texture, false);
	Icon->SetDesiredSizeOverride(Size);
	Button.AddChild(Icon);
	if (UButtonSlot* Slot = Cast<UButtonSlot>(Icon->Slot))
	{
		Slot->SetPadding(FMargin(8.0f));
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetVerticalAlignment(VAlign_Center);
	}
	return Icon;
}

int32 GetLobbySurvivorMarkerSlot(const AActor& MarkerActor)
{
	for (const FName& Tag : MarkerActor.Tags)
	{
		const FString TagText = Tag.ToString();
		const FString Prefix(TEXT("PHLobbySurvivorPreview_"));
		if (TagText.StartsWith(Prefix))
		{
			return FCString::Atoi(*TagText.RightChop(Prefix.Len()));
		}
	}
	return INDEX_NONE;
}

bool IsFullLobbyPreviewRequested()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return FParse::Param(FCommandLine::Get(), TEXT("PHPreviewFullLobby"));
#endif
}

int32 GetLobbyPreviewSurvivorCount()
{
#if UE_BUILD_SHIPPING
	return 0;
#else
	int32 PreviewCount = 4;
	FParse::Value(FCommandLine::Get(), TEXT("PHPreviewSurvivorCount="), PreviewCount);
	return FMath::Clamp(PreviewCount, 0, 4);
#endif
}

bool IsHunterLobbyPreviewRequested()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return IsFullLobbyPreviewRequested()
		&& FParse::Param(FCommandLine::Get(), TEXT("PHPreviewHunter"));
#endif
}

bool IsFriendsPanelPreviewRequested()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return FParse::Param(FCommandLine::Get(), TEXT("PHPreviewFriendsPanel"));
#endif
}

bool IsRoleTooltipPreviewRequested()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return FParse::Param(FCommandLine::Get(), TEXT("PHPreviewRoleTooltip"));
#endif
}

bool IsCancelButtonPreviewRequested()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return FParse::Param(FCommandLine::Get(), TEXT("PHPreviewCancelButton"));
#endif
}

bool IsClientUpdateNoticePreviewRequested()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return FParse::Param(FCommandLine::Get(), TEXT("PHPreviewClientUpdateRequired"));
#endif
}
}

void UPHMatchmakingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree == nullptr)
	{
		return;
	}

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("MatchmakingCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	if (SurvivorRoleIcon == nullptr)
	{
		SurvivorRoleIcon = LoadObject<UTexture2D>(nullptr,
			TEXT("/Game/PropHunt/UI/Matchmaking/T_UI_Role_Survivor.T_UI_Role_Survivor"));
	}
	if (KillerRoleIcon == nullptr)
	{
		KillerRoleIcon = LoadObject<UTexture2D>(nullptr,
			TEXT("/Game/PropHunt/UI/Matchmaking/T_UI_Role_Killer.T_UI_Role_Killer"));
	}
	if (RandomRoleIcon == nullptr)
	{
		RandomRoleIcon = LoadObject<UTexture2D>(nullptr,
			TEXT("/Game/PropHunt/UI/Matchmaking/T_UI_Role_Random.T_UI_Role_Random"));
	}
	if (QuitIcon == nullptr)
	{
		QuitIcon = LoadObject<UTexture2D>(nullptr,
			TEXT("/Game/PropHunt/UI/Matchmaking/T_UI_Action_Quit.T_UI_Action_Quit"));
	}
	if (CustomizationIcon == nullptr)
	{
		CustomizationIcon = LoadObject<UTexture2D>(nullptr,
			TEXT("/Game/PropHunt/UI/Matchmaking/T_UI_Action_Customization.T_UI_Action_Customization"));
	}
	if (GameLogo == nullptr)
	{
		GameLogo = LoadObject<UTexture2D>(nullptr,
			TEXT("/Game/PropHunt/UI/Matchmaking/T_UI_Logo_PropCaper.T_UI_Logo_PropCaper"));
	}

	GameLogoImage = WidgetTree->ConstructWidget<UImage>(
		UImage::StaticClass(), TEXT("PropCaperLogo"));
	GameLogoImage->SetBrushFromTexture(GameLogo, true);
	GameLogoImage->SetDesiredSizeOverride(FVector2D(720.0f, 190.0f));
	GameLogoImage->SetVisibility(GameLogo != nullptr ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* LogoSlot = RootCanvas->AddChildToCanvas(GameLogoImage))
	{
		LogoSlot->SetAnchors(FAnchors(0.5f, 0.0f));
		LogoSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		LogoSlot->SetPosition(FVector2D(0.0f, 18.0f));
		LogoSlot->SetSize(FVector2D(720.0f, 190.0f));
		LogoSlot->SetZOrder(15);
	}

	PodiumInviteButtons.Reset();
	PodiumNameCards.Reset();
	PodiumNameLabels.Reset();
	for (int32 SlotIndex = 0; SlotIndex < 4; ++SlotIndex)
	{
		UButton* PodiumButton = WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(),
			*FString::Printf(TEXT("InvitePodiumButton%d"), SlotIndex + 1));
		ApplyRoundedButtonStyle(*PodiumButton, FLinearColor(0.012f, 0.075f, 0.11f, 0.94f));
		PodiumButton->SetVisibility(ESlateVisibility::Collapsed);
		UTextBlock* PlusLabel = AddButtonLabel(
			*WidgetTree,
			*PodiumButton,
			*FString::Printf(TEXT("InvitePodiumPlus%d"), SlotIndex + 1),
			TEXT("+"));
		FSlateFontInfo PlusFont = PlusLabel->GetFont();
		PlusFont.Size = 25;
		PlusLabel->SetFont(PlusFont);
		PlusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.34f, 0.88f, 1.0f, 1.0f)));
		if (UCanvasPanelSlot* PodiumButtonSlot = RootCanvas->AddChildToCanvas(PodiumButton))
		{
			PodiumButtonSlot->SetAutoSize(false);
			PodiumButtonSlot->SetSize(FVector2D(46.0f, 46.0f));
			PodiumButtonSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			PodiumButtonSlot->SetZOrder(18);
		}
		PodiumButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleInviteFriendClicked);
		PodiumInviteButtons.Add(PodiumButton);

		UBorder* NameCard = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), *FString::Printf(TEXT("PodiumNameCard%d"), SlotIndex + 1));
		ApplyRoundedBorderStyle(
			*NameCard,
			FLinearColor(0.006f, 0.018f, 0.03f, 0.88f),
			FLinearColor(0.18f, 0.70f, 0.88f, 0.68f),
			8.0f);
		NameCard->SetPadding(FMargin(10.0f, 5.0f));
		NameCard->SetVisibility(ESlateVisibility::Collapsed);
		UTextBlock* NameLabel = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), *FString::Printf(TEXT("PodiumNameLabel%d"), SlotIndex + 1));
		NameLabel->SetJustification(ETextJustify::Center);
		NameLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.91f, 0.96f, 1.0f, 1.0f)));
		NameLabel->SetFont(FSlateFontInfo(NameLabel->GetFont().FontObject, 12));
		NameCard->SetContent(NameLabel);
		if (UCanvasPanelSlot* NameCardSlot = RootCanvas->AddChildToCanvas(NameCard))
		{
			NameCardSlot->SetSize(FVector2D(172.0f, 34.0f));
			NameCardSlot->SetAlignment(FVector2D(0.5f, 0.0f));
			NameCardSlot->SetZOrder(17);
		}
		PodiumNameCards.Add(NameCard);
		PodiumNameLabels.Add(NameLabel);
	}

	MainMenuPanel = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("MatchmakingSidePanel"));
	MainMenuPanel->SetBrushColor(FLinearColor::Transparent);
	MainMenuPanel->SetPadding(FMargin(10.0f));
	if (UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(MainMenuPanel))
	{
		PanelSlot->SetAnchors(FAnchors(0.0f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.0f, 0.5f));
		PanelSlot->SetPosition(FVector2D(24.0f, 24.0f));
		PanelSlot->SetSize(FVector2D(96.0f, 430.0f));
		PanelSlot->SetZOrder(10);
	}

	UVerticalBox* Menu = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("MatchmakingMenu"));
	MainMenuPanel->SetContent(Menu);

	SteamIdentityCard = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("SteamIdentityCard"));
	ApplyRoundedBorderStyle(
		*SteamIdentityCard,
		FLinearColor(0.0f, 0.0f, 0.0f, 0.0f),
		FLinearColor(0.16f, 0.58f, 0.76f, 0.20f),
		12.0f);
	SteamIdentityCard->SetPadding(FMargin(10.0f));
	if (UCanvasPanelSlot* IdentitySlot = RootCanvas->AddChildToCanvas(SteamIdentityCard))
	{
		IdentitySlot->SetAnchors(FAnchors(0.0f, 0.0f));
		IdentitySlot->SetPosition(FVector2D(24.0f, 24.0f));
		IdentitySlot->SetSize(FVector2D(280.0f, 72.0f));
		IdentitySlot->SetZOrder(15);
	}
	UHorizontalBox* SteamIdentityRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("SteamIdentityRow"));
	SteamIdentityCard->SetContent(SteamIdentityRow);
	LocalSteamAvatar = WidgetTree->ConstructWidget<UImage>(
		UImage::StaticClass(), TEXT("LocalSteamAvatar"));
	LocalSteamAvatar->SetDesiredSizeOverride(FVector2D(52.0f, 52.0f));
	if (UHorizontalBoxSlot* AvatarSlot = SteamIdentityRow->AddChildToHorizontalBox(LocalSteamAvatar))
	{
		AvatarSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
		AvatarSlot->SetVerticalAlignment(VAlign_Center);
	}
	LocalSteamName = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("LocalSteamName"));
	LocalSteamName->SetText(FText::GetEmpty());
	LocalSteamName->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	LocalSteamName->SetFont(FSlateFontInfo(LocalSteamName->GetFont().FontObject, 18));
	if (UHorizontalBoxSlot* NameSlot = SteamIdentityRow->AddChildToHorizontalBox(LocalSteamName))
	{
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NameSlot->SetVerticalAlignment(VAlign_Center);
	}

	ClientUpdateNoticeCard = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("ClientUpdateRequiredCard"));
	ApplyRoundedBorderStyle(
		*ClientUpdateNoticeCard,
		FLinearColor(0.12f, 0.018f, 0.012f, 0.985f),
		FLinearColor(1.0f, 0.30f, 0.16f, 0.94f),
		14.0f);
	ClientUpdateNoticeCard->SetPadding(FMargin(20.0f, 14.0f));
	ClientUpdateNoticeCard->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* UpdateNoticeSlot = RootCanvas->AddChildToCanvas(ClientUpdateNoticeCard))
	{
		UpdateNoticeSlot->SetAnchors(FAnchors(0.5f, 0.0f));
		UpdateNoticeSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		UpdateNoticeSlot->SetPosition(FVector2D(0.0f, 180.0f));
		UpdateNoticeSlot->SetSize(FVector2D(540.0f, 112.0f));
		UpdateNoticeSlot->SetZOrder(40);
	}
	UVerticalBox* UpdateNoticeContent = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("ClientUpdateRequiredContent"));
	ClientUpdateNoticeCard->SetContent(UpdateNoticeContent);
	ClientUpdateNoticeTitle = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("ClientUpdateRequiredTitle"));
	ClientUpdateNoticeTitle->SetText(FText::FromString(TEXT("CLIENT OBSOLÈTE")));
	ClientUpdateNoticeTitle->SetJustification(ETextJustify::Center);
	ClientUpdateNoticeTitle->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.40f, 0.22f, 1.0f)));
	ClientUpdateNoticeTitle->SetFont(FSlateFontInfo(ClientUpdateNoticeTitle->GetFont().FontObject, 22));
	UpdateNoticeContent->AddChildToVerticalBox(ClientUpdateNoticeTitle);
	ClientUpdateNoticeDescription = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("ClientUpdateRequiredDescription"));
	ClientUpdateNoticeDescription->SetText(FText::FromString(
		TEXT("Relance Steam et mets à jour Prop Caper pour rejoindre la file d'attente.")));
	ClientUpdateNoticeDescription->SetJustification(ETextJustify::Center);
	ClientUpdateNoticeDescription->SetAutoWrapText(true);
	ClientUpdateNoticeDescription->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.88f, 0.82f, 1.0f)));
	ClientUpdateNoticeDescription->SetFont(FSlateFontInfo(ClientUpdateNoticeDescription->GetFont().FontObject, 16));
	if (UVerticalBoxSlot* DescriptionSlot = UpdateNoticeContent->AddChildToVerticalBox(ClientUpdateNoticeDescription))
	{
		DescriptionSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
		DescriptionSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	RoleTooltipCard = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("RoleTooltipCard"));
	ApplyRoundedBorderStyle(
		*RoleTooltipCard,
		FLinearColor(0.007f, 0.013f, 0.024f, 0.985f),
		FLinearColor(0.18f, 0.78f, 1.0f, 0.86f),
		12.0f);
	RoleTooltipCard->SetPadding(FMargin(14.0f));
	RoleTooltipCard->SetVisibility(ESlateVisibility::Collapsed);
	RoleTooltipCard->SetRenderOpacity(0.0f);
	if (UCanvasPanelSlot* TooltipSlot = RootCanvas->AddChildToCanvas(RoleTooltipCard))
	{
		TooltipSlot->SetAnchors(FAnchors(0.0f, 0.0f));
		TooltipSlot->SetAlignment(FVector2D(0.0f, 0.5f));
		TooltipSlot->SetPosition(FVector2D(146.0f, 360.0f));
		TooltipSlot->SetSize(FVector2D(350.0f, 138.0f));
		TooltipSlot->SetZOrder(30);
	}
	UHorizontalBox* TooltipRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("RoleTooltipRow"));
	RoleTooltipCard->SetContent(TooltipRow);
	RoleTooltipIconContainer = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("RoleTooltipIconSize"));
	RoleTooltipIconContainer->SetWidthOverride(70.0f);
	RoleTooltipIconContainer->SetHeightOverride(70.0f);
	RoleTooltipIcon = WidgetTree->ConstructWidget<UImage>(
		UImage::StaticClass(), TEXT("RoleTooltipIcon"));
	RoleTooltipIcon->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
	RoleTooltipIconContainer->SetContent(RoleTooltipIcon);
	if (UHorizontalBoxSlot* TooltipIconSlot = TooltipRow->AddChildToHorizontalBox(RoleTooltipIconContainer))
	{
		TooltipIconSlot->SetPadding(FMargin(0.0f, 0.0f, 14.0f, 0.0f));
		TooltipIconSlot->SetVerticalAlignment(VAlign_Center);
	}
	UVerticalBox* TooltipText = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("RoleTooltipText"));
	if (UHorizontalBoxSlot* TooltipTextSlot = TooltipRow->AddChildToHorizontalBox(TooltipText))
	{
		TooltipTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TooltipTextSlot->SetVerticalAlignment(VAlign_Center);
	}
	UTextBlock* TooltipEyebrow = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("RoleTooltipEyebrow"));
	TooltipEyebrow->SetText(FText::FromString(TEXT("CHOISIS TON CAMP")));
	TooltipEyebrow->SetColorAndOpacity(FSlateColor(FLinearColor(0.42f, 0.49f, 0.58f, 1.0f)));
	TooltipEyebrow->SetFont(FSlateFontInfo(TooltipEyebrow->GetFont().FontObject, 9));
	TooltipText->AddChildToVerticalBox(TooltipEyebrow);
	RoleTooltipTitle = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("RoleTooltipTitle"));
	RoleTooltipTitle->SetFont(FSlateFontInfo(RoleTooltipTitle->GetFont().FontObject, 18));
	if (UVerticalBoxSlot* TooltipTitleSlot = TooltipText->AddChildToVerticalBox(RoleTooltipTitle))
	{
		TooltipTitleSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 5.0f));
	}
	RoleTooltipDescription = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("RoleTooltipDescription"));
	RoleTooltipDescription->SetAutoWrapText(true);
	RoleTooltipDescription->SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.91f, 0.95f, 1.0f)));
	RoleTooltipDescription->SetFont(FSlateFontInfo(RoleTooltipDescription->GetFont().FontObject, 11));
	TooltipText->AddChildToVerticalBox(RoleTooltipDescription);

	LobbyPanel = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("MatchmakingLobbyPanel"));
	ApplyRoundedBorderStyle(
		*LobbyPanel,
		FLinearColor(0.004f, 0.014f, 0.026f, 0.88f),
		FLinearColor(0.17f, 0.66f, 0.86f, 0.58f),
		4.0f);
	LobbyPanel->SetPadding(FMargin(18.0f, 12.0f));
	LobbyPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* LobbyPanelSlot = RootCanvas->AddChildToCanvas(LobbyPanel))
	{
		LobbyPanelSlot->SetAnchors(FAnchors(0.5f, 0.0f));
		LobbyPanelSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		LobbyPanelSlot->SetPosition(FVector2D(0.0f, 214.0f));
		LobbyPanelSlot->SetSize(FVector2D(420.0f, 82.0f));
		LobbyPanelSlot->SetZOrder(16);
	}
	UVerticalBox* LobbyContent = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("MatchmakingLobbyContent"));
	LobbyPanel->SetContent(LobbyContent);
	auto AddLobbyLabel = [this, LobbyContent](
		const FName Name, const FString& Text, const int32 FontSize, const FLinearColor& Color)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Label->SetText(FText::FromString(Text));
		Label->SetJustification(ETextJustify::Center);
		Label->SetColorAndOpacity(FSlateColor(Color));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FontSize;
		Label->SetFont(Font);
		if (UVerticalBoxSlot* Slot = LobbyContent->AddChildToVerticalBox(Label))
		{
			Slot->SetPadding(FMargin(0.0f, 3.0f));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Label;
	};
	LobbyTitleLabel = AddLobbyLabel(
		TEXT("MatchmakingLobbyTitle"), TEXT("ÉQUIPE"), 9,
		FLinearColor(0.42f, 0.78f, 0.92f, 1.0f));
	LobbyTitleLabel->SetVisibility(ESlateVisibility::Collapsed);
	LobbyRosterCountLabel = AddLobbyLabel(
		TEXT("MatchmakingLobbyRosterCount"), TEXT("EN ATTENTE"), 18,
		FLinearColor(0.45f, 0.82f, 1.0f, 1.0f));
	LobbyRoleLabel = AddLobbyLabel(
		TEXT("MatchmakingLobbyRole"), TEXT("0 / 4 SURVIVANTS"), 12, FLinearColor::White);
	LobbyCountdownLabel = AddLobbyLabel(
		TEXT("MatchmakingLobbyCountdown"), TEXT("EN ATTENTE"), 12,
		FLinearColor(0.45f, 0.82f, 1.0f, 1.0f));
	LobbyCountdownLabel->SetVisibility(ESlateVisibility::Collapsed);

	UHorizontalBox* ReadyIcons = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("MatchmakingLobbyReadyIcons"));
	if (UCanvasPanelSlot* ReadyIconsSlot = RootCanvas->AddChildToCanvas(ReadyIcons))
	{
		ReadyIconsSlot->SetAnchors(FAnchors(0.0f, 1.0f));
		ReadyIconsSlot->SetAlignment(FVector2D(0.0f, 1.0f));
		ReadyIconsSlot->SetPosition(FVector2D(24.0f, -24.0f));
		ReadyIconsSlot->SetSize(FVector2D(600.0f, 108.0f));
		ReadyIconsSlot->SetZOrder(20);
	}
	LobbySurvivorReadyIcons.Reset();
	for (int32 SlotIndex = 0; SlotIndex < 4; ++SlotIndex)
	{
		UImage* SurvivorIcon = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), *FString::Printf(TEXT("LobbySurvivorReadyIcon%d"), SlotIndex + 1));
		FSlateBrush SurvivorReadyBrush;
		SurvivorReadyBrush.SetResourceObject(SurvivorRoleIcon);
		SurvivorReadyBrush.DrawAs = ESlateBrushDrawType::Image;
		SurvivorReadyBrush.ImageSize = FVector2D(76.0f, 76.0f);
		SurvivorReadyBrush.SetUVRegion(FBox2f(FVector2f(0.30f, 0.20f), FVector2f(0.70f, 0.72f)));
		SurvivorIcon->SetBrush(SurvivorReadyBrush);
		SurvivorIcon->SetDesiredSizeOverride(FVector2D(76.0f, 76.0f));
		SurvivorIcon->SetColorAndOpacity(FLinearColor(0.40f, 0.86f, 1.0f, 1.0f));
		SurvivorIcon->SetVisibility(ESlateVisibility::Collapsed);
		if (UHorizontalBoxSlot* SurvivorIconSlot = ReadyIcons->AddChildToHorizontalBox(SurvivorIcon))
		{
			// Keep the original roster-slot footprint while drawing the portrait
			// itself 20% smaller and centered inside that stable space.
			SurvivorIconSlot->SetPadding(FMargin(10.0f, 0.0f));
			SurvivorIconSlot->SetVerticalAlignment(VAlign_Center);
		}
		LobbySurvivorReadyIcons.Add(SurvivorIcon);
	}
	LobbyHunterReadyIcon = WidgetTree->ConstructWidget<UImage>(
		UImage::StaticClass(), TEXT("LobbyHunterReadyIcon"));
	FSlateBrush HunterReadyBrush;
	HunterReadyBrush.SetResourceObject(KillerRoleIcon);
	HunterReadyBrush.DrawAs = ESlateBrushDrawType::Image;
	HunterReadyBrush.ImageSize = FVector2D(86.0f, 86.0f);
	HunterReadyBrush.SetUVRegion(FBox2f(FVector2f(0.30f, 0.20f), FVector2f(0.70f, 0.72f)));
	LobbyHunterReadyIcon->SetBrush(HunterReadyBrush);
	LobbyHunterReadyIcon->SetDesiredSizeOverride(FVector2D(86.0f, 86.0f));
	LobbyHunterReadyIcon->SetColorAndOpacity(FLinearColor(1.0f, 0.30f, 0.22f, 1.0f));
	LobbyHunterReadyIcon->SetVisibility(ESlateVisibility::Collapsed);
	if (UHorizontalBoxSlot* HunterReadySlot = ReadyIcons->AddChildToHorizontalBox(LobbyHunterReadyIcon))
	{
		HunterReadySlot->SetPadding(FMargin(27.0f, 0.0f, 11.0f, 0.0f));
		HunterReadySlot->SetVerticalAlignment(VAlign_Center);
	}

	HunterPreviewCard = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("HunterPrivatePreviewCard"));
	HunterPreviewCard->SetBrushColor(FLinearColor(0.28f, 0.025f, 0.02f, 0.96f));
	HunterPreviewCard->SetPadding(FMargin(14.0f, 10.0f));
	HunterPreviewCard->SetVisibility(ESlateVisibility::Collapsed);
	UTextBlock* HunterPreviewLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("HunterPrivatePreviewLabel"));
	HunterPreviewLabel->SetText(FText::FromString(TEXT("VOTRE TUEUR")));
	HunterPreviewLabel->SetJustification(ETextJustify::Center);
	HunterPreviewLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.22f, 0.16f, 1.0f)));
	FSlateFontInfo HunterFont = HunterPreviewLabel->GetFont();
	HunterFont.Size = 19;
	HunterPreviewLabel->SetFont(HunterFont);
	HunterPreviewCard->SetContent(HunterPreviewLabel);
	if (UVerticalBoxSlot* HunterPreviewSlot = Menu->AddChildToVerticalBox(HunterPreviewCard))
	{
		HunterPreviewSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 4.0f));
		HunterPreviewSlot->SetHorizontalAlignment(HAlign_Right);
	}

	RoleActionsPanel = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("MatchmakingRoleActions"));
	if (UVerticalBoxSlot* RoleActionsSlot = Menu->AddChildToVerticalBox(RoleActionsPanel))
	{
		RoleActionsSlot->SetPadding(FMargin(0.0f, 10.0f));
		RoleActionsSlot->SetHorizontalAlignment(HAlign_Center);
	}

	FindButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("PropTicketButton"));
	ApplyRoundedButtonStyle(*FindButton, FLinearColor(0.025f, 0.14f, 0.24f, 0.98f));
	AddButtonIcon(*WidgetTree, *FindButton, TEXT("PropTicketIcon"), SurvivorRoleIcon);
	if (UVerticalBoxSlot* FindButtonSlot = RoleActionsPanel->AddChildToVerticalBox(FindButton))
	{
		FindButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 3.0f));
		FindButtonSlot->SetHorizontalAlignment(HAlign_Center);
	}

	HostButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("HunterTicketButton"));
	ApplyRoundedButtonStyle(*HostButton, FLinearColor(0.24f, 0.025f, 0.02f, 0.98f));
	AddButtonIcon(*WidgetTree, *HostButton, TEXT("HunterTicketIcon"), KillerRoleIcon);
	if (UVerticalBoxSlot* HostButtonSlot = RoleActionsPanel->AddChildToVerticalBox(HostButton))
	{
		HostButtonSlot->SetPadding(FMargin(0.0f, 3.0f));
		HostButtonSlot->SetHorizontalAlignment(HAlign_Center);
	}

	RandomButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("FindRandomRoleButton"));
	ApplyRoundedButtonStyle(*RandomButton, FLinearColor(0.18f, 0.055f, 0.30f, 0.98f));
	AddButtonIcon(*WidgetTree, *RandomButton, TEXT("FindRandomRoleIcon"), RandomRoleIcon);
	if (UVerticalBoxSlot* RandomButtonSlot = RoleActionsPanel->AddChildToVerticalBox(RandomButton))
	{
		RandomButtonSlot->SetPadding(FMargin(0.0f, 3.0f));
		RandomButtonSlot->SetHorizontalAlignment(HAlign_Center);
	}

	CustomizationButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("CustomizationButton"));
	ApplyRoundedButtonStyle(*CustomizationButton, FLinearColor(0.035f, 0.16f, 0.17f, 0.98f));
	AddButtonIcon(*WidgetTree, *CustomizationButton, TEXT("CustomizationIcon"), CustomizationIcon);
	if (UVerticalBoxSlot* CustomizationButtonSlot = RoleActionsPanel->AddChildToVerticalBox(CustomizationButton))
	{
		CustomizationButtonSlot->SetPadding(FMargin(0.0f, 3.0f));
		CustomizationButtonSlot->SetHorizontalAlignment(HAlign_Center);
	}

	QuitButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("QuitMatchmakingButton"));
	ApplyRoundedButtonStyle(*QuitButton, FLinearColor(0.055f, 0.035f, 0.025f, 0.90f));
	UHorizontalBox* QuitContent = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("QuitMatchmakingContent"));
	QuitButton->AddChild(QuitContent);
	if (UButtonSlot* QuitContentSlot = Cast<UButtonSlot>(QuitContent->Slot))
	{
		QuitContentSlot->SetHorizontalAlignment(HAlign_Center);
		QuitContentSlot->SetVerticalAlignment(VAlign_Center);
	}
	UImage* QuitImage = WidgetTree->ConstructWidget<UImage>(
		UImage::StaticClass(), TEXT("QuitMatchmakingIcon"));
	QuitImage->SetBrushFromTexture(QuitIcon, false);
	QuitImage->SetDesiredSizeOverride(FVector2D(27.0f, 27.0f));
	if (UHorizontalBoxSlot* QuitImageSlot = QuitContent->AddChildToHorizontalBox(QuitImage))
	{
		QuitImageSlot->SetPadding(FMargin(8.0f, 7.0f, 7.0f, 7.0f));
		QuitImageSlot->SetVerticalAlignment(VAlign_Center);
	}
	UTextBlock* QuitLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("QuitMatchmakingLabel"));
	QuitLabel->SetText(FText::FromString(TEXT("QUITTER LE JEU")));
	QuitLabel->SetJustification(ETextJustify::Center);
	QuitLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.78f, 0.68f, 1.0f)));
	QuitLabel->SetFont(FSlateFontInfo(QuitLabel->GetFont().FontObject, 11));
	if (UHorizontalBoxSlot* QuitLabelSlot = QuitContent->AddChildToHorizontalBox(QuitLabel))
	{
		QuitLabelSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		QuitLabelSlot->SetVerticalAlignment(VAlign_Center);
	}
	if (UCanvasPanelSlot* QuitButtonSlot = RootCanvas->AddChildToCanvas(QuitButton))
	{
		QuitButtonSlot->SetAnchors(FAnchors(0.0f, 1.0f));
		QuitButtonSlot->SetAlignment(FVector2D(0.0f, 1.0f));
		QuitButtonSlot->SetPosition(FVector2D(24.0f, -24.0f));
		QuitButtonSlot->SetSize(FVector2D(184.0f, 46.0f));
		QuitButtonSlot->SetZOrder(20);
	}

	CancelButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("CancelMatchmakingButton"));
	ApplyRoundedButtonStyle(*CancelButton, FLinearColor(0.34f, 0.035f, 0.025f, 0.98f));
	UTextBlock* CancelLabel = AddButtonLabel(
		*WidgetTree, *CancelButton, TEXT("CancelMatchmakingLabel"), TEXT("‹   RETOUR AU MENU"));
	CancelLabel->SetFont(FSlateFontInfo(CancelLabel->GetFont().FontObject, 13));
	CancelLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.34f, 0.24f, 1.0f)));
	if (UButtonSlot* CancelContentSlot = Cast<UButtonSlot>(CancelLabel->Slot))
	{
		CancelContentSlot->SetPadding(FMargin(18.0f, 12.0f));
		CancelContentSlot->SetHorizontalAlignment(HAlign_Center);
		CancelContentSlot->SetVerticalAlignment(VAlign_Center);
	}
	CancelButton->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* CancelButtonSlot = RootCanvas->AddChildToCanvas(CancelButton))
	{
		CancelButtonSlot->SetAnchors(FAnchors(1.0f, 1.0f));
		CancelButtonSlot->SetAlignment(FVector2D(1.0f, 1.0f));
		CancelButtonSlot->SetPosition(FVector2D(-24.0f, -24.0f));
		CancelButtonSlot->SetSize(FVector2D(196.0f, 46.0f));
		CancelButtonSlot->SetZOrder(20);
	}

	StatusLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("MatchmakingStatus"));
	StatusLabel->SetText(FText::GetEmpty());
	StatusLabel->SetJustification(ETextJustify::Center);
	StatusLabel->SetAutoWrapText(true);
	StatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	StatusLabel->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* StatusSlot = Menu->AddChildToVerticalBox(StatusLabel))
	{
		StatusSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		StatusSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	UTextBlock* PatchLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("ReleaseVersionLabel"));
	PatchLabel->SetText(FText::FromString(TEXT("0.1.1907001")));
	PatchLabel->SetJustification(ETextJustify::Center);
	PatchLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.34f, 0.39f, 0.45f, 0.82f)));
	PatchLabel->SetFont(FSlateFontInfo(PatchLabel->GetFont().FontObject, 11));
	if (UCanvasPanelSlot* PatchSlot = RootCanvas->AddChildToCanvas(PatchLabel))
	{
		PatchSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		PatchSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		PatchSlot->SetPosition(FVector2D(0.0f, -12.0f));
		PatchSlot->SetSize(FVector2D(180.0f, 22.0f));
		PatchSlot->SetZOrder(15);
	}

	FriendsDismissButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("SocialFriendsDismissButton"));
	FriendsDismissButton->SetBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	FriendsDismissButton->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* DismissSlot = RootCanvas->AddChildToCanvas(FriendsDismissButton))
	{
		DismissSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		DismissSlot->SetOffsets(FMargin(0.0f));
		DismissSlot->SetZOrder(20);
	}

	FriendsPanel = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("SocialFriendsSidePanel"));
	ApplyRoundedBorderStyle(
		*FriendsPanel,
		FLinearColor(0.004f, 0.012f, 0.022f, 0.90f),
		FLinearColor(0.16f, 0.58f, 0.76f, 0.52f),
		4.0f);
	FriendsPanel->SetPadding(FMargin(14.0f));
	FriendsPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* FriendsPanelSlot = RootCanvas->AddChildToCanvas(FriendsPanel))
	{
		FriendsPanelSlot->SetAnchors(FAnchors(0.0f, 0.5f));
		FriendsPanelSlot->SetAlignment(FVector2D(0.0f, 0.5f));
		FriendsPanelSlot->SetPosition(FVector2D(24.0f, 0.0f));
		FriendsPanelSlot->SetSize(FVector2D(350.0f, 620.0f));
		FriendsPanelSlot->SetZOrder(21);
	}

	UVerticalBox* FriendsContent = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("SocialFriendsContent"));
	FriendsPanel->SetContent(FriendsContent);

	UHorizontalBox* FriendsHeader = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("SocialFriendsHeader"));
	if (UVerticalBoxSlot* HeaderSlot = FriendsContent->AddChildToVerticalBox(FriendsHeader))
	{
		HeaderSlot->SetPadding(FMargin(2.0f, 2.0f, 0.0f, 4.0f));
	}

	UTextBlock* FriendsTitle = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("SocialFriendsTitle"));
	FriendsTitle->SetText(FText::FromString(TEXT("INVITER UN AMI")));
	FriendsTitle->SetJustification(ETextJustify::Left);
	FriendsTitle->SetColorAndOpacity(FSlateColor(FLinearColor(0.93f, 0.78f, 0.36f, 1.0f)));
	FriendsTitle->SetFont(FSlateFontInfo(FriendsTitle->GetFont().FontObject, 19));
	if (UHorizontalBoxSlot* TitleSlot = FriendsHeader->AddChildToHorizontalBox(FriendsTitle))
	{
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
	}
	CloseFriendsButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("CloseSteamFriendsButton"));
	ApplyRoundedButtonStyle(*CloseFriendsButton, FLinearColor(0.05f, 0.07f, 0.095f, 1.0f));
	UTextBlock* CloseLabel = AddButtonLabel(
		*WidgetTree, *CloseFriendsButton, TEXT("CloseSteamFriendsLabel"), TEXT("X"));
	CloseLabel->SetFont(FSlateFontInfo(CloseLabel->GetFont().FontObject, 13));
	CloseLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.76f, 0.82f, 1.0f)));
	if (UHorizontalBoxSlot* CloseSlot = FriendsHeader->AddChildToHorizontalBox(CloseFriendsButton))
	{
		CloseSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
		CloseSlot->SetVerticalAlignment(VAlign_Center);
	}

	FriendsStatusLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("SocialFriendsStatus"));
	FriendsStatusLabel->SetText(FText::FromString(TEXT("Chargement des amis Steam...")));
	FriendsStatusLabel->SetJustification(ETextJustify::Left);
	FriendsStatusLabel->SetAutoWrapText(true);
	FriendsStatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.48f, 0.58f, 0.68f, 1.0f)));
	FriendsStatusLabel->SetFont(FSlateFontInfo(FriendsStatusLabel->GetFont().FontObject, 11));
	if (UVerticalBoxSlot* FriendsStatusSlot = FriendsContent->AddChildToVerticalBox(FriendsStatusLabel))
	{
		FriendsStatusSlot->SetPadding(FMargin(2.0f, 0.0f, 0.0f, 12.0f));
	}

	RefreshFriendsButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("RefreshSteamFriendsButton"));
	ApplyRoundedButtonStyle(*RefreshFriendsButton, FLinearColor(0.035f, 0.075f, 0.115f, 1.0f));
	UTextBlock* RefreshLabel = AddButtonLabel(
		*WidgetTree, *RefreshFriendsButton, TEXT("RefreshSteamFriendsLabel"), TEXT("ACTUALISER"));
	RefreshLabel->SetFont(FSlateFontInfo(RefreshLabel->GetFont().FontObject, 11));
	RefreshLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.60f, 0.72f, 0.82f, 1.0f)));
	if (UVerticalBoxSlot* RefreshSlot = FriendsContent->AddChildToVerticalBox(RefreshFriendsButton))
	{
		RefreshSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	}

	FriendsList = WidgetTree->ConstructWidget<UScrollBox>(
		UScrollBox::StaticClass(), TEXT("SteamFriendsList"));
	FriendsList->SetScrollBarVisibility(ESlateVisibility::Visible);
	// Keep the invite row safely separated from the scrollbar so a scroll gesture
	// cannot accidentally land on the friend's clickable entry.
	FriendsList->SetScrollbarPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
	if (UVerticalBoxSlot* ListSlot = FriendsContent->AddChildToVerticalBox(FriendsList))
	{
		ListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ListSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	}

	HostButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleHostClicked);
	FindButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleFindClicked);
	RandomButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleRandomClicked);
	FindButton->OnHovered.AddDynamic(this, &UPHMatchmakingWidget::HandleSurvivorTooltipHovered);
	HostButton->OnHovered.AddDynamic(this, &UPHMatchmakingWidget::HandleKillerTooltipHovered);
	RandomButton->OnHovered.AddDynamic(this, &UPHMatchmakingWidget::HandleRandomTooltipHovered);
	CancelButton->OnHovered.AddDynamic(this, &UPHMatchmakingWidget::HandleCancelTooltipHovered);
	CustomizationButton->OnHovered.AddDynamic(this, &UPHMatchmakingWidget::HandleCustomizationTooltipHovered);
	QuitButton->OnHovered.AddDynamic(this, &UPHMatchmakingWidget::HandleQuitTooltipHovered);
	FindButton->OnUnhovered.AddDynamic(this, &UPHMatchmakingWidget::HandleRoleTooltipUnhovered);
	HostButton->OnUnhovered.AddDynamic(this, &UPHMatchmakingWidget::HandleRoleTooltipUnhovered);
	RandomButton->OnUnhovered.AddDynamic(this, &UPHMatchmakingWidget::HandleRoleTooltipUnhovered);
	CancelButton->OnUnhovered.AddDynamic(this, &UPHMatchmakingWidget::HandleRoleTooltipUnhovered);
	CustomizationButton->OnUnhovered.AddDynamic(this, &UPHMatchmakingWidget::HandleRoleTooltipUnhovered);
	QuitButton->OnUnhovered.AddDynamic(this, &UPHMatchmakingWidget::HandleRoleTooltipUnhovered);
	CancelButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleCancelClicked);
	RefreshFriendsButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleRefreshFriendsClicked);
	CloseFriendsButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleCloseFriendsClicked);
	FriendsDismissButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleCloseFriendsClicked);
	QuitButton->OnClicked.AddDynamic(this, &UPHMatchmakingWidget::HandleQuitClicked);
}

void UPHMatchmakingWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UPHMatchmakingGatewaySubsystem* Gateway = GetGatewaySubsystem())
	{
		Gateway->OnStateChanged.AddDynamic(this, &UPHMatchmakingWidget::HandleGatewayStateChanged);
		Gateway->OnFinished.AddDynamic(this, &UPHMatchmakingWidget::HandleOperationFinished);
	}
	if (UPHSocialInviteSubsystem* Social = GetSocialInviteSubsystem())
	{
		Social->OnFriendsChanged.AddDynamic(this, &UPHMatchmakingWidget::HandleFriendsChanged);
		Social->OnInviteChanged.AddDynamic(this, &UPHMatchmakingWidget::HandleInviteChanged);
	}
	RefreshLocalSteamIdentity();
	RefreshClientUpdateNotice();
	RefreshMatchmakingControls();
	RefreshLobbyPresentation();
	if (IsFullLobbyPreviewRequested())
	{
		RefreshLobbyCharacterPreview();
	}
	if (IsFriendsPanelPreviewRequested())
	{
		HandleInviteFriendClicked();
	}
	if (IsRoleTooltipPreviewRequested())
	{
		HandleSurvivorTooltipHovered();
	}
	if (IsCancelButtonPreviewRequested())
	{
		SetButtonsEnabled(false, true, true);
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
	ClearLobbyCharacterPreview();
	if (UPHMatchmakingGatewaySubsystem* Gateway = GetGatewaySubsystem())
	{
		Gateway->OnStateChanged.RemoveAll(this);
		Gateway->OnFinished.RemoveAll(this);
	}
	if (UPHSocialInviteSubsystem* Social = GetSocialInviteSubsystem())
	{
		Social->OnFriendsChanged.RemoveAll(this);
		Social->OnInviteChanged.RemoveAll(this);
	}
	Super::NativeDestruct();
}

void UPHMatchmakingWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const float TooltipFadeSpeed = RoleTooltipTargetOpacity > RoleTooltipOpacity ? 16.0f : 36.0f;
	RoleTooltipOpacity = FMath::FInterpTo(
		RoleTooltipOpacity, RoleTooltipTargetOpacity, InDeltaTime, TooltipFadeSpeed);
	if (RoleTooltipCard != nullptr)
	{
		if (RoleTooltipAnchorButton.IsValid() && RootCanvas != nullptr)
		{
			const FGeometry& ButtonGeometry = RoleTooltipAnchorButton->GetCachedGeometry();
			const FGeometry& RootGeometry = RootCanvas->GetCachedGeometry();
			const FVector2D ButtonRightCenter = ButtonGeometry.LocalToAbsolute(FVector2D(
				ButtonGeometry.GetLocalSize().X,
				ButtonGeometry.GetLocalSize().Y * 0.5f));
			const FVector2D ButtonLeftCenter = ButtonGeometry.LocalToAbsolute(FVector2D(
				0.0f,
				ButtonGeometry.GetLocalSize().Y * 0.5f));
			if (UCanvasPanelSlot* TooltipSlot = Cast<UCanvasPanelSlot>(RoleTooltipCard->Slot))
			{
				const FVector2D RightAnchor = RootGeometry.AbsoluteToLocal(ButtonRightCenter);
				const FVector2D LeftAnchor = RootGeometry.AbsoluteToLocal(ButtonLeftCenter);
				constexpr float TooltipWidth = 350.0f;
				const bool bPlaceOnLeft = RightAnchor.X + 18.0f + TooltipWidth
					> RootGeometry.GetLocalSize().X - 16.0f;
				FVector2D TooltipPosition = bPlaceOnLeft
					? LeftAnchor - FVector2D(18.0f, 0.0f)
					: RightAnchor + FVector2D(18.0f, 0.0f);
				TooltipSlot->SetAlignment(FVector2D(bPlaceOnLeft ? 1.0f : 0.0f, 0.5f));
				constexpr float TooltipHalfHeight = 69.0f;
				constexpr float TooltipScreenMargin = 16.0f;
				TooltipPosition.Y = FMath::Clamp(
					TooltipPosition.Y,
					TooltipHalfHeight + TooltipScreenMargin,
					FMath::Max(
						TooltipHalfHeight + TooltipScreenMargin,
						RootGeometry.GetLocalSize().Y - TooltipHalfHeight - TooltipScreenMargin));
				TooltipSlot->SetPosition(TooltipPosition);
			}
		}
		if (RoleTooltipTargetOpacity > 0.0f && RoleTooltipCard->GetVisibility() == ESlateVisibility::Collapsed)
		{
			RoleTooltipCard->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		RoleTooltipCard->SetRenderOpacity(RoleTooltipOpacity);
		RoleTooltipCard->SetRenderTranslation(FVector2D((1.0f - RoleTooltipOpacity) * -12.0f, 0.0f));
		if (RoleTooltipTargetOpacity <= 0.0f && RoleTooltipOpacity <= 0.045f)
		{
			RoleTooltipOpacity = 0.0f;
			RoleTooltipCard->SetRenderOpacity(0.0f);
			RoleTooltipCard->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	RefreshPodiumInviteButtons();
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextLocalProfileRefreshTime)
	{
		NextLocalProfileRefreshTime = Now + 1.0;
		RefreshLocalSteamIdentity();
	}
	if (FriendsPanel != nullptr
		&& FriendsPanel->GetVisibility() == ESlateVisibility::Visible
		&& (!FriendInviteCooldownEnds.IsEmpty() || !PendingInviteFriendUserId.IsEmpty()))
	{
		if (Now >= NextFriendCooldownRefreshTime)
		{
			NextFriendCooldownRefreshTime = Now + 0.5;
			RebuildFriendsList();
		}
	}
}

void UPHMatchmakingWidget::ShowRoleTooltip(
	UButton* AnchorButton,
	UTexture2D* Icon,
	const FString& Title,
	const FString& Description,
	const FLinearColor& AccentColor)
{
	if (RoleTooltipCard == nullptr || RoleTooltipIcon == nullptr
		|| RoleTooltipTitle == nullptr || RoleTooltipDescription == nullptr)
	{
		return;
	}
	ApplyRoundedBorderStyle(
		*RoleTooltipCard,
		FLinearColor(0.007f, 0.013f, 0.024f, 0.985f),
		AccentColor.CopyWithNewOpacity(0.9f),
		12.0f);
	if (RoleTooltipIconContainer != nullptr)
	{
		RoleTooltipIconContainer->SetVisibility(Icon != nullptr
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
	RoleTooltipIcon->SetBrushFromTexture(Icon, false);
	RoleTooltipTitle->SetText(FText::FromString(Title));
	RoleTooltipTitle->SetColorAndOpacity(FSlateColor(AccentColor));
	RoleTooltipDescription->SetText(FText::FromString(Description));
	RoleTooltipAnchorButton = AnchorButton;
	RoleTooltipCard->SetVisibility(ESlateVisibility::HitTestInvisible);
	RoleTooltipTargetOpacity = 1.0f;
}

void UPHMatchmakingWidget::HandleSurvivorTooltipHovered()
{
	ShowRoleTooltip(
		FindButton,
		SurvivorRoleIcon,
		TEXT("SURVIVANT"),
		TEXT("Cache-toi parmi les objets, coopère avec les autres et échappe au Tueur."),
		FLinearColor(0.18f, 0.78f, 1.0f, 1.0f));
}

void UPHMatchmakingWidget::HandleKillerTooltipHovered()
{
	ShowRoleTooltip(
		HostButton,
		KillerRoleIcon,
		TEXT("TUEUR"),
		TEXT("Traque les Survivants cachés, démasque leurs objets et ne laisse personne fuir."),
		FLinearColor(1.0f, 0.22f, 0.16f, 1.0f));
}

void UPHMatchmakingWidget::HandleRandomTooltipHovered()
{
	ShowRoleTooltip(
		RandomButton,
		RandomRoleIcon,
		TEXT("ALÉATOIRE"),
		TEXT("Laisse le matchmaking choisir ton camp et trouve plus vite un salon disponible."),
		FLinearColor(0.76f, 0.36f, 1.0f, 1.0f));
}

void UPHMatchmakingWidget::HandleCancelTooltipHovered()
{
	ShowRoleTooltip(
		CancelButton,
		nullptr,
		TEXT("RETOUR AU MENU"),
		TEXT("Annule la recherche en cours et retourne au choix du rôle."),
		FLinearColor(1.0f, 0.28f, 0.18f, 1.0f));
}

void UPHMatchmakingWidget::HandleCustomizationTooltipHovered()
{
	ShowRoleTooltip(
		CustomizationButton,
		CustomizationIcon,
		TEXT("PERSONNALISATION"),
		TEXT("Équipe tes sons, tes tags et bientôt les tenues de tes personnages."),
		FLinearColor(0.30f, 0.86f, 0.82f, 1.0f));
}

void UPHMatchmakingWidget::HandleQuitTooltipHovered()
{
	ShowRoleTooltip(
		QuitButton,
		QuitIcon,
		TEXT("QUITTER"),
		TEXT("Referme la porte et quitte Prop Caper."),
		FLinearColor(1.0f, 0.65f, 0.16f, 1.0f));
}

void UPHMatchmakingWidget::HandleRoleTooltipUnhovered()
{
	RoleTooltipTargetOpacity = 0.0f;
}

void UPHMatchmakingWidget::HandleHostClicked()
{
	SetButtonsEnabled(false, false);
	SetStatus(TEXT("Création du ticket Tueur NOVA..."));
	if (UPHMatchmakingGatewaySubsystem* Gateway = GetGatewaySubsystem())
	{
		Gateway->RequestTicket(EPHMatchmakingPreference::Hunter);
	}
}

void UPHMatchmakingWidget::HandleFindClicked()
{
	SetButtonsEnabled(false, false);
	SetStatus(TEXT("Création du ticket Survivant NOVA..."));
	if (UPHMatchmakingGatewaySubsystem* Gateway = GetGatewaySubsystem())
	{
		Gateway->RequestTicket(EPHMatchmakingPreference::Prop);
	}
}

void UPHMatchmakingWidget::HandleRandomClicked()
{
	SetButtonsEnabled(false, false);
	SetStatus(TEXT("Recherche d’un rôle aléatoire disponible..."));
	if (UPHMatchmakingGatewaySubsystem* Gateway = GetGatewaySubsystem())
	{
		Gateway->RequestTicket(EPHMatchmakingPreference::Random);
	}
}

void UPHMatchmakingWidget::HandleCancelClicked()
{
	if (UPHMatchmakingGatewaySubsystem* Gateway = GetGatewaySubsystem())
	{
		Gateway->CancelTicket();
	}
}

void UPHMatchmakingWidget::HandleInviteFriendClicked()
{
	if (MainMenuPanel != nullptr && FriendsPanel != nullptr && FriendsDismissButton != nullptr)
	{
		MainMenuPanel->SetVisibility(ESlateVisibility::Collapsed);
		FriendsDismissButton->SetVisibility(ESlateVisibility::Visible);
		FriendsPanel->SetVisibility(ESlateVisibility::Visible);
	}
	HandleRefreshFriendsClicked();
}

void UPHMatchmakingWidget::HandleCloseFriendsClicked()
{
	if (FriendsPanel != nullptr && MainMenuPanel != nullptr && FriendsDismissButton != nullptr)
	{
		FriendsPanel->SetVisibility(ESlateVisibility::Collapsed);
		FriendsDismissButton->SetVisibility(ESlateVisibility::Collapsed);
		MainMenuPanel->SetVisibility(ESlateVisibility::Visible);
	}
}

void UPHMatchmakingWidget::HandleRefreshFriendsClicked()
{
	if (FriendsStatusLabel != nullptr)
	{
		FriendsStatusLabel->SetText(FText::FromString(TEXT("Chargement des amis Steam...")));
	}
	if (UPHSocialInviteSubsystem* Social = GetSocialInviteSubsystem())
	{
		if (Social->RefreshSteamFriends())
		{
			return;
		}
	}
	if (FriendsStatusLabel != nullptr)
	{
		FriendsStatusLabel->SetText(FText::FromString(
			TEXT("Liste indisponible. Lance PropHunt depuis Steam.")));
	}
}

void UPHMatchmakingWidget::HandleFriendInviteRequested(const FString& FriendUserId)
{
	const double Now = FPlatformTime::Seconds();
	if (const double* CooldownEnd = FriendInviteCooldownEnds.Find(FriendUserId);
		CooldownEnd != nullptr && *CooldownEnd > Now)
	{
		if (FriendsStatusLabel != nullptr)
		{
			FriendsStatusLabel->SetText(FText::FromString(FString::Printf(
				TEXT("Patiente encore %d s avant de réinviter cet ami."),
				FMath::CeilToInt(*CooldownEnd - Now))));
		}
		return;
	}
	if (!PendingInviteFriendUserId.IsEmpty())
	{
		return;
	}

	PendingInviteFriendUserId = FriendUserId;
	if (FriendsStatusLabel != nullptr)
	{
		FriendsStatusLabel->SetText(FText::FromString(TEXT("Préparation de l'invitation PropHunt...")));
	}
	RebuildFriendsList();
	if (UPHSocialInviteSubsystem* Social = GetSocialInviteSubsystem())
	{
		if (Social->InviteFriend(FriendUserId))
		{
			return;
		}
	}
	PendingInviteFriendUserId.Reset();
	RebuildFriendsList();
}

void UPHMatchmakingWidget::HandleFriendsChanged(const bool bSucceeded, const FString& Message)
{
	RefreshLocalSteamIdentity();
	if (FriendsStatusLabel != nullptr)
	{
		FriendsStatusLabel->SetText(FText::FromString(Message));
		FriendsStatusLabel->SetColorAndOpacity(FSlateColor(bSucceeded
			? FLinearColor(0.45f, 0.85f, 0.65f, 1.0f)
			: FLinearColor(1.0f, 0.35f, 0.25f, 1.0f)));
	}
	RebuildFriendsList();
}

void UPHMatchmakingWidget::HandleInviteChanged(const bool bSucceeded, const FString& Message)
{
	if (!PendingInviteFriendUserId.IsEmpty())
	{
		if (bSucceeded)
		{
			FriendInviteCooldownEnds.Add(
				PendingInviteFriendUserId,
				FPlatformTime::Seconds() + FriendInviteCooldownSeconds);
		}
		PendingInviteFriendUserId.Reset();
		NextFriendCooldownRefreshTime = 0.0;
	}
	if (FriendsStatusLabel != nullptr)
	{
		FriendsStatusLabel->SetText(FText::FromString(Message));
		FriendsStatusLabel->SetColorAndOpacity(FSlateColor(bSucceeded
			? FLinearColor(0.45f, 0.85f, 0.65f, 1.0f)
			: FLinearColor(1.0f, 0.35f, 0.25f, 1.0f)));
	}
	RebuildFriendsList();
}

void UPHMatchmakingWidget::RebuildFriendsList()
{
	if (FriendsList == nullptr || WidgetTree == nullptr)
	{
		return;
	}
	FriendsList->ClearChildren();
	const double Now = FPlatformTime::Seconds();
	for (auto CooldownIt = FriendInviteCooldownEnds.CreateIterator(); CooldownIt; ++CooldownIt)
	{
		if (CooldownIt.Value() <= Now)
		{
			CooldownIt.RemoveCurrent();
		}
	}
	const UPHSocialInviteSubsystem* Social = GetSocialInviteSubsystem();
	const TArray<FPHSocialFriendEntry> Entries = Social != nullptr
		? Social->GetFriends() : TArray<FPHSocialFriendEntry>();
	int32 OnlineCount = 0;
	for (const FPHSocialFriendEntry& Entry : Entries)
	{
		OnlineCount += Entry.bOnline ? 1 : 0;
	}
	if (FriendsStatusLabel != nullptr)
	{
		FriendsStatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("%d AMI%s DISPONIBLE%s"),
			OnlineCount,
			OnlineCount > 1 ? TEXT("S") : TEXT(""),
			OnlineCount > 1 ? TEXT("S") : TEXT(""))));
		FriendsStatusLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.48f, 0.68f, 0.60f, 1.0f)));
	}
	if (OnlineCount == 0)
	{
		UTextBlock* EmptyLabel = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("SteamFriendsEmptyLabel"));
		EmptyLabel->SetText(FText::FromString(TEXT("Aucun ami en ligne pour le moment.")));
		EmptyLabel->SetJustification(ETextJustify::Center);
		EmptyLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.6f, 0.68f, 1.0f)));
		FriendsList->AddChild(EmptyLabel);
		return;
	}

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FPHSocialFriendEntry& Entry = Entries[Index];
		if (!Entry.bOnline)
		{
			continue;
		}
		const double CooldownEnd = FriendInviteCooldownEnds.FindRef(Entry.UserId);
		const int32 CooldownSecondsRemaining = CooldownEnd > Now
			? FMath::CeilToInt(CooldownEnd - Now)
			: 0;
		const bool bInvitePending = PendingInviteFriendUserId == Entry.UserId;
		UPHSocialFriendButton* FriendButton = WidgetTree->ConstructWidget<UPHSocialFriendButton>(
			UPHSocialFriendButton::StaticClass(),
			*FString::Printf(TEXT("SteamFriendInvite%d"), Index));
		ApplyRoundedButtonStyle(*FriendButton, Entry.bPlayingPropHunt
			? FLinearColor(0.025f, 0.15f, 0.12f, 0.82f)
			: FLinearColor(0.012f, 0.045f, 0.072f, 0.76f));
		FriendButton->InitializeFriend(Entry.UserId);
		FriendButton->SetIsEnabled(!bInvitePending && CooldownSecondsRemaining == 0);
		FriendButton->OnInviteRequested.AddDynamic(
			this, &UPHMatchmakingWidget::HandleFriendInviteRequested);

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), *FString::Printf(TEXT("SteamFriendRow%d"), Index));
		FriendButton->AddChild(Row);
		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Row->Slot))
		{
			ContentSlot->SetHorizontalAlignment(HAlign_Fill);
			ContentSlot->SetVerticalAlignment(VAlign_Fill);
		}

		USizeBox* AvatarSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), *FString::Printf(TEXT("SteamFriendAvatarSize%d"), Index));
		AvatarSize->SetWidthOverride(36.0f);
		AvatarSize->SetHeightOverride(36.0f);
		UBorder* Avatar = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), *FString::Printf(TEXT("SteamFriendAvatar%d"), Index));
		ApplyRoundedBorderStyle(
			*Avatar,
			Entry.bPlayingPropHunt
				? FLinearColor(0.055f, 0.24f, 0.18f, 1.0f)
				: FLinearColor(0.04f, 0.12f, 0.18f, 1.0f),
			Entry.bPlayingPropHunt
				? FLinearColor(0.24f, 0.88f, 0.56f, 0.9f)
				: FLinearColor(0.18f, 0.62f, 0.82f, 0.82f),
			8.0f);
		if (Entry.AvatarTexture != nullptr)
		{
			UImage* AvatarImage = WidgetTree->ConstructWidget<UImage>(
				UImage::StaticClass(), *FString::Printf(TEXT("SteamFriendAvatarImage%d"), Index));
			FSlateBrush AvatarBrush;
			AvatarBrush.SetResourceObject(Entry.AvatarTexture);
			AvatarBrush.ImageSize = FVector2D(36.0f, 36.0f);
			AvatarBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
			AvatarBrush.TintColor = FSlateColor(FLinearColor::White);
			AvatarBrush.OutlineSettings.CornerRadii = FVector4(7.0f);
			AvatarBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			AvatarImage->SetBrush(AvatarBrush);
			Avatar->SetContent(AvatarImage);
		}
		else
		{
			UTextBlock* Initial = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), *FString::Printf(TEXT("SteamFriendInitial%d"), Index));
			Initial->SetText(FText::FromString(Entry.DisplayName.IsEmpty()
				? TEXT("?") : Entry.DisplayName.Left(1).ToUpper()));
			Initial->SetJustification(ETextJustify::Center);
			Initial->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Initial->SetFont(FSlateFontInfo(Initial->GetFont().FontObject, 17));
			Avatar->SetContent(Initial);
		}
		AvatarSize->SetContent(Avatar);
		if (UHorizontalBoxSlot* AvatarSlot = Row->AddChildToHorizontalBox(AvatarSize))
		{
			AvatarSlot->SetPadding(FMargin(8.0f, 6.0f, 10.0f, 6.0f));
			AvatarSlot->SetVerticalAlignment(VAlign_Center);
		}

		UVerticalBox* Identity = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), *FString::Printf(TEXT("SteamFriendIdentity%d"), Index));
		UTextBlock* DisplayName = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), *FString::Printf(TEXT("SteamFriendName%d"), Index));
		const FString CompactDisplayName = Entry.DisplayName.Len() > 18
			? Entry.DisplayName.Left(15) + TEXT("...")
			: Entry.DisplayName;
		DisplayName->SetText(FText::FromString(CompactDisplayName));
		DisplayName->SetColorAndOpacity(FSlateColor(FLinearColor(0.91f, 0.94f, 0.97f, 1.0f)));
		DisplayName->SetFont(FSlateFontInfo(DisplayName->GetFont().FontObject, 14));
		Identity->AddChildToVerticalBox(DisplayName);
		UTextBlock* Presence = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), *FString::Printf(TEXT("SteamFriendPresence%d"), Index));
		Presence->SetText(FText::FromString(Entry.bPlayingPropHunt
			? TEXT("DANS PROP HUNT") : TEXT("EN LIGNE")));
		Presence->SetColorAndOpacity(FSlateColor(Entry.bPlayingPropHunt
			? FLinearColor(0.36f, 0.86f, 0.58f, 1.0f)
			: FLinearColor(0.42f, 0.68f, 0.80f, 1.0f)));
		Presence->SetFont(FSlateFontInfo(Presence->GetFont().FontObject, 10));
		Identity->AddChildToVerticalBox(Presence);
		if (UHorizontalBoxSlot* IdentitySlot = Row->AddChildToHorizontalBox(Identity))
		{
			IdentitySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			IdentitySlot->SetVerticalAlignment(VAlign_Center);
		}

		UTextBlock* InviteLabel = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), *FString::Printf(TEXT("SteamFriendInviteLabel%d"), Index));
		const FString InviteActionText = bInvitePending
			? TEXT("...")
			: (CooldownSecondsRemaining > 0
				? FString::Printf(TEXT("%ds"), CooldownSecondsRemaining)
				: TEXT("+"));
		InviteLabel->SetText(FText::FromString(InviteActionText));
		InviteLabel->SetColorAndOpacity(FSlateColor(
			bInvitePending || CooldownSecondsRemaining > 0
				? FLinearColor(0.42f, 0.47f, 0.52f, 1.0f)
				: FLinearColor(0.94f, 0.76f, 0.32f, 1.0f)));
		InviteLabel->SetFont(FSlateFontInfo(
			InviteLabel->GetFont().FontObject,
			CooldownSecondsRemaining > 0 ? 10 : 21));
		UBorder* InviteBubble = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), *FString::Printf(TEXT("SteamFriendInviteBubble%d"), Index));
		ApplyRoundedBorderStyle(
			*InviteBubble,
			bInvitePending || CooldownSecondsRemaining > 0
				? FLinearColor(0.04f, 0.05f, 0.065f, 0.74f)
				: FLinearColor(0.04f, 0.15f, 0.18f, 0.82f),
			bInvitePending || CooldownSecondsRemaining > 0
				? FLinearColor(0.24f, 0.28f, 0.34f, 0.55f)
				: FLinearColor(0.24f, 0.82f, 0.94f, 0.72f),
			8.0f);
		InviteBubble->SetPadding(FMargin(8.0f, 2.0f));
		InviteBubble->SetContent(InviteLabel);
		USizeBox* InviteBubbleSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), *FString::Printf(TEXT("SteamFriendInviteBubbleSize%d"), Index));
		InviteBubbleSize->SetWidthOverride(38.0f);
		InviteBubbleSize->SetHeightOverride(32.0f);
		InviteBubbleSize->SetContent(InviteBubble);
		if (UHorizontalBoxSlot* InviteSlot = Row->AddChildToHorizontalBox(InviteBubbleSize))
		{
			InviteSlot->SetPadding(FMargin(8.0f, 0.0f, 8.0f, 0.0f));
			InviteSlot->SetVerticalAlignment(VAlign_Center);
		}

		USizeBox* RowSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), *FString::Printf(TEXT("SteamFriendRowSize%d"), Index));
		RowSize->SetHeightOverride(50.0f);
		RowSize->SetContent(FriendButton);
		FriendsList->AddChild(RowSize);
		USpacer* Gap = WidgetTree->ConstructWidget<USpacer>(
			USpacer::StaticClass(), *FString::Printf(TEXT("SteamFriendGap%d"), Index));
		Gap->SetSize(FVector2D(0.0f, 4.0f));
		FriendsList->AddChild(Gap);
	}
}

void UPHMatchmakingWidget::HandleQuitClicked()
{
	if (UPHSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->LeaveSessionAndQuitGame();
	}
}

void UPHMatchmakingWidget::HandleGatewayStateChanged(
	const EPHGatewayMatchmakingState NewState,
	const FString& StatusMessage)
{
	SetStatus(StatusMessage);
	RefreshClientUpdateNotice();
	RefreshMatchmakingControls();
	RefreshLobbyPresentation();
}

void UPHMatchmakingWidget::HandleOperationFinished(const bool bSucceeded, const FString& Message)
{
	SetStatus(Message);
	RefreshClientUpdateNotice();
	RefreshMatchmakingControls();
	RefreshLobbyPresentation();
}

void UPHMatchmakingWidget::RefreshMatchmakingControls()
{
	const UPHMatchmakingGatewaySubsystem* Gateway = GetGatewaySubsystem();
	const UPHSessionSubsystem* Sessions = GetSessionSubsystem();
	const bool bSteamAvailable = Sessions != nullptr && Sessions->IsSteamAvailable();
	const EPHGatewayMatchmakingState GatewayState = Gateway != nullptr
		? Gateway->GetState()
		: EPHGatewayMatchmakingState::Idle;
	const bool bHasLocalTicket = Gateway != nullptr && !Gateway->GetTicketId().IsEmpty();
	const bool bCanStart = bSteamAvailable
		&& !bHasLocalTicket
		&& (GatewayState == EPHGatewayMatchmakingState::Idle
			|| GatewayState == EPHGatewayMatchmakingState::Failed);
	const bool bCanCancel = bHasLocalTicket
		&& GatewayState != EPHGatewayMatchmakingState::Cancelling
		&& GatewayState != EPHGatewayMatchmakingState::Connecting;
	SetButtonsEnabled(bCanStart, bCanCancel, bHasLocalTicket);
}

void UPHMatchmakingWidget::RefreshLocalSteamIdentity()
{
	UPHSocialInviteSubsystem* Social = GetSocialInviteSubsystem();
	const UPHSessionSubsystem* Sessions = GetSessionSubsystem();
	const FPHSocialFriendEntry Profile = Social != nullptr
		? Social->GetLocalSteamProfile()
		: FPHSocialFriendEntry();
	const bool bSteamAvailable = Sessions != nullptr && Sessions->IsSteamAvailable();
	const bool bHasIdentity = bSteamAvailable && !Profile.DisplayName.IsEmpty();
	if (SteamIdentityCard != nullptr)
	{
		SteamIdentityCard->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (LocalSteamName != nullptr)
	{
		constexpr int32 MaxSteamNameCharacters = 18;
		const FString CompactSteamName = Profile.DisplayName.Len() > MaxSteamNameCharacters
			? Profile.DisplayName.Left(MaxSteamNameCharacters - 3) + TEXT("...")
			: Profile.DisplayName;
		LocalSteamName->SetText(FText::FromString(bHasIdentity
			? CompactSteamName
			: (bSteamAvailable ? TEXT("CONNEXION STEAM...") : TEXT("STEAM OBLIGATOIRE"))));
		LocalSteamName->SetJustification(bHasIdentity ? ETextJustify::Left : ETextJustify::Center);
		LocalSteamName->SetColorAndOpacity(FSlateColor(bHasIdentity
			? FLinearColor::White
			: (bSteamAvailable
				? FLinearColor(0.60f, 0.82f, 0.92f, 1.0f)
				: FLinearColor(1.0f, 0.58f, 0.16f, 1.0f))));
	}
	if (LocalSteamAvatar != nullptr)
	{
		ApplyRoundedAvatarBrush(*LocalSteamAvatar, Profile.AvatarTexture);
		LocalSteamAvatar->SetVisibility(bHasIdentity && Profile.AvatarTexture != nullptr
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
}

void UPHMatchmakingWidget::RefreshClientUpdateNotice()
{
	if (ClientUpdateNoticeCard == nullptr)
	{
		return;
	}
	const UPHMatchmakingGatewaySubsystem* Gateway = GetGatewaySubsystem();
	const bool bUpdateRequired = IsClientUpdateNoticePreviewRequested()
		|| (Gateway != nullptr && Gateway->IsClientUpdateRequired());
	ClientUpdateNoticeCard->SetVisibility(bUpdateRequired
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed);
}

void UPHMatchmakingWidget::RefreshLobbyPresentation()
{
	const UPHMatchmakingGatewaySubsystem* Gateway = GetGatewaySubsystem();
	const bool bDebugFullLobby = IsFullLobbyPreviewRequested();
	const bool bHasLobby = bDebugFullLobby || (Gateway != nullptr && Gateway->HasLobbySnapshot());
	if (LobbyPanel != nullptr)
	{
		LobbyPanel->SetVisibility(bHasLobby ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (MainMenuPanel != nullptr)
	{
		MainMenuPanel->SetPadding(FMargin(10.0f));
		if (UCanvasPanelSlot* MenuSlot = Cast<UCanvasPanelSlot>(MainMenuPanel->Slot))
		{
			MenuSlot->SetSize(FVector2D(96.0f, 430.0f));
		}
	}
	if (RoleActionsPanel != nullptr)
	{
		RoleActionsPanel->SetVisibility(bHasLobby ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (HunterPreviewCard != nullptr)
	{
		const bool bHunter = bHasLobby && (IsHunterLobbyPreviewRequested()
			|| (Gateway != nullptr && Gateway->GetLobbyAssignedRole() == EPHPlayerRole::Hunter));
		HunterPreviewCard->SetVisibility(bHunter ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (HostButton != nullptr) HostButton->SetVisibility(bHasLobby ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (FindButton != nullptr) FindButton->SetVisibility(bHasLobby ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (RandomButton != nullptr) RandomButton->SetVisibility(bHasLobby ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (CustomizationButton != nullptr) CustomizationButton->SetVisibility(bHasLobby ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (QuitButton != nullptr)
	{
		// The waiting lobby has one unambiguous exit: cancel the ticket and return
		// to the role menu. Closing the application remains an accueil-only action.
		QuitButton->SetVisibility(bHasLobby ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (!bHasLobby)
	{
		for (UImage* SurvivorIcon : LobbySurvivorReadyIcons)
		{
			if (SurvivorIcon != nullptr)
			{
				SurvivorIcon->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		if (LobbyHunterReadyIcon != nullptr)
		{
			LobbyHunterReadyIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
		ClearLobbyCharacterPreview();
		RefreshPodiumInviteButtons();
		return;
	}

	const bool bHunter = IsHunterLobbyPreviewRequested()
		|| (!bDebugFullLobby && Gateway != nullptr
			&& Gateway->GetLobbyAssignedRole() == EPHPlayerRole::Hunter);
	const int32 SurvivorsPresent = bDebugFullLobby ? GetLobbyPreviewSurvivorCount()
		: (Gateway != nullptr ? Gateway->GetLobbySurvivorsPresent() : 0);
	const FString Phase = bDebugFullLobby ? TEXT("countdown") : Gateway->GetLobbyPhase();
	if (LobbyRoleLabel != nullptr)
	{
		LobbyRoleLabel->SetText(FText::FromString(FString::Printf(
			TEXT("%d / 4 SURVIVANT%s"),
			SurvivorsPresent,
			SurvivorsPresent > 1 ? TEXT("S") : TEXT(""))));
	}
	const bool bHunterReady = bHunter || Phase != TEXT("waiting");
	for (int32 SlotIndex = 0; SlotIndex < LobbySurvivorReadyIcons.Num(); ++SlotIndex)
	{
		if (UImage* SurvivorIcon = LobbySurvivorReadyIcons[SlotIndex])
		{
			const bool bSurvivorPresent = SlotIndex < SurvivorsPresent;
			SurvivorIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
			SurvivorIcon->SetColorAndOpacity(bSurvivorPresent
				? FLinearColor(0.40f, 0.86f, 1.0f, 1.0f)
				: FLinearColor(0.20f, 0.34f, 0.42f, 0.24f));
		}
	}
	if (LobbyHunterReadyIcon != nullptr)
	{
		LobbyHunterReadyIcon->SetVisibility(bHunterReady
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
	if (LobbyRosterCountLabel != nullptr)
	{
		FString CountdownText;
		if (Phase == TEXT("waiting")) CountdownText = TEXT("EN ATTENTE");
		else if (Phase == TEXT("countdown")) CountdownText = FString::Printf(
			TEXT("DÉPART DANS %d s"), bDebugFullLobby ? 24 : Gateway->GetLobbyCountdownSeconds());
		else if (Phase == TEXT("locked")) CountdownText = FString::Printf(
			TEXT("DÉPART DANS %d s"), Gateway->GetLobbyCountdownSeconds());
		else CountdownText = TEXT("PRÉPARATION DU SERVEUR...");
		LobbyRosterCountLabel->SetText(FText::FromString(CountdownText));
		LobbyRosterCountLabel->SetColorAndOpacity(FSlateColor(
			Phase == TEXT("locked")
				? FLinearColor(1.0f, 0.22f, 0.16f, 1.0f)
				: (Phase == TEXT("server_starting")
					? FLinearColor(1.0f, 0.48f, 0.20f, 1.0f)
					: (Phase == TEXT("waiting")
						? FLinearColor(0.62f, 0.68f, 0.74f, 1.0f)
						: FLinearColor(0.45f, 0.82f, 1.0f, 1.0f)))));
	}
	RefreshLobbyCharacterPreview();
	RefreshPodiumInviteButtons();
}

void UPHMatchmakingWidget::RefreshLobbyCharacterPreview()
{
	const UPHMatchmakingGatewaySubsystem* Gateway = GetGatewaySubsystem();
	const bool bDebugFullLobby = IsFullLobbyPreviewRequested();
	if ((Gateway == nullptr || !Gateway->HasLobbySnapshot())
		&& !bDebugFullLobby)
	{
		ClearLobbyCharacterPreview();
		return;
	}
	if (GetWorld() == nullptr)
	{
		ClearLobbyCharacterPreview();
		return;
	}

	TArray<int32> OccupiedSlots;
	if (bDebugFullLobby)
	{
		for (int32 PreviewSlotIndex = 1; PreviewSlotIndex <= GetLobbyPreviewSurvivorCount(); ++PreviewSlotIndex)
		{
			OccupiedSlots.Add(PreviewSlotIndex);
		}
	}
	else
	{
		OccupiedSlots = Gateway->GetOccupiedSurvivorSlots();
	}
	OccupiedSlots.Sort();
	const bool bShowPrivateHunter = IsHunterLobbyPreviewRequested()
		|| (!bDebugFullLobby && Gateway->GetLobbyAssignedRole() == EPHPlayerRole::Hunter);
	const int32 ExpectedPreviewCount = OccupiedSlots.Num() + (bShowPrivateHunter ? 1 : 0);
	if (LobbyPreviewActors.Num() == ExpectedPreviewCount
		&& PreviewedSurvivorSlots == OccupiedSlots
		&& bPreviewedPrivateHunter == bShowPrivateHunter)
	{
		return;
	}
	ClearLobbyCharacterPreview();

	APlayerController* PlayerController = GetOwningPlayer();
	if (PlayerController == nullptr)
	{
		return;
	}
	FVector CameraLocation;
	FRotator CameraRotation;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FVector Forward = CameraRotation.Vector();
	const FVector Right = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Y);
	const FVector Up = FVector::UpVector;
	const FRotator FacingRotation = (-Forward).Rotation();

	struct FLobbyPreviewMarker
	{
		int32 SlotIndex = INDEX_NONE;
		FTransform Transform;
	};
	TArray<FLobbyPreviewMarker> SurvivorMarkers;
	TOptional<FTransform> HunterMarker;
	for (TActorIterator<AActor> ActorIterator(GetWorld()); ActorIterator; ++ActorIterator)
	{
		AActor* MarkerActor = *ActorIterator;
		if (!IsValid(MarkerActor))
		{
			continue;
		}
		if (MarkerActor->ActorHasTag(TEXT("PHLobbyHunterPreview")))
		{
			HunterMarker = MarkerActor->GetActorTransform();
		}
		if (!MarkerActor->ActorHasTag(TEXT("PHLobbySurvivorPreview")))
		{
			continue;
		}

		const int32 MarkerSlotIndex = GetLobbySurvivorMarkerSlot(*MarkerActor);
		if (MarkerSlotIndex > 0)
		{
			FLobbyPreviewMarker& Marker = SurvivorMarkers.AddDefaulted_GetRef();
			Marker.SlotIndex = MarkerSlotIndex;
			Marker.Transform = MarkerActor->GetActorTransform();
		}
	}
	SurvivorMarkers.Sort([](const FLobbyPreviewMarker& Left, const FLobbyPreviewMarker& RightMarker)
	{
		return Left.SlotIndex < RightMarker.SlotIndex;
	});

	UClass* SurvivorPreviewClass = StaticLoadClass(
		AActor::StaticClass(), nullptr,
		TEXT("/Game/PropHunt/Characters/Props/BP_PH_PropCharacter.BP_PH_PropCharacter_C"));
	UClass* HunterPreviewClass = StaticLoadClass(
		AActor::StaticClass(), nullptr,
		TEXT("/Game/PropHunt/Characters/Hunter/BP_PH_HunterCharacter.BP_PH_HunterCharacter_C"));
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;

	auto SpawnPreview = [this, &SpawnParameters](
		UClass* PreviewClass, const FTransform& Transform, const float TargetHeight)
	{
		if (PreviewClass == nullptr)
		{
			return;
		}
		if (AActor* Preview = GetWorld()->SpawnActor<AActor>(PreviewClass, Transform, SpawnParameters))
		{
			Preview->SetReplicates(false);
			Preview->SetActorEnableCollision(false);
			Preview->SetActorTickEnabled(false);
			if (ACharacter* Character = Cast<ACharacter>(Preview))
			{
				if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
				{
					Movement->DisableMovement();
				}
			}
			FVector BoundsOrigin;
			FVector BoundsExtent;
			Preview->GetActorBounds(true, BoundsOrigin, BoundsExtent, false);
			const float CurrentHeight = BoundsExtent.Z * 2.0f;
			if (CurrentHeight > KINDA_SMALL_NUMBER)
			{
				const float ScaleCorrection = FMath::Clamp(TargetHeight / CurrentHeight, 0.2f, 3.0f);
				Preview->SetActorScale3D(Preview->GetActorScale3D() * ScaleCorrection);
			}
			LobbyPreviewActors.Add(Preview);
		}
	};

	for (const int32 OccupiedSlot : OccupiedSlots)
	{
		const FLobbyPreviewMarker* MatchingMarker = SurvivorMarkers.FindByPredicate(
			[OccupiedSlot](const FLobbyPreviewMarker& Marker)
			{
				return Marker.SlotIndex == OccupiedSlot;
			});
		if (MatchingMarker != nullptr)
		{
			FTransform PreviewTransform = MatchingMarker->Transform;
			PreviewTransform.SetScale3D(FVector(1.0f));
			SpawnPreview(SurvivorPreviewClass, PreviewTransform, 182.0f);
		}
		else
		{
			const float CenteredSlot = static_cast<float>(OccupiedSlot - 1) - 1.5f;
			const FVector Location = CameraLocation + Forward * 650.0f
				+ Right * (CenteredSlot * 125.0f) - Up * 135.0f;
			SpawnPreview(SurvivorPreviewClass, FTransform(FacingRotation, Location), 182.0f);
		}
	}
	PreviewedSurvivorSlots = OccupiedSlots;
	bPreviewedPrivateHunter = bShowPrivateHunter;
	if (bShowPrivateHunter)
	{
		if (HunterMarker.IsSet())
		{
			FTransform PreviewTransform = HunterMarker.GetValue();
			PreviewTransform.SetScale3D(FVector(1.0f));
			SpawnPreview(HunterPreviewClass, PreviewTransform, 210.0f);
		}
		else
		{
			const FVector Location = CameraLocation + Forward * 315.0f + Right * 265.0f - Up * 175.0f;
			SpawnPreview(HunterPreviewClass, FTransform(FacingRotation, Location), 210.0f);
		}
	}
}

void UPHMatchmakingWidget::RefreshPodiumMarkers()
{
	LobbyPodiumMarkers.SetNumZeroed(4);
	if (GetWorld() == nullptr)
	{
		return;
	}
	for (TActorIterator<AActor> ActorIterator(GetWorld()); ActorIterator; ++ActorIterator)
	{
		AActor* MarkerActor = *ActorIterator;
		if (!IsValid(MarkerActor) || !MarkerActor->ActorHasTag(TEXT("PHLobbySurvivorPreview")))
		{
			continue;
		}
		const int32 MarkerSlot = GetLobbySurvivorMarkerSlot(*MarkerActor);
		if (MarkerSlot >= 1 && MarkerSlot <= LobbyPodiumMarkers.Num())
		{
			LobbyPodiumMarkers[MarkerSlot - 1] = MarkerActor;
		}
	}
}

void UPHMatchmakingWidget::RefreshPodiumInviteButtons()
{
	const UPHMatchmakingGatewaySubsystem* Gateway = GetGatewaySubsystem();
	const UPHSessionSubsystem* Sessions = GetSessionSubsystem();
	APlayerController* PlayerController = GetOwningPlayer();
	const bool bDebugFullLobby = IsFullLobbyPreviewRequested();
	const bool bHasLobby = bDebugFullLobby || (Gateway != nullptr && Gateway->HasLobbySnapshot());
	const bool bFriendsOpen = FriendsPanel != nullptr
		&& FriendsPanel->GetVisibility() == ESlateVisibility::Visible;
	const bool bCanInvite = bDebugFullLobby
		? bHasLobby && !bFriendsOpen
		: bHasLobby
			&& Gateway != nullptr
			&& !Gateway->IsLobbyLocked()
			&& Sessions != nullptr
			&& Sessions->IsSteamAvailable()
			&& !bFriendsOpen;
	if (LobbyPodiumMarkers.Num() != 4
		|| LobbyPodiumMarkers.ContainsByPredicate([](const TObjectPtr<AActor>& Marker)
		{
			return !IsValid(Marker);
		}))
	{
		RefreshPodiumMarkers();
	}
	TArray<int32> OccupiedSlots;
	if (bDebugFullLobby)
	{
		for (int32 PreviewSlotIndex = 1; PreviewSlotIndex <= GetLobbyPreviewSurvivorCount(); ++PreviewSlotIndex)
		{
			OccupiedSlots.Add(PreviewSlotIndex);
		}
	}
	else if (Gateway != nullptr)
	{
		OccupiedSlots = Gateway->GetOccupiedSurvivorSlots();
	}
	static const TCHAR* PreviewNames[] = {TEXT("Thomas"), TEXT("Alpha45"), TEXT("Amenos1"), TEXT("Amonaton [FR]")};
	for (int32 SlotIndex = 0; SlotIndex < PodiumInviteButtons.Num(); ++SlotIndex)
	{
		UButton* Button = PodiumInviteButtons[SlotIndex];
		UBorder* NameCard = PodiumNameCards.IsValidIndex(SlotIndex)
			? PodiumNameCards[SlotIndex] : nullptr;
		UTextBlock* NameLabel = PodiumNameLabels.IsValidIndex(SlotIndex)
			? PodiumNameLabels[SlotIndex] : nullptr;
		AActor* Marker = LobbyPodiumMarkers.IsValidIndex(SlotIndex)
			? LobbyPodiumMarkers[SlotIndex] : nullptr;
		const bool bOccupied = OccupiedSlots.Contains(SlotIndex + 1);
		if (!bHasLobby || Button == nullptr || !IsValid(Marker) || PlayerController == nullptr)
		{
			if (Button != nullptr) Button->SetVisibility(ESlateVisibility::Collapsed);
			if (NameCard != nullptr) NameCard->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}

		FVector2D InvitePosition;
		// Keep the empty-slot action visually attached to the missing character,
		// around upper-body height rather than floating far above the lineup.
		const FVector InviteWorldPosition = Marker->GetActorLocation() + FVector(0.0f, 0.0f, 125.0f);
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			PlayerController, InviteWorldPosition, InvitePosition, false))
		{
			Button->SetVisibility(ESlateVisibility::Collapsed);
			if (NameCard != nullptr) NameCard->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Button->Slot))
		{
			CanvasSlot->SetPosition(InvitePosition);
		}
		Button->SetVisibility(!bOccupied && bCanInvite
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

		if (!bOccupied || NameCard == nullptr || NameLabel == nullptr)
		{
			if (NameCard != nullptr) NameCard->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		FVector2D NamePosition;
		const FVector NameWorldPosition = Marker->GetActorLocation() - FVector(0.0f, 0.0f, 105.0f);
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			PlayerController, NameWorldPosition, NamePosition, false))
		{
			NameCard->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		if (UCanvasPanelSlot* NameCanvasSlot = Cast<UCanvasPanelSlot>(NameCard->Slot))
		{
			NameCanvasSlot->SetPosition(NamePosition + FVector2D(0.0f, 12.0f));
		}
		FString DisplayName = bDebugFullLobby
			? FString(PreviewNames[SlotIndex])
			: Gateway->GetSurvivorDisplayName(SlotIndex + 1);
		if (DisplayName.IsEmpty())
		{
			DisplayName = TEXT("Joueur Steam");
		}
		constexpr int32 MaxPodiumNameCharacters = 20;
		if (DisplayName.Len() > MaxPodiumNameCharacters)
		{
			DisplayName = DisplayName.Left(MaxPodiumNameCharacters - 3) + TEXT("...");
		}
		NameLabel->SetText(FText::FromString(DisplayName));
		NameCard->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UPHMatchmakingWidget::ClearLobbyCharacterPreview()
{
	for (AActor* PreviewActor : LobbyPreviewActors)
	{
		if (IsValid(PreviewActor))
		{
			PreviewActor->Destroy();
		}
	}
	LobbyPreviewActors.Reset();
	PreviewedSurvivorSlots.Reset();
	bPreviewedPrivateHunter = false;
}

void UPHMatchmakingWidget::SetButtonsEnabled(
	const bool bRoleButtonsEnabled,
	const bool bCancelEnabled,
	const bool bShowCancel)
{
	if (HostButton != nullptr)
	{
		HostButton->SetIsEnabled(bRoleButtonsEnabled);
	}
	if (FindButton != nullptr)
	{
		FindButton->SetIsEnabled(bRoleButtonsEnabled);
	}
	if (RandomButton != nullptr)
	{
		RandomButton->SetIsEnabled(bRoleButtonsEnabled);
	}
	if (CancelButton != nullptr)
	{
		CancelButton->SetIsEnabled(bCancelEnabled);
		CancelButton->SetVisibility(bShowCancel ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (QuitButton != nullptr)
	{
		const UPHMatchmakingGatewaySubsystem* Gateway = GetGatewaySubsystem();
		const bool bHasLobby = IsFullLobbyPreviewRequested()
			|| (Gateway != nullptr && Gateway->HasLobbySnapshot());
		QuitButton->SetVisibility(bShowCancel || bHasLobby
			? ESlateVisibility::Collapsed
			: ESlateVisibility::Visible);
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

UPHMatchmakingGatewaySubsystem* UPHMatchmakingWidget::GetGatewaySubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UPHMatchmakingGatewaySubsystem>() : nullptr;
}

UPHSocialInviteSubsystem* UPHMatchmakingWidget::GetSocialInviteSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UPHSocialInviteSubsystem>() : nullptr;
}
