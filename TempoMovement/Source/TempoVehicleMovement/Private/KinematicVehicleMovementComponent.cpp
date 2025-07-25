// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicVehicleMovementComponent.h"

UKinematicVehicleMovementComponent::UKinematicVehicleMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bAutoRegisterUpdatedComponent = true;
}

FVector UKinematicVehicleMovementComponent::GetActorFeetLocation() const
{
	const FVector ActorLocation = GetOwner()->GetActorLocation();
	UE_LOG(LogTemp, Warning, TEXT("ActorLocation: %s"), *ActorLocation.ToString());
	return ActorLocation;
}

void UKinematicVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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
		const FVector InputLocal = GetOwner()->GetActorTransform().InverseTransformVector(InputVector);
		// DrawDebugLine(GetWorld(), GetActorFeetLocation(), GetActorFeetLocation() + InputVector * 200, FColor::Blue, false, 0.1, 0, 5.0);
		if (!FMath::IsNearlyZero(-InputVector.X, 0.5))
		{
			NormalizedAcceleration = AccelerationInputMultiplier * -InputVector.X;
		}
		if (!FMath::IsNearlyZero(-InputVector.Y, 0.5))
		{
			SteeringInput = SteeringInputMultiplier * -InputVector.Y;
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
