// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMotionVectorRewarpViewExtension.h"

#include "Misc/AutomationTest.h"

// Unit tests for the motion vector rewarp's extrapolation factor: the number of scene ticks a
// camera's velocity vectors have to be stretched over. The GPU pass itself needs a scene render and
// is not covered here.
//
// Run via Scripts/Test.sh Tempo.Sensors.MotionVectorRewarp, or from the editor console with
//   Automation RunTests Tempo.Sensors.MotionVectorRewarp

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoMotionVectorRewarpTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoMotionVectorRewarpExtrapolationFactorTest, "Tempo.Sensors.MotionVectorRewarp.ExtrapolationFactor", TempoMotionVectorRewarpTestFlags)

bool FTempoMotionVectorRewarpExtrapolationFactorTest::RunTest(const FString& Parameters)
{
	using FExt = FTempoMotionVectorRewarpViewExtension;

	// A 10 Hz camera on a 100 Hz sim stretches over ten ticks.
	TestEqual(TEXT("10 Hz camera at 100 Hz"), FExt::ComputeExtrapolationFactor(1.1, 1.0, 0.01), 10.0f);

	// A 30 Hz camera at 100 Hz alternates between three and four ticks; whatever elapsed is used
	// as is, without rounding to a fixed rate ratio.
	TestEqual(TEXT("3 ticks elapsed"), FExt::ComputeExtrapolationFactor(1.03, 1.0, 0.01), 3.0f);
	TestEqual(TEXT("4 ticks elapsed"), FExt::ComputeExtrapolationFactor(1.04, 1.0, 0.01), 4.0f);

	// Rendering every tick, or faster than the scene advances, needs no rewarp.
	TestEqual(TEXT("one tick elapsed"), FExt::ComputeExtrapolationFactor(1.01, 1.0, 0.01), 1.0f);
	// A fixed step recomputes its delta every tick, so one tick can come out a rounding error
	// longer than this tick's delta; that is still one tick.
	TestEqual(TEXT("one tick plus rounding"), FExt::ComputeExtrapolationFactor(1.0 + 0.01 * (1.0 + 1e-6), 1.0, 0.01), 1.0f);
	TestEqual(TEXT("less than one tick elapsed"), FExt::ComputeExtrapolationFactor(1.005, 1.0, 0.01), 1.0f);
	TestEqual(TEXT("no time elapsed"), FExt::ComputeExtrapolationFactor(1.0, 1.0, 0.01), 1.0f);

	// No previous capture (first render, or a tile that was just reactivated).
	TestEqual(TEXT("no previous capture"), FExt::ComputeExtrapolationFactor(1.1, -1.0, 0.01), 1.0f);

	// A paused world advances nothing; a zero tick delta must not divide.
	TestEqual(TEXT("zero tick delta"), FExt::ComputeExtrapolationFactor(1.1, 1.0, 0.0), 1.0f);
	TestEqual(TEXT("negative tick delta"), FExt::ComputeExtrapolationFactor(1.1, 1.0, -0.01), 1.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
