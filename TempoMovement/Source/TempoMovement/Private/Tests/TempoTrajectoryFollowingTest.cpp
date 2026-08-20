// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "SplineActor.h"
#include "TrajectoryFollowingController.h"

#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

// Unit tests for ATrajectoryFollowingController: how progress along a trajectory is advanced, when a
// trajectory ends, and what each end behavior does about it.
//
// The controller is driven directly — spawn it, call FollowTrajectory, then call Tick with the deltas
// we want — rather than through a UTrajectoryFollowingComponent and the engine's tick, so a test can
// step the trajectory by an exact amount. Every test follows a straight spline in Teleport mode, so
// the pawn's location *is* the sampled target and arc length along the spline is just distance in x;
// nothing here depends on a movement component, physics, or the RHI.
//
// Runs headlessly via:
//   Scripts/Test.sh Tempo.Movement
//   Automation RunTests Tempo.Movement.TrajectoryFollowing   (from the editor console)

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoTrajectoryTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	// Spline length used by most tests, in cm.
	constexpr double TestSplineLength = 1000.0;

	// Tolerance for a distance the controller integrated over several ticks (cm). Generous relative to
	// the quantities under test (hundreds of cm), because a spline's arc length is measured from its
	// polyline approximation and need not agree with the straight-line distance to the last decimal.
	constexpr double DistanceTol = 1.0;

	// RAII fixture: a transient Game world holding a straight spline along +x from the origin and a
	// bare pawn for the controller to possess and teleport along it.
	struct FTrajectoryTestFixture
	{
		UWorld* World = nullptr;
		ASplineActor* SplineActor = nullptr;
		APawn* Pawn = nullptr;
		ATrajectoryFollowingController* Controller = nullptr;

		explicit FTrajectoryTestFixture(double Length = TestSplineLength)
		{
			World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);

			SplineActor = World->SpawnActor<ASplineActor>();
			USplineComponent* Spline = SplineActor->GetSpline();
			Spline->ClearSplinePoints(false);
			Spline->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::World, false);
			Spline->AddSplinePoint(FVector(Length, 0.0, 0.0), ESplineCoordinateSpace::World, false);
			// A straight line: pin both tangents along +x so the curve doesn't bow between the points
			// and its arc length is the distance between them.
			Spline->SetTangentAtSplinePoint(0, FVector(Length, 0.0, 0.0), ESplineCoordinateSpace::World, false);
			Spline->SetTangentAtSplinePoint(1, FVector(Length, 0.0, 0.0), ESplineCoordinateSpace::World, false);
			Spline->UpdateSpline();

			Pawn = World->SpawnActor<APawn>();
			USceneComponent* Root = NewObject<USceneComponent>(Pawn);
			Pawn->SetRootComponent(Root);
			Root->RegisterComponent();

			Controller = World->SpawnActor<ATrajectoryFollowingController>();
		}

		// Config shared by the tests: teleport (so the pawn lands exactly on the target) and hold at
		// the end unless the test says otherwise.
		static FTrajectoryFollowingConfig ConstantSpeedConfig(double SpeedCmS)
		{
			FTrajectoryFollowingConfig Config;
			Config.SpeedMode = ETrajectorySpeedMode::ConstantSpeed;
			Config.Speed = SpeedCmS;
			Config.bTeleport = true;
			Config.EndBehavior = ETrajectoryEndBehavior::Clamp;
			return Config;
		}

		void Follow(const FTrajectoryFollowingConfig& Config) const
		{
			Controller->FollowTrajectory(SplineActor, Pawn, Config);
		}

		// Tick the controller Count times with a fixed delta.
		void Tick(int32 Count, float DeltaSeconds = 0.1f) const
		{
			for (int32 TickIndex = 0; TickIndex < Count; ++TickIndex)
			{
				Controller->Tick(DeltaSeconds);
			}
		}

		double PawnX() const { return Pawn->GetActorLocation().X; }

		~FTrajectoryTestFixture()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}

		FTrajectoryTestFixture(const FTrajectoryTestFixture&) = delete;
		FTrajectoryTestFixture& operator=(const FTrajectoryTestFixture&) = delete;
	};
}

//
// Constant speed: distance is integrated per tick, so speed is a live input.
//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoTrajectoryConstantSpeedTest,
	"Tempo.Movement.TrajectoryFollowing.ConstantSpeed", TempoTrajectoryTestFlags)
bool FTempoTrajectoryConstantSpeedTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = DistanceTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.4f, got %.4f"), What, B, A));
		}
	};

	FTrajectoryTestFixture Fixture;
	Fixture.Follow(FTrajectoryTestFixture::ConstantSpeedConfig(100.0));

	// 10 ticks of 0.1 s at 100 cm/s = 100 cm, and the pawn is teleported onto it.
	Fixture.Tick(10);
	Near(TEXT("Distance after 1 s at 100 cm/s"), Fixture.Controller->GetDistanceAlongSpline(), 100.0);
	Near(TEXT("Pawn x after 1 s at 100 cm/s"), Fixture.PawnX(), 100.0);

	// Doubling the speed neither moves the pawn nor rewinds its progress: the change applies to the
	// *next* increment only. (Deriving distance from the clock instead would put the target at
	// 200 cm/s * 1 s = 200 cm here, teleporting the pawn a metre down the spline.)
	Fixture.Controller->SetSpeed(200.0);
	Near(TEXT("Distance is unchanged by SetSpeed"), Fixture.Controller->GetDistanceAlongSpline(), 100.0);
	Near(TEXT("Pawn does not move on SetSpeed"), Fixture.PawnX(), 100.0);

	// ... and from there it advances at the new speed.
	Fixture.Tick(10);
	Near(TEXT("Distance after a further 1 s at 200 cm/s"), Fixture.Controller->GetDistanceAlongSpline(), 300.0);
	Near(TEXT("Pawn x after a further 1 s at 200 cm/s"), Fixture.PawnX(), 300.0);

	// Halving it again is equally continuous, including part way through the spline.
	Fixture.Controller->SetSpeed(50.0);
	Fixture.Tick(10);
	Near(TEXT("Distance after 1 s at 50 cm/s"), Fixture.Controller->GetDistanceAlongSpline(), 350.0);

	return true;
}

//
// Zero speed: an ordinary value, not a degenerate one. The follower holds its place indefinitely
// without ending the trajectory or losing its progress, and resumes from where it stopped.
//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoTrajectoryHoldTest,
	"Tempo.Movement.TrajectoryFollowing.Hold", TempoTrajectoryTestFlags)
bool FTempoTrajectoryHoldTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = DistanceTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.4f, got %.4f"), What, B, A));
		}
	};

	FTrajectoryTestFixture Fixture;
	Fixture.Follow(FTrajectoryTestFixture::ConstantSpeedConfig(100.0));
	Fixture.Tick(20);
	Near(TEXT("Distance before the hold"), Fixture.Controller->GetDistanceAlongSpline(), 200.0);

	// Held: 10 s of ticks move the pawn not at all, and leave it where it was — not back at the
	// spline's start, which is where a duration-based follower lands when its duration collapses to
	// zero (length / 0 speed).
	Fixture.Controller->SetSpeed(0.0);
	Fixture.Tick(100);
	Near(TEXT("Distance is held"), Fixture.Controller->GetDistanceAlongSpline(), 200.0);
	Near(TEXT("Pawn is held"), Fixture.PawnX(), 200.0);

	// The trajectory did not end while held: released, it carries on from the same point rather than
	// restarting or jumping to the end.
	Fixture.Controller->SetSpeed(100.0);
	Fixture.Tick(10);
	Near(TEXT("Distance after release"), Fixture.Controller->GetDistanceAlongSpline(), 300.0);
	Near(TEXT("Pawn after release"), Fixture.PawnX(), 300.0);

	return true;
}

//
// A negative speed is a direction, not an error: the follower drives back along the spline.
//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoTrajectoryReverseTest,
	"Tempo.Movement.TrajectoryFollowing.Reverse", TempoTrajectoryTestFlags)
bool FTempoTrajectoryReverseTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = DistanceTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.4f, got %.4f"), What, B, A));
		}
	};

	// One world at a time: TempoCore's service subsystem binds per world and ensures if a second
	// world is created while the first is still up, so each case gets its own scope.
	{
		FTrajectoryTestFixture Fixture;
		Fixture.Follow(FTrajectoryTestFixture::ConstantSpeedConfig(100.0));
		Fixture.Tick(50);
		Near(TEXT("Distance driven out"), Fixture.Controller->GetDistanceAlongSpline(), 500.0);

		// Backing up retraces the road it came in on, at the speed asked for.
		Fixture.Controller->SetSpeed(-200.0);
		Fixture.Tick(10);
		Near(TEXT("Distance after 1 s at -200 cm/s"), Fixture.Controller->GetDistanceAlongSpline(), 300.0);
		Near(TEXT("Pawn backed up"), Fixture.PawnX(), 300.0);

		// The pawn keeps its heading while reversing — it backs up rather than turning around. (The
		// target's rotation is the spline tangent, which does not depend on the direction of travel.)
		Near(TEXT("Heading while reversing"), Fixture.Pawn->GetActorRotation().Yaw, 0.0, 1.0);

		// Reversing does not run off the start of the spline, and does not end the trajectory: it
		// stops there and drives out again when asked.
		Fixture.Tick(100);
		Near(TEXT("Reversing stops at the spline start"), Fixture.Controller->GetDistanceAlongSpline(), 0.0);
		Fixture.Controller->SetSpeed(100.0);
		Fixture.Tick(10);
		Near(TEXT("Driving out again from the start"), Fixture.Controller->GetDistanceAlongSpline(), 100.0);
	}

	// A Destroy follower backed onto the spline's start is not destroyed by it — only the far end
	// ends a trajectory.
	{
		FTrajectoryTestFixture Destroyer;
		FTrajectoryFollowingConfig Config = FTrajectoryTestFixture::ConstantSpeedConfig(100.0);
		Config.EndBehavior = ETrajectoryEndBehavior::Destroy;
		Destroyer.Follow(Config);
		Destroyer.Tick(20);
		Destroyer.Controller->SetSpeed(-100.0);
		Destroyer.Tick(100);
		Near(TEXT("Backed onto the start"), Destroyer.Controller->GetDistanceAlongSpline(), 0.0);
		if (!IsValid(Destroyer.Pawn))
		{
			AddError(TEXT("Backing onto the spline's start should not destroy the pawn"));
		}
	}

	return true;
}

//
// End of trajectory. The arc-length modes end where the spline does, whatever speeds they were driven
// at along the way.
//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoTrajectoryEndBehaviorTest,
	"Tempo.Movement.TrajectoryFollowing.EndBehavior", TempoTrajectoryTestFlags)
bool FTempoTrajectoryEndBehaviorTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = DistanceTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.4f, got %.4f"), What, B, A));
		}
	};

	// Clamp: hold the final pose, and keep holding it.
	{
		FTrajectoryTestFixture Fixture;
		Fixture.Follow(FTrajectoryTestFixture::ConstantSpeedConfig(1000.0));
		Fixture.Tick(20);
		Near(TEXT("Clamp stops at the end of the spline"), Fixture.Controller->GetDistanceAlongSpline(), TestSplineLength);
		Near(TEXT("Clamp leaves the pawn at the last point"), Fixture.PawnX(), TestSplineLength);
		Fixture.Tick(20);
		Near(TEXT("Clamp holds the final pose"), Fixture.PawnX(), TestSplineLength);
	}

	// Loop: wrap back to the start, carrying the overshoot so the pace is unbroken across the wrap.
	{
		FTrajectoryTestFixture Fixture;
		FTrajectoryFollowingConfig Config = FTrajectoryTestFixture::ConstantSpeedConfig(1000.0);
		Config.EndBehavior = ETrajectoryEndBehavior::Loop;
		Fixture.Follow(Config);
		// 1.5 s at 1000 cm/s = 1500 cm over a 1000 cm spline.
		Fixture.Tick(15);
		Near(TEXT("Loop carries the overshoot"), Fixture.Controller->GetDistanceAlongSpline(), 500.0);
		Near(TEXT("Loop puts the pawn back near the start"), Fixture.PawnX(), 500.0);
	}

	// Reset: teleport back to the start of the spline and go again.
	{
		FTrajectoryTestFixture Fixture;
		FTrajectoryFollowingConfig Config = FTrajectoryTestFixture::ConstantSpeedConfig(1000.0);
		Config.EndBehavior = ETrajectoryEndBehavior::Reset;
		Fixture.Follow(Config);
		// 1.2 s at 1000 cm/s = 1200 cm over a 1000 cm spline: the reset tick teleports the pawn back
		// to the wrapped point, 200 cm in, rather than leaving it clamped at the end.
		Fixture.Tick(12);
		Near(TEXT("Reset wraps to the carried overshoot"), Fixture.Controller->GetDistanceAlongSpline(), 200.0);
		Near(TEXT("Reset teleports the pawn back toward the start"), Fixture.PawnX(), 200.0);
	}

	// Destroy: the pawn is destroyed when it reaches the end.
	{
		FTrajectoryTestFixture Fixture;
		FTrajectoryFollowingConfig Config = FTrajectoryTestFixture::ConstantSpeedConfig(1000.0);
		Config.EndBehavior = ETrajectoryEndBehavior::Destroy;
		Fixture.Follow(Config);
		Fixture.Tick(5);
		if (!IsValid(Fixture.Pawn))
		{
			AddError(TEXT("Destroy should not fire before the end of the spline"));
		}
		Fixture.Tick(15);
		if (IsValid(Fixture.Pawn))
		{
			AddError(TEXT("Destroy should destroy the pawn at the end of the spline"));
		}
	}

	// A trajectory held at 0 speed never reaches its end, however long it is ticked — the end is a
	// place on the spline now, not a moment on a clock.
	{
		FTrajectoryTestFixture Fixture;
		FTrajectoryFollowingConfig Config = FTrajectoryTestFixture::ConstantSpeedConfig(1000.0);
		Config.EndBehavior = ETrajectoryEndBehavior::Destroy;
		Fixture.Follow(Config);
		Fixture.Tick(5);
		Fixture.Controller->SetSpeed(0.0);
		Fixture.Tick(200);
		if (!IsValid(Fixture.Pawn))
		{
			AddError(TEXT("A held trajectory should not reach its end and destroy the pawn"));
		}
	}

	return true;
}

//
// SpeedVsTime: the speed curve is integrated tick by tick, and the trajectory ends on whichever comes
// first — the end of the spline or the end of the curve.
//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoTrajectorySpeedVsTimeTest,
	"Tempo.Movement.TrajectoryFollowing.SpeedVsTime", TempoTrajectoryTestFlags)
bool FTempoTrajectorySpeedVsTimeTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.4f, got %.4f"), What, B, A));
		}
	};

	auto MakeConfig = [](TFunctionRef<void(FRichCurve&)> BuildCurve)
	{
		FTrajectoryFollowingConfig Config;
		Config.SpeedMode = ETrajectorySpeedMode::SpeedVsTime;
		Config.bTeleport = true;
		Config.EndBehavior = ETrajectoryEndBehavior::Clamp;
		BuildCurve(*Config.TimeToSpeed.GetRichCurve());
		return Config;
	};

	// A flat curve is just a constant speed: 100 cm/s for 4 s.
	{
		FTrajectoryTestFixture Fixture(/*Length=*/5000.0);
		Fixture.Follow(MakeConfig([](FRichCurve& Curve)
		{
			Curve.AddKey(0.0f, 100.0f);
			Curve.AddKey(4.0f, 100.0f);
		}));
		Fixture.Tick(10);
		Near(TEXT("Flat speed curve after 1 s"), Fixture.Controller->GetDistanceAlongSpline(), 100.0, DistanceTol);
		Near(TEXT("Pawn under a flat speed curve"), Fixture.PawnX(), 100.0, DistanceTol);
	}

	// A ramp from 0 to 200 cm/s over 2 s covers its integral, 200 cm. The controller integrates with
	// the speed at the start of each tick, so a fine delta is worth a couple of cm of tolerance.
	{
		FTrajectoryTestFixture Fixture(/*Length=*/5000.0);
		Fixture.Follow(MakeConfig([](FRichCurve& Curve)
		{
			Curve.AddKey(0.0f, 0.0f);
			Curve.AddKey(2.0f, 200.0f);
		}));
		Fixture.Tick(200, /*DeltaSeconds=*/0.01f);
		Near(TEXT("Ramped speed curve integral"), Fixture.Controller->GetDistanceAlongSpline(), 200.0, 5.0);
	}

	// The curve's own duration ends the trajectory when the spline outlives it: 100 cm/s for 2 s over
	// a 5000 cm spline stops at 200 cm, and stays there under Clamp.
	{
		FTrajectoryTestFixture Fixture(/*Length=*/5000.0);
		Fixture.Follow(MakeConfig([](FRichCurve& Curve)
		{
			Curve.AddKey(0.0f, 100.0f);
			Curve.AddKey(2.0f, 100.0f);
		}));
		Fixture.Tick(100);
		Near(TEXT("An exhausted speed curve ends the trajectory"), Fixture.Controller->GetDistanceAlongSpline(), 200.0, DistanceTol);
	}

	// ... and the end of the spline ends it when the curve outlives *that*: 1000 cm/s for 10 s over a
	// 1000 cm spline stops at the spline's end after 1 s.
	{
		FTrajectoryTestFixture Fixture;
		Fixture.Follow(MakeConfig([](FRichCurve& Curve)
		{
			Curve.AddKey(0.0f, 1000.0f);
			Curve.AddKey(10.0f, 1000.0f);
		}));
		Fixture.Tick(50);
		Near(TEXT("A short spline ends a long speed curve"), Fixture.Controller->GetDistanceAlongSpline(), TestSplineLength, DistanceTol);
		Near(TEXT("Pawn clamped at the end of the spline"), Fixture.PawnX(), TestSplineLength, DistanceTol);
	}

	// SetSpeed is a ConstantSpeed control and declines to act on a curve-paced trajectory, rather than
	// silently having no effect.
	{
		FTrajectoryTestFixture Fixture(/*Length=*/5000.0);
		Fixture.Follow(MakeConfig([](FRichCurve& Curve)
		{
			Curve.AddKey(0.0f, 100.0f);
			Curve.AddKey(4.0f, 100.0f);
		}));
		if (Fixture.Controller->SetSpeed(0.0))
		{
			AddError(TEXT("SetSpeed should refuse a trajectory that is not in ConstantSpeed mode"));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
