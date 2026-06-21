// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "WheeledVehicleVelocityController.h"

#include "Curves/CurveFloat.h"

#include "CoreMinimal.h"
#include "GameFramework/Controller.h"

#include "TrajectoryFollowingController.generated.h"

class APawn;
class ASplineActor;
class USplineComponent;

// How trajectory time maps onto the spline geometry the follower is traversing.
UENUM(BlueprintType)
enum class ETrajectorySpeedMode : uint8
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

// What a follower does when it reaches the end of its trajectory.
UENUM(BlueprintType)
enum class ETrajectoryEndBehavior : uint8
{
	// Hold the final pose (clamp time to the trajectory's duration).
	Clamp,
	// Wrap back to the start (time modulo duration); a steering follower drives back to the start.
	Loop,
	// Teleport the pawn (even a non-teleport/steering follower) back to the start and restart,
	// repeating indefinitely.
	Reset,
};

// Per-follower settings governing how a pawn follows a spline: the speed model that turns trajectory
// time into a point on the spline, plus how the pawn is driven to that point. Authored on a
// UTrajectoryFollowingComponent (or set over the ConfigureTrajectoryFollowing RPC) and copied onto
// the controller it spawns, so two pawns can share one spline yet traverse it at different speeds or
// with different gains and end behavior.
USTRUCT(BlueprintType)
struct FTrajectoryFollowingConfig
{
	GENERATED_BODY()

	// Selects how trajectory time maps onto the spline geometry.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	ETrajectorySpeedMode SpeedMode = ETrajectorySpeedMode::ConstantSpeed;

	// ConstantSpeed mode: speed along the spline, in cm/s.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "SpeedMode == ETrajectorySpeedMode::ConstantSpeed", EditConditionHides, ClampMin = 0.0))
	double Speed = 100.0;

	// SplinePointVsTime mode: maps time in seconds (X) to spline input key / point index (Y).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "SpeedMode == ETrajectorySpeedMode::SplinePointVsTime", EditConditionHides))
	FRuntimeFloatCurve TimeToInputKey;

	// DistanceVsTime mode: maps time in seconds (X) to distance along the spline in cm (Y).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "SpeedMode == ETrajectorySpeedMode::DistanceVsTime", EditConditionHides))
	FRuntimeFloatCurve TimeToDistance;

	// SpeedVsTime mode: maps time in seconds (X) to speed along the spline in cm/s (Y).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "SpeedMode == ETrajectorySpeedMode::SpeedVsTime", EditConditionHides))
	FRuntimeFloatCurve TimeToSpeed;

	// If true, teleport the pawn to the target pose each tick (exact, bypasses control).
	// If false, steer toward the target with AddMovementInput.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	bool bTeleport = true;

	// Proportional gain on position error (1/s) in the AddMovementInput control law.
	// For a wheeled vehicle, gain on along-track error: cm/s of target speed per cm the pawn lags the target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "!bTeleport", ClampMin = 0.0))
	double PositionGain = 1.0;

	// Scales the computed velocity correction (cm/s) into a normalized movement input.
	// Roughly 1 / (pawn's max speed in cm/s). Ignored for wheeled vehicles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "!bTeleport", ClampMin = 0.0))
	double InputScale = 0.002;

	// Steering guidance gain for wheeled vehicles: commanded yaw rate (deg/s) per degree of heading
	// error to the target point. Ignored for non-vehicle pawns (which can translate directly).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "!bTeleport", ClampMin = 0.0))
	double YawRateGain = 2.0;

	// What to do when the trajectory's duration is reached.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	ETrajectoryEndBehavior EndBehavior = ETrajectoryEndBehavior::Clamp;
};

// Drives a possessed pawn along an ASplineActor. Each tick it samples the spline at the time since
// following began (mapping time onto the spline via the config's speed model) and either teleports
// the pawn to the target pose, or steers toward it (letting the pawn and its movement component
// respond). In the non-teleport path the steering law adapts to the pawn:
//   * Wheeled vehicles (Chaos or kinematic) can't strafe, so the target is converted into a
//     body-frame velocity (trajectory pace + along-track catch-up, plus a yaw rate that turns
//     the vehicle toward the target) and tracked via VehicleVelocityController.
//   * Other pawns use AddMovementInput, which drives translation only, so their heading is
//     governed by their movement component, not the trajectory's orientation.
// Only bTeleport reproduces the target rotation exactly.
// Typically spawned by a UTrajectoryFollowingComponent, which supplies the pawn, spline, and config;
// can also be spawned and wired manually via FollowTrajectory.
UCLASS(Blueprintable)
class TEMPOMOVEMENT_API ATrajectoryFollowingController : public AController
{
	GENERATED_BODY()

public:
	ATrajectoryFollowingController();

	virtual void Tick(float DeltaSeconds) override;

	// Possess InPawn, adopt InConfig, and follow InSpline. Called by the
	// UTrajectoryFollowingComponent that spawns this controller; can also be called manually.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	void FollowTrajectory(ASplineActor* InSpline, APawn* InPawn, const FTrajectoryFollowingConfig& InConfig);

	// World-space transform (location + tangent-aligned rotation) at the given time, in seconds,
	// mapping time onto the spline geometry via the config's speed model. Time is clamped to
	// [0, GetDuration()]; end-of-trajectory behavior (clamp/loop/reset) is applied in Tick.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	FTransform GetTransformAtTime(float Time) const;

	// Time to traverse the whole trajectory, in seconds. 0 if there is no spline or speed model.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	float GetDuration() const;

protected:
	// The spline being followed. Set via FollowTrajectory (typically by the component).
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Trajectory")
	ASplineActor* Spline = nullptr;

	// Following settings. Set via FollowTrajectory; editable here for standalone/manual use.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	FTrajectoryFollowingConfig Config;

	// Converts the desired body-frame velocity into native vehicle inputs when steering a wheeled
	// vehicle (bTeleport == false). Unused for non-vehicle pawns.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	FWheeledVehicleVelocityController VehicleVelocityController;

	// Draw a debug arrow from the pawn to its current target point. Only ever drawn in the PIE
	// editor viewport, never in a packaged/standalone game (and compiled out of Shipping/Test).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableDebugDraw = false;

	// Sample the trajectory at a time already normalized to [0, GetDuration()].
	FTransform SampleAtTime(float Time) const;

	// Distance along the spline (cm) at the given time, for the arc-length speed modes.
	double DistanceAtTime(float Time) const;

	// Integrate Config.TimeToSpeed into the cumulative SpeedDistanceCache (used by SpeedVsTime).
	// Rebuilt whenever FollowTrajectory adopts a new config.
	void RebuildSpeedDistanceCache();

private:
	// Time (seconds) elapsed along the trajectory, accumulated from the per-tick sim delta rather
	// than absolute world time, so it holds steady whenever the simulation is paused or stepped.
	// Reset to 0 when FollowTrajectory (re)starts following.
	double ElapsedSeconds = 0.0;

	// Cumulative distance (cm) vs. time, integrated from Config.TimeToSpeed for the SpeedVsTime mode.
	// Runtime state, rebuilt in FollowTrajectory; not serialized.
	FRichCurve SpeedDistanceCache;
};
