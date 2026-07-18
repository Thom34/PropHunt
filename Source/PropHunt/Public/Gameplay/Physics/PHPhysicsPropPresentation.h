#pragma once

#include "CoreMinimal.h"

struct FPHPhysicsPropPresentationSettings
{
	float LocationSmoothingSpeed = 24.0f;
	float RotationSmoothingSpeed = 24.0f;
	float MaximumExtrapolationSeconds = 0.10f;
	float TeleportDistance = 200.0f;
};

struct FPHPhysicsPropPresentationState
{
	bool bInitialized = false;
	FVector Location = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	FVector LastNetworkLocation = FVector::ZeroVector;
	FQuat LastNetworkRotation = FQuat::Identity;
	FVector LastNetworkLinearVelocity = FVector::ZeroVector;
	FVector LastNetworkAngularVelocityDegrees = FVector::ZeroVector;
	double LastSnapshotClientTimeSeconds = 0.0;
};

namespace PHPhysicsPropPresentation
{
	PROPHUNT_API FVector ComputeJumpTargetVelocity(
		const FVector& CurrentVelocity,
		const FVector& BoostDirection,
		float JumpVelocity,
		float HorizontalBoostVelocity,
		float MaximumHorizontalSpeed);

	PROPHUNT_API float ComputeGroundContactCorrection(
		bool bServerGrounded,
		float VisualBoundsBottom,
		float GroundHeight,
		float MaximumCorrection);

	PROPHUNT_API void Reset(
		FPHPhysicsPropPresentationState& State,
		const FVector& NetworkLocation,
		const FQuat& NetworkRotation,
		double ClientTimeSeconds);

	PROPHUNT_API void Advance(
		FPHPhysicsPropPresentationState& State,
		const FVector& NetworkLocation,
		const FQuat& NetworkRotation,
		const FVector& LinearVelocity,
		const FVector& AngularVelocityDegrees,
		double ClientTimeSeconds,
		float DeltaSeconds,
		const FPHPhysicsPropPresentationSettings& Settings);
}
