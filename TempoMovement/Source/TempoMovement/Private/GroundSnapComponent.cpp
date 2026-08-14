// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "GroundSnapComponent.h"

#include "TempoMovement.h"

#include "TempoConversion.h"
#include "TempoCoreUtils.h"

#include "DrawDebugHelpers.h"

UGroundSnapComponent::UGroundSnapComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	// So we can draw our debug rectangle in the editor and Blueprint preview viewports, where the owner is
	// posed but not playing. Outside a game world TickComponent only draws: it never moves the owner.
	bTickInEditor = true;
}

void UGroundSnapComponent::BeginPlay()
{
	Super::BeginPlay();

	check(GetOwner());

	if (!bOverrideOwnerExtents && bCacheOwnerExtents)
	{
		CachedExtents = FVector2D(UTempoCoreUtils::GetActorLocalBounds(GetOwner(), bIncludeHiddenComponentsInExtents).GetExtent());
	}
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
	const FVector2D Extents = bOverrideOwnerExtents ? ExtentsOverride :
		bCacheOwnerExtents && CachedExtents.IsSet() ? CachedExtents.GetValue() :
		FVector2D(UTempoCoreUtils::GetActorLocalBounds(GetOwner(), bIncludeHiddenComponentsInExtents).GetExtent());
	const FVector2D Center = bOverrideOwnerExtents ? ExtentsCenter : FVector2D::ZeroVector;

	// GetActorLocalBounds divides out the owner's transform, so we have to apply the owner's scale ourselves.
	// The extents take the magnitude only: a negative scale would mirror our four corners into the opposite
	// winding order. The center is a point, so it keeps the sign.
	const FVector OwnerScale = GetOwner()->GetActorScale();
	const FVector2D ScaledExtents = Extents * FVector2D(FMath::Abs(OwnerScale.X), FMath::Abs(OwnerScale.Y));
	const FVector2D ScaledCenter = Center * FVector2D(OwnerScale.X, OwnerScale.Y);

	const FVector OwnerLocation = GetOwner()->GetActorLocation();

	// Yaw only. The corners must not depend on the pitch and roll we write at the end of this function: if
	// they did, the footprint we sample would be a function of our own last answer, and anywhere the ground
	// is not locally planar the two would chase each other frame to frame instead of settling.
	const FRotator OwnerYaw(0.0, GetOwner()->GetActorRotation().Yaw, 0.0);

	// Where we trace from, as XY offsets from the owner's origin. Consecutive corners are adjacent, which the
	// debug rectangle relies on. Keeping them as offsets rather than world points also keeps the plane fit
	// below in small numbers, however far from the world origin the owner is.
	TArray<FVector2D> CornerOffsets;
	const TArray<FVector2D> CornerSigns = { FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, -1), FVector2D(-1, 1) };
	for (const FVector2D& CornerSign : CornerSigns)
	{
		CornerOffsets.Add(FVector2D(OwnerYaw.RotateVector(FVector(ScaledCenter + CornerSign * ScaledExtents, 0.0))));
	}

#if WITH_EDITOR
	// The rectangle shows where the traces would start from the owner's current pose. Editor and Blueprint
	// preview viewports only: during play we are snapping, and a rectangle stuck to the owner says nothing.
	if (bDrawDebug && !bIsGameWorld)
	{
		for (int32 I = 0; I < CornerOffsets.Num(); ++I)
		{
			const int32 J = I == CornerOffsets.Num() - 1 ? 0 : I + 1;
			DrawDebugLine(GetWorld(), OwnerLocation + FVector(CornerOffsets[I], 0.0),
				OwnerLocation + FVector(CornerOffsets[J], 0.0), FColor::Red, false, -1, 0, 3.0);
		}
	}
#endif

	if (!bIsGameWorld)
	{
		// Drawing is all an editor tick does. Tracing here would move the owner and dirty the level.
		return;
	}

	const FCollisionQueryParams Params(TEXT("GroundSnap"), false, GetOwner());

	TArray<FHitResult> GroundHits;
	for (const FVector2D& CornerOffset : CornerOffsets)
	{
		const FVector TraceCorner = OwnerLocation + FVector(CornerOffset, 0.0);
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

	// Moments of the ground hits, in XY relative to the owner's origin and Z relative to its height, for the
	// least squares fit of the plane Z = A * X + B * Y + C below. Fitting every corner at once, with none of
	// them judged on its own, is what makes this hold up on rough ground: the plane is the orientation of the
	// footprint as a whole, and detail smaller than the footprint averages out of it rather than steering it.
	double Sxx = 0.0, Sxy = 0.0, Syy = 0.0, Sx = 0.0, Sy = 0.0, Sxz = 0.0, Syz = 0.0, Sz = 0.0;
	for (int32 I = 0; I < GroundHits.Num(); ++I)
	{
		const double X = CornerOffsets[I].X;
		const double Y = CornerOffsets[I].Y;
		const double Z = GroundHits[I].Location.Z - OwnerLocation.Z;

		Sxx += X * X;
		Sxy += X * Y;
		Syy += Y * Y;
		Sx += X;
		Sy += Y;
		Sxz += X * Z;
		Syz += Y * Z;
		Sz += Z;
	}
	const double NumHits = GroundHits.Num();

	// Bias the fit gently toward flat. Without this the normal equations are singular whenever the corners do
	// not span both axes - a footprint with no extent in one of them - and ill conditioned just before that,
	// where a plane can be fit but says more about the last bits of the corner heights than about the ground.
	// Degrading smoothly toward flat beats falling off a cliff at whatever conditioning threshold we picked,
	// and at any usable footprint size the bias is far below anything measurable.
	constexpr double RegularizationFactor = 1.0e-3;
	const double Regularization = NumHits * FMath::Max(RegularizationFactor * ScaledExtents.SizeSquared(), UE_DOUBLE_SMALL_NUMBER);

	// Solve the symmetric normal equations by Cramer's rule. The regularization above makes them positive
	// definite, so the determinant is never zero.
	const double M00 = Sxx + Regularization, M01 = Sxy, M02 = Sx;
	const double M11 = Syy + Regularization, M12 = Sy, M22 = NumHits;
	const double Adj00 = M11 * M22 - M12 * M12;
	const double Adj01 = M02 * M12 - M01 * M22;
	const double Adj02 = M01 * M12 - M02 * M11;
	const double Adj11 = M00 * M22 - M02 * M02;
	const double Adj12 = M01 * M02 - M00 * M12;
	const double Adj22 = M00 * M11 - M01 * M01;
	const double Det = M00 * Adj00 + M01 * Adj01 + M02 * Adj02;

	const double A = (Adj00 * Sxz + Adj01 * Syz + Adj02 * Sz) / Det;
	const double B = (Adj01 * Sxz + Adj11 * Syz + Adj12 * Sz) / Det;
	const double C = (Adj02 * Sxz + Adj12 * Syz + Adj22 * Sz) / Det;

	// The plane was fit about the owner's origin, so C is the ground height there and (-A, -B, 1) is its
	// normal, both without any further evaluation.
	FVector Normal = FVector(-A, -B, 1.0).GetSafeNormal();

	// Cap how far the ground may tilt the owner. The cap is on the fitted plane rather than on any one corner
	// because a corner can only be judged alone by the surface normal underneath it, and on rough ground that
	// normal describes whichever facet the trace happened to land on rather than the ground the owner stands
	// on - and steps every time the trace crosses from one triangle to the next.
	const FVector TiltAxis = FVector::CrossProduct(FVector::UpVector, Normal);
	const double MaxSlopeRad = QuantityConverter<Deg2Rad>::Convert(static_cast<double>(MaxSlopeAngle));
	if (bLimitSlopeAngle && !TiltAxis.IsNearlyZero() && FMath::Acos(FMath::Clamp(Normal.Z, -1.0, 1.0)) > MaxSlopeRad)
	{
		// Only the amount of tilt is capped, never the direction, so this stays continuous as the fit crosses
		// the limit: right at the limit it leaves the normal exactly where the fit put it.
		Normal = FQuat(TiltAxis.GetUnsafeNormal(), MaxSlopeRad).RotateVector(FVector::UpVector);
	}

	// Solve for the pitch and roll that put the owner's up axis on that normal, and leave its yaw exactly as we
	// found it. Rotated into the owner's yaw frame, the normal is the up axis of FRotator(Pitch, 0, Roll),
	// which is (-cos(Roll) * sin(Pitch), sin(Roll), cos(Roll) * cos(Pitch)), so the two angles read straight
	// off it. Deriving the yaw from the normal instead - orthogonalizing a forward vector against it, say -
	// would feed a slope-dependent yaw back in every frame, and an owner standing still on a slope would creep
	// around toward its contour line.
	const FVector NormalInYawFrame = FRotator(0.0, -OwnerYaw.Yaw, 0.0).RotateVector(Normal);
	const FRotator NewRotation = FRotator(
		QuantityConverter<Rad2Deg>::Convert(FMath::Atan2(-NormalInYawFrame.X, NormalInYawFrame.Z)),
		OwnerYaw.Yaw,
		QuantityConverter<Rad2Deg>::Convert(FMath::Asin(FMath::Clamp(NormalInYawFrame.Y, -1.0, 1.0))));
	const FVector NewLocation = FVector(OwnerLocation.X, OwnerLocation.Y, OwnerLocation.Z + C);

	GetOwner()->SetActorTransform(FTransform(NewRotation, NewLocation, GetOwner()->GetActorScale()));
}
