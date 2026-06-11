// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TrajectoryFollowingComponent.h"

#include "SplineTrajectory.h"
#include "TrajectoryFollowingController.h"

#include "GameFramework/Pawn.h"

UTrajectoryFollowingComponent::UTrajectoryFollowingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTrajectoryFollowingComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!Trajectory)
	{
		return;
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTrajectoryFollowingComponent on %s: owner is not a pawn; cannot follow trajectory."), *GetNameSafe(GetOwner()));
		return;
	}

	if (!ControllerClass)
	{
		ControllerClass = ATrajectoryFollowingController::StaticClass();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = OwnerPawn;
	if (ATrajectoryFollowingController* Controller = GetWorld()->SpawnActor<ATrajectoryFollowingController>(ControllerClass, SpawnParameters))
	{
		Controller->FollowTrajectory(Trajectory, OwnerPawn, Config);
	}
}
