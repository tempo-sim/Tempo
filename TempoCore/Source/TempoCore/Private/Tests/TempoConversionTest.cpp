// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoConversion.h"

#include "Misc/AutomationTest.h"

// These are pure, engine-object-free unit tests for the unit/handedness conversion helpers in
// TempoConversion.h. They run headlessly (no world, no RHI) via:
//   Scripts/Test.sh            (runs all "Tempo." automation tests)
//   Automation RunTests Tempo.Core.Conversion   (from the editor console)

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// EditorContext satisfies the required application-context flag; EngineFilter places these in the
	// engine test group. Both flags exist as enum-class members in UE 5.6 and 5.7.
	constexpr EAutomationTestFlags TempoConversionTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoConversionUnitFactorsTest,
	"Tempo.Core.Conversion.UnitFactors", TempoConversionTestFlags)
bool FTempoConversionUnitFactorsTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double Actual, double Expected, double Tol = 1e-9)
	{
		if (!FMath::IsNearlyEqual(Actual, Expected, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.12f, got %.12f"), What, Expected, Actual));
		}
	};

	Near(TEXT("CM2M(100) == 1 m"), QuantityConverter<CM2M>::Convert(100.0), 1.0);
	Near(TEXT("M2CM(1) == 100 cm"), QuantityConverter<M2CM>::Convert(1.0), 100.0);
	Near(TEXT("Deg2Rad(180) == PI"), QuantityConverter<Deg2Rad>::Convert(180.0), UE_PI);
	Near(TEXT("Rad2Deg(PI) == 180"), QuantityConverter<Rad2Deg>::Convert(UE_PI), 180.0);

	// Round trips through inverse unit conversions.
	Near(TEXT("M2CM(CM2M(x)) == x"), QuantityConverter<M2CM>::Convert(QuantityConverter<CM2M>::Convert(42.0)), 42.0);
	Near(TEXT("Rad2Deg(Deg2Rad(x)) == x"), QuantityConverter<Rad2Deg>::Convert(QuantityConverter<Deg2Rad>::Convert(37.5)), 37.5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoConversionHandednessVectorTest,
	"Tempo.Core.Conversion.HandednessVector", TempoConversionTestFlags)
bool FTempoConversionHandednessVectorTest::RunTest(const FString& Parameters)
{
	auto NearVec = [this](const TCHAR* What, const FVector& Actual, const FVector& Expected, double Tol = 1e-9)
	{
		if (!Actual.Equals(Expected, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %s, got %s"), What, *Expected.ToString(), *Actual.ToString()));
		}
	};

	const FVector V(1.0, 2.0, 3.0);

	// Handedness conversion negates Y only.
	NearVec(TEXT("L2R negates Y"), QuantityConverter<UC_NONE, L2R>::Convert(V), FVector(1.0, -2.0, 3.0));
	NearVec(TEXT("R2L negates Y"), QuantityConverter<UC_NONE, R2L>::Convert(V), FVector(1.0, -2.0, 3.0));

	// Handedness conversion is its own inverse (symmetric).
	NearVec(TEXT("R2L(L2R(v)) == v"),
		QuantityConverter<UC_NONE, R2L>::Convert(QuantityConverter<UC_NONE, L2R>::Convert(V)), V);

	// FVector2D variant negates Y too.
	const FVector2D V2(5.0, 7.0);
	const FVector2D R2 = QuantityConverter<UC_NONE, L2R>::Convert(V2);
	if (!R2.Equals(FVector2D(5.0, -7.0), 1e-9))
	{
		AddError(FString::Printf(TEXT("L2R FVector2D: expected (5, -7), got %s"), *R2.ToString()));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoConversionHandednessRotationTest,
	"Tempo.Core.Conversion.HandednessRotation", TempoConversionTestFlags)
bool FTempoConversionHandednessRotationTest::RunTest(const FString& Parameters)
{
	// FRotator: convention is (Pitch, Yaw, Roll) -> (-Pitch, -Yaw, Roll).
	const FRotator R(10.0, 20.0, 30.0); // Pitch, Yaw, Roll
	const FRotator RConv = QuantityConverter<UC_NONE, L2R>::Convert(R);
	if (!RConv.Equals(FRotator(-10.0, -20.0, 30.0), 1e-9))
	{
		AddError(FString::Printf(TEXT("L2R FRotator: expected (-10, -20, 30), got %s"), *RConv.ToString()));
	}
	// Symmetric: applying twice returns the original.
	const FRotator RRoundTrip = QuantityConverter<UC_NONE, R2L>::Convert(RConv);
	if (!RRoundTrip.Equals(R, 1e-9))
	{
		AddError(FString::Printf(TEXT("R2L(L2R(rotator)) != original: got %s"), *RRoundTrip.ToString()));
	}

	// FQuat: (x, y, z, w) -> (-x, y, -z, w).
	const FQuat Q(0.1, 0.2, 0.3, 0.9);
	const FQuat QConv = QuantityConverter<UC_NONE, L2R>::Convert(Q);
	if (!FMath::IsNearlyEqual(QConv.X, -Q.X) || !FMath::IsNearlyEqual(QConv.Y, Q.Y) ||
		!FMath::IsNearlyEqual(QConv.Z, -Q.Z) || !FMath::IsNearlyEqual(QConv.W, Q.W))
	{
		AddError(FString::Printf(TEXT("L2R FQuat: expected (-0.1, 0.2, -0.3, 0.9), got (%f, %f, %f, %f)"),
			QConv.X, QConv.Y, QConv.Z, QConv.W));
	}
	// Symmetric round trip.
	const FQuat QRoundTrip = QuantityConverter<UC_NONE, R2L>::Convert(QConv);
	if (!FMath::IsNearlyEqual(QRoundTrip.X, Q.X) || !FMath::IsNearlyEqual(QRoundTrip.Y, Q.Y) ||
		!FMath::IsNearlyEqual(QRoundTrip.Z, Q.Z) || !FMath::IsNearlyEqual(QRoundTrip.W, Q.W))
	{
		AddError(TEXT("R2L(L2R(quat)) != original"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoConversionCombinedTest,
	"Tempo.Core.Conversion.CombinedUnitAndHandedness", TempoConversionTestFlags)
bool FTempoConversionCombinedTest::RunTest(const FString& Parameters)
{
	// The combined converter applies the unit conversion, then the handedness conversion:
	// (100, 200, 300) cm -> (1, 2, 3) m -> (1, -2, 3) m (right-handed).
	const FVector Cm(100.0, 200.0, 300.0);
	const FVector Result = QuantityConverter<CM2M, L2R>::Convert(Cm);
	if (!Result.Equals(FVector(1.0, -2.0, 3.0), 1e-9))
	{
		AddError(FString::Printf(TEXT("CM2M+L2R: expected (1, -2, 3), got %s"), *Result.ToString()));
	}

	// Inverse combination round-trips back to the original (M2CM + R2L).
	const FVector Back = QuantityConverter<M2CM, R2L>::Convert(Result);
	if (!Back.Equals(Cm, 1e-6))
	{
		AddError(FString::Printf(TEXT("M2CM+R2L(CM2M+L2R(v)) != original: got %s"), *Back.ToString()));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
