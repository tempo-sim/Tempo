// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "TempoMovementTypes.generated.h"

USTRUCT(BlueprintType)
struct FNormalizedDrivingInput {
	GENERATED_BODY();

	// Normalized acceleration in [-1.0 <-> 1.0]
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Acceleration = 0.0;

	// Normalized steering in [-1.0 <-> 1.0]
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Steering = 0.0;
};
