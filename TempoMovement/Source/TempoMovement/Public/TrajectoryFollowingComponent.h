// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/SubclassOf.h"

#include "TrajectoryFollowingController.h"

#include "TrajectoryFollowingComponent.generated.h"

class ASplineActor;

// Raised on each trajectory end (see FTrajectoryEndEvent). Declared here rather than on the
// controller because this component is what a subscriber can find on a pawn: it is present from the
// moment it is added, whereas the controller is not spawned until a spline has been configured, and
// one binding here covers every trajectory the pawn is subsequently given.
DECLARE_MULTICAST_DELEGATE_OneParam(FTrajectoryEndSignature, const FTrajectoryEndEvent&);

// Add to a pawn to have it follow an ASplineActor. On BeginPlay (and whenever reconfigured) it spawns
// a ATrajectoryFollowingController, hands it the spline, this pawn, and Config, and lets the
// controller possess and drive the pawn. Because the spline is referenced (not owned), several pawns
// can each carry one of these components pointing at the same spline while traversing it with their
// own Config (e.g. different speed models or end behavior).
UCLASS(ClassGroup = (Tempo), meta = (BlueprintSpawnableComponent))
class TEMPOMOVEMENT_API UTrajectoryFollowingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTrajectoryFollowingComponent();

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Point this pawn at InSpline with InConfig and (re)start following, spawning the controller if
	// needed. Used by the ConfigureTrajectoryFollowing RPC to set up or retarget a follower at runtime.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	void ConfigureAndFollow(ASplineActor* InSpline, const FTrajectoryFollowingConfig& InConfig);

	// Change the ConstantSpeed speed (cm/s) of the trajectory already being followed, without
	// restarting it or moving the pawn; 0 holds the pawn where it is, and a negative value drives it
	// back along the spline (reversing, for a steering follower). Used by the SetTrajectorySpeed
	// RPC to pace a follower against something outside the trajectory (an obstacle, a signal, a
	// hand-off), which ConfigureAndFollow cannot do — it restarts following from the spline's start.
	// Returns false if there is no controller yet or the trajectory is not in ConstantSpeed mode.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	bool SetSpeed(double SpeedCmS);

	// Raised each time this pawn's trajectory reaches its end, whatever end behavior is configured,
	// and before that behavior's own effects — so a Destroy subscriber is notified while the pawn is
	// still alive.
	FTrajectoryEndSignature OnTrajectoryEnd;

	// Raise OnTrajectoryEnd. Called by the controller driving this pawn, which is where the end is
	// detected; not meant for anyone else.
	void NotifyTrajectoryEnd(const FTrajectoryEndEvent& Event);

	const FTrajectoryFollowingConfig& GetConfig() const { return Config; }

protected:
	// Spawn (or reuse) the controller and have it follow the current Spline with the current Config.
	// No-op if no spline is set or the owner is not a pawn.
	void StartFollowing();

	// The spline this pawn should follow. EditInstanceOnly so it can reference a placed ASplineActor
	// in the level. If unset, no controller is spawned.
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Trajectory")
	ASplineActor* Spline = nullptr;

	// How this pawn follows the spline (speed model, teleport vs. steer, gains, end behavior).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	FTrajectoryFollowingConfig Config;

	// Controller class spawned to drive the owning pawn. Override to use a custom control law.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	TSubclassOf<ATrajectoryFollowingController> ControllerClass = ATrajectoryFollowingController::StaticClass();

private:
	// The controller spawned to drive this pawn, reused across reconfigurations.
	UPROPERTY(Transient)
	ATrajectoryFollowingController* Controller = nullptr;
};
