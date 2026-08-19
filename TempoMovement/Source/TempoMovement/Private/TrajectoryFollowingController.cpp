// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TrajectoryFollowingController.h"

#include "KinematicVehicleMovementComponent.h"
#include "SplineActor.h"

#include "ChaosVehicleMovementComponent.h"
#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"

namespace
{
	// Largest time key on a curve (its effective duration). 0 if the curve is empty.
	float CurveMaxTime(const FRuntimeFloatCurve& Curve)
	{
		const FRichCurve* RichCurve = Curve.GetRichCurveConst();
		if (!RichCurve || RichCurve->GetNumKeys() == 0)
		{
			return 0.0f;
		}
		float MinTime = 0.0f;
		float MaxTime = 0.0f;
		RichCurve->GetTimeRange(MinTime, MaxTime);
		return MaxTime;
	}
}

ATrajectoryFollowingController::ATrajectoryFollowingController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATrajectoryFollowingController::FollowTrajectory(ASplineActor* InSpline, APawn* InPawn, const FTrajectoryFollowingConfig& InConfig)
{
	Spline = InSpline;
	Config = InConfig;
	ElapsedSeconds = 0.0;
	DistanceAlongSpline = 0.0;
	bReachedTrajectoryEnd = false;
	VehicleVelocityController.Reset();
	if (InPawn)
	{
		Possess(InPawn);
	}
}

bool ATrajectoryFollowingController::SetSpeed(double SpeedCmS)
{
	if (Config.SpeedMode != ETrajectorySpeedMode::ConstantSpeed)
	{
		return false;
	}
	// Nothing to re-anchor: the target pose is sampled at DistanceAlongSpline, which this does not
	// touch, so the new speed only changes how fast that distance grows from here on.
	Config.Speed = FMath::Max(SpeedCmS, 0.0);
	return true;
}

bool ATrajectoryFollowingController::IsArcLengthMode() const
{
	return Config.SpeedMode == ETrajectorySpeedMode::ConstantSpeed || Config.SpeedMode == ETrajectorySpeedMode::SpeedVsTime;
}

double ATrajectoryFollowingController::CurrentSpeed() const
{
	switch (Config.SpeedMode)
	{
	case ETrajectorySpeedMode::ConstantSpeed:
		return FMath::Max(Config.Speed, 0.0);
	case ETrajectorySpeedMode::SpeedVsTime:
	{
		// Signed on purpose: a negative key is how a speed curve drives back along the spline.
		const FRichCurve* Curve = Config.TimeToSpeed.GetRichCurveConst();
		return (Curve && Curve->GetNumKeys() > 0) ? Curve->Eval(ElapsedSeconds) : 0.0;
	}
	default:
		return 0.0;
	}
}

double ATrajectoryFollowingController::GetDistanceAlongSpline() const
{
	return IsArcLengthMode() ? DistanceAlongSpline : DistanceAtTime(ElapsedSeconds);
}

float ATrajectoryFollowingController::ClockDuration() const
{
	switch (Config.SpeedMode)
	{
	case ETrajectorySpeedMode::SplinePointVsTime:
		return CurveMaxTime(Config.TimeToInputKey);
	case ETrajectorySpeedMode::DistanceVsTime:
		return CurveMaxTime(Config.TimeToDistance);
	case ETrajectorySpeedMode::SpeedVsTime:
		return CurveMaxTime(Config.TimeToSpeed);
	case ETrajectorySpeedMode::ConstantSpeed:
	default:
		// No authored clock: a constant-speed trajectory ends where the spline does, however long
		// that takes at whatever speeds it was driven at.
		return 0.0f;
	}
}

float ATrajectoryFollowingController::GetDuration() const
{
	if (Config.SpeedMode != ETrajectorySpeedMode::ConstantSpeed)
	{
		return ClockDuration();
	}
	const USplineComponent* SplineComponent = Spline ? Spline->GetSpline() : nullptr;
	return (!SplineComponent || Config.Speed <= 0.0) ? 0.0f : SplineComponent->GetSplineLength() / Config.Speed;
}

void ATrajectoryFollowingController::RestartTrajectory(double SplineLength, float ClockEnd)
{
	// A constant-speed loop can carry its overshoot exactly, since distance is its only state, which
	// keeps the follower's pace unbroken across the wrap. Anything driven by a curve restarts the
	// curve instead: its speed (or geometry) is authored from t = 0, so resuming it part-way through
	// would not be a loop.
	if (Config.SpeedMode == ETrajectorySpeedMode::ConstantSpeed)
	{
		DistanceAlongSpline = SplineLength > 0.0 ? FMath::Fmod(DistanceAlongSpline, SplineLength) : 0.0;
		return;
	}
	DistanceAlongSpline = 0.0;
	ElapsedSeconds = ClockEnd > 0.0f ? FMath::Fmod(ElapsedSeconds, static_cast<double>(ClockEnd)) : 0.0;
}

FTransform ATrajectoryFollowingController::GetTransformAtTime(float Time) const
{
	const float Duration = GetDuration();
	const float ClampedTime = FMath::Clamp(Time, 0.0f, FMath::Max(Duration, 0.0f));
	return SampleAtTime(ClampedTime);
}

FTransform ATrajectoryFollowingController::GetTransformAtDistance(double Distance) const
{
	const USplineComponent* SplineComponent = Spline ? Spline->GetSpline() : nullptr;
	if (!SplineComponent)
	{
		return FTransform::Identity;
	}
	const double ClampedDistance = FMath::Clamp(Distance, 0.0, SplineComponent->GetSplineLength());
	return SplineComponent->GetTransformAtDistanceAlongSpline(ClampedDistance, ESplineCoordinateSpace::World);
}

FTransform ATrajectoryFollowingController::SampleAtTime(float Time) const
{
	const USplineComponent* SplineComponent = Spline ? Spline->GetSpline() : nullptr;
	if (!SplineComponent)
	{
		return FTransform::Identity;
	}

	if (Config.SpeedMode == ETrajectorySpeedMode::SplinePointVsTime)
	{
		const FRichCurve* Curve = Config.TimeToInputKey.GetRichCurveConst();
		const float InputKey = Curve ? Curve->Eval(Time) : 0.0f;
		return SplineComponent->GetTransformAtSplineInputKey(InputKey, ESplineCoordinateSpace::World);
	}

	return GetTransformAtDistance(DistanceAtTime(Time));
}

double ATrajectoryFollowingController::DistanceAtTime(float Time) const
{
	switch (Config.SpeedMode)
	{
	case ETrajectorySpeedMode::DistanceVsTime:
	{
		const FRichCurve* Curve = Config.TimeToDistance.GetRichCurveConst();
		return Curve ? Curve->Eval(Time) : 0.0;
	}
	case ETrajectorySpeedMode::SpeedVsTime:
		// Only an estimate: the follower integrates this curve tick by tick (see Tick), so the true
		// distance depends on the deltas it was integrated over, not on Time alone. Reported as if
		// the current speed had held throughout, for the benefit of GetTransformAtTime's callers.
		return CurrentSpeed() * Time;
	case ETrajectorySpeedMode::ConstantSpeed:
	default:
		return Config.Speed * Time;
	}
}

void ATrajectoryFollowingController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !Spline)
	{
		return;
	}

	const USplineComponent* SplineComponent = Spline->GetSpline();
	if (!SplineComponent)
	{
		return;
	}

	// Advance the trajectory. The clock runs for every mode (the curves are functions of it); the
	// arc-length modes additionally integrate the distance their speed carries the pawn over this
	// tick, and it is that distance — not the clock — their target is sampled at. Integrating rather
	// than recomputing distance from the clock is what lets the speed change mid-trajectory without
	// the target jumping, and what makes speed 0 an ordinary value (the distance simply stops
	// growing) rather than a division by zero.
	//
	// Nothing advances once the trajectory has ended and been clamped: both parameters stay at the
	// end, so every subsequent tick re-targets the final pose. The clock in particular must stop,
	// since a SpeedVsTime curve evaluated past its last key returns that key's speed and would
	// otherwise keep driving the pawn down the spline forever.
	const bool bArcLength = IsArcLengthMode();
	const double SpeedThisTick = bReachedTrajectoryEnd ? 0.0 : CurrentSpeed();
	const double SplineLength = SplineComponent->GetSplineLength();
	bool bResetThisTick = false;
	if (!bReachedTrajectoryEnd)
	{
		ElapsedSeconds += DeltaSeconds;
		if (bArcLength)
		{
			DistanceAlongSpline = FMath::Max(DistanceAlongSpline + SpeedThisTick * DeltaSeconds, 0.0);
		}

		// Apply end-of-trajectory behavior once the trajectory runs out, in whichever domain governs
		// this mode: the arc-length modes end at the end of the spline, the time-domain modes when
		// their curve runs out. Only one of the two can fire for most modes (ClockDuration is 0 for
		// ConstantSpeed, and the time-domain modes never advance a distance), but SpeedVsTime ends on
		// either, since its speed curve has an authored duration of its own and need not cover the
		// whole spline.
		const float ClockEnd = ClockDuration();
		const bool bReachedSplineEnd = bArcLength && SplineLength > 0.0 && DistanceAlongSpline >= SplineLength;
		const bool bReachedClockEnd = ClockEnd > 0.0f && ElapsedSeconds >= ClockEnd;
		if (bReachedSplineEnd || bReachedClockEnd)
		{
			switch (Config.EndBehavior)
			{
			case ETrajectoryEndBehavior::Clamp:
				// Give back the part of this tick that ran past the end, so the pawn holds the pose
				// the trajectory actually ends at rather than one partial tick beyond it, then stop.
				if (bReachedClockEnd)
				{
					DistanceAlongSpline -= SpeedThisTick * (ElapsedSeconds - ClockEnd);
					ElapsedSeconds = ClockEnd;
				}
				DistanceAlongSpline = FMath::Clamp(DistanceAlongSpline, 0.0, SplineLength);
				bReachedTrajectoryEnd = true;
				break;
			case ETrajectoryEndBehavior::Loop:
			case ETrajectoryEndBehavior::Reset:
				RestartTrajectory(SplineLength, ClockEnd);
				bResetThisTick = Config.EndBehavior == ETrajectoryEndBehavior::Reset;
				break;
			case ETrajectoryEndBehavior::Destroy:
				// Destroy the followed pawn once it reaches the end. Destroying the pawn tears down
				// its UTrajectoryFollowingComponent, whose EndPlay unpossesses and destroys this
				// controller, so return immediately without touching any more state.
				ControlledPawn->Destroy();
				return;
			}
		}
	}

	const FTransform Target = bArcLength ? GetTransformAtDistance(DistanceAlongSpline) : GetTransformAtTime(ElapsedSeconds);
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

	// Feedforward: the trajectory's own velocity, from a forward finite difference. For the
	// arc-length modes that difference is taken along the spline at the speed actually being
	// integrated, so a held follower feeds forward exactly zero.
	const float Lookahead = FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER);
	const FVector AheadLocation = bArcLength
		? GetTransformAtDistance(DistanceAlongSpline + SpeedThisTick * Lookahead).GetLocation()
		: GetTransformAtTime(ElapsedSeconds + Lookahead).GetLocation();
	const FVector FeedforwardVelocity = (AheadLocation - Target.GetLocation()) / Lookahead;

	// Deadband: errors smaller than the band produce no corrective input, so the pawn coasts on the
	// feedforward instead of chasing tiny tracking errors. Zeros the error when within the band.
	const auto ApplyDeadband = [](double Error, double Deadband)
	{
		return FMath::Abs(Error) <= Deadband ? 0.0 : Error;
	};

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
		// Floored at zero: a follower that has overshot its target — which is the steady state of a
		// held (0 speed) trajectory, and of one clamped at its end — should brake and stay put, not
		// reverse back down the spline. ApplyChaosAccelInput already brakes rather than reverses
		// while the vehicle is rolling forward, but a standing vehicle under a sustained negative
		// command would shift into reverse.
		const double AlongTrackError = ApplyDeadband(ToTargetLocal.X, Config.PositionDeadband);
		const double TargetLinVelCmS = FMath::Max(FeedforwardVelocity.Size() + Config.PositionGain * AlongTrackError, 0.0);
		// Steer toward the target point: heading error in the pawn's frame.
		const double HeadingErrorDeg = ApplyDeadband(FMath::RadiansToDegrees(FMath::Atan2(ToTargetLocal.Y, ToTargetLocal.X)), Config.HeadingDeadband);
		const double TargetYawRateDegS = Config.YawRateGain * HeadingErrorDeg;

		VehicleVelocityController.TrackBodyVelocity(ControlledPawn, TargetLinVelCmS, TargetYawRateDegS, DeltaSeconds);
		return;
	}

	// Other pawns: PD + velocity-feedforward control law, mapped onto AddMovementInput.
	const FVector CurrentVelocity = ControlledPawn->GetVelocity();
	FVector PositionError = Target.GetLocation() - CurrentLocation;
	if (PositionError.Size() <= Config.PositionDeadband)
	{
		PositionError = FVector::ZeroVector;
	}
	const FVector DesiredVelocity = FeedforwardVelocity + Config.PositionGain * PositionError;
	const FVector VelocityError = DesiredVelocity - CurrentVelocity;

	const FVector WorldInput = VelocityError * Config.InputScale;
	ControlledPawn->AddMovementInput(WorldInput.GetSafeNormal(), FMath::Min(WorldInput.Size(), 1.0));
}
