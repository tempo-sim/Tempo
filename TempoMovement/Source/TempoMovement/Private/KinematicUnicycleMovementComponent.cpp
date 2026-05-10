// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicUnicycleModelMovementComponent.h"

void UKinematicUnicycleModelMovementComponent::SimulateMotion(float DeltaTime, float Steering, float NewLinearVelocity, FVector& OutNewVelocity, float& OutNewAngularVelocity)
{
	const float HeadingAngle = FMath::DegreesToRadians(GetOwner()->GetActorRotation().Yaw);
	OutNewVelocity = NewLinearVelocity * FVector(FMath::Cos(HeadingAngle), FMath::Sin(HeadingAngle), 0.0);
	OutNewAngularVelocity = SteeringToAngularVelocityFactor * Steering;
}

float UKinematicUnicycleModelMovementComponent::ComputeNormalizedSteeringForYawRate(float TargetYawRateRadS, float CurrentLinearVelocityMps) const
{
	if (FMath::IsNearlyZero(SteeringToAngularVelocityFactor) || FMath::IsNearlyZero(MaxSteering))
	{
		return 0.0f;
	}
	// Unicycle forward model: yaw_rate_deg_s_LH = SteeringToAngularVelocityFactor * Steering.
	// Invert and convert from right-handed rad/s.
	const float TargetYawRateDegS_LH = -FMath::RadiansToDegrees(TargetYawRateRadS);
	const float Steering = TargetYawRateDegS_LH / SteeringToAngularVelocityFactor;
	return FMath::Clamp(Steering / MaxSteering, -1.0f, 1.0f);
}
