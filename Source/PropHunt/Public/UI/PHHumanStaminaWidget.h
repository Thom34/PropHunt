#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "PHHumanStaminaWidget.generated.h"

class UProgressBar;
class UTextBlock;

UCLASS()
class PROPHUNT_API UPHHumanStaminaWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> StaminaBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StaminaLabel;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> CaptureProgressBar;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> DownedRecoveryBar;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> CarryStruggleBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CaptureProgressLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MatchTimerLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> InteractionPromptLabel;
};
