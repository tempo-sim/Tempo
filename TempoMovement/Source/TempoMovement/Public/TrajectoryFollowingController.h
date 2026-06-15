// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "WheeledVehicleVelocityController.h"

#include "CoreMinimal.h"
#include "GameFramework/Controller.h"

#include "TrajectoryFollowingController.generated.h"

class APawn;
class ASplineTrajectory;

// What a follower does when it reaches the end of its trajectory.
UENUM(BlueprintType)
enum class ESplineTrajectoryEndBehavior : uint8
{
	// Hold the final pose (clamp time to the trajectory's duration).
	Clamp,
	// Wrap back to the start (time modulo duration); a steering follower drives back to the start.
	Loop,
	// Teleport the pawn (even a non-teleport/steering follower) back to the start and restart,
	// repeating indefinitely.
	Reset,
};

// Per-follower settings governing how a pawn follows a trajectory. Authored on a
// UTrajectoryFollowingComponent and copied onto the controller it spawns, so two pawns can
// share one trajectory yet steer with different gains or end behavior.
USTRUCT(BlueprintType)
struct FTrajectoryFollowingConfig
{
	GENERATED_BODY()

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
	ESplineTrajectoryEndBehavior EndBehavior = ESplineTrajectoryEndBehavior::Clamp;
};

// Drives a possessed pawn along an ASplineTrajectory. Each tick it samples the trajectory
// at the time since this controller spawned and either teleports the pawn to the
// target pose, or steers toward it (letting the pawn and its movement component respond).
// In the non-teleport path the steering law adapts to the pawn:
//   * Wheeled vehicles (Chaos or kinematic) can't strafe, so the target is converted into a
//     body-frame velocity (trajectory pace + along-track catch-up, plus a yaw rate that turns
//     the vehicle toward the target) and tracked via VehicleVelocityController.
//   * Other pawns use AddMovementInput, which drives translation only, so their heading is
//     governed by their movement component, not the trajectory's orientation.
// Only bTeleport reproduces the target rotation exactly.
// Typically spawned by a UTrajectoryFollowingComponent, which supplies the pawn, trajectory,
// and config; can also be spawned and wired manually via FollowTrajectory.
UCLASS(Blueprintable)
class TEMPOMOVEMENT_API ATrajectoryFollowingController : public AController
{
	GENERATED_BODY()

public:
	ATrajectoryFollowingController();

	virtual void Tick(float DeltaSeconds) override;

	// Possess InPawn, adopt InConfig, and follow InTrajectory. Called by the
	// UTrajectoryFollowingComponent that spawns this controller; can also be called manually.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	void FollowTrajectory(ASplineTrajectory* InTrajectory, APawn* InPawn, const FTrajectoryFollowingConfig& InConfig);

protected:
	// The trajectory being followed. Set via FollowTrajectory (typically by the component).
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Trajectory")
	ASplineTrajectory* Trajectory = nullptr;

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
	bool bEnableDebugDraw = true;

private:
	// Time (seconds) elapsed along the trajectory, accumulated from the per-tick sim delta rather
	// than absolute world time, so it holds steady whenever the simulation is paused or stepped.
	// Reset to 0 when FollowTrajectory (re)starts following.
	double ElapsedSeconds = 0.0;
};
