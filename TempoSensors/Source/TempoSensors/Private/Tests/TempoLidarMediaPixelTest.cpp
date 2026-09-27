// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidarParticipatingMedia.h"

#include "Misc/AutomationTest.h"

// Tests for FTempoLidarMediaPixel, the packing the participating media resolve writes and the lidar
// decode reads. Pure logic. Run via Scripts/Test.sh Tempo.Sensors.LidarMediaPixel.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoLidarMediaPixelTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	// Packed as ResolveCS packs it: range | intensity << 16, transmittance | albedo << 16 | label << 24.
	FTempoLidarMediaPixel MakePixel(uint32 RangeCode, uint32 IntensityCode, uint32 TransmittanceCode, uint32 AlbedoCode, uint32 Label)
	{
		FTempoLidarMediaPixel Pixel;
		Pixel.RangeAndIntensity = RangeCode | (IntensityCode << 16);
		Pixel.TransmittanceAlbedoAndLabel = TransmittanceCode | (AlbedoCode << 16) | (Label << 24);
		return Pixel;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarMediaPixelUnpackTest, "Tempo.Sensors.LidarMediaPixel.Unpack", TempoLidarMediaPixelTestFlags)

bool FTempoLidarMediaPixelUnpackTest::RunTest(const FString& Parameters)
{
	const FTempoLidarMediaPixel Pixel = MakePixel(0x1234u, 0xABCDu, 0x8000u, 0x40u, 0x2Au);
	TestTrue(TEXT("medium echo present"), Pixel.HasMediumEcho());
	TestEqual(TEXT("range code"), static_cast<uint32>(Pixel.MediumRangeCode()), 0x1234u);
	TestEqual(TEXT("intensity code"), static_cast<uint32>(Pixel.MediumIntensityCode()), 0xABCDu);
	TestEqual(TEXT("transmittance code"), static_cast<uint32>(Pixel.SurfaceTransmittanceCode()), 0x8000u);
	TestEqual(TEXT("reflectivity byte"), static_cast<uint32>(Pixel.MediumReflectivityByte()), 0x40u);
	TestEqual(TEXT("label"), static_cast<uint32>(Pixel.MediumLabel()), 0x2Au);
	TestEqual(TEXT("range at max"), MakePixel(0xFFFFu, 0u, 0u, 0u, 0u).MediumRange(10000.0f), 10000.0f);
	TestNearlyEqual(TEXT("intensity"), MakePixel(1u, 0xFFFFu, 0u, 0u, 0u).MediumIntensity(), 1.0f, 1e-6f);
	TestNearlyEqual(TEXT("transmittance"), MakePixel(1u, 0u, 0xFFFFu, 0u, 0u).SurfaceTransmittance(), 1.0f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarMediaPixelNoEchoTest, "Tempo.Sensors.LidarMediaPixel.NoEcho", TempoLidarMediaPixelTestFlags)

bool FTempoLidarMediaPixelNoEchoTest::RunTest(const FString& Parameters)
{
	// A zero range code means no echo, whatever the other lanes hold; a cleared pixel means no
	// echo and full transmittance is not implied, so the decode must read the lane.
	const FTempoLidarMediaPixel NoEcho = MakePixel(0u, 0xFFFFu, 0xFFFFu, 0xFFu, 0xFFu);
	TestFalse(TEXT("no medium echo"), NoEcho.HasMediumEcho());
	TestEqual(TEXT("label still readable"), static_cast<uint32>(NoEcho.MediumLabel()), 0xFFu);
	const FTempoLidarMediaPixel Cleared;
	TestFalse(TEXT("cleared has no echo"), Cleared.HasMediumEcho());
	TestEqual(TEXT("cleared transmittance is zero"), Cleared.SurfaceTransmittance(), 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
