// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/SubclassOf.h"

#include "TrajectoryFollowingController.h"

#include "TrajectoryFollowingComponent.generated.h"

class ASplineTrajectory;

// Add to a pawn to have it follow an ASplineTrajectory. On BeginPlay it spawns a
// ATrajectoryFollowingController, hands it the trajectory, this pawn, and Config, and lets the
// controller possess and drive the pawn. Because the trajectory is referenced (not owned),
// several pawns can each carry one of these components pointing at the same trajectory while
// following it with their own Config (e.g. staggered StartTimes).
UCLASS(ClassGroup = (Tempo), meta = (BlueprintSpawnableComponent))
class TEMPOMOVEMENT_API UTrajectoryFollowingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTrajectoryFollowingComponent();

	virtual void BeginPlay() override;

protected:
	// The trajectory this pawn should follow. EditInstanceOnly so it can reference a placed
	// trajectory in the level. If unset, no controller is spawned.
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Trajectory")
	ASplineTrajectory* Trajectory = nullptr;

	// How this pawn follows the trajectory (start time, teleport vs. steer, gains).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	FTrajectoryFollowingConfig Config;

	// Controller class spawned to drive the owning pawn. Override to use a custom control law.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	TSubclassOf<ATrajectoryFollowingController> ControllerClass = ATrajectoryFollowingController::StaticClass();
};
