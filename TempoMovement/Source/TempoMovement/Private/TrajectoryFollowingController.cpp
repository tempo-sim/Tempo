// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TrajectoryFollowingController.h"

#include "SplineTrajectory.h"

#include "GameFramework/Pawn.h"

ATrajectoryFollowingController::ATrajectoryFollowingController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATrajectoryFollowingController::BeginPlay()
{
	Super::BeginPlay();

	SpawnTimeSeconds = GetWorld()->GetTimeSeconds();
}

void ATrajectoryFollowingController::FollowTrajectory(ASplineTrajectory* InTrajectory, APawn* InPawn, const FTrajectoryFollowingConfig& InConfig)
{
	Trajectory = InTrajectory;
	Config = InConfig;
	if (InPawn)
	{
		Possess(InPawn);
	}
}

void ATrajectoryFollowingController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !Trajectory)
	{
		return;
	}

	const double Time = GetWorld()->GetTimeSeconds() - SpawnTimeSeconds;

	const FTransform Target = Trajectory->GetTransformAtTime(Time);

	if (Config.bTeleport)
	{
		ControlledPawn->SetActorTransform(Target, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}

	// PD + velocity-feedforward control law, mapped onto AddMovementInput.
	const FVector CurrentLocation = ControlledPawn->GetActorLocation();
	const FVector CurrentVelocity = ControlledPawn->GetVelocity();

	// Feedforward: the trajectory's own velocity, from a forward finite difference.
	const float Lookahead = FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER);
	const FVector AheadLocation = Trajectory->GetTransformAtTime(Time + Lookahead).GetLocation();
	const FVector FeedforwardVelocity = (AheadLocation - Target.GetLocation()) / Lookahead;

	// Proportional position correction plus feedforward, minus the pawn's current velocity.
	const FVector PositionError = Target.GetLocation() - CurrentLocation;
	const FVector DesiredVelocity = FeedforwardVelocity + Config.PositionGain * PositionError;
	const FVector VelocityError = DesiredVelocity - CurrentVelocity;

	const FVector WorldInput = VelocityError * Config.InputScale;
	ControlledPawn->AddMovementInput(WorldInput.GetSafeNormal(), FMath::Min(WorldInput.Size(), 1.0));
}
