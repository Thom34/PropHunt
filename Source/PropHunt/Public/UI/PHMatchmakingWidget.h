#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Online/PHSessionSubsystem.h"

#include "PHMatchmakingWidget.generated.h"

class UButton;
class UTextBlock;

UCLASS()
class PROPHUNT_API UPHMatchmakingWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleHostClicked();

	UFUNCTION()
	void HandleFindClicked();

	UFUNCTION()
	void HandleRandomClicked();

	UFUNCTION()
	void HandleQuitClicked();

	UFUNCTION()
	void HandleStateChanged(EPHMatchmakingState NewState);

	UFUNCTION()
	void HandleLobbySuggested(const FPHLobbySummary& Lobby);

	UFUNCTION()
	void HandleAvailabilityChanged(const FPHMatchmakingAvailability& NewAvailability);

	UFUNCTION()
	void HandleOperationFinished(bool bSucceeded, const FString& Message);

	void SetButtonsEnabled(bool bEnabled);
	void RefreshAvailability();
	void SetStatus(const FString& Status);
	UPHSessionSubsystem* GetSessionSubsystem() const;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusLabel;

	UPROPERTY(Transient)
	TObjectPtr<UButton> HostButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> FindButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RandomButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> QuitButton;

	FTimerHandle AvailabilityRefreshTimer;
};
