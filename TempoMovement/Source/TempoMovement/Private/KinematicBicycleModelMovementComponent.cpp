// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicBicycleModelMovementComponent.h"

#include "TempoConversion.h"

FTempoTwist UKinematicBicycleModelMovementComponent::SimulateMotion(float DeltaTime, float Steering, float NewLinearVelocity)
{
	const float HeadingAngleRad = QuantityConverter<Deg2Rad>::Convert(GetOwner()->GetActorRotation().Yaw);
	const float SteeringRad = QuantityConverter<Deg2Rad>::Convert(Steering);
	const float RearAxleDistance = AxleRatio * Wheelbase;
	const float Beta = FMath::Atan2(RearAxleDistance * FMath::Tan(SteeringRad), Wheelbase);
	const FVector LinearVelocity = NewLinearVelocity * FVector(FMath::Cos(HeadingAngleRad + Beta), FMath::Sin(HeadingAngleRad + Beta), 0.0);
	const float YawRateDegS = QuantityConverter<Rad2Deg>::Convert(NewLinearVelocity * FMath::Sin(SteeringRad) / Wheelbase);
	return FTempoTwist(LinearVelocity, FVector(0.0, 0.0, YawRateDegS));
}

float UKinematicBicycleModelMovementComponent::ComputeNormalizedSteeringForYawRate(float TargetYawRateDegS, float CurrentLinearVelocityCmS) const
{
	if (FMath::IsNearlyZero(CurrentLinearVelocityCmS) || FMath::IsNearlyZero(MaxSteering))
	{
		// Cannot produce yaw rate from zero speed; saturate steering toward the requested direction
		// so the vehicle will turn as soon as it starts moving.
		return FMath::Sign(TargetYawRateDegS);
	}

	// Bicycle: yaw_rate = v * sin(steer) / wheelbase. Asin takes a unitless argument, so convert
	// the yaw-rate-times-wheelbase quotient to radians before inverting.
	const float TargetYawRateRadS = QuantityConverter<Deg2Rad>::Convert(TargetYawRateDegS);
	const float SinArg = FMath::Clamp(TargetYawRateRadS * Wheelbase / CurrentLinearVelocityCmS, -1.0f, 1.0f);
	const float SteeringDeg = QuantityConverter<Rad2Deg>::Convert(FMath::Asin(SinArg));
	return FMath::Clamp(SteeringDeg / MaxSteering, -1.0f, 1.0f);
}
