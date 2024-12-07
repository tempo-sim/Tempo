// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicBicycleModelMovementComponent.h"

UKinematicBicycleModelMovementComponent::UKinematicBicycleModelMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UKinematicBicycleModelMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Current State
	const float HeadingAngle = GetOwner()->GetActorRotation().Yaw;

	// Remap and clamp Command (if any).
	float Acceleration = 0.0;
	if (LatestCommand.IsSet())
	{
		Acceleration = LatestCommand.GetValue().Acceleration;
		SteeringAngle = LatestCommand.GetValue().SteeringAngle;
		LatestCommand.Reset();
	}
	
	// Update state.
	float DeltaVelocity = DeltaTime * Acceleration;
	if (LinearVelocity > 0.0 && DeltaVelocity < 0.0)
	{
		// If slowing down, don't start reversing.
		DeltaVelocity = FMath::Max(-LinearVelocity, DeltaVelocity);
	}
	LinearVelocity += DeltaVelocity;
	if (!bReverseEnabled)
	{
		LinearVelocity = FMath::Max(LinearVelocity, 0.0);
	}
	const float Beta = FMath::DegreesToRadians(SteeringAngle + HeadingAngle);
	Velocity = LinearVelocity * FVector(FMath::Cos(Beta), FMath::Sin(Beta), 0.0);
	AngularVelocity = FMath::RadiansToDegrees(LinearVelocity * FMath::Sin(FMath::DegreesToRadians(SteeringAngle)) / Wheelbase);

	GetOwner()->AddActorWorldOffset(FVector(DeltaTime * Velocity));
	GetOwner()->AddActorWorldRotation(FRotator(0.0, DeltaTime * AngularVelocity, 0.0));
}

void UKinematicBicycleModelMovementComponent::HandleDrivingCommand(const FDrivingCommand& Command)
{
	LatestCommand = Command;
}
