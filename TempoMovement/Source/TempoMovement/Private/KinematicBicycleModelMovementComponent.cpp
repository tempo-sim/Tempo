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
