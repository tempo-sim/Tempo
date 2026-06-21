// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TrajectoryFollowingComponent.h"

#include "SplineActor.h"
#include "TrajectoryFollowingController.h"

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
		UE_LOG(LogTemp, Warning, TEXT("UTrajectoryFollowingComponent on %s: owner is not a pawn; cannot follow spline."), *GetNameSafe(GetOwner()));
		return;
	}

	if (!ControllerClass)
	{
		ControllerClass = ATrajectoryFollowingController::StaticClass();
	}

	if (!Controller)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = OwnerPawn;
		Controller = GetWorld()->SpawnActor<ATrajectoryFollowingController>(ControllerClass, SpawnParameters);
	}

	if (Controller)
	{
		Controller->FollowTrajectory(Spline, OwnerPawn, Config);
	}
}
