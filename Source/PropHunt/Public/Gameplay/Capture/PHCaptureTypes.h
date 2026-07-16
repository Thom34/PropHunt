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
	Eliminated
};

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
}
