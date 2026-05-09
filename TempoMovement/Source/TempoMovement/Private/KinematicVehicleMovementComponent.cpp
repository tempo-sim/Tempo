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
		FMath::Sign(LinearVelocity) * -1.0 * NoInputNormalizedDeceleration * MaxDeceleration :
		ControlInput.X;
	const float SteeringInput = ControlInput.Y;

	const float Acceleration = NormalizedAcceleration > 0.0 ?
		FMath::Min(MaxAcceleration, NormalizedAcceleration * MaxAcceleration) : LinearVelocity > 0.0 ?
		// Moving forward, slowing down
		FMath::Max(-MaxDeceleration, NormalizedAcceleration * MaxDeceleration) :
		// Moving backwards, speeding up (in reverse)
		FMath::Max(-MaxAcceleration, NormalizedAcceleration * MaxAcceleration);

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

	FVector NewVelocity = Velocity;
	float NewAngularVelocity = AngularVelocity;
	SimulateMotion(DeltaTime, SteeringAngle, NewLinearVelocity, NewVelocity, NewAngularVelocity);

	FHitResult MoveHitResult;
	GetOwner()->AddActorWorldOffset(DeltaTime * NewVelocity, true, &MoveHitResult);
	LinearVelocity = NewLinearVelocity;
	Velocity = NewVelocity;

	FHitResult RotateHitResult;
	GetOwner()->AddActorWorldRotation(FRotator(0.0, DeltaTime * NewAngularVelocity, 0.0), true, &RotateHitResult);
	AngularVelocity = NewAngularVelocity;
}
