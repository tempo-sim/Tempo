// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "VehicleWheelComponent.h"

#include "TempoMovementInterface.h"

void UVehicleWheelComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const FVector OwnerLinearVelocity = GetOwner()->GetVelocity();
	FVector OwnerAngularVelocity = FVector::ZeroVector;

	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const UPawnMovementComponent* PawnMovementComponent = Pawn->GetMovementComponent())
		{
			if (const ITempoMovementInterface* TempoMovement = Cast<ITempoMovementInterface>(PawnMovementComponent))
			{
				OwnerAngularVelocity = TempoMovement->GetAngularVelocity();
			}
		}
	}

	const FVector ComponentRelativeLocation = GetComponentLocation() - GetOwner()->GetActorLocation();
	const FVector ComponentVelocity = OwnerLinearVelocity + FVector::CrossProduct(ComponentRelativeLocation, OwnerAngularVelocity);

	const float RotationRate = ComponentVelocity.Size() * FMath::Abs(FVector::CrossProduct(ComponentVelocity, GetComponentTransform().TransformVectorNoScale(RotationAxis)).Size()) / WheelRadius;

	AddLocalRotation(RotationAxis.Rotation(), RotationRate * DeltaTime);
}
