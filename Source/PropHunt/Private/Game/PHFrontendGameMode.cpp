#include "Game/PHFrontendGameMode.h"

#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "Game/PHPlayerController.h"
#include "Engine/StaticMeshActor.h"
#include "Online/PHSessionSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogPHFrontend, Log, All);

APHFrontendGameMode::APHFrontendGameMode()
{
	PlayerControllerClass = APHPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
}

void APHFrontendGameMode::StartPlay()
{
	Super::StartPlay();
	RefreshResultsPresentation();
}

void APHFrontendGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (NewPlayer == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	ACameraActor* FrontendCamera = nullptr;
	for (TActorIterator<ACameraActor> CameraIterator(GetWorld()); CameraIterator; ++CameraIterator)
	{
		if (IsValid(*CameraIterator))
		{
			FrontendCamera = *CameraIterator;
			break;
		}
	}

	NewPlayer->SetIgnoreMoveInput(true);
	NewPlayer->SetIgnoreLookInput(true);
	if (FrontendCamera != nullptr)
	{
		NewPlayer->SetViewTarget(FrontendCamera);
	}
	UE_LOG(LogPHFrontend, Log, TEXT("Frontend controller initialized without Pawn; camera=%s."),
		FrontendCamera != nullptr ? *FrontendCamera->GetName() : TEXT("none"));
}

void APHFrontendGameMode::RefreshResultsPresentation()
{
	UGameInstance* GameInstance = GetGameInstance();
	const UPHSessionSubsystem* Sessions = GameInstance != nullptr
		? GameInstance->GetSubsystem<UPHSessionSubsystem>()
		: nullptr;
	if (Sessions == nullptr || !Sessions->IsCurrentWorldResultsMap()
		|| !Sessions->HasCachedMatchResults() || GetWorld() == nullptr)
	{
		return;
	}

	const int32 VisibleSlotCount = FMath::Clamp(
		Sessions->GetCachedMatchResults().PlayerRows.Num(), 0, 5);
	for (TActorIterator<AStaticMeshActor> ActorIterator(GetWorld()); ActorIterator; ++ActorIterator)
	{
		AStaticMeshActor* SlotActor = *ActorIterator;
		if (!IsValid(SlotActor) || !SlotActor->ActorHasTag(TEXT("PHResultsSlot")))
		{
			continue;
		}

		int32 SlotIndex = 0;
		for (const FName& Tag : SlotActor->Tags)
		{
			const FString TagText = Tag.ToString();
			if (TagText.StartsWith(TEXT("PHResultsSlot_")))
			{
				SlotIndex = FCString::Atoi(*TagText.RightChop(14));
				break;
			}
		}

		const bool bVisible = SlotIndex > 0 && SlotIndex <= VisibleSlotCount;
		SlotActor->SetActorHiddenInGame(!bVisible);
		SlotActor->SetActorEnableCollision(bVisible);
	}
	UE_LOG(LogPHFrontend, Log, TEXT("Results presentation configured for %d visible player slot(s)."),
		VisibleSlotCount);
}
