// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoPositionValidation.h"

namespace TempoPositionValidation
{

bool IsBelowGround(
	const UWorld* World,
	const FVector& Position,
	const FPositionValidationParams& Params,
	FVector* OutGroundPosition)
{
	check(World);

	FCollisionQueryParams QueryParams(TEXT("BelowGroundCheck"));
	for (AActor* Actor : Params.IgnoredActors)
	{
		QueryParams.AddIgnoredActor(Actor);
	}

	// Trace upward from position
	FHitResult UpHit;
	const FVector UpStart = Position;
	const FVector UpEnd = Position + FVector::UpVector * Params.SearchDistance;
	const bool bUpHit = World->LineTraceSingleByChannel(
		UpHit, UpStart, UpEnd, Params.CollisionChannel, QueryParams);

	// Trace downward from position
	FHitResult DownHit;
	const FVector DownStart = Position;
	const FVector DownEnd = Position - FVector::UpVector * Params.SearchDistance;
	const bool bDownHit = World->LineTraceSingleByChannel(
		DownHit, DownStart, DownEnd, Params.CollisionChannel, QueryParams);

	// If we hit something above but nothing below, we're underground
	if (bUpHit && !bDownHit)
	{
		if (OutGroundPosition)
		{
			*OutGroundPosition = UpHit.ImpactPoint;
		}
		return true;
	}

	return false;
}

FPositionValidationResult ValidatePosition(
	const UWorld* World,
	const FVector& Position,
	const FPositionValidationParams& Params)
{
	FPositionValidationResult Result;

	// Check if below ground
	FVector GroundPosition;
	Result.bIsBelowGround = IsBelowGround(World, Position, Params, &GroundPosition);
	if (Result.bIsBelowGround)
	{
		Result.bIsValid = false;
		Result.GroundHeight = GroundPosition.Z;
		Result.DepthBelowGround = GroundPosition.Z - Position.Z;

		if (Params.bComputeSuggestedPosition)
		{
			// Suggest position slightly above ground (10cm buffer)
			Result.SuggestedPosition = FVector(Position.X, Position.Y, GroundPosition.Z + 10.0f);
		}
	}

	return Result;
}

TOptional<float> FindGroundHeight(
	const UWorld* World,
	const FVector2D& Position2D,
	const FPositionValidationParams& Params)
{
	check(World);

	FCollisionQueryParams QueryParams(TEXT("FindGround"));
	for (AActor* Actor : Params.IgnoredActors)
	{
		QueryParams.AddIgnoredActor(Actor);
	}

	// Start high and trace down
	const FVector Start(Position2D.X, Position2D.Y, Params.SearchDistance);
	const FVector End(Position2D.X, Position2D.Y, -Params.SearchDistance);

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, Params.CollisionChannel, QueryParams))
	{
		return Hit.ImpactPoint.Z;
	}

	return TOptional<float>();
}

} // namespace TempoPositionValidation
