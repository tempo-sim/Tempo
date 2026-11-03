// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "Misc/AutomationTest.h"

// Tests for EstimateLidarSurfaceNormal, which gives the lidar decode the normal of the geometry a
// pixel saw, rebuilt from its neighbors' depths, in place of the rendered normal, which includes the
// material's normal map.
//
// Each case renders a small synthetic scene the way the lidar tile does: a perspective pixel grid
// whose pixel (column, row) looks along (1, column * Step, -row * Step), with each pixel's point
// where that ray meets the nearest surface. The checks are on what the decode does with the
// estimate, not on the estimate's arithmetic: whether a beam that falls between pixel centers gets
// the range the true surface gives it, and whether the incidence cutoff keeps or drops it.
//
// They need no RHI and no world. Run via Scripts/Test.sh Tempo.Sensors.LidarSurfaceNormal.

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoLidarSurfaceTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	// Image plane distance between adjacent pixels: about 0.06 degrees, a lidar tile upsampled ~2.5x
	// over a 0.15 degree beam spacing.
	constexpr double Step = 1e-3;
	constexpr double MaxAngleOfIncidenceDeg = 87.5;
	const double SinMaxAngleOfIncidence = FMath::Sin(FMath::DegreesToRadians(MaxAngleOfIncidenceDeg));
	const double CosMaxAngleOfIncidence = FMath::Cos(FMath::DegreesToRadians(MaxAngleOfIncidenceDeg));

	struct FTestPlane
	{
		FVector Point;
		FVector Normal;
	};

	FVector PixelRay(double Column, double Row)
	{
		return FVector(1.0, Column * Step, -Row * Step);
	}

	// Where Ray (from the sensor at the origin) meets Plane, or nothing if it misses or meets it
	// behind the sensor.
	TOptional<FVector> Hit(const FVector& Ray, const FTestPlane& Plane)
	{
		const double Denominator = FVector::DotProduct(Plane.Normal, Ray);
		if (FMath::IsNearlyZero(Denominator))
		{
			return {};
		}
		const double T = FVector::DotProduct(Plane.Normal, Plane.Point) / Denominator;
		return T > 0.0 ? TOptional<FVector>(T * Ray) : TOptional<FVector>();
	}

	// What the pixel at (Column, Row) sees: the nearest of Planes, or a far backdrop (sky).
	FVector Render(const TArray<FTestPlane>& Planes, double Column, double Row)
	{
		const FVector Ray = PixelRay(Column, Row);
		FVector Nearest = Ray * 1e7;
		for (const FTestPlane& Plane : Planes)
		{
			const TOptional<FVector> Point = Hit(Ray, Plane);
			if (Point.IsSet() && Point->X < Nearest.X)
			{
				Nearest = *Point;
			}
		}
		return Nearest;
	}

	// The center pixel (0, 0) and its four neighbors, as the decode passes them. Mask drops neighbors
	// the way a tile edge does: bit i clears Neighbors[i].
	struct FNeighborhood
	{
		FVector Center;
		FVector Points[4];
		const FVector* Neighbors[4];

		FNeighborhood(const TArray<FTestPlane>& Planes, uint8 DropMask = 0)
		{
			Center = Render(Planes, 0, 0);
			const FIntPoint Offsets[4] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Points[Index] = Render(Planes, Offsets[Index].X, Offsets[Index].Y);
				Neighbors[Index] = (DropMask & (1 << Index)) ? nullptr : &Points[Index];
			}
		}

		FNeighborhood(const FNeighborhood&) = delete;
	};

	// The decode's range for a beam along BeamRay whose nearest pixel is Center: where the beam meets
	// the plane through Center with Normal, if the incidence cutoff keeps it.
	TOptional<double> DecodedRange(const FVector& Center, const FVector& Normal, const FVector& BeamRay)
	{
		const FVector BeamUnit = BeamRay.GetSafeNormal();
		if (FVector::DotProduct(Normal.GetSafeNormal(), -BeamUnit) < CosMaxAngleOfIncidence)
		{
			return {};
		}
		return FMath::LinePlaneIntersection(FVector::ZeroVector, BeamUnit, FPlane(Center, Normal)).Length();
	}

	// A plane through (Distance, 0, 0) whose normal faces the sensor at IncidenceDeg from the x axis,
	// tilted about an axis RollDeg around the line of sight, so the tilt is not along an image axis.
	FTestPlane TiltedPlane(double Distance, double IncidenceDeg, double RollDeg)
	{
		const FVector Normal = FRotator(0.0, 0.0, RollDeg).RotateVector(
			FVector(-FMath::Cos(FMath::DegreesToRadians(IncidenceDeg)), FMath::Sin(FMath::DegreesToRadians(IncidenceDeg)), 0.0));
		return { FVector(Distance, 0.0, 0.0), Normal };
	}

	void TestFit(FAutomationTestBase& Test, const FString& What, ETempoLidarSurfaceFit Actual, ETempoLidarSurfaceFit Expected)
	{
		Test.TestEqual(*FString::Printf(TEXT("%s: fit"), *What), static_cast<int32>(Actual), static_cast<int32>(Expected));
	}

	// Normal tipped by AngleDeg about an axis perpendicular to it: what a normal map does to a pixel.
	FVector Bumped(const FVector& Normal, double AngleDeg)
	{
		const FVector Axis = FVector::CrossProduct(Normal, FVector(0.3, 0.5, 0.8)).GetSafeNormal();
		return Normal.RotateAngleAxis(AngleDeg, Axis);
	}
}

// On a plane, a beam between pixel centers gets the plane's own range whatever the normal map did,
// where the rendered normal would have put it somewhere else.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarSurfacePlaneTest,
	"Tempo.Sensors.LidarSurfaceNormal.PlaneRangeIgnoresNormalMap", TempoLidarSurfaceTestFlags)
bool FTempoLidarSurfacePlaneTest::RunTest(const FString& Parameters)
{
	struct FCase { double Distance; double IncidenceDeg; double RollDeg; };
	const TArray<FCase> Cases = {
		{ 1000.0, 0.0, 0.0 },
		{ 1000.0, 40.0, 30.0 },
		{ 5000.0, 75.0, 110.0 },
		{ 20000.0, 85.0, 0.0 },   // tilted along the image's column axis, near the cutoff
		{ 20000.0, 85.0, 90.0 },  // ... and along its row axis
	};
	// A beam up to half a pixel from its nearest pixel's center.
	const FVector BeamRay = PixelRay(0.4, -0.35);

	for (const FCase& Case : Cases)
	{
		const FString What = FString::Printf(TEXT("d=%.0f incidence=%.0f roll=%.0f"), Case.Distance, Case.IncidenceDeg, Case.RollDeg);
		const FTestPlane Plane = TiltedPlane(Case.Distance, Case.IncidenceDeg, Case.RollDeg);
		const FVector ShadingNormal = Bumped(Plane.Normal, 20.0);
		const FNeighborhood Pixels({ Plane });

		FVector Normal;
		const ETempoLidarSurfaceFit Fit = EstimateLidarSurfaceNormal(Pixels.Center, Pixels.Neighbors, ShadingNormal, SinMaxAngleOfIncidence, Normal);
		TestFit(*this, What, Fit, ETempoLidarSurfaceFit::Plane);

		const double TrueRange = Hit(BeamRay, Plane)->Length();
		const TOptional<double> Range = DecodedRange(Pixels.Center, Normal, BeamRay);
		if (!TestTrue(*FString::Printf(TEXT("%s: beam returns"), *What), Range.IsSet()))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s: range"), *What), *Range, TrueRange, TrueRange * 1e-9);

		// The test's sensitivity: the rendered normal would have been visibly wrong, or dropped the beam.
		const TOptional<double> ShadingRange = DecodedRange(Pixels.Center, ShadingNormal, BeamRay);
		if (Case.IncidenceDeg > 0.0 && ShadingRange.IsSet())
		{
			TestTrue(*FString::Printf(TEXT("%s: the bumped normal moves the range (%f vs %f)"), *What, *ShadingRange, TrueRange),
				FMath::Abs(*ShadingRange - TrueRange) > TrueRange * 1e-5);
		}
	}
	return true;
}

// A neighbor off the edge of the tile leaves the other one on that axis, which is just as exact.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarSurfaceTileEdgeTest,
	"Tempo.Sensors.LidarSurfaceNormal.TileEdgeUsesOneSide", TempoLidarSurfaceTestFlags)
bool FTempoLidarSurfaceTileEdgeTest::RunTest(const FString& Parameters)
{
	const FTestPlane Plane = TiltedPlane(3000.0, 60.0, 25.0);
	const FVector BeamRay = PixelRay(-0.45, 0.3);
	const double TrueRange = Hit(BeamRay, Plane)->Length();

	// Each corner of a tile, and each edge.
	for (const int32 DropMask : { 0b0101, 0b0110, 0b1001, 0b1010, 0b0001, 0b0010, 0b0100, 0b1000 })
	{
		const FNeighborhood Pixels({ Plane }, static_cast<uint8>(DropMask));
		FVector Normal;
		const ETempoLidarSurfaceFit Fit = EstimateLidarSurfaceNormal(Pixels.Center, Pixels.Neighbors, Bumped(Plane.Normal, 15.0), SinMaxAngleOfIncidence, Normal);
		TestFit(*this, FString::Printf(TEXT("mask %d"), DropMask), Fit, ETempoLidarSurfaceFit::Plane);
		const TOptional<double> Range = DecodedRange(Pixels.Center, Normal, BeamRay);
		if (TestTrue(*FString::Printf(TEXT("mask %d: beam returns"), DropMask), Range.IsSet()))
		{
			TestEqual(*FString::Printf(TEXT("mask %d: range"), DropMask), *Range, TrueRange, TrueRange * 1e-9);
		}
	}

	// No neighbors at all (a one-pixel tile): nothing to rebuild from.
	const FNeighborhood Alone({ Plane }, 0b1111);
	const FVector ShadingNormal = Bumped(Plane.Normal, 15.0);
	FVector Normal;
	TestFit(*this, TEXT("no neighbors"),
		EstimateLidarSurfaceNormal(Alone.Center, Alone.Neighbors, ShadingNormal, SinMaxAngleOfIncidence, Normal), ETempoLidarSurfaceFit::None);
	TestTrue(TEXT("no neighbors: the shading normal"), Normal.Equals(ShadingNormal.GetSafeNormal(), 1e-12));
	return true;
}

// At a silhouette the pixel takes its tangent from the neighbor on its own surface, on whichever
// side that is, not from the jump to what is behind.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarSurfaceSilhouetteTest,
	"Tempo.Sensors.LidarSurfaceNormal.SilhouetteUsesOwnSurface", TempoLidarSurfaceTestFlags)
bool FTempoLidarSurfaceSilhouetteTest::RunTest(const FString& Parameters)
{
	// A wall far behind the box, and one so close behind it that the jump to it is less steep than
	// the incidence cutoff and only the choice of the less steep side keeps it out of the normal.
	for (const double WallDistance : { 10000.0, 1012.0 })
	for (const double EdgeColumn : { 0.5, -0.5 })
	{
		const FTestPlane Wall = TiltedPlane(WallDistance, 30.0, 60.0);
		// A box in front of the wall whose edge falls between the center pixel and one neighbor:
		// the center is on the box (a plane at 1000, tilted), cut off at EdgeColumn.
		const FTestPlane Box = TiltedPlane(1000.0, 50.0, -20.0);
		FVector Points[4];
		const FVector* Neighbors[4];
		const FIntPoint Offsets[4] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const bool bOnBox = EdgeColumn > 0.0 ? Offsets[Index].X < EdgeColumn : Offsets[Index].X > EdgeColumn;
			Points[Index] = bOnBox ? *Hit(PixelRay(Offsets[Index].X, Offsets[Index].Y), Box)
				: *Hit(PixelRay(Offsets[Index].X, Offsets[Index].Y), Wall);
			Neighbors[Index] = &Points[Index];
		}
		const FVector Center = *Hit(PixelRay(0, 0), Box);

		const FString What = FString::Printf(TEXT("wall at %.0f, edge at column %+.1f"), WallDistance, EdgeColumn);
		FVector Normal;
		const ETempoLidarSurfaceFit Fit = EstimateLidarSurfaceNormal(Center, Neighbors, Bumped(Box.Normal, 20.0), SinMaxAngleOfIncidence, Normal);
		TestFit(*this, What, Fit, ETempoLidarSurfaceFit::Plane);
		TestTrue(*FString::Printf(TEXT("%s: the box's own normal, not one bridging to the wall"), *What),
			Normal.Equals(Box.Normal, 1e-9));
	}
	return true;
}

// A feature one pixel wide (a pole) has no continuous neighbor across it, only along it. The
// geometry still fixes the tangent along it, and the shading normal only the direction across.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarSurfaceThinFeatureTest,
	"Tempo.Sensors.LidarSurfaceNormal.ThinFeatureKeepsItsTangent", TempoLidarSurfaceTestFlags)
bool FTempoLidarSurfaceThinFeatureTest::RunTest(const FString& Parameters)
{
	// The pole's face: leaning back 50 degrees from vertical, so its tangent along the image's rows
	// is far from the line of sight but not perpendicular to it.
	const FTestPlane Pole = { FVector(2000.0, 0.0, 0.0), FVector(-FMath::Cos(FMath::DegreesToRadians(50.0)), 0.0, -FMath::Sin(FMath::DegreesToRadians(50.0))) };
	const FTestPlane Wall = { FVector(20000.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0) };

	FVector Points[4];
	const FVector* Neighbors[4];
	Points[0] = *Hit(PixelRay(-1, 0), Wall);
	Points[1] = *Hit(PixelRay(1, 0), Wall);
	Points[2] = *Hit(PixelRay(0, -1), Pole);
	Points[3] = *Hit(PixelRay(0, 1), Pole);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Neighbors[Index] = &Points[Index];
	}
	const FVector Center = *Hit(PixelRay(0, 0), Pole);
	const FVector RowTangent = (Points[3] - Points[2]).GetSafeNormal();

	// A normal map that tips the shading normal along the pole as well as across it.
	const FVector ShadingNormal = Pole.Normal.RotateAngleAxis(25.0, FVector(0.0, 1.0, 0.0)).RotateAngleAxis(15.0, FVector(0.0, 0.0, 1.0));

	FVector Normal;
	const ETempoLidarSurfaceFit Fit = EstimateLidarSurfaceNormal(Center, Neighbors, ShadingNormal, SinMaxAngleOfIncidence, Normal);
	TestFit(*this, TEXT("pole"), Fit, ETempoLidarSurfaceFit::Tangent);
	TestEqual(TEXT("perpendicular to the pole's own tangent"), FVector::DotProduct(Normal, RowTangent), 0.0, 1e-12);
	TestTrue(TEXT("faces the sensor"), FVector::DotProduct(Normal, Center) < 0.0);

	// Along the pole, then, a beam between rows gets the pole's range, which the bumped normal would
	// not have given it.
	const FVector BeamRay = PixelRay(0.0, 0.45);
	const double TrueRange = Hit(BeamRay, Pole)->Length();
	const TOptional<double> Range = DecodedRange(Center, Normal, BeamRay);
	if (TestTrue(TEXT("beam returns"), Range.IsSet()))
	{
		TestEqual(TEXT("range along the pole"), *Range, TrueRange, TrueRange * 1e-9);
	}
	const TOptional<double> ShadingRange = DecodedRange(Center, ShadingNormal, BeamRay);
	TestTrue(TEXT("the bumped normal would have moved it"), !ShadingRange.IsSet() || FMath::Abs(*ShadingRange - TrueRange) > TrueRange * 1e-5);
	return true;
}

// From depths alone a surface past the incidence cutoff looks like a jump, so it cannot set the
// normal. It must still be dropped, not rescued by being treated as a jump: the shading normal,
// which sees the true grazing angle, decides. An isolated pixel the same way keeps its return.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLidarSurfaceGrazingTest,
	"Tempo.Sensors.LidarSurfaceNormal.PastCutoffStillDrops", TempoLidarSurfaceTestFlags)
bool FTempoLidarSurfaceGrazingTest::RunTest(const FString& Parameters)
{
	const FVector BeamRay = PixelRay(0.2, -0.2);

	// Ground far ahead of a sensor a little above it: 89 degrees of incidence, past 87.5, tilted
	// along the image's rows and with an unbumped shading normal so the true angle is known.
	for (const double RollDeg : { 90.0, 75.0 })
	{
		const FTestPlane Ground = TiltedPlane(20000.0, 89.0, RollDeg);
		const FNeighborhood Pixels({ Ground });
		FVector Normal;
		const ETempoLidarSurfaceFit Fit = EstimateLidarSurfaceNormal(Pixels.Center, Pixels.Neighbors, Ground.Normal, SinMaxAngleOfIncidence, Normal);
		const FString What = FString::Printf(TEXT("roll %.0f, fit %d"), RollDeg, static_cast<int32>(Fit));
		TestFalse(*FString::Printf(TEXT("%s: dropped by the cutoff"), *What), DecodedRange(Pixels.Center, Normal, BeamRay).IsSet());
	}

	// A small object against the sky: every neighbor jumps away, and the return is kept.
	const FTestPlane Sign = TiltedPlane(1500.0, 20.0, 45.0);
	FVector Points[4];
	const FVector* Neighbors[4];
	const FIntPoint Offsets[4] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Points[Index] = PixelRay(Offsets[Index].X, Offsets[Index].Y) * 1e7;
		Neighbors[Index] = &Points[Index];
	}
	const FVector Center = *Hit(PixelRay(0, 0), Sign);
	FVector Normal;
	TestFit(*this, TEXT("isolated"), EstimateLidarSurfaceNormal(Center, Neighbors, Sign.Normal, SinMaxAngleOfIncidence, Normal), ETempoLidarSurfaceFit::None);
	TestTrue(TEXT("isolated: returns"), DecodedRange(Center, Normal, BeamRay).IsSet());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
