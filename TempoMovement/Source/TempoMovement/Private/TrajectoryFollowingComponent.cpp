// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TrajectoryFollowingComponent.h"

#include "SplineActor.h"
#include "TrajectoryFollowingController.h"

#include "TempoMovement.h"

#include "GameFramework/Pawn.h"

UTrajectoryFollowingComponent::UTrajectoryFollowingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTrajectoryFollowingComponent::BeginPlay()
{
	Super::BeginPlay();

	StartFollowing();
}

void UTrajectoryFollowingComponent::ConfigureAndFollow(ASplineActor* InSpline, const FTrajectoryFollowingConfig& InConfig)
{
	Spline = InSpline;
	Config = InConfig;
	StartFollowing();
}

bool UTrajectoryFollowingComponent::SetSpeed(double SpeedCmS)
{
	if (!Controller)
	{
		return false;
	}
	// Keep the component's own Config in step with the controller's, so a later StartFollowing
	// (a reconfigure that leaves the speed model alone) does not resurrect the superseded speed.
	if (!Controller->SetSpeed(SpeedCmS))
	{
		return false;
	}
	Config.Speed = FMath::Max(SpeedCmS, 0.0);
	return true;
}

void UTrajectoryFollowingComponent::StartFollowing()
{
	if (!Spline)
	{
		return;
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		UE_LOG(LogTempoMovement, Warning, TEXT("UTrajectoryFollowingComponent on %s: owner is not a pawn; cannot follow spline."), *GetNameSafe(GetOwner()));
		return;
	}

	if (!ControllerClass)
	{
		ControllerClass = ATrajectoryFollowingController::StaticClass();
	}

	if (!Controller)
	{
		// Don't set the pawn as the controller's owner: once the controller possesses the pawn,
		// PossessedBy() makes the pawn owned by the controller, which would form an owner loop.
		Controller = GetWorld()->SpawnActor<ATrajectoryFollowingController>(ControllerClass);
	}

	if (Controller)
	{
		Controller->FollowTrajectory(Spline, OwnerPawn, Config);
	}
}

void UTrajectoryFollowingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Controller)
	{
		Controller->UnPossess();
		Controller->Destroy();
		Controller = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}
