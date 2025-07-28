// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicUnicycleModelMovementComponent.h"

void UKinematicUnicycleModelMovementComponent::SimulateMotion(float DeltaTime, float Steering, float NewLinearVelocity, FVector& OutNewVelocity, float& OutNewAngularVelocity)
{
	const float HeadingAngle = FMath::DegreesToRadians(GetOwner()->GetActorRotation().Yaw);
	OutNewVelocity = NewLinearVelocity * FVector(FMath::Cos(HeadingAngle), FMath::Sin(HeadingAngle), 0.0);
	OutNewAngularVelocity = SteeringToAngularVelocityFactor * Steering;
}
