// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicVehicleMovementComponent.h"

FVector UKinematicVehicleMovementComponent::GetActorFeetLocation() const
{
	return GetActorLocation();
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

	const float NormalizedAcceleration = FMath::IsNearlyZero(ControlInput.X) ?
		-FMath::Sign(LinearVelocity) * NoInputNormalizedDeceleration :
		ControlInput.X;
	const float SteeringInput = ControlInput.Y;

	// |speed| is increasing when acceleration and velocity have the same sign (or the vehicle
	// is stopped). Otherwise we're decelerating.
	const bool bSpeedIncreasing = FMath::IsNearlyZero(LinearVelocity) ||
		FMath::Sign(NormalizedAcceleration) == FMath::Sign(LinearVelocity);
	const float AccelLimit = bSpeedIncreasing ? MaxAcceleration : MaxDeceleration;
	const float Acceleration = FMath::Clamp(NormalizedAcceleration * AccelLimit, -AccelLimit, AccelLimit);

	const float SteeringAngle = FMath::Clamp(SteeringInput * MaxSteering, -MaxSteering, MaxSteering);

	float DeltaVelocity = DeltaTime * Acceleration;
	if (LinearVelocity > 0.0 && DeltaVelocity < 0.0)
	{
		// If slowing down, don't start reversing.
		DeltaVelocity = FMath::Max(-LinearVelocity, DeltaVelocity);
	}
	float NewLinearVelocity = LinearVelocity + DeltaVelocity;
	if (!bReverseEnabled)
	{
		NewLinearVelocity = FMath::Max(NewLinearVelocity, 0.0);
	}

	NewLinearVelocity = FMath::Clamp(NewLinearVelocity, -MaxSpeed, MaxSpeed);

	const FTempoTwist Motion = SimulateMotion(DeltaTime, SteeringAngle, NewLinearVelocity);

	FHitResult MoveHitResult;
	GetOwner()->AddActorWorldOffset(DeltaTime * Motion.Linear, true, &MoveHitResult);
	LinearVelocity = NewLinearVelocity;
	Velocity = Motion.Linear;

	FHitResult RotateHitResult;
	GetOwner()->AddActorWorldRotation(FRotator(0.0, DeltaTime * Motion.Angular.Z, 0.0), true, &RotateHitResult);
	AngularVelocity = Motion.Angular.Z;
}
