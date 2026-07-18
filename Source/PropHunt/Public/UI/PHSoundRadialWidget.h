#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "PHSoundRadialWidget.generated.h"

class UBorder;
class UTextBlock;

UCLASS()
class PROPHUNT_API UPHSoundRadialWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(const TArray<FText>& InLabels);
	int32 GetSelectedEmoteIndex() const { return SelectedEmoteIndex; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void RefreshLabels();
	void RefreshSelection();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> SegmentBorders;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> SegmentLabels;

	TArray<FText> ConfiguredLabels;
	int32 SelectedEmoteIndex = -1;
};
