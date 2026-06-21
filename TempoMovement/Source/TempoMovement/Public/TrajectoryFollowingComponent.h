// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/SubclassOf.h"

#include "TrajectoryFollowingController.h"

#include "TrajectoryFollowingComponent.generated.h"

class ASplineActor;

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

	// Point this pawn at InSpline with InConfig and (re)start following, spawning the controller if
	// needed. Used by the ConfigureTrajectoryFollowing RPC to set up or retarget a follower at runtime.
	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	void ConfigureAndFollow(ASplineActor* InSpline, const FTrajectoryFollowingConfig& InConfig);

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
