#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Online/PHMatchmakingGatewaySubsystem.h"
#include "Online/PHSessionSubsystem.h"

#include "PHMatchmakingWidget.generated.h"

class UButton;
class UBorder;
class UCanvasPanel;
class UImage;
class UVerticalBox;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class AActor;
class UPHMatchmakingGatewaySubsystem;
class UPHSocialInviteSubsystem;

UCLASS(BlueprintType, Blueprintable)
class PROPHUNT_API UPHMatchmakingWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION()
	void HandleHostClicked();

	UFUNCTION()
	void HandleFindClicked();

	UFUNCTION()
	void HandleRandomClicked();

	UFUNCTION()
	void HandleCancelClicked();

	UFUNCTION()
	void HandleSurvivorTooltipHovered();

	UFUNCTION()
	void HandleKillerTooltipHovered();

	UFUNCTION()
	void HandleRandomTooltipHovered();

	UFUNCTION()
	void HandleCancelTooltipHovered();

	UFUNCTION()
	void HandleQuitTooltipHovered();

	UFUNCTION()
	void HandleCustomizationTooltipHovered();

	UFUNCTION()
	void HandleRoleTooltipUnhovered();

	UFUNCTION()
	void HandleInviteFriendClicked();

	UFUNCTION()
	void HandleCloseFriendsClicked();

	UFUNCTION()
	void HandleRefreshFriendsClicked();

	UFUNCTION()
	void HandleFriendInviteRequested(const FString& FriendUserId);

	UFUNCTION()
	void HandleFriendsChanged(bool bSucceeded, const FString& Message);

	UFUNCTION()
	void HandleInviteChanged(bool bSucceeded, const FString& Message);

	UFUNCTION()
	void HandleQuitClicked();

	UFUNCTION()
	void HandleGatewayStateChanged(EPHGatewayMatchmakingState NewState, const FString& StatusMessage);

	UFUNCTION()
	void HandleOperationFinished(bool bSucceeded, const FString& Message);

	void RefreshMatchmakingControls();
	void RefreshLobbyPresentation();
	void RefreshLobbyCharacterPreview();
	void RefreshPodiumInviteButtons();
	void RefreshPodiumMarkers();
	void ClearLobbyCharacterPreview();
	void RebuildFriendsList();
	void RefreshLocalSteamIdentity();
	void RefreshClientUpdateNotice();
	void ShowRoleTooltip(UButton* AnchorButton, UTexture2D* Icon, const FString& Title, const FString& Description, const FLinearColor& AccentColor);
	void SetButtonsEnabled(bool bRoleButtonsEnabled, bool bCancelEnabled, bool bShowCancel = false);
	void SetStatus(const FString& Status);
	UPHSessionSubsystem* GetSessionSubsystem() const;
	UPHMatchmakingGatewaySubsystem* GetGatewaySubsystem() const;
	UPHSocialInviteSubsystem* GetSocialInviteSubsystem() const;

protected:
	/** Presentation assets are assigned on WBP_PH_Matchmaking; C++ owns only matchmaking behavior. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|UI|Matchmaking")
	TObjectPtr<UTexture2D> SurvivorRoleIcon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|UI|Matchmaking")
	TObjectPtr<UTexture2D> KillerRoleIcon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|UI|Matchmaking")
	TObjectPtr<UTexture2D> RandomRoleIcon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|UI|Matchmaking")
	TObjectPtr<UTexture2D> QuitIcon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|UI|Matchmaking")
	TObjectPtr<UTexture2D> CustomizationIcon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PropHunt|UI|Matchmaking")
	TObjectPtr<UTexture2D> GameLogo;

private:

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> MainMenuPanel;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> FriendsPanel;

	UPROPERTY(Transient)
	TObjectPtr<UButton> FriendsDismissButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FriendsStatusLabel;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> FriendsList;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RefreshFriendsButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> CloseFriendsButton;

	TMap<FString, double> FriendInviteCooldownEnds;
	FString PendingInviteFriendUserId;
	double NextFriendCooldownRefreshTime = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusLabel;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> SteamIdentityCard;

	UPROPERTY(Transient)
	TObjectPtr<UImage> LocalSteamAvatar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LocalSteamName;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> ClientUpdateNoticeCard;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ClientUpdateNoticeTitle;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ClientUpdateNoticeDescription;

	UPROPERTY(Transient)
	TObjectPtr<UImage> GameLogoImage;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> RoleTooltipCard;

	UPROPERTY(Transient)
	TObjectPtr<UImage> RoleTooltipIcon;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> RoleTooltipIconContainer;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RoleTooltipTitle;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RoleTooltipDescription;

	float RoleTooltipOpacity = 0.0f;
	float RoleTooltipTargetOpacity = 0.0f;
	TWeakObjectPtr<UButton> RoleTooltipAnchorButton;

	double NextLocalProfileRefreshTime = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> LobbyPanel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LobbyTitleLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LobbyRoleLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LobbyCountdownLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LobbyRosterCountLabel;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> LobbySurvivorReadyIcons;

	UPROPERTY(Transient)
	TObjectPtr<UImage> LobbyHunterReadyIcon;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> LobbyPreviewActors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> LobbyPodiumMarkers;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> PodiumInviteButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> PodiumNameCards;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> PodiumNameLabels;

	TArray<int32> PreviewedSurvivorSlots;
	bool bPreviewedPrivateHunter = false;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> HunterPreviewCard;

	UPROPERTY(Transient)
	TObjectPtr<UButton> HostButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> FindButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RandomButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> CancelButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> QuitButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> CustomizationButton;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> RoleActionsPanel;
};
