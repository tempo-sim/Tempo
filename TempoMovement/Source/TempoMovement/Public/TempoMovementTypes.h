// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "TempoMovementTypes.generated.h"

USTRUCT(BlueprintType)
struct FNormalizedDrivingInput {
	GENERATED_BODY();

	FNormalizedDrivingInput() = default;
	FNormalizedDrivingInput(float AccelerationIn, float SteeringIn)
		: Acceleration(FMath::Clamp(AccelerationIn, -1.0, 1.0)), Steering(FMath::Clamp(SteeringIn, -1.0, 1.0))
	{}

	float GetAcceleration() const { return Acceleration; }

	float GetSteering() const { return Steering; }

protected:
	// Normalized acceleration in [-1.0 <-> 1.0]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=-1.0, UIMax=1.0, ClampMin=-1.0, ClampMax=1.0))
	float Acceleration = 0.0;

	// Normalized steering in [-1.0 <-> 1.0]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(UIMin=-1.0, UIMax=1.0, ClampMin=-1.0, ClampMax=1.0))
	float Steering = 0.0;
};
