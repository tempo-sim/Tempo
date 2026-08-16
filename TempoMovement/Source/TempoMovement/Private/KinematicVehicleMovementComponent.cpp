// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicVehicleMovementComponent.h"

FVector UKinematicVehicleMovementComponent::GetActorFeetLocation() const
{
	return GetActorLocation();
}

FVector UKinematicVehicleMovementComponent::GetRotationCenterWorldLocation() const
{
	return GetOwner()->GetActorTransform().TransformPosition(FVector(RotationCenter, 0.0));
}

FVector UKinematicVehicleMovementComponent::ComputePivotCorrection(float DeltaYawDegrees) const
{
	if (RotationCenter.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	// Where the rotation center sits relative to the owner's origin, in world space. TransformVector
	// applies the owner's scale, so RotationCenter stays an unscaled owner-local offset.
	const FVector CenterOffset = GetOwner()->GetActorTransform().TransformVector(FVector(RotationCenter, 0.0));

	// A world rotation pivots about the owner's origin, which swings the rotation center along an arc.
	// Translating by the negative of that arc puts the center back where it started, so the two
	// together read as a rotation about the center.
	return CenterOffset - FRotator(0.0, DeltaYawDegrees, 0.0).RotateVector(CenterOffset);
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

	// Motion.Linear is the velocity of the rotation center, not of the owner's origin. Offsetting the
	// origin by the same amount and folding in the pivot correction leaves the center translating by
	// exactly Motion.Linear while the owner yaws about it.
	const FRotator DeltaRotation(0.0, DeltaTime * Motion.Angular.Z, 0.0);
	const FVector PivotCorrection = ComputePivotCorrection(DeltaRotation.Yaw);

	FHitResult MoveHitResult;
	GetOwner()->AddActorWorldOffset(DeltaTime * Motion.Linear + PivotCorrection, true, &MoveHitResult);
	LinearVelocity = NewLinearVelocity;
	// AActor::GetVelocity() reports this alongside the actor's location, so it has to describe the
	// origin: the center's velocity plus the rate at which the origin swings about the center.
	Velocity = DeltaTime > 0.0f ? Motion.Linear + PivotCorrection / DeltaTime : Motion.Linear;

	FHitResult RotateHitResult;
	// Yaw is about the world Z axis, not the owner's own up axis. These models are horizontal-plane
	// simulations — Motion.Linear has no Z component, and SimulateMotion takes its heading from
	// GetActorRotation().Yaw — and GroundSnapComponent copies yaw through untouched while solving pitch
	// and roll from the terrain. Rotating about the owner's up axis instead would inject roll that
	// GroundSnapComponent discards, and would make the heading a function of the ground we drive over.
	GetOwner()->AddActorWorldRotation(DeltaRotation, true, &RotateHitResult);
	AngularVelocity = Motion.Angular.Z;
}
