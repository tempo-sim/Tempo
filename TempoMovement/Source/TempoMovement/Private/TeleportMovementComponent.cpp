// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TeleportMovementComponent.h"

#include "Kismet/KismetMathLibrary.h"

UTeleportMovementComponent::UTeleportMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UTeleportMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	LastTransform = GetOwner()->GetActorTransform();
}

void UTeleportMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const FTransform CurrentTransform = GetOwner()->GetActorTransform();
	const FVector InstantaneousVelocity = (CurrentTransform.GetLocation() - LastTransform.GetLocation()) / DeltaTime;
	Velocity = UKismetMathLibrary::WeightedMovingAverage_FVector(InstantaneousVelocity, Velocity, 1.0 - VelocitySmoothing);
	LastTransform = CurrentTransform;

	UpdateStoppedState();
}

void UTeleportMovementComponent::UpdateStoppedState()
{
	if (bStopped && Velocity.Size() > StoppedVelocityThreshold + StoppedVelocityHysteresis)
	{
		OnMovementBegan.Broadcast();
		bStopped = false;
	}
	else if (!bStopped && Velocity.Size() < StoppedVelocityThreshold - StoppedVelocityHysteresis)
	{
		OnMovementStopped.Broadcast();
		bStopped = true;
	}
}
