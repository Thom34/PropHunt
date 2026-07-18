#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "PHInGameMenuWidget.generated.h"

class UButton;

UCLASS()
class PROPHUNT_API UPHInGameMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

private:
	UFUNCTION()
	void HandleResumeClicked();

	UFUNCTION()
	void HandleReturnToLobbyClicked();

	UFUNCTION()
	void HandleQuitClicked();

	UPROPERTY(Transient)
	TObjectPtr<UButton> ResumeButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ReturnToLobbyButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> QuitButton;
};
