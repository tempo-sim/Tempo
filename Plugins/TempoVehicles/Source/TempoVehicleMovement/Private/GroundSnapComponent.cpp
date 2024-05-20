// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "GroundSnapComponent.h"

#include "TempoVehicleMovement.h"

UGroundSnapComponent::UGroundSnapComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

FRotator RotationFromNormal(const FVector& Normal, const FRotator& StartRotation)
{
	const FQuat StartQuat = StartRotation.Quaternion();
	const FVector UpVector = StartQuat.GetUpVector();

	const FVector RotationAxis = FVector::CrossProduct(UpVector, Normal).GetSafeNormal();
	const float RotationAngle = FMath::Acos(FVector::DotProduct(UpVector, Normal));

	return (FQuat(RotationAxis, RotationAngle) * StartQuat).Rotator();
}

void UGroundSnapComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                         FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	check(GetWorld());
	check(GetOwner());
	static constexpr float kSearchDist = 1000000.0; // 10000m
	FHitResult GroundHit;
	const FVector Start = GetOwner()->GetActorLocation() + kSearchDist * FVector::UpVector;
	const FVector End = GetOwner()->GetActorLocation() - kSearchDist * FVector::UpVector;
	FCollisionQueryParams Params(TEXT("GroundSnap"), false, GetOwner());
	GetWorld()->LineTraceSingleByChannel(GroundHit, Start, End, ECC_WorldStatic, Params);
	if (!GroundHit.bBlockingHit)
	{
		UE_LOG(LogTempoVehicleMovement, Warning, TEXT("Could not find ground below %s."), *GetName());
		return;
	}
	const FVector NewLocation = FVector(FVector2D(GetOwner()->GetActorLocation()), GroundHit.Location.Z);
	FRotator NewRotation = RotationFromNormal(GroundHit.Normal, GetOwner()->GetActorRotation());

	// Don't snap to super steep surfaces. Leave the rotation unchanged.
	static constexpr float kMaxSlopeAngle = 30.0;
	if (FMath::Abs(NewRotation.Pitch) > kMaxSlopeAngle || FMath::Abs(NewRotation.Roll) > kMaxSlopeAngle)
	{
		NewRotation = GetOwner()->GetActorRotation();
	}

	GetOwner()->SetActorTransform(FTransform(NewRotation, NewLocation, GetOwner()->GetActorScale()));
}
