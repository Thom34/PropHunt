#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "PHObjectiveProgressWidget.generated.h"

class APHObjectiveActor;
class UProgressBar;
class UTextBlock;

UCLASS()
class PROPHUNT_API UPHObjectiveProgressWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetObjective(APHObjectiveActor* InObjective);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<APHObjectiveActor> Objective;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> ProgressBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PercentageLabel;
};
