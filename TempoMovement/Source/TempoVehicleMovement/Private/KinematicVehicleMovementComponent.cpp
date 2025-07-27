// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicVehicleMovementComponent.h"

UKinematicVehicleMovementComponent::UKinematicVehicleMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bAutoRegisterUpdatedComponent = true;
}

void UKinematicVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const FVector Input = ConsumeInputVector();
	FVector ControlInput;
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		ControlInput = PlayerController->GetControlRotation().GetInverse().RotateVector(Input);
	}
	else
	{
		ControlInput = GetOwner()->GetActorRotation().GetInverse().RotateVector(Input);
	}

	float NormalizedAcceleration;
	if (LatestInput.IsSet())
	{
		NormalizedAcceleration = LatestInput.GetValue().Acceleration;
		SteeringInput = LatestInput.GetValue().Steering;
		LatestInput.Reset();
	}
	else
	{
		NormalizedAcceleration = FMath::IsNearlyZero(ControlInput.X) ?
			FMath::Sign(Speed) * -1.0 * NoInputNormalizedDeceleration * MaxDeceleration :
			AccelerationInputMultiplier * ControlInput.X;
		SteeringInput = FMath::IsNearlyZero(ControlInput.Y) ?
			SteeringInput = 0.0 :
			SteeringInputMultiplier * ControlInput.Y;
	}

	const float Acceleration = NormalizedAcceleration > 0.0 ?
		FMath::Min(MaxAcceleration, NormalizedAcceleration * MaxAcceleration) : Speed > 0.0 ?
		// Moving forward, slowing down
		FMath::Max(-MaxDeceleration, NormalizedAcceleration * MaxDeceleration) :
		// Moving backwards, speeding up (in reverse)
		FMath::Max(-MaxAcceleration, NormalizedAcceleration * MaxAcceleration);

	const float SteeringAngle = FMath::Clamp(SteeringInput * MaxSteerAngle, -MaxSteerAngle, MaxSteerAngle);

	float DeltaVelocity = DeltaTime * Acceleration;
	if (Speed > 0.0 && DeltaVelocity < 0.0)
	{
		// If slowing down, don't start reversing.
		DeltaVelocity = FMath::Max(-Speed, DeltaVelocity);
	}
	Speed += DeltaVelocity;
	if (!bReverseEnabled)
	{
		Speed = FMath::Max(Speed, 0.0);
	}

	Speed = FMath::Clamp(Speed, -MaxSpeed, MaxSpeed);

	UpdateState(DeltaTime, SteeringAngle);
}

void UKinematicVehicleMovementComponent::HandleDrivingInput(const FNormalizedDrivingInput& Input)
{
	LatestInput = Input;
}
