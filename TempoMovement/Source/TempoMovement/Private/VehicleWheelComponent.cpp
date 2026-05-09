// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "VehicleWheelComponent.h"

#include "TempoMovementInterface.h"
#include "GameFramework/PawnMovementComponent.h"

#include "Kismet/KismetMathLibrary.h"

UVehicleWheelComponent::UVehicleWheelComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UVehicleWheelComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Get owner up vector, linear velocity, angular velocity
	const FVector UpVector = GetOwner()->GetActorUpVector();
	const FVector OwnerLinearVelocity = GetOwner()->GetVelocity();
	FVector OwnerAngularVelocity = FVector::ZeroVector;
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const UPawnMovementComponent* PawnMovementComponent = Pawn->GetMovementComponent())
		{
			if (const ITempoMovementInterface* TempoMovement = Cast<ITempoMovementInterface>(PawnMovementComponent))
			{
				OwnerAngularVelocity = FMath::DegreesToRadians(TempoMovement->GetAngularVelocity());
			}
		}
	}

	// Transform owner linear and angular velocity to the component.
	const FVector ComponentRelativeLocation = GetComponentLocation() - GetOwner()->GetActorLocation();
	ComponentVelocity = OwnerLinearVelocity + FVector::CrossProduct(OwnerAngularVelocity, ComponentRelativeLocation);

	// Find component of component velocity in the plane normal to rotation axis.
	const FVector WorldRotationAxis = GetComponentTransform().TransformVector(RotationAxis);
	const FVector ComponentVelocityProjection = FVector::PointPlaneProject(ComponentVelocity, FPlane(WorldRotationAxis));

	// Find the direction and rate of rotation.
	const float RotationSign = FMath::Sign(FVector::CrossProduct(ComponentVelocityProjection, WorldRotationAxis).Dot(UpVector));
	const float RotationRate = FMath::RadiansToDegrees(ComponentVelocityProjection.Size() / WheelRadius);

	AddWorldRotation(UKismetMathLibrary::RotatorFromAxisAndAngle(WorldRotationAxis, DeltaTime * RotationSign * RotationRate));
}
