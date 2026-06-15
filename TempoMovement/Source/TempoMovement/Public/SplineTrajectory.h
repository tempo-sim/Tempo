// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "Curves/CurveFloat.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SplineTrajectory.generated.h"

class USplineComponent;

// How time maps onto the spline geometry.
UENUM(BlueprintType)
enum class ESplineTrajectorySpeedMode : uint8
{
	// Traverse the spline at a fixed speed: distance along the spline = Speed * time.
	ConstantSpeed,
	// Map time to a spline input key (point index) via a curve, letting the path dwell,
	// accelerate, decelerate, or reverse independently of arc length.
	SplinePointVsTime,
	// Map time directly to distance along the spline (cm) via a curve.
	DistanceVsTime,
	// Map time to speed along the spline (cm/s) via a curve; distance is its time integral.
	SpeedVsTime,
};

// An actor that defines a timed path through space. The geometry lives in a real USplineComponent
// (so it is editable with the in-editor spline gizmo and configurable over the ConfigureTrajectory
// RPC); SpeedMode selects how time maps onto that geometry. A trajectory is pure path data: a pawn
// follows one by carrying a UTrajectoryFollowingComponent that references it, and multiple pawns may
// share a single trajectory (each with its own following Config, including end behavior).
UCLASS()
class TEMPOMOVEMENT_API ASplineTrajectory : public AActor
{
	GENERATED_BODY()

public:
	ASplineTrajectory();

	virtual void OnConstruction(const FTransform& Transform) override;

	// World-space transform (location + tangent-aligned rotation) at the given time, in seconds.
	// Time is clamped to [0, GetDuration()]; end-of-trajectory behavior (clamp/loop/reset) is the
	// following controller's responsibility, not the trajectory's.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	FTransform GetTransformAtTime(float Time) const;

	// Time to traverse the whole trajectory, in seconds. 0 if the trajectory is empty.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	float GetDuration() const;

	USplineComponent* GetSpline() const { return Spline; }

	ESplineTrajectorySpeedMode GetSpeedMode() const { return SpeedMode; }

	// Setters used by the ConfigureTrajectory RPC. Speeds/distances are Unreal-native (cm, cm/s).
	void SetSpeedMode(ESplineTrajectorySpeedMode InSpeedMode) { SpeedMode = InSpeedMode; }
	void SetConstantSpeed(double InSpeed) { Speed = InSpeed; }
	void SetTimeToInputKeyCurve(const FRuntimeFloatCurve& InCurve) { TimeToInputKey = InCurve; }
	void SetTimeToDistanceCurve(const FRuntimeFloatCurve& InCurve) { TimeToDistance = InCurve; }
	void SetTimeToSpeedCurve(const FRuntimeFloatCurve& InCurve) { TimeToSpeed = InCurve; RebuildSpeedDistanceCache(); }

protected:
	// Sample the trajectory at a time already normalized to [0, GetDuration()].
	FTransform SampleAtTime(float Time) const;

	// Distance along the spline (cm) at the given time, for the arc-length speed modes.
	double DistanceAtTime(float Time) const;

	// Integrate TimeToSpeed into the cumulative SpeedDistanceCache (used by SpeedVsTime).
	void RebuildSpeedDistanceCache();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trajectory")
	USplineComponent* Spline = nullptr;

	// Selects how time maps onto the spline geometry.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	ESplineTrajectorySpeedMode SpeedMode = ESplineTrajectorySpeedMode::ConstantSpeed;

	// ConstantSpeed mode: speed along the spline, in cm/s.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "SpeedMode == ESplineTrajectorySpeedMode::ConstantSpeed", EditConditionHides, ClampMin = 0.0))
	double Speed = 100.0;

	// SplinePointVsTime mode: maps time in seconds (X) to spline input key / point index (Y).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "SpeedMode == ESplineTrajectorySpeedMode::SplinePointVsTime", EditConditionHides))
	FRuntimeFloatCurve TimeToInputKey;

	// DistanceVsTime mode: maps time in seconds (X) to distance along the spline in cm (Y).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "SpeedMode == ESplineTrajectorySpeedMode::DistanceVsTime", EditConditionHides))
	FRuntimeFloatCurve TimeToDistance;

	// SpeedVsTime mode: maps time in seconds (X) to speed along the spline in cm/s (Y).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "SpeedMode == ESplineTrajectorySpeedMode::SpeedVsTime", EditConditionHides))
	FRuntimeFloatCurve TimeToSpeed;

private:
	// Cumulative distance (cm) vs. time, integrated from TimeToSpeed. Rebuilt when TimeToSpeed
	// changes (RPC setter, OnConstruction); not serialized.
	FRichCurve SpeedDistanceCache;
};
