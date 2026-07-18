#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "PHPhysicsPropDataAsset.generated.h"

class UPhysicalMaterial;
class UAudioComponent;
class USoundBase;
class UStaticMesh;

UCLASS(BlueprintType)
class PROPHUNT_API UPHPhysicsPropDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPHPhysicsPropDataAsset();

	bool HasValidDefinition(FText* OutError = nullptr) const;
	void ConfigureImpactAudioComponent(UAudioComponent& AudioComponent) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop")
	FName PropId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop")
	TObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop")
	TObjectPtr<USoundBase> ImpactSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop")
	TObjectPtr<UPhysicalMaterial> FreePhysicalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop")
	TObjectPtr<UPhysicalMaterial> StraightenPhysicalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop", meta = (ClampMin = "0.01", ClampMax = "500.0", Units = "kg"))
	float MassOverrideKilograms;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Impact", meta = (ClampMin = "1.0", ClampMax = "1000.0", Units = "cm/s"))
	float ImpactImpulsePerMassThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Impact", meta = (ClampMin = "0.05", ClampMax = "2.0", Units = "s"))
	float ImpactSoundCooldownSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Impact", meta = (ClampMin = "0.0", ClampMax = "1000.0", Units = "cm"))
	float ImpactSoundInnerRadius;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Impact", meta = (ClampMin = "100.0", ClampMax = "10000.0", Units = "cm"))
	float ImpactSoundFalloffDistance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Impact")
	bool bImpactSoundOcclusion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Impact", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ImpactSoundOcclusionVolumeAttenuation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Impact", meta = (ClampMin = "20.0", ClampMax = "20000.0", Units = "Hz"))
	float ImpactSoundOcclusionLowPassFilterFrequency;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Network", meta = (ClampMin = "1.0", ClampMax = "120.0", Units = "Hz"))
	float NetworkUpdateFrequency;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Network", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float NetworkVisualLocationSmoothingSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Network", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float NetworkVisualRotationSmoothingSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Network", meta = (ClampMin = "0.0", ClampMax = "0.25", Units = "s"))
	float NetworkVisualMaximumExtrapolationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Network", meta = (ClampMin = "50.0", ClampMax = "2000.0", Units = "cm"))
	float NetworkVisualTeleportDistance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0"))
	float FreeLinearDamping;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0"))
	float FreeAngularDamping;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion")
	bool bUseExperimentalAngularAssists;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0"))
	float MovementTorqueDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0"))
	float MovementAngularAccelerationDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0", Units = "deg/s"))
	float MinimumMovementRotationSpeedDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0"))
	float EndTipAngularAccelerationDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EndBalanceTranslationScale;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0"))
	float LyingTumbleAngularAccelerationDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0", Units = "deg/s"))
	float MinimumLyingTumbleSpeedDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "1.0", Units = "cm/s"))
	float MaximumHorizontalSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.1"))
	float MovementVelocityInterpSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.1"))
	float MovementStopInterpSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float AxialVaultLiftAcceleration;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AxialEscapeAlignmentThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0", Units = "cm/s"))
	float AxialEscapeStallSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float AxialVaultLeverArmScale;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "1.0", ClampMax = "2000.0", Units = "deg/s"))
	float MaximumAngularVelocityDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MovementHopImpulse;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Jump", meta = (ClampMin = "0.0", Units = "cm/s"))
	float JumpVelocity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Jump", meta = (ClampMin = "0.0", Units = "cm/s"))
	float JumpHorizontalBoostVelocity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Jump", meta = (ClampMin = "1.0", Units = "cm/s"))
	float MaximumJumpHorizontalSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Jump", meta = (ClampMin = "1", ClampMax = "3"))
	int32 MaximumJumpCount;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Jump", meta = (ClampMin = "0.05", Units = "s"))
	float JumpCooldownSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Jump", meta = (ClampMin = "1.0", Units = "cm"))
	float GroundProbeDistance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Straighten", meta = (ClampMin = "0.0"))
	float StraightenAngularDamping;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Straighten", meta = (ClampMin = "0.0"))
	float StraightenTargetInterpSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Straighten", meta = (ClampMin = "0.0"))
	float StraightenInterpSpeedAcceleration;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Straighten", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlockedRotationMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PropHunt|Physics Prop|Motion")
	bool bUseContinuousCollisionDetection;
};
