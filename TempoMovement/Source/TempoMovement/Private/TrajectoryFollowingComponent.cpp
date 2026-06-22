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
