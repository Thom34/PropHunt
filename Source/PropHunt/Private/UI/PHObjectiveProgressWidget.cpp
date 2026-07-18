#include "UI/PHObjectiveProgressWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Gameplay/Objectives/PHObjectiveActor.h"

void UPHObjectiveProgressWidget::SetObjective(APHObjectiveActor* InObjective)
{
	Objective = InObjective;
}

void UPHObjectiveProgressWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree == nullptr)
	{
		return;
	}

	UOverlay* RootOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ObjectiveProgressRoot"));
	WidgetTree->RootWidget = RootOverlay;

	ProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ObjectiveProgressBar"));
	ProgressBar->SetFillColorAndOpacity(FLinearColor(0.95f, 0.65f, 0.08f, 1.0f));
	if (UOverlaySlot* ProgressSlot = RootOverlay->AddChildToOverlay(ProgressBar))
	{
		ProgressSlot->SetHorizontalAlignment(HAlign_Fill);
		ProgressSlot->SetVerticalAlignment(VAlign_Fill);
	}

	PercentageLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ObjectivePercentage"));
	PercentageLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	PercentageLabel->SetJustification(ETextJustify::Center);
	PercentageLabel->SetShadowOffset(FVector2D(1.0f, 1.0f));
	if (UOverlaySlot* LabelSlot = RootOverlay->AddChildToOverlay(PercentageLabel))
	{
		LabelSlot->SetHorizontalAlignment(HAlign_Fill);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UPHObjectiveProgressWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const float Progress = Objective != nullptr
		? FMath::Clamp(Objective->GetObjectiveProgress(), 0.0f, 1.0f)
		: 0.0f;
	if (ProgressBar != nullptr)
	{
		ProgressBar->SetPercent(Progress);
	}
	if (PercentageLabel != nullptr)
	{
		PercentageLabel->SetText(FText::FromString(FString::Printf(
			TEXT("GENERATEUR %d%%"),
			FMath::RoundToInt(Progress * 100.0f))));
	}
}
