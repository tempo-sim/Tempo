// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "Misc/AutomationTest.h"

// Tests for SelectLidarReturns, which decides what a beam reports when participating media give it
// a second echo. Pure logic, no RHI and no world. Run via Scripts/Test.sh Tempo.Sensors.LidarReturnModes.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoLidarReturnModesTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FTempoLidarEcho MakeEcho(float Distance, float Intensity, bool bMedium)
	{
		FTempoLidarEcho Echo;
		Echo.Distance = Distance;
		Echo.Intensity = Intensity;
		Echo.bMedium = bMedium;
		Echo.bValid = true;
		return Echo;
	}

	const FTempoLidarEcho NoEcho;

	// A near, weak dust echo in front of a far, strong wall echo: the usual case.
	const FTempoLidarEcho NearDust = MakeEcho(300.0f, 0.05f, true);
	const FTempoLidarEcho FarWall = MakeEcho(2000.0f, 0.4f, false);

	bool Same(const FTempoLidarEcho& A, const FTempoLidarEcho& B)
	{
		return A.bValid == B.bValid && (!A.bValid || (A.Distance == B.Distance && A.Intensity == B.Intensity && A.bMedium == B.bMedium));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarReturnModesSingleEchoTest, "Tempo.Sensors.LidarReturnModes.SingleEcho", TempoLidarReturnModesTestFlags)
bool FTempoLidarReturnModesSingleEchoTest::RunTest(const FString& Parameters)
{
	const ETempoLidarReturnMode Modes[] = { ETempoLidarReturnMode::Strongest, ETempoLidarReturnMode::First, ETempoLidarReturnMode::Last, ETempoLidarReturnMode::Dual };
	for (const ETempoLidarReturnMode Mode : Modes)
	{
		FTempoLidarEcho Primary, Secondary;

		// Nothing at all: no return in any mode.
		SelectLidarReturns(Mode, NoEcho, NoEcho, Primary, Secondary);
		TestFalse(TEXT("no echoes: no primary"), Primary.bValid);
		TestFalse(TEXT("no echoes: no secondary"), Secondary.bValid);

		// Only a surface: every mode reports it, and nothing else, so a lidar without media is unchanged.
		SelectLidarReturns(Mode, FarWall, NoEcho, Primary, Secondary);
		TestTrue(TEXT("surface only: primary is the surface"), Same(Primary, FarWall));
		TestFalse(TEXT("surface only: no secondary"), Secondary.bValid);

		// Only a medium echo: likewise.
		SelectLidarReturns(Mode, NoEcho, NearDust, Primary, Secondary);
		TestTrue(TEXT("medium only: primary is the medium"), Same(Primary, NearDust));
		TestFalse(TEXT("medium only: no secondary"), Secondary.bValid);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarReturnModesTwoEchoesTest, "Tempo.Sensors.LidarReturnModes.TwoEchoes", TempoLidarReturnModesTestFlags)
bool FTempoLidarReturnModesTwoEchoesTest::RunTest(const FString& Parameters)
{
	FTempoLidarEcho Primary, Secondary;

	SelectLidarReturns(ETempoLidarReturnMode::Strongest, FarWall, NearDust, Primary, Secondary);
	TestTrue(TEXT("strongest: the wall"), Same(Primary, FarWall));
	TestFalse(TEXT("strongest: no secondary"), Secondary.bValid);

	SelectLidarReturns(ETempoLidarReturnMode::First, FarWall, NearDust, Primary, Secondary);
	TestTrue(TEXT("first: the dust"), Same(Primary, NearDust));
	TestFalse(TEXT("first: no secondary"), Secondary.bValid);

	SelectLidarReturns(ETempoLidarReturnMode::Last, FarWall, NearDust, Primary, Secondary);
	TestTrue(TEXT("last: the wall"), Same(Primary, FarWall));
	TestFalse(TEXT("last: no secondary"), Secondary.bValid);

	SelectLidarReturns(ETempoLidarReturnMode::Dual, FarWall, NearDust, Primary, Secondary);
	TestTrue(TEXT("dual: the wall first"), Same(Primary, FarWall));
	TestTrue(TEXT("dual: the dust second"), Same(Secondary, NearDust));

	// Dense dust: the medium echo is now the stronger one, and the wall, seen through it, is weaker.
	const FTempoLidarEcho DenseDust = MakeEcho(300.0f, 0.2f, true);
	const FTempoLidarEcho DimWall = MakeEcho(2000.0f, 0.02f, false);

	SelectLidarReturns(ETempoLidarReturnMode::Strongest, DimWall, DenseDust, Primary, Secondary);
	TestTrue(TEXT("strongest through dense dust: the dust"), Same(Primary, DenseDust));

	SelectLidarReturns(ETempoLidarReturnMode::Last, DimWall, DenseDust, Primary, Secondary);
	TestTrue(TEXT("last through dense dust: still the wall"), Same(Primary, DimWall));

	SelectLidarReturns(ETempoLidarReturnMode::Dual, DimWall, DenseDust, Primary, Secondary);
	TestTrue(TEXT("dual through dense dust: the dust first"), Same(Primary, DenseDust));
	TestTrue(TEXT("dual through dense dust: the wall second"), Same(Secondary, DimWall));

	// Equal intensities: the surface wins, so media never displace a surface they do not outshine.
	const FTempoLidarEcho EqualDust = MakeEcho(300.0f, 0.4f, true);
	SelectLidarReturns(ETempoLidarReturnMode::Strongest, FarWall, EqualDust, Primary, Secondary);
	TestTrue(TEXT("tie: the surface"), Same(Primary, FarWall));
	SelectLidarReturns(ETempoLidarReturnMode::Dual, FarWall, EqualDust, Primary, Secondary);
	TestTrue(TEXT("tie in dual: the surface first"), Same(Primary, FarWall));
	TestTrue(TEXT("tie in dual: the dust second"), Same(Secondary, EqualDust));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
