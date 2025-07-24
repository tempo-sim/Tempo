// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicBicycleModelMovementComponent.h"

void UKinematicBicycleModelMovementComponent::UpdateState(float DeltaTime, float Steering)
{
	const float HeadingAngle = FMath::DegreesToRadians(GetOwner()->GetActorRotation().Yaw);

	const float RearAxleDistance = AxleRatio * Wheelbase;
	const float Beta = FMath::DegreesToRadians(HeadingAngle) + FMath::Atan2(RearAxleDistance * FMath::Tan(FMath::DegreesToRadians(Steering)),Wheelbase);
	Velocity = LinearVelocity * FVector(FMath::Cos(Beta), FMath::Sin(Beta), 0.0);
	AngularVelocity = FMath::RadiansToDegrees(LinearVelocity * FMath::Sin(FMath::DegreesToRadians(Steering)) / Wheelbase);

	GetOwner()->AddActorWorldOffset(FVector(DeltaTime * Velocity));
	GetOwner()->AddActorWorldRotation(FRotator(0.0, DeltaTime * AngularVelocity, 0.0));
}
