// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicUnicycleModelMovementComponent.h"

void UKinematicUnicycleModelMovementComponent::UpdateState(float DeltaTime, float Steering)
{
	const float HeadingAngle = FMath::DegreesToRadians(GetOwner()->GetActorRotation().Yaw);

	Velocity = LinearVelocity * FVector(FMath::Cos(HeadingAngle), FMath::Sin(HeadingAngle), 0.0);
	AngularVelocity = SteeringToAngularVelocityFactor * Steering;

	GetOwner()->AddActorWorldOffset(DeltaTime * Velocity);
	GetOwner()->AddActorWorldRotation(FRotator(0.0, DeltaTime * AngularVelocity, 0.0));
}
