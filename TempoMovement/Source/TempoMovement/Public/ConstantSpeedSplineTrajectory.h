// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "SplineTrajectory.h"

#include "ConstantSpeedSplineTrajectory.generated.h"

// Traverses the spline at a fixed speed: distance along the spline = Speed * time.
UCLASS()
class TEMPOMOVEMENT_API AConstantSpeedSplineTrajectory : public ASplineTrajectory
{
	GENERATED_BODY()

public:
	virtual float GetDuration() const override;

protected:
	virtual FTransform SampleAtTime(float Time) const override;

	// Speed along the spline, in cm/s.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (ClampMin = 0.0))
	double Speed = 100.0;
};
