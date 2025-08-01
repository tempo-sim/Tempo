// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "TempoMovementTypes.generated.h"

USTRUCT(BlueprintType)
struct FNormalizedDrivingInput {
	GENERATED_BODY();

	FNormalizedDrivingInput() = default;
	FNormalizedDrivingInput(float LongitudinalIn, float LateralIn)
		: Longitudinal(FMath::Clamp(LongitudinalIn, -1.0, 1.0)), Lateral(FMath::Clamp(LateralIn, -1.0, 1.0))
	{}

	float GetLongitudinal() const { return Longitudinal; }

	float GetLateral() const { return Lateral; }

protected:
	// Normalized longitudinal input [-1.0 <-> 1.0]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=-1.0, UIMax=1.0, ClampMin=-1.0, ClampMax=1.0))
	float Longitudinal = 0.0;

	// Normalized lateral input [-1.0 <-> 1.0]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=-1.0, UIMax=1.0, ClampMin=-1.0, ClampMax=1.0))
	float Lateral = 0.0;
};
