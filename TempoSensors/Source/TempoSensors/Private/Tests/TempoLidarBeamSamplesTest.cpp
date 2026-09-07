// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "Misc/AutomationTest.h"

// Tests for UTempoLidar::BuildBeamSamples, the per-beam geometry table the lidar decode reads
// instead of deriving each beam's mapping every frame.
//
// These deliberately do not re-implement the table's own formula and compare the two — a
// transcription of the same arithmetic would agree with a wrong implementation as readily as a
// right one. They check properties that a realistic bug breaks:
//
//   * PixelIndex and NearestDirectionUnit must describe the same pixel. PixelIndex is what the
//     decode fetches the pixel payload with and NearestDirectionUnit is what places the returned
//     point in space, so a transposed or off-by-one index puts every return's range on the wrong
//     bearing. This is checked by walking PixelIndex back to a direction through the same
//     projection the renderer used.
//   * RayDirectionUnit must agree with the AzimuthRad/ElevationRad the same entry reports. These
//     are stored separately, so recovering the angles from the direction catches an entry built
//     from one beam's angles and another's.
//   * Entry (h, v) must actually be beam (h, v): elevation must depend only on v and azimuth only
//     on h, which a transposed index cannot satisfy.
//   * Calibration and tile yaw must show up in the output angles, checked against the inputs
//     rather than against a formula.
//
// They need no RHI and no world. Run via Scripts/Test.sh Tempo.Sensors.LidarBeamSamples.

#if WITH_DEV_AUTOMATION_TESTS

// Defined at file scope in TempoLidar.cpp. Used here to walk a pixel index back to a direction,
// which is the independent half of the PixelIndex/NearestDirectionUnit cross-check.
FVector2D SphericalToPerspective(double AzimuthDeg, double ElevationDeg);
void PerspectiveToSpherical(const FVector2D& PerspectiveImagePlaneLocation, double& AzimuthDeg, double& ElevationDeg);
FVector SphericalToCartesian(double AzimuthDeg, double ElevationDeg, double Distance);

// BuildBeamSamples and the beam pattern that feeds it are protected.
struct FTempoLidarTestAccess
{
	static void SetVerticalPattern(UTempoLidar& Lidar, double VerticalFOV, int32 VerticalBeams,
		TArray<FLidarBeamCalibration> Calibration = {})
	{
		Lidar.VerticalFOV = VerticalFOV;
		Lidar.VerticalBeams = VerticalBeams;
		Lidar.BeamCalibration = MoveTemp(Calibration);
	}

	static void Build(const UTempoLidar& Lidar, FTempoLidarTile& Tile) { Lidar.BuildBeamSamples(Tile); }
	static double EffectiveVerticalFOV(const UTempoLidar& Lidar) { return Lidar.GetEffectiveVerticalFOV(); }
	static int32 EffectiveVerticalBeams(const UTempoLidar& Lidar) { return Lidar.GetEffectiveVerticalBeams(); }
};

namespace
{
	constexpr EAutomationTestFlags TempoLidarBeamTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	// Directions are stored single-precision, so comparisons of recovered angles work to about a
	// millidegree. Every property checked here fails by degrees or by whole pixels when it fails.
	constexpr double AngleTolDeg = 1e-3;
	constexpr double UnitTol = 1e-4;

	// Size a tile the way ConfigureTile does, so the beams land inside the grid the way they would
	// at runtime. This is test setup, not the thing under test.
	FTempoLidarTile MakeTile(double YawOffset, double FOVAngle, int32 HorizontalBeams,
		double VerticalFOV, double BeamDivergence)
	{
		FTempoLidarTile Tile;
		Tile.YawOffset = YawOffset;
		Tile.FOVAngle = FOVAngle;
		Tile.EffectiveFOVAngle = FOVAngle;
		Tile.HorizontalBeams = HorizontalBeams;

		const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(FOVAngle / 2.0, VerticalFOV / 2.0);
		const double AspectRatio = ImagePlaneSize.Y / ImagePlaneSize.X;
		const double HorizontalSpan = FOVAngle / BeamDivergence;
		Tile.SizeXYFOV = FVector2D(HorizontalSpan, AspectRatio * HorizontalSpan);
		Tile.SizeXY = FIntPoint(FMath::CeilToInt32(Tile.SizeXYFOV.X), FMath::CeilToInt32(Tile.SizeXYFOV.Y));
		Tile.bActive = true;
		return Tile;
	}

	UTempoLidar* MakeLidar()
	{
		return NewObject<UTempoLidar>(GetTransientPackage(), UTempoLidar::StaticClass(), NAME_None, RF_Transient);
	}

	// Walk a pixel index back to the direction of that pixel's center, independently of how
	// BuildBeamSamples got there.
	FVector DirectionOfPixel(const FTempoLidarTile& Tile, double VerticalFOV, int32 PixelIndex)
	{
		const FIntPoint Coord(PixelIndex % Tile.SizeXY.X, PixelIndex / Tile.SizeXY.X);
		const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(Tile.EffectiveFOVAngle / 2.0, VerticalFOV / 2.0);
		const FVector2D SizeXYOffset = (FVector2D(Tile.SizeXY) - Tile.SizeXYFOV) / 2.0;
		const FVector2D ImagePlaneLocation =
			((FVector2D(Coord.X, Coord.Y) - SizeXYOffset) / (Tile.SizeXYFOV - FVector2D::UnitVector)
				- (FVector2D::UnitVector / 2.0)) * ImagePlaneSize;
		double AzimuthDeg, ElevationDeg;
		PerspectiveToSpherical(ImagePlaneLocation, AzimuthDeg, ElevationDeg);
		return SphericalToCartesian(AzimuthDeg, ElevationDeg, 1.0);
	}

	// Recover azimuth/elevation from a unit direction produced by SphericalToCartesian, which is
	// (cos(El)cos(Az), cos(El)sin(Az), -sin(El)).
	void AnglesOfDirection(const FVector& Direction, double& AzimuthDeg, double& ElevationDeg)
	{
		AzimuthDeg = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
		ElevationDeg = FMath::RadiansToDegrees(-FMath::Asin(FMath::Clamp(Direction.Z, -1.0, 1.0)));
	}
}

// PixelIndex and NearestDirectionUnit must describe the same pixel, and RayDirectionUnit must agree
// with the angles the same entry reports.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarBeamSamplesConsistencyTest,
	"Tempo.Sensors.LidarBeamSamples.EntriesAreSelfConsistent", TempoLidarBeamTestFlags)
bool FTempoLidarBeamSamplesConsistencyTest::RunTest(const FString& Parameters)
{
	struct FCase { double Yaw; double FOV; int32 HB; double VFOV; int32 VB; double Divergence; };
	const TArray<FCase> Cases = {
		{ 0.0,   90.0, 21,  30.0,  9, 0.35 },   // odd beam counts: a beam sits exactly on center
		{ 0.0,  120.0, 20,  30.0,  8, 0.2  },   // even beam counts: none does
		{ 45.0,  60.0, 13,  20.0,  7, 0.1  },   // tile yaw applied
		{ 0.0,   90.0,  1,  30.0,  9, 0.35 },   // single horizontal beam: must not divide by zero
		{ 0.0,   90.0, 11,  30.0,  1, 0.35 },   // single vertical beam
	};

	for (const FCase& Case : Cases)
	{
		const FString What = FString::Printf(TEXT("yaw=%.0f fov=%.0f hb=%d vfov=%.0f vb=%d div=%.2f"),
			Case.Yaw, Case.FOV, Case.HB, Case.VFOV, Case.VB, Case.Divergence);

		UTempoLidar* Lidar = MakeLidar();
		FTempoLidarTestAccess::SetVerticalPattern(*Lidar, Case.VFOV, Case.VB);
		const double EffectiveVFOV = FTempoLidarTestAccess::EffectiveVerticalFOV(*Lidar);

		FTempoLidarTile Tile = MakeTile(Case.Yaw, Case.FOV, Case.HB, EffectiveVFOV, Case.Divergence);
		FTempoLidarTestAccess::Build(*Lidar, Tile);

		if (!TestTrue(*FString::Printf(TEXT("%s: table was built"), *What), Tile.BeamSamples.IsValid()))
		{
			return false;
		}
		const TArray<FTempoLidarBeamSample>& Samples = *Tile.BeamSamples;
		if (!TestEqual(*FString::Printf(TEXT("%s: table size"), *What), Samples.Num(), Case.HB * Case.VB))
		{
			return false;
		}

		const int32 NumPixels = Tile.SizeXY.X * Tile.SizeXY.Y;
		for (int32 Index = 0; Index < Samples.Num(); ++Index)
		{
			const FTempoLidarBeamSample& Sample = Samples[Index];
			const FString Where = FString::Printf(TEXT("%s: beam %d"), *What, Index);

			// In range, or the decode reads off the end of the image.
			if (Sample.PixelIndex < 0 || Sample.PixelIndex >= NumPixels)
			{
				AddError(FString::Printf(TEXT("%s: PixelIndex %d outside 0..%d"), *Where, Sample.PixelIndex, NumPixels - 1));
				return false;
			}

			// Both stored directions must be unit vectors.
			const FVector RayDirection(Sample.RayDirectionUnit);
			const FVector NearestDirection(Sample.NearestDirectionUnit);
			if (FMath::Abs(RayDirection.Size() - 1.0) > UnitTol || FMath::Abs(NearestDirection.Size() - 1.0) > UnitTol)
			{
				AddError(FString::Printf(TEXT("%s: directions are not unit (%f, %f)"),
					*Where, RayDirection.Size(), NearestDirection.Size()));
				return false;
			}

			// The pixel the decode will fetch must be the pixel the returned point is placed along.
			const FVector PixelDirection = DirectionOfPixel(Tile, EffectiveVFOV, Sample.PixelIndex);
			if (!PixelDirection.Equals(NearestDirection, UnitTol))
			{
				AddError(FString::Printf(
					TEXT("%s: PixelIndex %d points along (%f, %f, %f) but NearestDirectionUnit is (%f, %f, %f). "
						"The payload and the bearing would come from different pixels."),
					*Where, Sample.PixelIndex, PixelDirection.X, PixelDirection.Y, PixelDirection.Z,
					NearestDirection.X, NearestDirection.Y, NearestDirection.Z));
				return false;
			}

			// The reported angles must belong to this entry's own ray.
			double RayAzimuthDeg, RayElevationDeg;
			AnglesOfDirection(RayDirection, RayAzimuthDeg, RayElevationDeg);
			const double ReportedAzimuthDeg = -FMath::RadiansToDegrees(Sample.AzimuthRad) - Case.Yaw;
			const double ReportedElevationDeg = -FMath::RadiansToDegrees(Sample.ElevationRad);
			if (FMath::Abs(RayAzimuthDeg - ReportedAzimuthDeg) > AngleTolDeg
				|| FMath::Abs(RayElevationDeg - ReportedElevationDeg) > AngleTolDeg)
			{
				AddError(FString::Printf(
					TEXT("%s: RayDirectionUnit is at azimuth %f elevation %f but the entry reports %f / %f"),
					*Where, RayAzimuthDeg, RayElevationDeg, ReportedAzimuthDeg, ReportedElevationDeg));
				return false;
			}

			// distance = depth / divisor, so a non-positive divisor would produce infinite or
			// negative ranges. It is a product of two cosines of in-range angles.
			if (!(Sample.DepthToDistanceDivisor > 0.0f && Sample.DepthToDistanceDivisor <= 1.0f + UnitTol))
			{
				AddError(FString::Printf(TEXT("%s: DepthToDistanceDivisor %f outside (0, 1]"),
					*Where, Sample.DepthToDistanceDivisor));
				return false;
			}
		}

		// A lone beam has no spread to distribute, so it sits on the tile axis rather than at one
		// edge of the FOV, where an unguarded (-0.5 + 0 / 1) spread would put it.
		if (Case.HB == 1)
		{
			TestEqual(*FString::Printf(TEXT("%s: single horizontal beam is centered"), *What),
				static_cast<double>(Samples[0].AzimuthRad), FMath::DegreesToRadians(-Case.Yaw), 1e-6);
		}
		if (Case.VB == 1)
		{
			TestEqual(*FString::Printf(TEXT("%s: single vertical beam is centered"), *What),
				static_cast<double>(Samples[0].ElevationRad), 0.0, 1e-6);
		}
	}

	return true;
}

// Entry (h, v) must be beam (h, v). Without azimuth calibration, elevation depends only on v and
// azimuth only on h — an ordering a transposed index cannot reproduce.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarBeamSamplesOrderingTest,
	"Tempo.Sensors.LidarBeamSamples.IndexingIsRowMajorByBeam", TempoLidarBeamTestFlags)
bool FTempoLidarBeamSamplesOrderingTest::RunTest(const FString& Parameters)
{
	constexpr int32 HB = 17;
	constexpr int32 VB = 9;
	constexpr double FOV = 90.0;
	constexpr double VFOV = 30.0;

	UTempoLidar* Lidar = MakeLidar();
	FTempoLidarTestAccess::SetVerticalPattern(*Lidar, VFOV, VB);
	FTempoLidarTile Tile = MakeTile(0.0, FOV, HB, VFOV, 0.2);
	FTempoLidarTestAccess::Build(*Lidar, Tile);

	if (!TestTrue(TEXT("table was built"), Tile.BeamSamples.IsValid()))
	{
		return false;
	}
	const TArray<FTempoLidarBeamSample>& Samples = *Tile.BeamSamples;

	for (int32 v = 0; v < VB; ++v)
	{
		const float Elevation = Samples[v].ElevationRad;
		for (int32 h = 1; h < HB; ++h)
		{
			if (!FMath::IsNearlyEqual(Samples[h * VB + v].ElevationRad, Elevation, UE_KINDA_SMALL_NUMBER))
			{
				AddError(FString::Printf(
					TEXT("elevation for channel %d changes with the horizontal beam (%f at h=0, %f at h=%d); "
						"the table is not indexed h * VerticalBeams + v"),
					v, Elevation, Samples[h * VB + v].ElevationRad, h));
				return false;
			}
		}
	}

	for (int32 h = 0; h < HB; ++h)
	{
		const float Azimuth = Samples[h * VB].AzimuthRad;
		for (int32 v = 1; v < VB; ++v)
		{
			if (!FMath::IsNearlyEqual(Samples[h * VB + v].AzimuthRad, Azimuth, UE_KINDA_SMALL_NUMBER))
			{
				AddError(FString::Printf(
					TEXT("azimuth for horizontal beam %d changes with the channel (%f at v=0, %f at v=%d)"),
					h, Azimuth, Samples[h * VB + v].AzimuthRad, v));
				return false;
			}
		}
	}

	// Reported azimuth is negated from the internal convention, so it decreases as h increases, and
	// spans the tile's FOV. Same for elevation across channels.
	TestTrue(TEXT("azimuth decreases with horizontal beam index"),
		Samples[0].AzimuthRad > Samples[(HB - 1) * VB].AzimuthRad);
	TestTrue(TEXT("elevation decreases with channel index"),
		Samples[0].ElevationRad > Samples[VB - 1].ElevationRad);
	TestEqual(TEXT("azimuth spans the tile FOV"),
		static_cast<double>(Samples[0].AzimuthRad - Samples[(HB - 1) * VB].AzimuthRad),
		FMath::DegreesToRadians(FOV), 1e-4);
	TestEqual(TEXT("elevation spans the vertical FOV"),
		static_cast<double>(Samples[0].ElevationRad - Samples[VB - 1].ElevationRad),
		FMath::DegreesToRadians(VFOV), 1e-4);

	// The center beam of an odd grid looks straight down the tile axis.
	const FTempoLidarBeamSample& Center = Samples[(HB / 2) * VB + (VB / 2)];
	TestEqual(TEXT("center beam azimuth is zero"), static_cast<double>(Center.AzimuthRad), 0.0, 1e-6);
	TestEqual(TEXT("center beam elevation is zero"), static_cast<double>(Center.ElevationRad), 0.0, 1e-6);
	TestTrue(TEXT("center beam points along +X"),
		FVector(Center.RayDirectionUnit).Equals(FVector(1.0, 0.0, 0.0), UnitTol));

	return true;
}

// Per-channel calibration and the tile's yaw must reach the reported angles. Checked against the
// values fed in, not against the table's own arithmetic.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarBeamSamplesCalibrationTest,
	"Tempo.Sensors.LidarBeamSamples.CalibrationAndYawAreApplied", TempoLidarBeamTestFlags)
bool FTempoLidarBeamSamplesCalibrationTest::RunTest(const FString& Parameters)
{
	constexpr int32 HB = 11;
	constexpr double FOV = 90.0;
	constexpr double YawOffset = 30.0;

	// Deliberately non-uniform, and not symmetric about zero, so a table that ignored it or spread
	// channels evenly could not match.
	TArray<FLidarBeamCalibration> Calibration;
	for (const TPair<float, float>& Beam : TArray<TPair<float, float>>{
			{ -7.5f, 0.0f }, { -2.1f, 0.4f }, { 0.3f, -0.3f }, { 4.9f, 0.15f }, { 11.2f, -0.5f } })
	{
		FLidarBeamCalibration Entry;
		Entry.ElevationDeg = Beam.Key;
		Entry.AzimuthOffsetDeg = Beam.Value;
		Calibration.Add(Entry);
	}
	const int32 VB = Calibration.Num();

	UTempoLidar* Lidar = MakeLidar();
	FTempoLidarTestAccess::SetVerticalPattern(*Lidar, 30.0, 64, Calibration);

	TestEqual(TEXT("calibration drives the channel count"),
		FTempoLidarTestAccess::EffectiveVerticalBeams(*Lidar), VB);

	FTempoLidarTile Tile = MakeTile(YawOffset, FOV, HB,
		FTempoLidarTestAccess::EffectiveVerticalFOV(*Lidar), 0.2);
	FTempoLidarTestAccess::Build(*Lidar, Tile);

	if (!TestTrue(TEXT("table was built"), Tile.BeamSamples.IsValid()))
	{
		return false;
	}
	const TArray<FTempoLidarBeamSample>& Samples = *Tile.BeamSamples;
	if (!TestEqual(TEXT("table size follows the calibration"), Samples.Num(), HB * VB))
	{
		return false;
	}

	for (int32 h = 0; h < HB; ++h)
	{
		const double NominalAzimuthDeg = (-0.5 + static_cast<double>(h) / (HB - 1)) * FOV;
		for (int32 v = 0; v < VB; ++v)
		{
			const FTempoLidarBeamSample& Sample = Samples[h * VB + v];

			// Elevation comes straight from the calibration entry for this channel.
			const double ExpectedElevationDeg = Calibration[v].ElevationDeg;
			const double ActualElevationDeg = -FMath::RadiansToDegrees(Sample.ElevationRad);
			if (FMath::Abs(ActualElevationDeg - ExpectedElevationDeg) > AngleTolDeg)
			{
				AddError(FString::Printf(TEXT("beam (%d, %d): elevation %f, expected the calibrated %f"),
					h, v, ActualElevationDeg, ExpectedElevationDeg));
				return false;
			}

			// Azimuth is the nominal spin angle plus this channel's offset, plus the tile's yaw.
			const double ExpectedAzimuthDeg = NominalAzimuthDeg + Calibration[v].AzimuthOffsetDeg + YawOffset;
			const double ActualAzimuthDeg = -FMath::RadiansToDegrees(Sample.AzimuthRad);
			if (FMath::Abs(ActualAzimuthDeg - ExpectedAzimuthDeg) > AngleTolDeg)
			{
				AddError(FString::Printf(TEXT("beam (%d, %d): azimuth %f, expected %f (nominal %f + offset %f + yaw %f)"),
					h, v, ActualAzimuthDeg, ExpectedAzimuthDeg, NominalAzimuthDeg,
					Calibration[v].AzimuthOffsetDeg, YawOffset));
				return false;
			}
		}
	}

	// With per-channel azimuth offsets the columns are no longer straight, so the pixel a beam lands
	// on has to vary with the channel. If it did not, the offsets would not be reaching the mapping.
	bool bAnyColumnVaries = false;
	for (int32 h = 0; h < HB && !bAnyColumnVaries; ++h)
	{
		const int32 FirstX = Samples[h * VB].PixelIndex % Tile.SizeXY.X;
		for (int32 v = 1; v < VB; ++v)
		{
			if (Samples[h * VB + v].PixelIndex % Tile.SizeXY.X != FirstX)
			{
				bAnyColumnVaries = true;
				break;
			}
		}
	}
	TestTrue(TEXT("azimuth offsets shift which pixel column a channel samples"), bAnyColumnVaries);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
