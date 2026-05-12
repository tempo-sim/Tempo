// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicUnicycleModelMovementComponent.h"

#include "TempoConversion.h"

FTempoTwist UKinematicUnicycleModelMovementComponent::SimulateMotion(float DeltaTime, float Steering, float NewLinearVelocity)
{
	const float HeadingAngleRad = QuantityConverter<Deg2Rad>::Convert(GetOwner()->GetActorRotation().Yaw);
	const FVector LinearVelocity = NewLinearVelocity * FVector(FMath::Cos(HeadingAngleRad), FMath::Sin(HeadingAngleRad), 0.0);
	const float YawRateDegS = SteeringToAngularVelocityFactor * Steering;
	return FTempoTwist(LinearVelocity, FVector(0.0, 0.0, YawRateDegS));
}

float UKinematicUnicycleModelMovementComponent::ComputeNormalizedSteeringForYawRate(float TargetYawRateDegS, float CurrentLinearVelocityCmS) const
{
	if (FMath::IsNearlyZero(SteeringToAngularVelocityFactor) || FMath::IsNearlyZero(MaxSteering))
	{
		return 0.0f;
	}
	// Unicycle forward model: yaw_rate_deg_s = SteeringToAngularVelocityFactor * Steering.
	const float Steering = TargetYawRateDegS / SteeringToAngularVelocityFactor;
	return FMath::Clamp(Steering / MaxSteering, -1.0f, 1.0f);
}
