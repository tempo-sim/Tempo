// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TrajectoryFollowingController.h"

#include "KinematicVehicleMovementComponent.h"
#include "SplineTrajectory.h"

#include "ChaosVehicleMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"

ATrajectoryFollowingController::ATrajectoryFollowingController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATrajectoryFollowingController::FollowTrajectory(ASplineTrajectory* InTrajectory, APawn* InPawn, const FTrajectoryFollowingConfig& InConfig)
{
	Trajectory = InTrajectory;
	Config = InConfig;
	ElapsedSeconds = 0.0;
	VehicleVelocityController.Reset();
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

	// Advance along the trajectory by the simulation frame delta (not absolute world time) so the
	// target holds still whenever the sim is paused or between fixed steps. Unreal zeroes the tick
	// delta while paused, but checking explicitly also lets us hold without actuating below.
	const bool bPaused = GetWorld()->IsPaused();
	if (!bPaused)
	{
		ElapsedSeconds += DeltaSeconds;
	}

	// Apply end-of-trajectory behavior once the duration is reached.
	bool bResetThisTick = false;
	const float Duration = Trajectory->GetDuration();
	if (Duration > 0.0f && ElapsedSeconds >= Duration)
	{
		switch (Config.EndBehavior)
		{
		case ESplineTrajectoryEndBehavior::Clamp:
			ElapsedSeconds = Duration;
			break;
		case ESplineTrajectoryEndBehavior::Loop:
			ElapsedSeconds = FMath::Fmod(ElapsedSeconds, Duration);
			break;
		case ESplineTrajectoryEndBehavior::Reset:
			ElapsedSeconds = FMath::Fmod(ElapsedSeconds, Duration);
			bResetThisTick = true;
			break;
		}
	}

	const FTransform Target = Trajectory->GetTransformAtTime(ElapsedSeconds);
	const FVector CurrentLocation = ControlledPawn->GetActorLocation();

#if ENABLE_DRAW_DEBUG
	// PIE-only: visible while spectating a play-in-editor session, never in a packaged/standalone
	// game (where it could otherwise leak into sensor captures).
	if (bEnableDebugDraw && GetWorld()->WorldType == EWorldType::PIE)
	{
		DrawDebugDirectionalArrow(GetWorld(), CurrentLocation, Target.GetLocation(),
			/*ArrowSize*/ 50.0f, FColor::Green, /*bPersistent*/ false, /*LifeTime*/ -1.0f,
			/*DepthPriority*/ 0, /*Thickness*/ 3.0f);
	}
#endif

	// While paused we hold at the (frozen) target: don't teleport or actuate, just keep the arrow up.
	if (bPaused)
	{
		return;
	}

	// Reset end behavior: hard-teleport back to the start each cycle, even for a steering follower,
	// and clear any vehicle control windup. Skips actuation this tick; following resumes next tick.
	if (bResetThisTick)
	{
		ControlledPawn->SetActorTransform(Target, false, nullptr, ETeleportType::TeleportPhysics);
		VehicleVelocityController.Reset();
		return;
	}

	if (Config.bTeleport)
	{
		ControlledPawn->SetActorTransform(Target, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}

	// Feedforward: the trajectory's own velocity, from a forward finite difference.
	const float Lookahead = FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER);
	const FVector AheadLocation = Trajectory->GetTransformAtTime(ElapsedSeconds + Lookahead).GetLocation();
	const FVector FeedforwardVelocity = (AheadLocation - Target.GetLocation()) / Lookahead;

	// Wheeled vehicles can't strafe: convert the target into a body-frame velocity (forward speed +
	// yaw rate that turns the vehicle toward the target) and let the vehicle velocity controller
	// actuate throttle/steer/brake. AddMovementInput is a no-op on a Chaos vehicle.
	const UPawnMovementComponent* MovementComponent = ControlledPawn->GetMovementComponent();
	const bool bIsWheeledVehicle = MovementComponent &&
		(MovementComponent->IsA<UChaosVehicleMovementComponent>() || MovementComponent->IsA<UKinematicVehicleMovementComponent>());

	if (bIsWheeledVehicle)
	{
		const FVector ToTargetLocal = ControlledPawn->GetActorTransform().InverseTransformVectorNoScale(Target.GetLocation() - CurrentLocation);

		// Trajectory pace plus an along-track correction to catch up to / hang back from the target.
		const double TargetLinVelCmS = FeedforwardVelocity.Size() + Config.PositionGain * ToTargetLocal.X;
		// Steer toward the target point: heading error in the pawn's frame.
		const double HeadingErrorDeg = FMath::RadiansToDegrees(FMath::Atan2(ToTargetLocal.Y, ToTargetLocal.X));
		const double TargetYawRateDegS = Config.YawRateGain * HeadingErrorDeg;

		VehicleVelocityController.TrackBodyVelocity(ControlledPawn, TargetLinVelCmS, TargetYawRateDegS, DeltaSeconds);
		return;
	}

	// Other pawns: PD + velocity-feedforward control law, mapped onto AddMovementInput.
	const FVector CurrentVelocity = ControlledPawn->GetVelocity();
	const FVector PositionError = Target.GetLocation() - CurrentLocation;
	const FVector DesiredVelocity = FeedforwardVelocity + Config.PositionGain * PositionError;
	const FVector VelocityError = DesiredVelocity - CurrentVelocity;

	const FVector WorldInput = VelocityError * Config.InputScale;
	ControlledPawn->AddMovementInput(WorldInput.GetSafeNormal(), FMath::Min(WorldInput.Size(), 1.0));
}
