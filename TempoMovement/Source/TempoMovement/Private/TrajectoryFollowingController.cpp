// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TrajectoryFollowingController.h"

#include "KinematicVehicleMovementComponent.h"
#include "SplineActor.h"
#include "TrajectoryFollowingComponent.h"

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

	// Feedforward speeds (cm/s) below this are treated as carrying no direction of travel: a held or
	// clamped trajectory feeds forward zero, and its sign is then only floating-point noise. Matches
	// the standstill threshold the vehicle velocity controller uses.
	constexpr double DirectionOfTravelThresholdCmS = 1.0;
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
	bReversingLeg = false;
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
	// touch, so the new speed only changes how fast — and, if it is negative, which way — that
	// distance moves from here on.
	Config.Speed = SpeedCmS;
	return true;
}

void ATrajectoryFollowingController::NotifyTrajectoryEnd()
{
	APawn* EndedPawn = GetPawn();
	FTrajectoryEndEvent Event;
	Event.Pawn = EndedPawn;
	Event.EndBehavior = Config.EndBehavior;

	// Located the same way the movement service locates it, so both agree which component speaks for
	// a pawn.
	if (UTrajectoryFollowingComponent* Component =
			EndedPawn->FindComponentByClass<UTrajectoryFollowingComponent>())
	{
		Component->NotifyTrajectoryEnd(Event);
	}
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
		return Config.Speed;
	case ETrajectorySpeedMode::SpeedVsTime:
	{
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

double ATrajectoryFollowingController::ComputeBodyForwardSpeed(
	double FeedforwardBodyCmS, double AlongTrackErrorCm, double PositionGain, bool bReversing)
{
	const double Corrected = FeedforwardBodyCmS + PositionGain * AlongTrackErrorCm;
	return bReversing ? FMath::Min(Corrected, 0.0) : FMath::Max(Corrected, 0.0);
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
			// Floored at the spline's start: a follower driven backwards stops there and holds. That
			// is deliberately not an end — only reaching the far end is, so a Destroy follower backed
			// onto its start is not destroyed by it.
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
				// controller, so notify before any of that goes away, and return immediately without
				// touching any more state.
				NotifyTrajectoryEnd();
				ControlledPawn->Destroy();
				return;
			}

			NotifyTrajectoryEnd();
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
		const FTransform PawnTransform = ControlledPawn->GetActorTransform();
		const FVector ToTargetLocal = PawnTransform.InverseTransformVectorNoScale(Target.GetLocation() - CurrentLocation);
		// The trajectory's own velocity as a signed speed along the pawn's *nose*, which is the frame
		// the along-track error below is measured in and the frame the vehicle is commanded in.
		// Projecting it onto the path instead mixes frames: where the path runs against the pawn's
		// heading the two disagree about which way is forward, and the feedforward then subtracts from
		// the very correction that is trying to follow it (settling |error| = feedforward / gain short).
		const double FeedforwardBodyCmS = PawnTransform.InverseTransformVectorNoScale(FeedforwardVelocity).X;
		// Which way the pawn has to travel to follow this leg: backwards whenever the trajectory
		// carries its target towards the pawn's tail — because the commanded speed is negative, or
		// because the path itself runs against the pawn's heading. A reversing follower keeps its
		// heading and backs up rather than turning around.
		//
		// Latched, and only updated while the trajectory is actually moving: the feedforward vanishes
		// wherever a trajectory is held (0 speed) or clamped at its end, and a vanished feedforward
		// carries no direction. The direction of travel is a property of the leg, not of the instant,
		// and reading it off a zero would silently reset it to "forwards" exactly where the pawn is
		// still creeping the last of a reverse leg in.
		if (FMath::Abs(FeedforwardBodyCmS) > DirectionOfTravelThresholdCmS)
		{
			bReversingLeg = FeedforwardBodyCmS < 0.0;
		}

		// Trajectory pace plus an along-track correction to catch up to / hang back from the target.
		// The correction may brake the pawn to a stop but not reverse its direction of travel, so a
		// follower that has *overshot* its target holds where it is rather than backing up to fix it —
		// which is also what keeps a follower held at 0 speed, or clamped at its end, from creeping.
		const double AlongTrackError = ApplyDeadband(ToTargetLocal.X, Config.PositionDeadband);
		const double TargetLinVelCmS = ComputeBodyForwardSpeed(
			FeedforwardBodyCmS, AlongTrackError, Config.PositionGain, bReversingLeg);

		// Steer toward the target point: heading error in the pawn's frame, measured against whichever
		// end of the pawn leads. Backing up, the target is *behind*, so measuring it off the nose
		// would read as a ~180 degree error and command a hard turn instead of a steering correction.
		const FVector ToTargetAhead = bReversingLeg ? -ToTargetLocal : ToTargetLocal;
		const double HeadingErrorDeg = ApplyDeadband(FMath::RadiansToDegrees(FMath::Atan2(ToTargetAhead.Y, ToTargetAhead.X)), Config.HeadingDeadband);
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
