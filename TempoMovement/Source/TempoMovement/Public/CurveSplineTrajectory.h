// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "SplineTrajectory.h"

#include "CurveSplineTrajectory.generated.h"

// Traverses the spline according to a curve mapping time (seconds) to spline input key
// (i.e. the spline point index, fractional between points). Lets the path dwell,
// accelerate, decelerate, or reverse independently of arc length.
UCLASS()
class TEMPOMOVEMENT_API ACurveSplineTrajectory : public ASplineTrajectory
{
	GENERATED_BODY()

public:
	virtual float GetDuration() const override;

	// Replace the time-to-input-key curve (used by the ConfigureTrajectory RPC).
	void SetTimeToInputKeyCurve(const FRuntimeFloatCurve& InCurve) { TimeToInputKey = InCurve; }

protected:
	virtual FTransform SampleAtTime(float Time) const override;

	// Maps time in seconds (X) to spline input key / point index (Y).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	FRuntimeFloatCurve TimeToInputKey;
};
