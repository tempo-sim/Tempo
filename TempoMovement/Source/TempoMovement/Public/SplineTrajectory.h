// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SplineTrajectory.generated.h"

class USplineComponent;

// What to do when a queried time exceeds the trajectory's duration.
UENUM(BlueprintType)
enum class ESplineTrajectoryEndBehavior : uint8
{
	// Hold the final pose (clamp time to the trajectory's duration).
	Clamp,
	// Wrap back to the beginning (time modulo duration).
	Loop,
};

// Abstract base for an actor that defines a timed path through space.
// The geometry lives in a real USplineComponent (so it is editable with the in-editor
// spline gizmo and configurable over the ConfigureTrajectory RPC); subclasses define
// how time maps onto that geometry via SampleAtTime. A trajectory is pure path data: a pawn
// follows one by carrying a UTrajectoryFollowingComponent that references it, and multiple
// pawns may share a single trajectory.
UCLASS(Abstract)
class TEMPOMOVEMENT_API ASplineTrajectory : public AActor
{
	GENERATED_BODY()

public:
	ASplineTrajectory();

	// World-space transform (location + tangent-aligned rotation) at the given time, in seconds.
	// Negative times are clamped to the start; times past GetDuration() follow EndBehavior.
	// Non-virtual: subclasses implement the un-wrapped SampleAtTime.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	FTransform GetTransformAtTime(float Time) const;

	// Time to traverse the whole trajectory, in seconds. 0 if the trajectory is empty.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	virtual float GetDuration() const PURE_VIRTUAL(ASplineTrajectory::GetDuration, return 0.0f;);

	USplineComponent* GetSpline() const { return Spline; }

protected:
	// Sample the trajectory at a time already normalized to [0, GetDuration()].
	virtual FTransform SampleAtTime(float Time) const PURE_VIRTUAL(ASplineTrajectory::SampleAtTime, return FTransform::Identity;);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trajectory")
	USplineComponent* Spline = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	ESplineTrajectoryEndBehavior EndBehavior = ESplineTrajectoryEndBehavior::Clamp;
};
