// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "Misc/AutomationTest.h"

// Tests for the lidar's range noise: that its samples are standard normal, reproducible, and
// uncorrelated between neighboring beams and between a beam's echoes, and how a sample becomes a
// reported range. Pure logic, no RHI and no world. Run via Scripts/Test.sh Tempo.Sensors.LidarRangeNoise.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoLidarRangeNoiseTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	// About a 512x2048 scan's worth of beams: enough that the statistics below have standard errors
	// near 0.001.
	constexpr int32 NumSamples = 1 << 20;

	// Pearson correlation of A[i] with B[i].
	double Correlation(const TArray<float>& A, const TArray<float>& B)
	{
		double SumA = 0.0, SumB = 0.0, SumAA = 0.0, SumBB = 0.0, SumAB = 0.0;
		for (int32 I = 0; I < A.Num(); ++I)
		{
			SumA += A[I];
			SumB += B[I];
			SumAA += A[I] * A[I];
			SumBB += B[I] * B[I];
			SumAB += A[I] * B[I];
		}
		const double N = A.Num();
		const double Cov = SumAB / N - (SumA / N) * (SumB / N);
		const double VarA = SumAA / N - FMath::Square(SumA / N);
		const double VarB = SumBB / N - FMath::Square(SumB / N);
		return Cov / FMath::Sqrt(VarA * VarB);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarRangeNoiseDistributionTest, "Tempo.Sensors.LidarRangeNoise.StandardNormal", TempoLidarRangeNoiseTestFlags)
bool FTempoLidarRangeNoiseDistributionTest::RunTest(const FString& Parameters)
{
	const uint32 Stream = LidarRangeNoiseStream(0, 0, 0);
	double Sum = 0.0;
	double SumSquares = 0.0;
	double SumFourth = 0.0;
	int32 WithinOneSigma = 0;
	int32 WithinTwoSigma = 0;
	float MaxMagnitude = 0.0f;
	bool bAllFinite = true;
	for (int32 I = 0; I < NumSamples; ++I)
	{
		const float Sample = LidarRangeNoiseSample(Stream, static_cast<uint32>(I), 0u);
		bAllFinite &= FMath::IsFinite(Sample);
		Sum += Sample;
		SumSquares += Sample * Sample;
		SumFourth += FMath::Square(Sample * Sample);
		WithinOneSigma += FMath::Abs(Sample) <= 1.0f;
		WithinTwoSigma += FMath::Abs(Sample) <= 2.0f;
		MaxMagnitude = FMath::Max(MaxMagnitude, FMath::Abs(Sample));
	}

	const double Mean = Sum / NumSamples;
	const double Variance = SumSquares / NumSamples - Mean * Mean;
	TestTrue(TEXT("Every sample is finite"), bAllFinite);
	// Standard errors: 0.001 for the mean, 0.0014 for the variance, 0.0005 for the fractions.
	TestTrue(FString::Printf(TEXT("Mean %f is 0"), Mean), FMath::Abs(Mean) < 0.006);
	TestTrue(FString::Printf(TEXT("Variance %f is 1"), Variance), FMath::Abs(Variance - 1.0) < 0.008);
	// A normal's kurtosis is 3; a uniform's is 1.8 and a sum of two uniforms' 2.4.
	const double Kurtosis = (SumFourth / NumSamples) / (Variance * Variance);
	TestTrue(FString::Printf(TEXT("Kurtosis %f is a normal's"), Kurtosis), FMath::Abs(Kurtosis - 3.0) < 0.05);
	TestTrue(TEXT("68.3% within one standard deviation"), FMath::Abs(WithinOneSigma / double(NumSamples) - 0.6827) < 0.003);
	TestTrue(TEXT("95.4% within two standard deviations"), FMath::Abs(WithinTwoSigma / double(NumSamples) - 0.9545) < 0.002);
	// Bounded by the 24-bit uniform: sqrt(2 ln 2^24).
	TestTrue(FString::Printf(TEXT("Max magnitude %f is bounded"), MaxMagnitude), MaxMagnitude <= 5.77f);
	TestTrue(TEXT("The tail is reached"), MaxMagnitude > 4.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarRangeNoiseIndependenceTest, "Tempo.Sensors.LidarRangeNoise.Independence", TempoLidarRangeNoiseTestFlags)
bool FTempoLidarRangeNoiseIndependenceTest::RunTest(const FString& Parameters)
{
	const uint32 Stream = LidarRangeNoiseStream(0, 0, 0);
	const uint32 NextScan = LidarRangeNoiseStream(0, 1, 0);
	const uint32 OtherTile = LidarRangeNoiseStream(0, 0, 1);
	const uint32 OtherSeed = LidarRangeNoiseStream(1, 0, 0);

	TArray<float> Beams, NextBeams, SecondEchoes, NextScanBeams, OtherTileBeams, OtherSeedBeams;
	for (TArray<float>* Samples : { &Beams, &NextBeams, &SecondEchoes, &NextScanBeams, &OtherTileBeams, &OtherSeedBeams })
	{
		Samples->SetNumUninitialized(NumSamples);
	}
	for (int32 I = 0; I < NumSamples; ++I)
	{
		const uint32 Beam = static_cast<uint32>(I);
		Beams[I] = LidarRangeNoiseSample(Stream, Beam, 0u);
		NextBeams[I] = LidarRangeNoiseSample(Stream, Beam + 1u, 0u);
		SecondEchoes[I] = LidarRangeNoiseSample(Stream, Beam, 1u);
		NextScanBeams[I] = LidarRangeNoiseSample(NextScan, Beam, 0u);
		OtherTileBeams[I] = LidarRangeNoiseSample(OtherTile, Beam, 0u);
		OtherSeedBeams[I] = LidarRangeNoiseSample(OtherSeed, Beam, 0u);
	}

	// The standard error of a correlation of independent samples here is 0.001.
	auto TestUncorrelated = [this](const TCHAR* What, const TArray<float>& A, const TArray<float>& B)
	{
		const double R = Correlation(A, B);
		TestTrue(FString::Printf(TEXT("%s are uncorrelated (r = %f)"), What, R), FMath::Abs(R) < 0.006);
	};
	TestUncorrelated(TEXT("Neighboring beams"), Beams, NextBeams);
	TestUncorrelated(TEXT("A beam's two echoes"), Beams, SecondEchoes);
	TestUncorrelated(TEXT("Consecutive scans"), Beams, NextScanBeams);
	TestUncorrelated(TEXT("Tiles"), Beams, OtherTileBeams);
	TestUncorrelated(TEXT("Seeds"), Beams, OtherSeedBeams);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarRangeNoiseDeterminismTest, "Tempo.Sensors.LidarRangeNoise.Deterministic", TempoLidarRangeNoiseTestFlags)
bool FTempoLidarRangeNoiseDeterminismTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A stream is a function of its inputs"), LidarRangeNoiseStream(7, 42, 2), LidarRangeNoiseStream(7, 42, 2));
	TestNotEqual(TEXT("Seeds differ"), LidarRangeNoiseStream(7, 42, 2), LidarRangeNoiseStream(8, 42, 2));
	TestNotEqual(TEXT("Scans differ"), LidarRangeNoiseStream(7, 42, 2), LidarRangeNoiseStream(7, 43, 2));
	TestNotEqual(TEXT("Tiles differ"), LidarRangeNoiseStream(7, 42, 2), LidarRangeNoiseStream(7, 42, 1));

	const uint32 Stream = LidarRangeNoiseStream(7, 42, 2);
	bool bRepeats = true;
	for (uint32 Beam = 0; Beam < 1000u; ++Beam)
	{
		bRepeats &= LidarRangeNoiseSample(Stream, Beam, 0u) == LidarRangeNoiseSample(Stream, Beam, 0u);
	}
	TestTrue(TEXT("A sample is a function of its inputs"), bRepeats);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarRangeNoiseApplyTest, "Tempo.Sensors.LidarRangeNoise.Apply", TempoLidarRangeNoiseTestFlags)
bool FTempoLidarRangeNoiseApplyTest::RunTest(const FString& Parameters)
{
	constexpr float MinDistance = 10.0f;
	constexpr float MaxDistance = 10000.0f;

	TestEqual(TEXT("No noise leaves the range alone"), ApplyLidarRangeNoise(1234.5f, 3.0f, 0.0f, 0.0f, MinDistance, MaxDistance), 1234.5f);
	TestEqual(TEXT("A constant standard deviation scales the sample"), ApplyLidarRangeNoise(1000.0f, -1.5f, 2.0f, 0.0f, MinDistance, MaxDistance), 997.0f);
	// 2 cm + 0.001 * 5000 cm = 7 cm.
	TestEqual(TEXT("The proportional term grows with range"), ApplyLidarRangeNoise(5000.0f, 1.0f, 2.0f, 0.001f, MinDistance, MaxDistance), 5007.0f);
	TestEqual(TEXT("Noise does not push a range below MinDistance"), ApplyLidarRangeNoise(11.0f, -5.0f, 3.0f, 0.0f, MinDistance, MaxDistance), MinDistance);
	TestEqual(TEXT("Noise does not push a range past MaxDistance"), ApplyLidarRangeNoise(9999.0f, 5.0f, 3.0f, 0.0f, MinDistance, MaxDistance), MaxDistance);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
