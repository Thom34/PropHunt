#pragma once

#include "CoreMinimal.h"

#include "PHCaptureTypes.generated.h"

UENUM(BlueprintType)
enum class EPHPropCaptureState : uint8
{
	Free,
	Downed,
	Carried,
	Retained,
	Grace,
	Eliminated,
	Escaped
};

namespace PHCaptureFlow
{
	inline bool IsTerminal(const EPHPropCaptureState State)
	{
		return State == EPHPropCaptureState::Eliminated || State == EPHPropCaptureState::Escaped;
	}
}

namespace PHCaptureFlow
{
	inline float GetHorizontalAimDot(
		const FVector& AimDirection,
		const FVector& ToTarget)
	{
		const FVector2D HorizontalAim(AimDirection.X, AimDirection.Y);
		const FVector2D HorizontalTarget(ToTarget.X, ToTarget.Y);
		if (HorizontalTarget.IsNearlyZero())
		{
			return 1.0f;
		}
		if (HorizontalAim.IsNearlyZero())
		{
			return -1.0f;
		}
		return FVector2D::DotProduct(
			HorizontalAim.GetSafeNormal(),
			HorizontalTarget.GetSafeNormal());
	}

	inline bool IsDownedPickupDirectionAllowed(
		const FVector& AimDirection,
		const FVector& ToTarget,
		const float MinimumAimDot)
	{
		return GetHorizontalAimDot(AimDirection, ToTarget)
			>= FMath::Clamp(MinimumAimDot, -1.0f, 1.0f);
	}

	inline bool CanMeleeHitDown(const EPHPropCaptureState CurrentState)
	{
		return CurrentState == EPHPropCaptureState::Free;
	}

	inline bool ShouldEliminateOnRetention(
		const int32 NewRetentionCount,
		const int32 MaximumRetentionCount)
	{
		return MaximumRetentionCount > 0 && NewRetentionCount >= MaximumRetentionCount;
	}

	inline float GetRetentionDuration(
		const int32 RetentionCount,
		const float FirstRetentionDuration,
		const float SecondRetentionDuration)
	{
		return RetentionCount <= 1
			? FMath::Max(0.0f, FirstRetentionDuration)
			: FMath::Max(0.0f, SecondRetentionDuration);
	}

	inline bool ShouldAccelerateAllRetainedElimination(
		const int32 ActivePropCount,
		const int32 RetainedPropCount)
	{
		return ActivePropCount > 0 && RetainedPropCount == ActivePropCount;
	}

	inline bool PreservesKnockdownCycleProgress(
		const EPHPropCaptureState PreviousState,
		const EPHPropCaptureState NewState)
	{
		return (PreviousState == EPHPropCaptureState::Downed
				&& NewState == EPHPropCaptureState::Carried)
			|| (PreviousState == EPHPropCaptureState::Carried
				&& NewState == EPHPropCaptureState::Downed);
	}

	inline float GetRecoveryProgressAfterHunterDrop(
		const float CurrentProgress,
		const float DropBonus)
	{
		return FMath::Clamp(
			FMath::Clamp(CurrentProgress, 0.0f, 1.0f)
				+ FMath::Clamp(DropBonus, 0.0f, 0.25f),
			0.0f,
			1.0f);
	}
}
