#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/PHMatchTypes.h"

#include "PHMatchResultsWidget.generated.h"

class UBorder;
class UButton;
class UTextBlock;
class UVerticalBox;

UCLASS()
class PROPHUNT_API UPHMatchResultsWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION()
	void HandleContinueClicked();

	void RebuildPlayerRows(
		const FPHMatchResultsSnapshot& Snapshot,
		int32 LocalPlayerId,
		const FString& LocalPlayerName);

	UPROPERTY(Transient)
	TObjectPtr<UBorder> ResultsPanel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> OutcomeLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ReasonLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CountdownLabel;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> PlayerRowsBox;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ContinueButton;

	uint32 LastSnapshotSignature = 0;
	bool bHasRenderedSnapshot = false;
};
