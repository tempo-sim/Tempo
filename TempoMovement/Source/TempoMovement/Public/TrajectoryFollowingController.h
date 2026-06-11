// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Controller.h"

#include "TrajectoryFollowingController.generated.h"

class APawn;
class ASplineTrajectory;

// Per-follower settings governing how a pawn follows a trajectory. Authored on a
// UTrajectoryFollowingComponent and copied onto the controller it spawns, so two pawns can
// share one trajectory yet steer with different gains.
USTRUCT(BlueprintType)
struct FTrajectoryFollowingConfig
{
	GENERATED_BODY()

	// If true, teleport the pawn to the target pose each tick (exact, bypasses control).
	// If false, steer toward the target with AddMovementInput.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	bool bTeleport = true;

	// Proportional gain on position error (1/s) in the AddMovementInput control law.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "!bTeleport", ClampMin = 0.0))
	double PositionGain = 1.0;

	// Scales the computed velocity correction (cm/s) into a normalized movement input.
	// Roughly 1 / (pawn's max speed in cm/s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "!bTeleport", ClampMin = 0.0))
	double InputScale = 0.002;
};

// Drives a possessed pawn along an ASplineTrajectory. Each tick it samples the trajectory
// at the time since this controller spawned and either teleports the pawn to the
// target pose, or applies movement input to steer toward it (letting the pawn and its
// movement component respond). Note: AddMovementInput drives translation only, so in the
// non-teleport path the pawn's heading is governed by its movement component, not the
// trajectory's orientation; only bTeleport reproduces the target rotation exactly.
// Typically spawned by a UTrajectoryFollowingComponent, which supplies the pawn, trajectory,
// and config; can also be spawned and wired manually via FollowTrajectory.
UCLASS(Blueprintable)
class TEMPOMOVEMENT_API ATrajectoryFollowingController : public AController
{
	GENERATED_BODY()

public:
	ATrajectoryFollowingController();

	virtual void BeginPlay() override;
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

private:
	// World time (seconds) at which this controller spawned; trajectory time is relative to it.
	double SpawnTimeSeconds = 0.0;
};
