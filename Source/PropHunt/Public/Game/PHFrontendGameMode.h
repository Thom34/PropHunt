#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "PHFrontendGameMode.generated.h"

UCLASS()
class PROPHUNT_API APHFrontendGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APHFrontendGameMode();

	virtual void StartPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;

	void RefreshResultsPresentation();
};
