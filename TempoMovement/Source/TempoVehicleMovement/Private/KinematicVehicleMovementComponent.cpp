// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicVehicleMovementComponent.h"

UKinematicVehicleMovementComponent::UKinematicVehicleMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UKinematicVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Remap and clamp Command (if any).
	float NormalizedAcceleration = 0.0;
	if (LatestInput.IsSet())
	{
		NormalizedAcceleration = LatestInput.GetValue().Acceleration;
		SteeringInput = LatestInput.GetValue().Steering;
		LatestInput.Reset();
	}
	else
	{
		FVector InputVector = ConsumeInputVector();
		if (!InputVector.IsZero())
		{
			NormalizedAcceleration = AccelerationInputMultiplier * InputVector.X;
			SteeringInput = SteeringInputMultiplier * InputVector.Y;
		}
		else
		{
			const APawn* Pawn = GetPawnOwner();
			if (Cast<APlayerController>(Pawn->GetController()))
			{
				SteeringInput = 0.0;
			}
		}
	}

	const float Acceleration = NormalizedAcceleration > 0.0 ?
		FMath::Min(MaxAcceleration, NormalizedAcceleration * MaxAcceleration) : LinearVelocity > 0.0 ?
		// Moving forward, slowing down
		FMath::Max(-MaxDeceleration, NormalizedAcceleration * MaxDeceleration) :
		// Moving backwards, speeding up (in reverse)
		FMath::Min(-MaxAcceleration, NormalizedAcceleration * MaxAcceleration);

	const float SteeringAngle = FMath::Clamp(SteeringInput * MaxSteerAngle, -MaxSteerAngle, MaxSteerAngle);

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

	UpdateState(DeltaTime, SteeringAngle);
}

void UKinematicVehicleMovementComponent::HandleDrivingInput(const FNormalizedDrivingInput& Input)
{
	LatestInput = Input;
}
