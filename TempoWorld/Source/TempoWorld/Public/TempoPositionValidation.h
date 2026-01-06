// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

// Result of position validation
struct TEMPOWORLD_API FPositionValidationResult
{
	// Is the position valid (not underground)?
	bool bIsValid = true;

	// Is the position below ground level?
	bool bIsBelowGround = false;

	// Suggested corrected position (if validation failed and requested)
	TOptional<FVector> SuggestedPosition;

	// Ground height at this location (if detected)
	TOptional<float> GroundHeight;

	// Distance below ground in cm (if underground)
	float DepthBelowGround = 0.0f;
};

// Configuration for validation queries
struct TEMPOWORLD_API FPositionValidationParams
{
	// Collision channel to use for traces
	ECollisionChannel CollisionChannel = ECC_WorldStatic;

	// Maximum distance to search for ground (in cm)
	float SearchDistance = 1000000.0f; // 10km default (matches GroundSnapComponent)

	// Actors to ignore during traces
	TArray<AActor*> IgnoredActors;

	// Whether to compute suggested position
	bool bComputeSuggestedPosition = false;
};

namespace TempoPositionValidation
{
	/**
	 * Check if a position is below ground.
	 * Algorithm: Cast ray UP and DOWN from position.
	 * If UP hits but DOWN doesn't hit -> below ground.
	 *
	 * NOTE: This algorithm may produce false positives when the position is:
	 * - Under an overhanging cliff (nothing below, cliff above)
	 * - Under a bridge or elevated platform
	 * - Under tree canopy
	 * The algorithm detects "under something solid with nothing below" which isn't
	 * always the same as "underground."
	 *
	 * @param World The world to trace in
	 * @param Position The position to check (in Unreal coordinates - cm)
	 * @param Params Validation parameters
	 * @param OutGroundPosition If not null and position is underground, receives the ground position
	 * @return true if position is below ground
	 */
	TEMPOWORLD_API bool IsBelowGround(
		const UWorld* World,
		const FVector& Position,
		const FPositionValidationParams& Params,
		FVector* OutGroundPosition = nullptr);

	/**
	 * Full position validation (checks if below ground).
	 *
	 * @param World The world to trace in
	 * @param Position The position to check (in Unreal coordinates - cm)
	 * @param Params Validation parameters
	 * @return Validation result with all check outcomes
	 */
	TEMPOWORLD_API FPositionValidationResult ValidatePosition(
		const UWorld* World,
		const FVector& Position,
		const FPositionValidationParams& Params);

	/**
	 * Find the ground height at a given XY position.
	 *
	 * @param World The world to trace in
	 * @param Position XY position to find ground at (in Unreal coordinates - cm)
	 * @param Params Validation parameters
	 * @return Ground Z coordinate in cm, or empty if no ground found
	 */
	TEMPOWORLD_API TOptional<float> FindGroundHeight(
		const UWorld* World,
		const FVector2D& Position,
		const FPositionValidationParams& Params);
}
