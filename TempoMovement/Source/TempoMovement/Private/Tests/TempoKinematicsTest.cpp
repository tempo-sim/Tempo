// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "KinematicBicycleModelMovementComponent.h"
#include "KinematicUnicycleModelMovementComponent.h"

#include "TempoCoreTypes.h"

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

// Unit tests for the kinematic motion models in TempoMovement (bicycle and unicycle).
//
// Two kinds of target here:
//   * ComputeNormalizedSteeringForYawRate (the inverse model) is a const method that only reads the
//     component's own properties, so those tests just NewObject a component into the transient
//     package and call it directly — no world, no owner, no RHI.
//   * SimulateMotion (the forward model) reads GetOwner()->GetActorRotation(), so those tests build a
//     throwaway world, spawn an actor at a known heading, and create the component via NewObject with
//     the actor as outer (so GetOwner() resolves). See FKinematicTestFixture below.
//
// Both run headlessly under -nullrhi via:
//   Scripts/Test.sh Tempo.Movement
//   Automation RunTests Tempo.Movement.Kinematics   (from the editor console)
//
// The expected values for the inverse-model tests assume the class-default tuning constants
// (Wheelbase = 100 cm, AxleRatio = 0.5, MaxSteering = 10, SteeringToAngularVelocityFactor = 1). If
// those defaults change, these tests should be updated to match — that is intentional: the defaults
// are part of the configured behavior.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoKinematicsTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	constexpr double KinematicsTol = 1e-3;

	// Class defaults the inverse-model tests rely on (see KinematicVehicleMovementComponent.h and the
	// derived headers).
	constexpr double DefaultMaxSteering = 10.0;

	// RAII fixture for the forward-model (SimulateMotion) tests. Spins up a transient Game world,
	// spawns a bare actor with a scene-component root rotated to the requested yaw, and hands out
	// movement components parented to that actor. Everything is destroyed in the destructor.
	struct FKinematicTestFixture
	{
		UWorld* World = nullptr;
		AActor* Actor = nullptr;

		explicit FKinematicTestFixture(double YawDegrees)
		{
			World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);

			Actor = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Actor);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Actor->SetActorRotation(FRotator(0.0, YawDegrees, 0.0));
		}

		// Actor as outer => UActorComponent::GetOwner() returns it without needing registration, which
		// is all SimulateMotion reads.
		template <typename ComponentType>
		ComponentType* MakeComponent() const
		{
			return NewObject<ComponentType>(Actor);
		}

		~FKinematicTestFixture()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}

		FKinematicTestFixture(const FKinematicTestFixture&) = delete;
		FKinematicTestFixture& operator=(const FKinematicTestFixture&) = delete;
	};
}

//
// Inverse model: ComputeNormalizedSteeringForYawRate
//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoKinematicsBicycleInverseTest,
	"Tempo.Movement.Kinematics.BicycleInverse", TempoKinematicsTestFlags)
bool FTempoKinematicsBicycleInverseTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = KinematicsTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.6f, got %.6f"), What, B, A));
		}
	};

	UKinematicBicycleModelMovementComponent* Bike =
		NewObject<UKinematicBicycleModelMovementComponent>(GetTransientPackage());

	// yaw_rate = v * sin(steer) / wheelbase, so the inverse is steer = asin(yaw_rate * wheelbase / v),
	// normalized by MaxSteering. With wheelbase = 100, MaxSteering = 10:
	//   inverse(10 deg/s, 1000 cm/s) -> asin(0.01745)=1.00005 deg / 10 = 0.10001
	//   inverse(30 deg/s, 1000 cm/s) -> asin(0.05236)=3.0013 deg / 10 = 0.30013
	Near(TEXT("Bicycle inverse 10 deg/s @ 1000 cm/s"), Bike->ComputeNormalizedSteeringForYawRate(10.0, 1000.0), 0.1, 1e-3);
	Near(TEXT("Bicycle inverse 30 deg/s @ 1000 cm/s"), Bike->ComputeNormalizedSteeringForYawRate(30.0, 1000.0), 0.30013, 1e-3);

	// Sign symmetry: negating the target yaw rate negates the steering.
	Near(TEXT("Bicycle inverse is odd"),
		Bike->ComputeNormalizedSteeringForYawRate(-10.0, 1000.0),
		-Bike->ComputeNormalizedSteeringForYawRate(10.0, 1000.0));

	// Saturation: a yaw rate the model can't reach at this speed clamps the steering to +/-1.
	Near(TEXT("Bicycle inverse saturates high +"), Bike->ComputeNormalizedSteeringForYawRate(90.0, 100.0), 1.0);
	Near(TEXT("Bicycle inverse saturates high -"), Bike->ComputeNormalizedSteeringForYawRate(-90.0, 100.0), -1.0);

	// At (near) zero speed the bicycle can't produce a yaw rate, so it pre-steers fully toward the
	// requested turn direction (sign of the target), and returns 0 for a straight request.
	Near(TEXT("Bicycle inverse at zero speed, + turn"), Bike->ComputeNormalizedSteeringForYawRate(25.0, 0.0), 1.0);
	Near(TEXT("Bicycle inverse at zero speed, - turn"), Bike->ComputeNormalizedSteeringForYawRate(-25.0, 0.0), -1.0);
	Near(TEXT("Bicycle inverse at zero speed, straight"), Bike->ComputeNormalizedSteeringForYawRate(0.0, 0.0), 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoKinematicsUnicycleInverseTest,
	"Tempo.Movement.Kinematics.UnicycleInverse", TempoKinematicsTestFlags)
bool FTempoKinematicsUnicycleInverseTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = KinematicsTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.6f, got %.6f"), What, B, A));
		}
	};

	UKinematicUnicycleModelMovementComponent* Unicycle =
		NewObject<UKinematicUnicycleModelMovementComponent>(GetTransientPackage());

	// yaw_rate_deg_s = factor * steering (factor defaults to 1), normalized by MaxSteering (10). The
	// inverse is independent of speed.
	Near(TEXT("Unicycle inverse 5 deg/s"), Unicycle->ComputeNormalizedSteeringForYawRate(5.0, 500.0), 0.5);
	Near(TEXT("Unicycle inverse is speed-independent"),
		Unicycle->ComputeNormalizedSteeringForYawRate(5.0, 1.0),
		Unicycle->ComputeNormalizedSteeringForYawRate(5.0, 9999.0));

	// Sign symmetry and saturation.
	Near(TEXT("Unicycle inverse is odd"), Unicycle->ComputeNormalizedSteeringForYawRate(-5.0, 500.0), -0.5);
	Near(TEXT("Unicycle inverse saturates +"), Unicycle->ComputeNormalizedSteeringForYawRate(50.0, 500.0), 1.0);
	Near(TEXT("Unicycle inverse saturates -"), Unicycle->ComputeNormalizedSteeringForYawRate(-50.0, 500.0), -1.0);
	Near(TEXT("Unicycle inverse straight"), Unicycle->ComputeNormalizedSteeringForYawRate(0.0, 500.0), 0.0);

	return true;
}

//
// Forward model: SimulateMotion
//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoKinematicsBicycleForwardTest,
	"Tempo.Movement.Kinematics.BicycleForward", TempoKinematicsTestFlags)
bool FTempoKinematicsBicycleForwardTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = KinematicsTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.6f, got %.6f"), What, B, A));
		}
	};

	constexpr float DeltaTime = 0.1f; // Unused by SimulateMotion (it returns an instantaneous twist).
	constexpr float Speed = 500.0f;

	// Heading 0, no steering: drives straight along +X at exactly the commanded speed, no yaw.
	{
		const FKinematicTestFixture Fixture(0.0);
		UKinematicBicycleModelMovementComponent* Bike = Fixture.MakeComponent<UKinematicBicycleModelMovementComponent>();
		const FTempoTwist Motion = Bike->SimulateMotion(DeltaTime, /*Steering=*/0.0f, Speed);
		Near(TEXT("Bicycle straight Linear.X"), Motion.Linear.X, Speed);
		Near(TEXT("Bicycle straight Linear.Y"), Motion.Linear.Y, 0.0);
		Near(TEXT("Bicycle straight yaw rate"), Motion.Angular.Z, 0.0);
	}

	// Speed is preserved regardless of steering: |Linear| == |commanded speed|.
	{
		const FKinematicTestFixture Fixture(0.0);
		UKinematicBicycleModelMovementComponent* Bike = Fixture.MakeComponent<UKinematicBicycleModelMovementComponent>();
		for (const float Steering : { -8.0f, -3.0f, 3.0f, 8.0f })
		{
			const FTempoTwist Motion = Bike->SimulateMotion(DeltaTime, Steering, Speed);
			Near(*FString::Printf(TEXT("Bicycle speed preserved (steer=%.1f)"), Steering), Motion.Linear.Size(), Speed);
		}
	}

	// The owner's heading rotates the velocity vector: at yaw 90, straight motion points along +Y.
	{
		const FKinematicTestFixture Fixture(90.0);
		UKinematicBicycleModelMovementComponent* Bike = Fixture.MakeComponent<UKinematicBicycleModelMovementComponent>();
		const FTempoTwist Motion = Bike->SimulateMotion(DeltaTime, /*Steering=*/0.0f, Speed);
		Near(TEXT("Bicycle heading 90 Linear.X"), Motion.Linear.X, 0.0);
		Near(TEXT("Bicycle heading 90 Linear.Y"), Motion.Linear.Y, Speed);
	}

	// Positive steering yields positive yaw rate (and the slip angle Beta sits strictly between the
	// heading and the steering angle for an AxleRatio in (0, 1)). Negative steering mirrors it.
	{
		const FKinematicTestFixture Fixture(0.0);
		UKinematicBicycleModelMovementComponent* Bike = Fixture.MakeComponent<UKinematicBicycleModelMovementComponent>();

		const FTempoTwist Left = Bike->SimulateMotion(DeltaTime, /*Steering=*/10.0f, Speed);
		TestTrue(TEXT("Bicycle positive steering -> positive yaw rate"), Left.Angular.Z > 0.0);
		const double BetaDeg = FMath::RadiansToDegrees(FMath::Atan2(Left.Linear.Y, Left.Linear.X));
		TestTrue(TEXT("Bicycle slip angle is between 0 and steering"), BetaDeg > 0.0 && BetaDeg < 10.0);

		const FTempoTwist Right = Bike->SimulateMotion(DeltaTime, /*Steering=*/-10.0f, Speed);
		Near(TEXT("Bicycle yaw rate is odd in steering"), Right.Angular.Z, -Left.Angular.Z);
	}

	// A stationary bicycle cannot yaw, no matter the steering angle (yaw rate scales with speed).
	{
		const FKinematicTestFixture Fixture(0.0);
		UKinematicBicycleModelMovementComponent* Bike = Fixture.MakeComponent<UKinematicBicycleModelMovementComponent>();
		const FTempoTwist Motion = Bike->SimulateMotion(DeltaTime, /*Steering=*/10.0f, /*NewLinearVelocity=*/0.0f);
		Near(TEXT("Stationary bicycle Linear is zero"), Motion.Linear.Size(), 0.0);
		Near(TEXT("Stationary bicycle does not yaw"), Motion.Angular.Z, 0.0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoKinematicsUnicycleForwardTest,
	"Tempo.Movement.Kinematics.UnicycleForward", TempoKinematicsTestFlags)
bool FTempoKinematicsUnicycleForwardTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = KinematicsTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.6f, got %.6f"), What, B, A));
		}
	};

	constexpr float DeltaTime = 0.1f;
	constexpr float Speed = 400.0f;

	// Unlike the bicycle, the unicycle has no slip: velocity always points along the heading, and the
	// yaw rate is just factor * steering (factor defaults to 1) independent of speed.
	{
		const FKinematicTestFixture Fixture(0.0);
		UKinematicUnicycleModelMovementComponent* Unicycle = Fixture.MakeComponent<UKinematicUnicycleModelMovementComponent>();
		const FTempoTwist Motion = Unicycle->SimulateMotion(DeltaTime, /*Steering=*/7.0f, Speed);
		Near(TEXT("Unicycle Linear.X == speed"), Motion.Linear.X, Speed);
		Near(TEXT("Unicycle Linear.Y == 0 (no slip)"), Motion.Linear.Y, 0.0);
		Near(TEXT("Unicycle yaw rate == factor * steering"), Motion.Angular.Z, 7.0);
	}

	// Speed preserved and velocity follows heading.
	{
		const FKinematicTestFixture Fixture(90.0);
		UKinematicUnicycleModelMovementComponent* Unicycle = Fixture.MakeComponent<UKinematicUnicycleModelMovementComponent>();
		const FTempoTwist Motion = Unicycle->SimulateMotion(DeltaTime, /*Steering=*/0.0f, Speed);
		Near(TEXT("Unicycle heading 90 Linear.X"), Motion.Linear.X, 0.0);
		Near(TEXT("Unicycle heading 90 Linear.Y"), Motion.Linear.Y, Speed);
	}

	// The defining contrast with the bicycle: a stationary unicycle can still rotate in place.
	{
		const FKinematicTestFixture Fixture(0.0);
		UKinematicUnicycleModelMovementComponent* Unicycle = Fixture.MakeComponent<UKinematicUnicycleModelMovementComponent>();
		const FTempoTwist Motion = Unicycle->SimulateMotion(DeltaTime, /*Steering=*/7.0f, /*NewLinearVelocity=*/0.0f);
		Near(TEXT("Stationary unicycle Linear is zero"), Motion.Linear.Size(), 0.0);
		Near(TEXT("Stationary unicycle rotates in place"), Motion.Angular.Z, 7.0);
	}

	return true;
}

//
// Forward/inverse consistency: the inverse model recovers the steering that produced a yaw rate.
//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoKinematicsBicycleRoundTripTest,
	"Tempo.Movement.Kinematics.BicycleRoundTrip", TempoKinematicsTestFlags)
bool FTempoKinematicsBicycleRoundTripTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = KinematicsTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.6f, got %.6f"), What, B, A));
		}
	};

	const FKinematicTestFixture Fixture(0.0);
	UKinematicBicycleModelMovementComponent* Bike = Fixture.MakeComponent<UKinematicBicycleModelMovementComponent>();

	// For a steering angle within MaxSteering and a non-zero speed, running the forward model to get a
	// yaw rate and then the inverse model on that yaw rate should recover the original normalized
	// steering (= angle / MaxSteering). This cancels the wheelbase, so it checks the two models agree.
	constexpr float Speed = 800.0f;
	for (const float SteeringAngleDeg : { 2.0f, 4.0f, 8.0f })
	{
		const FTempoTwist Motion = Bike->SimulateMotion(0.1f, SteeringAngleDeg, Speed);
		const float Normalized = Bike->ComputeNormalizedSteeringForYawRate(Motion.Angular.Z, Speed);
		Near(*FString::Printf(TEXT("Bicycle round trip (angle=%.1f deg)"), SteeringAngleDeg),
			Normalized, SteeringAngleDeg / DefaultMaxSteering);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
