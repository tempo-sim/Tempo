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
	// Traverse the spline at a fixed speed, integrated per tick: distance += Speed * dt. Speed may be
	// changed while following (see SetSpeed) and takes effect on the next tick, without moving the
	// pawn or restarting the trajectory; 0 holds it in place.
	ConstantSpeed,
	// Map time to a spline input key (point index) via a curve, letting the path dwell,
	// accelerate, decelerate, or reverse independently of arc length.
	SplinePointVsTime,
	// Map time directly to distance along the spline (cm) via a curve.
	DistanceVsTime,
	// Map time to speed along the spline (cm/s) via a curve, integrated per tick:
	// distance += TimeToSpeed(time) * dt. A negative value drives back along the spline.
	SpeedVsTime,
};

// What a follower does when it reaches the end of its trajectory.
UENUM(BlueprintType)
enum class ETrajectoryEndBehavior : uint8
{
	// Hold the final pose (freeze progress at the end of the trajectory).
	Clamp,
	// Wrap back to the start of the spline; a steering follower drives back to the start.
	Loop,
	// Teleport the pawn (even a non-teleport/steering follower) back to the start and restart,
	// repeating indefinitely.
	Reset,
	// Destroy the followed pawn (and, via its component, this controller) when the end is reached.
	Destroy,
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

	// ConstantSpeed mode: speed along the spline, in cm/s. Negative drives back along the spline
	// (a steering follower reverses; it keeps its heading and backs up rather than turning around).
	// Settable while following via ATrajectoryFollowingController::SetSpeed (or the
	// SetTrajectorySpeed RPC).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "SpeedMode == ETrajectorySpeedMode::ConstantSpeed", EditConditionHides))
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

	// Position deadband (cm): position/along-track errors within this distance produce no corrective
	// input, so the pawn coasts on the trajectory feedforward instead of chasing tiny errors. 0 disables.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "!bTeleport", ClampMin = 0.0))
	double PositionDeadband = 10.0;

	// Heading deadband (deg) for wheeled vehicles: heading errors to the target point within this angle
	// produce no yaw-rate command, suppressing steering jitter near the path. 0 disables.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "!bTeleport", ClampMin = 0.0))
	double HeadingDeadband = 1.0;

	// What to do when the trajectory's duration is reached.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	ETrajectoryEndBehavior EndBehavior = ETrajectoryEndBehavior::Clamp;
};

// Drives a possessed pawn along an ASplineActor. Each tick it advances its progress along the
// trajectory, samples the spline there, and either teleports the pawn to the target pose or steers
// toward it (letting the pawn and its movement component respond).
//
// How progress is tracked depends on the speed mode, because the two families are parameterized
// differently:
//   * The arc-length modes (ConstantSpeed, SpeedVsTime) integrate distance along the spline directly
//     (distance += speed * dt). Speed is therefore a live input: changing it takes effect on the next
//     tick without moving the pawn, 0 holds it where it is, and a negative value drives back along
//     the spline (a steering follower reverses, keeping its heading, rather than turning around).
//     These modes end when the pawn reaches the end of the spline; backing up to the spline's start
//     stops there but does not end the trajectory.
//   * The time-domain modes (SplinePointVsTime, DistanceVsTime) are authored as functions of
//     trajectory time, so they advance a clock (ElapsedSeconds) and end when their curve runs out.
// SpeedVsTime needs both: the clock to evaluate its speed curve, and the integrated distance to know
// where on the spline that puts the pawn.
//
// In the non-teleport path the steering law adapts to the pawn:
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

	// Set the ConstantSpeed speed (cm/s) while following, without moving the pawn or restarting the
	// trajectory: the new speed takes effect on the next tick's integration step. 0 holds the pawn
	// where it is on the spline, indefinitely and without ending the trajectory; following resumes
	// from the same point when a speed is set again. Negative drives back along the spline, and stops
	// at its start (which is a hold, not the end of the trajectory — only the far end is that).
	// Only meaningful in ConstantSpeed mode; returns false (and changes nothing) in any other.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	bool SetSpeed(double SpeedCmS);

	// World-space transform (location + tangent-aligned rotation) at the given trajectory time, in
	// seconds. Only meaningful for the time-domain modes (SplinePointVsTime, DistanceVsTime), whose
	// geometry is a function of time; for the arc-length modes (ConstantSpeed, SpeedVsTime) the pose
	// depends on the integrated distance and the speeds it was integrated at, not on time alone, so
	// this reports the pose the trajectory *would* reach at that time had it run at the current speed
	// throughout. Prefer GetTransformAtDistance for those. Time is clamped to [0, GetDuration()];
	// end-of-trajectory behavior (clamp/loop/reset) is applied in Tick.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	FTransform GetTransformAtTime(float Time) const;

	// World-space transform at a distance (cm) along the spline, clamped to the spline's length.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	FTransform GetTransformAtDistance(double Distance) const;

	// How far along the spline the pawn's target currently is, in cm. Only advances for the
	// arc-length modes; for the time-domain modes it reports the distance implied by the clock.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	double GetDistanceAlongSpline() const;

	// Time to traverse the whole trajectory, in seconds. 0 if there is no spline or speed model.
	// For ConstantSpeed this is an estimate at the *current* speed, not a commitment: it changes
	// whenever the speed does, and no longer governs when the trajectory ends (reaching the end of
	// the spline does). It is 0 while held at 0 speed, where the true duration is unbounded.
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

	// Distance along the spline (cm) the time-domain modes' clock implies at the given time.
	double DistanceAtTime(float Time) const;

	// Whether this mode tracks progress as a distance along the spline (ConstantSpeed, SpeedVsTime)
	// rather than as a position on a time-parameterized curve.
	bool IsArcLengthMode() const;

	// The time (seconds) at which this mode's authored curve runs out, and with it the trajectory.
	// 0 for ConstantSpeed, which has no clock-driven end — it ends where the spline does.
	float ClockDuration() const;

	// Wrap progress back to the start for the Loop and Reset end behaviors.
	void RestartTrajectory(double SplineLength, float ClockEnd);

	// Speed along the spline (cm/s) the arc-length modes advance at right now: Config.Speed, or the
	// speed curve evaluated at the current clock. 0 for the time-domain modes, which have no
	// independent speed. Signed: negative drives back along the spline.
	double CurrentSpeed() const;

private:
	// Time (seconds) elapsed along the trajectory, accumulated from the per-tick sim delta rather
	// than absolute world time, so it holds steady whenever the simulation is paused or stepped.
	// The trajectory parameter for the time-domain modes; for SpeedVsTime it is only the argument
	// its speed curve is evaluated at. Unused by ConstantSpeed. Reset to 0 when FollowTrajectory
	// (re)starts following.
	double ElapsedSeconds = 0.0;

	// Set once the trajectory has run out under the Clamp end behavior, after which progress stops
	// advancing entirely and the pawn holds the final pose. Cleared when FollowTrajectory (re)starts
	// following, which is the only way to drive a clamped trajectory again.
	bool bReachedTrajectoryEnd = false;

	// Distance (cm) travelled along the spline, integrated per tick from CurrentSpeed() for the
	// arc-length modes. This — not the clock — is what those modes' target pose is sampled at, which
	// is why their speed can be changed mid-trajectory without the pose jumping. Reset to 0 when
	// FollowTrajectory (re)starts following.
	double DistanceAlongSpline = 0.0;
};
