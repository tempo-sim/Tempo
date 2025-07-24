// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicUnicycleModelMovementComponent.h"

UKinematicUnicycleModelMovementComponent::UKinematicUnicycleModelMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UKinematicUnicycleModelMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Current State
	const float HeadingAngle = FMath::DegreesToRadians(GetOwner()->GetActorRotation().Yaw);

	// Remap and clamp Command (if any).
	float Acceleration = 0.0;
	if (LatestCommand.IsSet())
	{
		Acceleration = LatestCommand.GetValue().Acceleration;
		SteeringAngle = LatestCommand.GetValue().SteeringAngle;
		LatestCommand.Reset();
	}
	else
	{
		FVector InputVector = ConsumeInputVector();
		if (!InputVector.IsZero())
		{
			Acceleration = AccelerationInputMultiplier * InputVector.X;
			SteeringAngle = SteeringInputMultiplier * InputVector.Y;
		}
		else
		{
			const APawn* Pawn = GetPawnOwner();
			if (Cast<APlayerController>(Pawn->GetController()))
			{
				SteeringAngle = 0.0;
			}
		}
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

	Velocity = LinearVelocity * FVector(FMath::Cos(HeadingAngle), FMath::Sin(HeadingAngle), 0.0);
	AngularVelocity = SteeringToAngularVelocityFactor * SteeringAngle;

	GetOwner()->AddActorWorldOffset(FVector(DeltaTime * Velocity));
	GetOwner()->AddActorWorldRotation(FRotator(0.0, DeltaTime * AngularVelocity, 0.0));
}

void UKinematicUnicycleModelMovementComponent::HandleDrivingCommand(const FDrivingCommand& Command)
{
	LatestCommand = Command;
}
