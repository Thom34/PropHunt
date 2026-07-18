#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "PHCharacterBase.generated.h"

class UPHSoundRadialWidget;
class USoundBase;

UCLASS(Abstract, Blueprintable)
class PROPHUNT_API APHCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	APHCharacterBase();

	virtual void PostInitializeComponents() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "PropHunt|Sound Emote")
	float GetSoundEmoteCooldownSeconds() const { return SoundEmoteCooldownSeconds; }

	UFUNCTION(BlueprintPure, Category = "PropHunt|Sound Emote")
	int32 GetSoundEmoteCount() const { return FMath::Min(SoundEmoteLabels.Num(), SoundEmoteSounds.Num()); }

#if !UE_BUILD_SHIPPING
	void RequestSoundEmoteForSmoke(int32 EmoteIndex);
	int32 GetSoundEmoteSmokeAcceptedCount() const { return SoundEmoteSmokeAcceptedCount; }
	int32 GetSoundEmoteSmokePlaybackCount() const { return SoundEmoteSmokePlaybackCount; }
#endif

protected:
	virtual void MoveForward(float Value);
	virtual void MoveRight(float Value);
	void LookYaw(float Value);
	void LookPitch(float Value);
	virtual void RequestJump();
	virtual void RequestStopJump();
	virtual bool CanUseSoundEmote() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Presentation", meta = (DisplayName = "On Sound Emote Played"))
	void BP_OnSoundEmotePlayed(int32 EmoteIndex, const FText& Subtitle);

	UFUNCTION(BlueprintImplementableEvent, Category = "PropHunt|Presentation", meta = (DisplayName = "On Local View Ready"))
	void BP_OnLocalViewReady();

private:
	void OpenSoundRadial();
	void CloseSoundRadial();
	void CleanupSoundRadial();

	UFUNCTION(Server, Reliable)
	void ServerRequestSoundEmote(int32 EmoteIndex);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlaySoundEmote(int32 EmoteIndex);

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Movement", meta = (ClampMin = "0.0", Units = "cm/s", AllowPrivateAccess = "true"))
	float MaximumWalkSpeed;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Movement", meta = (ClampMin = "0.0", Units = "cm/s^2", AllowPrivateAccess = "true"))
	float MaximumAcceleration;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Movement", meta = (ClampMin = "0.0", Units = "cm/s^2", AllowPrivateAccess = "true"))
	float BrakingDeceleration;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Movement", meta = (ClampMin = "0.0", Units = "cm/s", AllowPrivateAccess = "true"))
	float JumpVelocity;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0", AllowPrivateAccess = "true"))
	float AirControlRatio;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Input", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float YawInputScale;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Input", meta = (ClampMin = "0.0", AllowPrivateAccess = "true"))
	float PitchInputScale;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Sound Emote", meta = (AllowPrivateAccess = "true"))
	TArray<FText> SoundEmoteLabels;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Sound Emote", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<USoundBase>> SoundEmoteSounds;

	UPROPERTY(EditDefaultsOnly, Category = "PropHunt|Sound Emote", meta = (ClampMin = "0.5", ClampMax = "30.0", Units = "s", AllowPrivateAccess = "true"))
	float SoundEmoteCooldownSeconds;

	UPROPERTY(Transient)
	TObjectPtr<UPHSoundRadialWidget> SoundRadialWidget;

	double LastServerSoundEmoteTime;

#if !UE_BUILD_SHIPPING
	int32 SoundEmoteSmokeAcceptedCount = 0;
	int32 SoundEmoteSmokePlaybackCount = 0;
#endif
};
