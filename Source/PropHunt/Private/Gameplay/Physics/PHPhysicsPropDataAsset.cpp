#include "Gameplay/Physics/PHPhysicsPropDataAsset.h"

#include "Components/AudioComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Engine/StaticMesh.h"

UPHPhysicsPropDataAsset::UPHPhysicsPropDataAsset()
	: PropId(NAME_None)
	, MassOverrideKilograms(8.3912f)
	, ImpactImpulsePerMassThreshold(50.0f)
	, ImpactSoundCooldownSeconds(0.12f)
	, ImpactSoundInnerRadius(150.0f)
	, ImpactSoundFalloffDistance(1800.0f)
	, bImpactSoundOcclusion(true)
	, ImpactSoundOcclusionVolumeAttenuation(0.35f)
	, ImpactSoundOcclusionLowPassFilterFrequency(1200.0f)
	, NetworkUpdateFrequency(30.0f)
	, NetworkVisualLocationSmoothingSpeed(24.0f)
	, NetworkVisualRotationSmoothingSpeed(24.0f)
	, NetworkVisualMaximumExtrapolationSeconds(0.10f)
	, NetworkVisualTeleportDistance(200.0f)
	, FreeLinearDamping(0.01f)
	, FreeAngularDamping(0.0f)
	, bUseExperimentalAngularAssists(true)
	, MovementTorqueDegrees(900000.0f)
	, MovementAngularAccelerationDegrees(1080.0f)
	, MinimumMovementRotationSpeedDegrees(260.0f)
	, EndTipAngularAccelerationDegrees(1800.0f)
	, EndBalanceTranslationScale(0.0f)
	, LyingTumbleAngularAccelerationDegrees(900.0f)
	, MinimumLyingTumbleSpeedDegrees(220.0f)
	, MaximumHorizontalSpeed(628.0f)
	, MovementVelocityInterpSpeed(8.0f)
	, MovementStopInterpSpeed(12.0f)
	, AxialVaultLiftAcceleration(1650.0f)
	, AxialEscapeAlignmentThreshold(0.82f)
	, AxialEscapeStallSpeed(45.0f)
	, AxialVaultLeverArmScale(0.8f)
	, MaximumAngularVelocityDegrees(550.0f)
	, MovementHopImpulse(0.0f)
	, JumpVelocity(480.0f)
	, JumpHorizontalBoostVelocity(225.0f)
	, MaximumJumpHorizontalSpeed(628.0f)
	, MaximumJumpCount(2)
	, JumpCooldownSeconds(0.18f)
	, GroundProbeDistance(25.0f)
	, StraightenAngularDamping(10000.0f)
	, StraightenTargetInterpSpeed(5.0f)
	, StraightenInterpSpeedAcceleration(20.0f)
	, BlockedRotationMultiplier(0.02f)
	, bUseContinuousCollisionDetection(true)
{
}

void UPHPhysicsPropDataAsset::ConfigureImpactAudioComponent(UAudioComponent& AudioComponent) const
{
	AudioComponent.bAllowSpatialization = true;
	AudioComponent.SetSound(ImpactSound);

	FSoundAttenuationSettings Attenuation;
	Attenuation.bAttenuate = true;
	Attenuation.bSpatialize = true;
	Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation.AttenuationShapeExtents = FVector(
		FMath::Clamp(ImpactSoundInnerRadius, 0.0f, 1000.0f), 0.0f, 0.0f);
	Attenuation.FalloffDistance = FMath::Clamp(ImpactSoundFalloffDistance, 100.0f, 10000.0f);
	Attenuation.bEnableOcclusion = bImpactSoundOcclusion;
	Attenuation.OcclusionTraceChannel = ECC_Visibility;
	Attenuation.OcclusionLowPassFilterFrequency = FMath::Clamp(
		ImpactSoundOcclusionLowPassFilterFrequency, 20.0f, 20000.0f);
	Attenuation.OcclusionVolumeAttenuation = FMath::Clamp(
		ImpactSoundOcclusionVolumeAttenuation, 0.0f, 1.0f);
	AudioComponent.AdjustAttenuation(Attenuation);
}

bool UPHPhysicsPropDataAsset::HasValidDefinition(FText* OutError) const
{
	auto Fail = [OutError](const TCHAR* Message)
	{
		if (OutError != nullptr)
		{
			*OutError = FText::FromString(Message);
		}
		return false;
	};

	if (PropId.IsNone())
	{
		return Fail(TEXT("Physics Prop definition requires a stable PropId."));
	}
	if (StaticMesh == nullptr)
	{
		return Fail(TEXT("Physics Prop definition requires a cooked Static Mesh."));
	}
	if (ImpactSound == nullptr)
	{
		return Fail(TEXT("Physics Prop definition requires an impact sound."));
	}
	if (!FMath::IsFinite(MassOverrideKilograms) || MassOverrideKilograms <= 0.0f)
	{
		return Fail(TEXT("Physics Prop mass override must be finite and positive."));
	}
	if (!FMath::IsFinite(ImpactImpulsePerMassThreshold) || ImpactImpulsePerMassThreshold <= 0.0f)
	{
		return Fail(TEXT("Physics Prop impact threshold must be finite and positive."));
	}
	if (!FMath::IsFinite(ImpactSoundCooldownSeconds) || ImpactSoundCooldownSeconds < 0.05f)
	{
		return Fail(TEXT("Physics Prop impact sound cooldown must be at least 0.05 seconds."));
	}
	if (!FMath::IsFinite(ImpactSoundInnerRadius) || ImpactSoundInnerRadius < 0.0f
		|| !FMath::IsFinite(ImpactSoundFalloffDistance) || ImpactSoundFalloffDistance < 100.0f)
	{
		return Fail(TEXT("Physics Prop impact sound distances must be finite and usable."));
	}
	if (!FMath::IsFinite(ImpactSoundOcclusionVolumeAttenuation)
		|| ImpactSoundOcclusionVolumeAttenuation < 0.0f
		|| ImpactSoundOcclusionVolumeAttenuation > 1.0f
		|| !FMath::IsFinite(ImpactSoundOcclusionLowPassFilterFrequency)
		|| ImpactSoundOcclusionLowPassFilterFrequency < 20.0f)
	{
		return Fail(TEXT("Physics Prop impact sound occlusion settings are invalid."));
	}
	if (!FMath::IsFinite(NetworkUpdateFrequency) || NetworkUpdateFrequency < 1.0f || NetworkUpdateFrequency > 120.0f)
	{
		return Fail(TEXT("Physics Prop network update frequency must be between 1 and 120 Hz."));
	}
	if (!FMath::IsFinite(NetworkVisualLocationSmoothingSpeed)
		|| NetworkVisualLocationSmoothingSpeed < 1.0f
		|| NetworkVisualLocationSmoothingSpeed > 120.0f
		|| !FMath::IsFinite(NetworkVisualRotationSmoothingSpeed)
		|| NetworkVisualRotationSmoothingSpeed < 1.0f
		|| NetworkVisualRotationSmoothingSpeed > 120.0f)
	{
		return Fail(TEXT("Physics Prop network visual smoothing speeds must be between 1 and 120."));
	}
	if (!FMath::IsFinite(NetworkVisualMaximumExtrapolationSeconds)
		|| NetworkVisualMaximumExtrapolationSeconds < 0.0f
		|| NetworkVisualMaximumExtrapolationSeconds > 0.25f)
	{
		return Fail(TEXT("Physics Prop network visual extrapolation must be between zero and 0.25 seconds."));
	}
	if (!FMath::IsFinite(NetworkVisualTeleportDistance)
		|| NetworkVisualTeleportDistance < 50.0f
		|| NetworkVisualTeleportDistance > 2000.0f)
	{
		return Fail(TEXT("Physics Prop network visual teleport distance must be between 50 and 2000 cm."));
	}
	if (!FMath::IsFinite(MovementTorqueDegrees) || MovementTorqueDegrees < 0.0f)
	{
		return Fail(TEXT("Physics Prop movement torque must be finite and non-negative."));
	}
	if (!FMath::IsFinite(MovementAngularAccelerationDegrees) || MovementAngularAccelerationDegrees < 0.0f)
	{
		return Fail(TEXT("Physics Prop movement angular acceleration must be finite and non-negative."));
	}
	if (!FMath::IsFinite(MinimumMovementRotationSpeedDegrees) || MinimumMovementRotationSpeedDegrees < 0.0f)
	{
		return Fail(TEXT("Physics Prop minimum movement rotation speed must be finite and non-negative."));
	}
	if (!FMath::IsFinite(EndTipAngularAccelerationDegrees) || EndTipAngularAccelerationDegrees < 0.0f)
	{
		return Fail(TEXT("Physics Prop end tip angular acceleration must be finite and non-negative."));
	}
	if (!FMath::IsFinite(EndBalanceTranslationScale)
		|| EndBalanceTranslationScale < 0.0f
		|| EndBalanceTranslationScale > 1.0f)
	{
		return Fail(TEXT("Physics Prop end balance translation scale must be between zero and one."));
	}
	if (!FMath::IsFinite(LyingTumbleAngularAccelerationDegrees) || LyingTumbleAngularAccelerationDegrees < 0.0f)
	{
		return Fail(TEXT("Physics Prop lying tumble angular acceleration must be finite and non-negative."));
	}
	if (!FMath::IsFinite(MinimumLyingTumbleSpeedDegrees) || MinimumLyingTumbleSpeedDegrees < 0.0f)
	{
		return Fail(TEXT("Physics Prop minimum lying tumble speed must be finite and non-negative."));
	}
	if (!FMath::IsFinite(MaximumHorizontalSpeed) || MaximumHorizontalSpeed <= 0.0f)
	{
		return Fail(TEXT("Physics Prop maximum horizontal speed must be finite and positive."));
	}
	if (!FMath::IsFinite(MovementVelocityInterpSpeed) || MovementVelocityInterpSpeed <= 0.0f)
	{
		return Fail(TEXT("Physics Prop movement velocity interpolation speed must be finite and positive."));
	}
	if (!FMath::IsFinite(MovementStopInterpSpeed) || MovementStopInterpSpeed <= 0.0f)
	{
		return Fail(TEXT("Physics Prop movement stop interpolation speed must be finite and positive."));
	}
	if (!FMath::IsFinite(AxialVaultLiftAcceleration) || AxialVaultLiftAcceleration < 0.0f)
	{
		return Fail(TEXT("Physics Prop axial vault lift acceleration must be finite and non-negative."));
	}
	if (!FMath::IsFinite(AxialEscapeAlignmentThreshold)
		|| AxialEscapeAlignmentThreshold < 0.0f
		|| AxialEscapeAlignmentThreshold > 1.0f)
	{
		return Fail(TEXT("Physics Prop axial escape alignment threshold must be between zero and one."));
	}
	if (!FMath::IsFinite(AxialEscapeStallSpeed) || AxialEscapeStallSpeed < 0.0f)
	{
		return Fail(TEXT("Physics Prop axial escape stall speed must be finite and non-negative."));
	}
	if (!FMath::IsFinite(AxialVaultLeverArmScale)
		|| AxialVaultLeverArmScale < 0.1f
		|| AxialVaultLeverArmScale > 1.0f)
	{
		return Fail(TEXT("Physics Prop axial vault lever arm scale must be between 0.1 and one."));
	}
	if (!FMath::IsFinite(MaximumAngularVelocityDegrees) || MaximumAngularVelocityDegrees <= 0.0f)
	{
		return Fail(TEXT("Physics Prop maximum angular velocity must be finite and positive."));
	}
	if (!FMath::IsFinite(MovementHopImpulse) || MovementHopImpulse < 0.0f)
	{
		return Fail(TEXT("Physics Prop movement hop impulse must be finite and non-negative."));
	}
	if (!FMath::IsFinite(JumpVelocity) || JumpVelocity < 0.0f)
	{
		return Fail(TEXT("Physics Prop jump velocity must be finite and non-negative."));
	}
	if (!FMath::IsFinite(JumpHorizontalBoostVelocity) || JumpHorizontalBoostVelocity < 0.0f)
	{
		return Fail(TEXT("Physics Prop jump horizontal boost must be finite and non-negative."));
	}
	if (!FMath::IsFinite(MaximumJumpHorizontalSpeed)
		|| MaximumJumpHorizontalSpeed < MaximumHorizontalSpeed)
	{
		return Fail(TEXT("Physics Prop maximum jump horizontal speed must be finite and at least the normal maximum speed."));
	}
	if (MaximumJumpCount < 1 || MaximumJumpCount > 3)
	{
		return Fail(TEXT("Physics Prop maximum jump count must be between one and three."));
	}
	if (!FMath::IsFinite(JumpCooldownSeconds) || JumpCooldownSeconds < 0.05f)
	{
		return Fail(TEXT("Physics Prop jump cooldown must be at least 0.05 seconds."));
	}
	if (!FMath::IsFinite(GroundProbeDistance) || GroundProbeDistance <= 0.0f)
	{
		return Fail(TEXT("Physics Prop ground probe distance must be finite and positive."));
	}

	if (OutError != nullptr)
	{
		*OutError = FText::GetEmpty();
	}
	return true;
}
