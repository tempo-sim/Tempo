// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "GroundSnapComponent.h"

#include "TempoMovement.h"

#include "TempoConversion.h"
#include "TempoCoreUtils.h"

#include "DrawDebugHelpers.h"
#include "Kismet/KismetMathLibrary.h"

UGroundSnapComponent::UGroundSnapComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	// So we can draw our debug rectangle in the editor and Blueprint preview viewports, where the owner is
	// posed but not playing. Outside a game world TickComponent only draws: it never moves the owner.
	bTickInEditor = true;
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

	// We tick in the editor to draw the debug rectangle, but snapping must only ever happen during play.
	const bool bIsGameWorld = UTempoCoreUtils::IsGameWorld(this);

	// Both of these are in unscaled owner-local space, matching what GetActorLocalBounds returns.
	const FVector2D Extents = bOverrideOwnerExtents ? ExtentsOverride : FVector2D(UTempoCoreUtils::GetActorLocalBounds(GetOwner(), bIncludeHiddenComponentsInExtents).GetExtent());
	const FVector2D Center = bOverrideOwnerExtents ? ExtentsCenter : FVector2D::ZeroVector;

	// GetActorLocalBounds divides out the owner's transform, so we have to apply the owner's scale ourselves.
	// The extents take the magnitude only: a negative scale would mirror our four corners into the opposite
	// winding order, which flips every normal we compute below. The center is a point, so it keeps the sign.
	const FVector OwnerScale = GetOwner()->GetActorScale();
	const FVector2D ExtentsScale = FVector2D(FMath::Abs(OwnerScale.X), FMath::Abs(OwnerScale.Y));
	const FVector2D CenterScale = FVector2D(OwnerScale.X, OwnerScale.Y);

	const FRotator OwnerRotation = GetOwner()->GetActorRotation();
	const FVector OwnerLocation = GetOwner()->GetActorLocation();
	const FCollisionQueryParams Params(TEXT("GroundSnap"), false, GetOwner());

	// The corners we trace from, in world space at the owner's origin height. Consecutive corners are
	// adjacent, which both the debug rectangle and the per-corner normals below rely on.
	TArray<FVector> TraceCorners;
	const TArray<FVector2D> Offsets = { FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, -1), FVector2D(-1, 1) };
	for (const FVector2D& Offset : Offsets)
	{
		const FVector2D LocalOffset = Center * CenterScale + Offset * Extents * ExtentsScale;
		TraceCorners.Add(OwnerLocation + OwnerRotation.RotateVector(FVector(LocalOffset, 0.0)));
	}

#if WITH_EDITOR
	// The rectangle shows where the traces would start from the owner's current pose. Editor and Blueprint
	// preview viewports only: during play we are snapping, and a rectangle stuck to the owner says nothing.
	if (bDrawDebug && !bIsGameWorld)
	{
		for (int32 I = 0; I < TraceCorners.Num(); ++I)
		{
			const int32 J = I == TraceCorners.Num() - 1 ? 0 : I + 1;
			DrawDebugLine(GetWorld(), TraceCorners[I], TraceCorners[J], FColor::Red, false, -1, 0, 3.0);
		}
	}
#endif

	if (!bIsGameWorld)
	{
		// Drawing is all an editor tick does. Tracing here would move the owner and dirty the level.
		return;
	}

	TArray<FHitResult> GroundHits;
	for (const FVector& TraceCorner : TraceCorners)
	{
		FHitResult GroundHit;
		const FVector Start = TraceCorner + SearchDistance * FVector::UpVector;
		const FVector End = TraceCorner - SearchDistance * FVector::UpVector;
		GetWorld()->LineTraceSingleByChannel(GroundHit, Start, End, ECC_WorldStatic, Params);
		if (!GroundHit.bBlockingHit)
		{
			UE_LOG(LogTempoMovement, Warning, TEXT("Could not find ground below %s."), *GetName());
			return;
		}
		GroundHits.Add(GroundHit);
	}

	ensure(GroundHits.Num() == 4);

	// Compute the normals from every combination of three ground hits, by looking to the left and right of each corner.
	TArray<FVector> AllNormals;
	TArray<FVector> AllPoints;
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
		AllPoints.Add(GroundHitI);
	}

	ensure(AllNormals.Num() == 4);
	ensure(AllPoints.Num() == 4);

	// Reject any normals that are too steep
	TArray<FVector> Normals;
	TArray<FVector> Points;
	for (int32 I = 0; I < 4; ++I)
	{
		if (bLimitSlopeAngle && FVector::DotProduct(AllNormals[I], FVector::UpVector) < FMath::Cos(QuantityConverter<Deg2Rad>::Convert(MaxSlopeAngle)))
		{
			// This normal is too steep.
			continue;
		}
		Normals.Add(AllNormals[I]);
		Points.Add(AllPoints[I]);
	}

	// Normals and Points are appended together above, so they always describe the same surviving corners.
	ensure(Normals.Num() == Points.Num());

	FRotator NewRotation = GetOwner()->GetActorRotation();
	FVector NewLocation = GetOwner()->GetActorLocation();

	const double AvgDenominator = Normals.Num();
	if (AvgDenominator > 0.0)
	{
		FVector NormalAvgNumerator = FVector::ZeroVector;
		for (const FVector& Normal : Normals)
		{
			NormalAvgNumerator += Normal;
		}
		const FVector NewNormal = NormalAvgNumerator / AvgDenominator;
		NewRotation = RotationFromNormal(NewNormal, GetOwner()->GetActorRotation());

		FVector PointAvgNumerator = FVector::ZeroVector;
		for (const FVector& Point : Points)
		{
			PointAvgNumerator += Point;
		}
		const FVector PlanePoint = PointAvgNumerator / AvgDenominator;

		// The owner's origin is assumed to sit on the ground, so rather than take the average corner height
		// we fit the plane (PlanePoint, NewNormal) to the surviving corners and evaluate it at the origin's
		// XY. The two agree whenever the surviving corners are symmetric about the origin, and diverge on a
		// slope when they are not: a non-zero ExtentsCenter, or a corner dropped by the slope limit.
		const FVector2D OriginFromPlanePoint = FVector2D(NewLocation - PlanePoint);
		if (FMath::Abs(NewNormal.Z) > UE_KINDA_SMALL_NUMBER)
		{
			NewLocation.Z = PlanePoint.Z - FVector2D::DotProduct(FVector2D(NewNormal), OriginFromPlanePoint) / NewNormal.Z;
		}
		else
		{
			// The fit is near-vertical, so it tells us nothing about the height at the origin. Only reachable
			// with bLimitSlopeAngle off, which otherwise keeps NewNormal.Z at or above cos(MaxSlopeAngle).
			NewLocation.Z = PlanePoint.Z;
		}
	}

	GetOwner()->SetActorTransform(FTransform(NewRotation, NewLocation, GetOwner()->GetActorScale()));
}
