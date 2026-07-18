#include "Gameplay/Physics/PHPhysicsPropPresentation.h"

namespace
{
FQuat GetSafeRotation(const FQuat& Candidate)
{
	if (Candidate.ContainsNaN() || Candidate.SizeSquared() <= SMALL_NUMBER)
	{
		return FQuat::Identity;
	}
	return Candidate.GetNormalized();
}

FVector GetSafeVector(const FVector& Candidate)
{
	return Candidate.ContainsNaN() ? FVector::ZeroVector : Candidate;
}
}

FVector PHPhysicsPropPresentation::ComputeJumpTargetVelocity(
	const FVector& CurrentVelocity,
	const FVector& BoostDirection,
	const float JumpVelocity,
	const float HorizontalBoostVelocity,
	const float MaximumHorizontalSpeed)
{
	const FVector SafeCurrentVelocity = GetSafeVector(CurrentVelocity);
	FVector HorizontalVelocity(SafeCurrentVelocity.X, SafeCurrentVelocity.Y, 0.0f);
	const FVector SafeBoost = GetSafeVector(BoostDirection).GetClampedToMaxSize(1.0f);
	const FVector SafeBoostDirection = SafeBoost.GetSafeNormal2D();
	if (!SafeBoostDirection.IsNearlyZero())
	{
		HorizontalVelocity += SafeBoostDirection
			* FMath::Clamp(HorizontalBoostVelocity, 0.0f, 1000.0f)
			* SafeBoost.Size2D();
	}
	HorizontalVelocity = HorizontalVelocity.GetClampedToMaxSize(
		FMath::Clamp(MaximumHorizontalSpeed, 1.0f, 2500.0f));
	return FVector(
		HorizontalVelocity.X,
		HorizontalVelocity.Y,
		FMath::Clamp(JumpVelocity, 0.0f, 1000.0f));
}

float PHPhysicsPropPresentation::ComputeGroundContactCorrection(
	const bool bServerGrounded,
	const float VisualBoundsBottom,
	const float GroundHeight,
	const float MaximumCorrection)
{
	if (!bServerGrounded || !FMath::IsFinite(VisualBoundsBottom)
		|| !FMath::IsFinite(GroundHeight) || !FMath::IsFinite(MaximumCorrection))
	{
		return 0.0f;
	}

	const float Correction = GroundHeight - VisualBoundsBottom;
	const float SafeMaximumCorrection = FMath::Clamp(MaximumCorrection, 0.0f, 100.0f);
	return FMath::Abs(Correction) <= SafeMaximumCorrection ? Correction : 0.0f;
}

void PHPhysicsPropPresentation::Reset(
	FPHPhysicsPropPresentationState& State,
	const FVector& NetworkLocation,
	const FQuat& NetworkRotation,
	const double ClientTimeSeconds)
{
	const FVector SafeLocation = GetSafeVector(NetworkLocation);
	const FQuat SafeRotation = GetSafeRotation(NetworkRotation);
	State.bInitialized = true;
	State.Location = SafeLocation;
	State.Rotation = SafeRotation;
	State.LastNetworkLocation = SafeLocation;
	State.LastNetworkRotation = SafeRotation;
	State.LastNetworkLinearVelocity = FVector::ZeroVector;
	State.LastNetworkAngularVelocityDegrees = FVector::ZeroVector;
	State.LastSnapshotClientTimeSeconds = FMath::IsFinite(ClientTimeSeconds)
		? ClientTimeSeconds
		: 0.0;
}

void PHPhysicsPropPresentation::Advance(
	FPHPhysicsPropPresentationState& State,
	const FVector& NetworkLocation,
	const FQuat& NetworkRotation,
	const FVector& LinearVelocity,
	const FVector& AngularVelocityDegrees,
	const double ClientTimeSeconds,
	const float DeltaSeconds,
	const FPHPhysicsPropPresentationSettings& Settings)
{
	const FVector SafeNetworkLocation = GetSafeVector(NetworkLocation);
	const FQuat SafeNetworkRotation = GetSafeRotation(NetworkRotation);
	const FVector SafeLinearVelocity = GetSafeVector(LinearVelocity).GetClampedToMaxSize(3000.0f);
	const FVector SafeAngularVelocity = GetSafeVector(AngularVelocityDegrees).GetClampedToMaxSize(2000.0f);
	const double SafeClientTime = FMath::IsFinite(ClientTimeSeconds)
		? ClientTimeSeconds
		: State.LastSnapshotClientTimeSeconds;

	if (!State.bInitialized)
	{
		Reset(State, SafeNetworkLocation, SafeNetworkRotation, SafeClientTime);
		return;
	}

	const bool bNewSnapshot = !SafeNetworkLocation.Equals(State.LastNetworkLocation, 0.01f)
		|| !SafeNetworkRotation.Equals(State.LastNetworkRotation, 0.0001f)
		|| !SafeLinearVelocity.Equals(State.LastNetworkLinearVelocity, 0.1f)
		|| !SafeAngularVelocity.Equals(State.LastNetworkAngularVelocityDegrees, 0.1f)
		|| SafeClientTime < State.LastSnapshotClientTimeSeconds;
	if (bNewSnapshot)
	{
		State.LastNetworkLocation = SafeNetworkLocation;
		State.LastNetworkRotation = SafeNetworkRotation;
		State.LastNetworkLinearVelocity = SafeLinearVelocity;
		State.LastNetworkAngularVelocityDegrees = SafeAngularVelocity;
		State.LastSnapshotClientTimeSeconds = SafeClientTime;
	}

	const float TeleportDistance = FMath::Clamp(Settings.TeleportDistance, 50.0f, 2000.0f);
	if (FVector::DistSquared(State.Location, SafeNetworkLocation) > FMath::Square(TeleportDistance))
	{
		Reset(State, SafeNetworkLocation, SafeNetworkRotation, SafeClientTime);
		return;
	}

	const float SafeDeltaSeconds = FMath::Clamp(
		FMath::IsFinite(DeltaSeconds) ? DeltaSeconds : 0.0f,
		0.0f,
		0.1f);
	const float MaximumExtrapolation = FMath::Clamp(
		Settings.MaximumExtrapolationSeconds,
		0.0f,
		0.25f);
	const float SnapshotAge = FMath::Clamp(
		static_cast<float>(SafeClientTime - State.LastSnapshotClientTimeSeconds),
		0.0f,
		MaximumExtrapolation);

	const FVector PredictedLocation = SafeNetworkLocation + SafeLinearVelocity * SnapshotAge;

	FQuat PredictedRotation = SafeNetworkRotation;
	const float AngularSpeedDegrees = SafeAngularVelocity.Size();
	if (SnapshotAge > 0.0f && AngularSpeedDegrees > KINDA_SMALL_NUMBER)
	{
		const FQuat ExtrapolatedRotation(
			SafeAngularVelocity / AngularSpeedDegrees,
			FMath::DegreesToRadians(AngularSpeedDegrees * SnapshotAge));
		PredictedRotation = (ExtrapolatedRotation * SafeNetworkRotation).GetNormalized();
	}

	const float LocationSpeed = FMath::Clamp(Settings.LocationSmoothingSpeed, 1.0f, 120.0f);
	const float RotationSpeed = FMath::Clamp(Settings.RotationSmoothingSpeed, 1.0f, 120.0f);
	const float LocationAlpha = 1.0f - FMath::Exp(-LocationSpeed * SafeDeltaSeconds);
	const float RotationAlpha = 1.0f - FMath::Exp(-RotationSpeed * SafeDeltaSeconds);
	State.Location = FMath::Lerp(State.Location, PredictedLocation, LocationAlpha);
	State.Rotation = FQuat::Slerp(State.Rotation, PredictedRotation, RotationAlpha).GetNormalized();
}
