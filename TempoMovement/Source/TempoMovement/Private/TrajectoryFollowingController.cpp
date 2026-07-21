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
	RebuildSpeedDistanceCache();
	VehicleVelocityController.Reset();
	if (InPawn)
	{
		Possess(InPawn);
	}
}

float ATrajectoryFollowingController::GetDuration() const
{
	const USplineComponent* SplineComponent = Spline ? Spline->GetSpline() : nullptr;
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
		return (!SplineComponent || Config.Speed <= 0.0) ? 0.0f : SplineComponent->GetSplineLength() / Config.Speed;
	}
}

FTransform ATrajectoryFollowingController::GetTransformAtTime(float Time) const
{
	const float Duration = GetDuration();
	const float ClampedTime = FMath::Clamp(Time, 0.0f, FMath::Max(Duration, 0.0f));
	return SampleAtTime(ClampedTime);
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

	const double Distance = DistanceAtTime(Time);
	return SplineComponent->GetTransformAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
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
		return SpeedDistanceCache.GetNumKeys() > 0 ? SpeedDistanceCache.Eval(Time) : 0.0;
	case ETrajectorySpeedMode::ConstantSpeed:
	default:
		return Config.Speed * Time;
	}
}

void ATrajectoryFollowingController::RebuildSpeedDistanceCache()
{
	SpeedDistanceCache.Reset();

	const FRichCurve* SpeedCurve = Config.TimeToSpeed.GetRichCurveConst();
	if (!SpeedCurve || SpeedCurve->GetNumKeys() == 0)
	{
		return;
	}

	float MinTime = 0.0f;
	float MaxTime = 0.0f;
	SpeedCurve->GetTimeRange(MinTime, MaxTime);
	MinTime = FMath::Max(MinTime, 0.0f);

	SpeedDistanceCache.AddKey(MinTime, 0.0f);
	if (MaxTime <= MinTime)
	{
		return;
	}

	// Trapezoidal integration of speed -> cumulative distance, sampled finely so any curve
	// interpolation mode is captured well enough for runtime lookup.
	constexpr int32 NumSamples = 256;
	const float DeltaTime = (MaxTime - MinTime) / NumSamples;
	double CumulativeDistance = 0.0;
	float PrevSpeed = SpeedCurve->Eval(MinTime);
	for (int32 SampleIndex = 1; SampleIndex <= NumSamples; ++SampleIndex)
	{
		const float SampleTime = MinTime + SampleIndex * DeltaTime;
		const float SampleSpeed = SpeedCurve->Eval(SampleTime);
		CumulativeDistance += 0.5 * (PrevSpeed + SampleSpeed) * DeltaTime;
		SpeedDistanceCache.AddKey(SampleTime, static_cast<float>(CumulativeDistance));
		PrevSpeed = SampleSpeed;
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

	ElapsedSeconds += DeltaSeconds;

	// Apply end-of-trajectory behavior once the duration is reached.
	bool bResetThisTick = false;
	const float Duration = GetDuration();
	if (Duration > 0.0f && ElapsedSeconds >= Duration)
	{
		switch (Config.EndBehavior)
		{
		case ETrajectoryEndBehavior::Clamp:
			ElapsedSeconds = Duration;
			break;
		case ETrajectoryEndBehavior::Loop:
			ElapsedSeconds = FMath::Fmod(ElapsedSeconds, Duration);
			break;
		case ETrajectoryEndBehavior::Reset:
			ElapsedSeconds = FMath::Fmod(ElapsedSeconds, Duration);
			bResetThisTick = true;
			break;
		case ETrajectoryEndBehavior::Destroy:
			// Destroy the followed pawn once it reaches the end. Destroying the pawn tears down its
			// UTrajectoryFollowingComponent, whose EndPlay unpossesses and destroys this controller,
			// so return immediately without touching any more state.
			ControlledPawn->Destroy();
			return;
		}
	}

	const FTransform Target = GetTransformAtTime(ElapsedSeconds);
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

	// Feedforward: the trajectory's own velocity, from a forward finite difference.
	const float Lookahead = FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER);
	const FVector AheadLocation = GetTransformAtTime(ElapsedSeconds + Lookahead).GetLocation();
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
		const double AlongTrackError = ApplyDeadband(ToTargetLocal.X, Config.PositionDeadband);
		const double TargetLinVelCmS = FeedforwardVelocity.Size() + Config.PositionGain * AlongTrackError;
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
