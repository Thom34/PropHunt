#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Game/PHMatchTypes.h"

#include "PHPlayerState.generated.h"

UCLASS(Blueprintable)
class PROPHUNT_API APHPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	APHPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Player")
	EPHPlayerRole GetPlayerRole() const { return PlayerRole; }

	void SetPlayerRole(EPHPlayerRole NewRole);

protected:
	UFUNCTION()
	void OnRep_PlayerRole(EPHPlayerRole PreviousRole);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Player", meta = (DisplayName = "On Player Role Changed"))
	void BP_OnPlayerRoleChanged(EPHPlayerRole PreviousRole, EPHPlayerRole NewRole);

private:
	UPROPERTY(ReplicatedUsing = OnRep_PlayerRole, VisibleInstanceOnly, BlueprintReadOnly, Category = "PropHunt|Player", meta = (AllowPrivateAccess = "true"))
	EPHPlayerRole PlayerRole;
};
