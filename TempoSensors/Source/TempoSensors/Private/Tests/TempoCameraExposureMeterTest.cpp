// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCameraExposureMeter.h"

#include "Misc/AutomationTest.h"

// Pure unit tests for the camera light meter's reduction of a read-back histogram to the engine's
// average scene luminance (ComputeAverageLuminanceWithoutOutlier). No RHI; run via
// Scripts/Test.sh Tempo.Sensors.ExposureMeter.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// ClientContext too: nothing here needs the editor.
	constexpr EAutomationTestFlags TempoExposureMeterTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	FTempoCameraExposureMeter::FSettings MakeSettings(float LowFraction, float HighFraction)
	{
		FTempoCameraExposureMeter::FSettings Settings;
		Settings.Histogram.HistogramLogMin = -8.0f;
		Settings.Histogram.HistogramLogMax = 4.0f;
		Settings.LowFraction = LowFraction;
		Settings.HighFraction = HighFraction;
		return Settings;
	}

	// The log2 luminance a bucket stands for: the range spread over the buckets' centres, ends included.
	double BucketLog(int32 Index)
	{
		return -8.0 + 12.0 * Index / (TempoExposureHistogramSize - 1);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoExposureMeterAverageLuminanceTest,
	"Tempo.Sensors.ExposureMeter.AverageLuminance", TempoExposureMeterTestFlags)
bool FTempoExposureMeterAverageLuminanceTest::RunTest(const FString& Parameters)
{
	uint32 Histogram[TempoExposureHistogramSize] = {};
	float Luminance = 0.0f;

	// Nothing binned: no measurement.
	TestFalse(TEXT("empty histogram"), FTempoCameraExposureMeter::AverageLuminance(Histogram, MakeSettings(0.1f, 0.9f), Luminance));

	// Everything in one bucket: that bucket's luminance, whatever the band.
	Histogram[40] = 1000;
	TestTrue(TEXT("one bucket: valid"), FTempoCameraExposureMeter::AverageLuminance(Histogram, MakeSettings(0.75f, 0.85f), Luminance));
	TestNearlyEqual(TEXT("one bucket: its luminance"), Luminance, static_cast<float>(FMath::Exp2(BucketLog(40))), 1e-5f);

	// The band trims the lowest LowFraction and everything above HighFraction of the weight. Half at
	// each end of the range, band 10%..90%: 40 units of each remain, averaging to the range's middle.
	FMemory::Memzero(Histogram);
	Histogram[0] = 50;
	Histogram[TempoExposureHistogramSize - 1] = 50;
	TestTrue(TEXT("two ends: valid"), FTempoCameraExposureMeter::AverageLuminance(Histogram, MakeSettings(0.1f, 0.9f), Luminance));
	TestNearlyEqual(TEXT("two ends: geometric mean"), Luminance, 0.25f, 1e-5f);

	// A bright outlier above the band is ignored entirely: 99 units in bucket 30, 1 at the top.
	FMemory::Memzero(Histogram);
	Histogram[30] = 99;
	Histogram[TempoExposureHistogramSize - 1] = 1;
	TestTrue(TEXT("outlier: valid"), FTempoCameraExposureMeter::AverageLuminance(Histogram, MakeSettings(0.1f, 0.9f), Luminance));
	TestNearlyEqual(TEXT("outlier: ignored"), Luminance, static_cast<float>(FMath::Exp2(BucketLog(30))), 1e-5f);

	// The engine's special case: an empty band means a fixed average of one.
	TestTrue(TEXT("empty band: valid"), FTempoCameraExposureMeter::AverageLuminance(Histogram, MakeSettings(0.5f, 0.5f), Luminance));
	TestNearlyEqual(TEXT("empty band: one"), Luminance, 1.0f, 1e-6f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
