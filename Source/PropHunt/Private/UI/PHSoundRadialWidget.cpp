#include "UI/PHSoundRadialWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"

void UPHSoundRadialWidget::Configure(const TArray<FText>& InLabels)
{
	ConfiguredLabels = InLabels;
	RefreshLabels();
}

void UPHSoundRadialWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree == nullptr)
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("SoundRadialRoot"));
	WidgetTree->RootWidget = RootCanvas;

	static const FVector2D SegmentPositions[] = {
		FVector2D(0.0f, -145.0f),
		FVector2D(185.0f, 0.0f),
		FVector2D(0.0f, 145.0f),
		FVector2D(-185.0f, 0.0f)
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SegmentPositions); ++Index)
	{
		UBorder* Segment = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), *FString::Printf(TEXT("SoundRadialSegment_%d"), Index));
		Segment->SetPadding(FMargin(14.0f, 12.0f));
		Segment->SetBrushColor(FLinearColor(0.025f, 0.04f, 0.075f, 0.94f));
		RootCanvas->AddChild(Segment);
		if (UCanvasPanelSlot* SegmentSlot = Cast<UCanvasPanelSlot>(Segment->Slot))
		{
			SegmentSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			SegmentSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			SegmentSlot->SetPosition(SegmentPositions[Index]);
			SegmentSlot->SetSize(FVector2D(176.0f, 72.0f));
		}

		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), *FString::Printf(TEXT("SoundRadialLabel_%d"), Index));
		Label->SetJustification(ETextJustify::Center);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Label->SetFont(FSlateFontInfo(Label->GetFont().FontObject, 19));
		Segment->SetContent(Label);
		SegmentBorders.Add(Segment);
		SegmentLabels.Add(Label);
	}

	UTextBlock* CenterLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("SoundRadialCenterLabel"));
	CenterLabel->SetText(FText::FromString(TEXT("RELACHE X\nPOUR JOUER")));
	CenterLabel->SetJustification(ETextJustify::Center);
	CenterLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.72f, 0.12f, 1.0f)));
	CenterLabel->SetFont(FSlateFontInfo(CenterLabel->GetFont().FontObject, 17));
	RootCanvas->AddChild(CenterLabel);
	if (UCanvasPanelSlot* CenterSlot = Cast<UCanvasPanelSlot>(CenterLabel->Slot))
	{
		CenterSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		CenterSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CenterSlot->SetPosition(FVector2D::ZeroVector);
		CenterSlot->SetSize(FVector2D(170.0f, 70.0f));
	}

	RefreshLabels();
	RefreshSelection();
}

void UPHSoundRadialWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	APlayerController* PlayerController = GetOwningPlayer();
	if (PlayerController == nullptr)
	{
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
	if (!PlayerController->GetMousePosition(MouseX, MouseY) || ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		return;
	}

	const FVector2D FromCenter(
		MouseX - static_cast<float>(ViewportWidth) * 0.5f,
		MouseY - static_cast<float>(ViewportHeight) * 0.5f);
	int32 NewSelection = -1;
	if (FromCenter.SizeSquared() >= FMath::Square(42.0f))
	{
		if (FMath::Abs(FromCenter.X) > FMath::Abs(FromCenter.Y))
		{
			NewSelection = FromCenter.X >= 0.0f ? 1 : 3;
		}
		else
		{
			NewSelection = FromCenter.Y >= 0.0f ? 2 : 0;
		}
	}

	if (SelectedEmoteIndex != NewSelection)
	{
		SelectedEmoteIndex = NewSelection;
		RefreshSelection();
	}
}

void UPHSoundRadialWidget::RefreshLabels()
{
	for (int32 Index = 0; Index < SegmentLabels.Num(); ++Index)
	{
		if (SegmentLabels[Index] != nullptr)
		{
			SegmentLabels[Index]->SetText(ConfiguredLabels.IsValidIndex(Index)
				? ConfiguredLabels[Index]
				: FText::FromString(TEXT("SON")));
		}
	}
}

void UPHSoundRadialWidget::RefreshSelection()
{
	for (int32 Index = 0; Index < SegmentBorders.Num(); ++Index)
	{
		if (SegmentBorders[Index] != nullptr)
		{
			SegmentBorders[Index]->SetBrushColor(Index == SelectedEmoteIndex
				? FLinearColor(0.95f, 0.43f, 0.08f, 0.98f)
				: FLinearColor(0.025f, 0.04f, 0.075f, 0.94f));
		}
	}
}
