// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "GroundSnapComponent.h"

#include "TempoVehicleMovement.h"

#include "TempoCoreUtils.h"

#include "Kismet/KismetMathLibrary.h"

UGroundSnapComponent::UGroundSnapComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

FRotator RotationFromNormal(const FVector& Normal, const FRotator& StartRotation)
{
	const FVector PitchAxis = StartRotation.RotateVector(FVector::RightVector);
	return UKismetMathLibrary::MakeRotFromXZ(FQuat(PitchAxis, UE_PI / 2.0).RotateVector(Normal), Normal);
}

void UGroundSnapComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                         FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	check(GetWorld());
	check(GetOwner());

	const FVector2D Extents = bOverrideOwnerExtents ? ExtentsOverride : FVector2D(UTempoCoreUtils::GetActorLocalBounds(GetOwner()).GetExtent());

	TArray<FHitResult> GroundHits;
	TArray<FVector2D> Offsets = { FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, -1), FVector2D(-1, 1) };
	for (const FVector2D& Offset : Offsets)
	{
		const FRotator OwnerRotation = GetOwner()->GetActorRotation();
		const FVector RotatedScaledOffset = OwnerRotation.RotateVector(FVector(Offset * Extents, 0.0));
		const FVector OwnerLocation = GetOwner()->GetActorLocation();
		FHitResult GroundHit;
		const FVector Start = OwnerLocation + SearchDistance * FVector::UpVector + RotatedScaledOffset;
		const FVector End = OwnerLocation - SearchDistance * FVector::UpVector + RotatedScaledOffset;
		FCollisionQueryParams Params(TEXT("GroundSnap"), false, GetOwner());
		GetWorld()->LineTraceSingleByChannel(GroundHit, Start, End, ECC_WorldStatic, Params);
		if (!GroundHit.bBlockingHit)
		{
			UE_LOG(LogTempoVehicleMovement, Warning, TEXT("Could not find ground below %s."), *GetName());
			return;
		}
		GroundHits.Add(GroundHit);
	}

	check(GroundHits.Num() == 4);

	// Compute the normals from every combination of three ground hits, by looking to the left and right of each corner.
	TArray<FVector> AllNormals;
	TArray<float> AllHeights;
	for (int32 I = 0; I < 4; ++I)
	{
		int32 J = I == 3 ? 0 : I + 1; // (I + 1) % 4
		int32 K = I == 0 ? 3 : I - 1; // (I - 1) % 4
		const FVector& GroundHitI = GroundHits[I].Location;
		const FVector& GroundHitJ = GroundHits[J].Location;
		const FVector& GroundHitK = GroundHits[K].Location;
		const FVector IJ = GroundHitJ - GroundHitI;
		const FVector IK = GroundHitK - GroundHitI;
		AllNormals.Add(FVector::CrossProduct(IK, IJ).GetSafeNormal());
		AllHeights.Add(GroundHitI.Z);
	}

	check(AllNormals.Num() == 4);
	check(AllHeights.Num() == 4);

	// Reject any normals that are too steep
	TArray<FVector> Normals;
	TArray<float> Heights;
	for (int32 I = 0; I < 4; ++I)
	{
		if (bLimitSlopeAngle && FVector::DotProduct(AllNormals[I], FVector::UpVector) < FMath::Cos(FMath::DegreesToRadians(MaxSlopeAngle)))
		{
			// This normal is too steep.
			continue;
		}
		Normals.Add(AllNormals[I]);
		Heights.Add(AllHeights[I]);
	}

	FRotator NewRotation = GetOwner()->GetActorRotation();
	FVector NormalAvgNumerator = FVector::ZeroVector;
	const float NormalAvgDenominator = Normals.Num();
	for (const FVector& Normal : Normals)
	{
		NormalAvgNumerator += Normal;
	}
	if (NormalAvgDenominator > 0.0)
	{
		const FVector NewNormal = NormalAvgNumerator / NormalAvgDenominator;
		NewRotation = RotationFromNormal(NewNormal, GetOwner()->GetActorRotation());
	}

	FVector NewLocation = GetOwner()->GetActorLocation();
	float HeightAvgNumerator = 0.0;
	const float HeightAvgDenominator = Heights.Num();
	for (const float Height : Heights)
	{
		HeightAvgNumerator += Height;
	}
	if (HeightAvgDenominator > 0.0)
	{
		NewLocation.Z = HeightAvgNumerator / HeightAvgDenominator;
	}

	GetOwner()->SetActorTransform(FTransform(NewRotation, NewLocation, GetOwner()->GetActorScale()));
}
