// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicBicycleModelMovementComponent.h"

void UKinematicBicycleModelMovementComponent::SimulateMotion(float DeltaTime, float Steering, float NewLinearVelocity, FVector& OutNewVelocity, float& OutNewAngularVelocity)
{
	const float HeadingAngle = FMath::DegreesToRadians(GetOwner()->GetActorRotation().Yaw);
	const float RearAxleDistance = AxleRatio * Wheelbase;
	const float Beta = FMath::Atan2(RearAxleDistance * FMath::Tan(FMath::DegreesToRadians(Steering)),Wheelbase);
	OutNewVelocity = NewLinearVelocity * FVector(FMath::Cos(HeadingAngle + Beta), FMath::Sin(HeadingAngle + Beta), 0.0);
	OutNewAngularVelocity = FMath::RadiansToDegrees(NewLinearVelocity * FMath::Sin(FMath::DegreesToRadians(Steering)) / Wheelbase);
}

float UKinematicBicycleModelMovementComponent::ComputeNormalizedSteeringForYawRate(float TargetYawRateRadS, float CurrentLinearVelocityMps) const
{
	// Input is right-handed; Unreal yaw is left-handed.
	const float TargetYawRateRadS_LH = -TargetYawRateRadS;

	if (FMath::IsNearlyZero(CurrentLinearVelocityMps) || FMath::IsNearlyZero(MaxSteering))
	{
		// Cannot produce yaw rate from zero speed; saturate steering toward the requested direction
		// so the vehicle will turn as soon as it starts moving.
		return FMath::Sign(TargetYawRateRadS_LH);
	}

	const float WheelbaseM = Wheelbase / 100.0f;
	const float SinArg = FMath::Clamp(TargetYawRateRadS_LH * WheelbaseM / CurrentLinearVelocityMps, -1.0f, 1.0f);
	const float SteeringRad = FMath::Asin(SinArg);
	const float SteeringDeg = FMath::RadiansToDegrees(SteeringRad);
	return FMath::Clamp(SteeringDeg / MaxSteering, -1.0f, 1.0f);
}
