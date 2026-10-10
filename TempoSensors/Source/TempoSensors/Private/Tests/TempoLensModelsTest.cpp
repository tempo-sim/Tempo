// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLensModels.h"

#include "Misc/AutomationTest.h"

// Pure, engine-object-free unit tests for the camera/lidar distortion math in TempoLensModels.
// All targets here are static functions or const methods with no engine dependencies, so they run
// headlessly (no world, no RHI). Run via Scripts/Test.sh, or from the editor console with
//   Automation RunTests Tempo.Sensors.LensModels

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoLensTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	// Newton-Raphson inverses in the lens code converge to ~1e-6; allow a slightly looser tolerance.
	constexpr double LensTol = 1e-4;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLensFactoryTest,
	"Tempo.Sensors.LensModels.Factory", TempoLensTestFlags)
bool FTempoLensFactoryTest::RunTest(const FString& Parameters)
{
	for (const ETempoLensModel Model : {
		ETempoLensModel::Pinhole, ETempoLensModel::BrownConrady, ETempoLensModel::Rational,
		ETempoLensModel::KannalaBrandt, ETempoLensModel::DoubleSphere })
	{
		FTempoLensParameters Params;
		Params.LensModel = Model;
		const TUniquePtr<FLensModel> Lens = CreateLensModel(Params, 0.0, 0.0);
		TestNotNull(*FString::Printf(TEXT("CreateLensModel(%d) returns a model"), static_cast<int32>(Model)), Lens.Get());
	}

	// Pinhole is implemented as a zero-coefficient Brown-Conrady: OutputToRender must be the identity.
	FTempoLensParameters Pinhole;
	Pinhole.LensModel = ETempoLensModel::Pinhole;
	const TUniquePtr<FLensModel> Lens = CreateLensModel(Pinhole, 0.0, 0.0);
	for (const FVector2D& P : { FVector2D(0.0, 0.0), FVector2D(0.3, -0.2), FVector2D(-0.5, 0.5) })
	{
		const FVector2D R = Lens->OutputToRender(P.X, P.Y);
		if (!R.Equals(P, LensTol))
		{
			AddError(FString::Printf(TEXT("Pinhole OutputToRender not identity at %s -> %s"), *P.ToString(), *R.ToString()));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLensBrownConradyTest,
	"Tempo.Sensors.LensModels.BrownConrady", TempoLensTestFlags)
bool FTempoLensBrownConradyTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = LensTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.8f, got %.8f"), What, B, A));
		}
	};

	// Zero coefficients => pass-through.
	Near(TEXT("BC Distort identity (K=0)"), FBrownConradyDistortion::SolveDistortion(0.5, 0.0, 0.0, 0.0), 0.5);
	Near(TEXT("BC Undistort identity (K=0)"), FBrownConradyDistortion::SolveInverseDistortion(0.5, 0.0, 0.0, 0.0), 0.5);

	// Forward/inverse are mutual inverses across a range of coefficients (barrel and pincushion).
	const double KSets[][3] = { {-0.2, 0.0, 0.0}, {0.15, 0.05, 0.0}, {-0.1, 0.02, -0.005} };
	for (const auto& K : KSets)
	{
		for (const double R : { 0.1, 0.3, 0.6, 0.8 })
		{
			const double Rd = FBrownConradyDistortion::SolveDistortion(R, K[0], K[1], K[2]);
			const double RBack = FBrownConradyDistortion::SolveInverseDistortion(Rd, K[0], K[1], K[2]);
			Near(*FString::Printf(TEXT("BC round trip K=(%.3f,%.3f,%.3f) R=%.2f"), K[0], K[1], K[2], R), RBack, R);
		}
	}

	// Barrel distortion (K1 < 0) has a finite maximum output radius; pincushion (K1 > 0) does not.
	TestTrue(TEXT("BC barrel has finite max output radius"),
		FBrownConradyDistortion::ComputeMaxOutputRadius(-0.2, 0.0, 0.0) > 0.0);
	TestEqual(TEXT("BC pincushion has no max output radius"),
		FBrownConradyDistortion::ComputeMaxOutputRadius(0.2, 0.0, 0.0), -1.0);

	// Pinhole focal length: FOutput = (W/2) / tan(HFOV/2). For HFOV=90, W=1000 => 500 / tan(45) = 500.
	const FBrownConradyDistortion PinholeModel(0.0, 0.0, 0.0);
	Near(TEXT("BC pinhole FOutput for 90deg/1000px"),
		PinholeModel.ComputeFOutputForFullImage(FIntPoint(1000, 500), 90.0), 500.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLensRationalTest,
	"Tempo.Sensors.LensModels.Rational", TempoLensTestFlags)
bool FTempoLensRationalTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = LensTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.8f, got %.8f"), What, B, A));
		}
	};

	// With zero denominator coefficients the Rational model reduces to Brown-Conrady.
	for (const double R : { 0.1, 0.3, 0.6, 0.8 })
	{
		const double Rational = FRationalDistortion::SolveDistortion(R, 0.15, 0.05, 0.0, 0.0, 0.0, 0.0);
		const double BrownConrady = FBrownConradyDistortion::SolveDistortion(R, 0.15, 0.05, 0.0);
		Near(*FString::Printf(TEXT("Rational reduces to BC at R=%.2f"), R), Rational, BrownConrady);
	}

	// Forward/inverse round trip with nonzero numerator and denominator coefficients.
	for (const double R : { 0.1, 0.3, 0.6, 0.8 })
	{
		const double Rd = FRationalDistortion::SolveDistortion(R, 0.2, 0.05, 0.0, 0.1, 0.01, 0.0);
		const double RBack = FRationalDistortion::SolveInverseDistortion(Rd, 0.2, 0.05, 0.0, 0.1, 0.01, 0.0);
		Near(*FString::Printf(TEXT("Rational round trip at R=%.2f"), R), RBack, R);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLensKannalaBrandtTest,
	"Tempo.Sensors.LensModels.KannalaBrandt", TempoLensTestFlags)
bool FTempoLensKannalaBrandtTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = LensTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.8f, got %.8f"), What, B, A));
		}
	};

	// Pure equidistant fisheye (all K = 0): theta_d == theta.
	for (const double Theta : { 0.1, 0.5, 1.0, 1.5 })
	{
		Near(*FString::Printf(TEXT("KB equidistant theta_d==theta at %.2f"), Theta),
			FKannalaBrandtDistortion::SolveDistortion(Theta, 0.0, 0.0, 0.0, 0.0), Theta);
	}

	// Forward/inverse round trip with nonzero coefficients.
	double RoundTripThetaMax = 0.0, RoundTripThetaDMax = 0.0;
	FKannalaBrandtDistortion::ComputeMaxTheta(0.05, 0.01, 0.0, 0.0, RoundTripThetaMax, RoundTripThetaDMax);
	for (const double Theta : { 0.1, 0.5, 1.0, 1.5 })
	{
		const double ThetaD = FKannalaBrandtDistortion::SolveDistortion(Theta, 0.05, 0.01, 0.0, 0.0);
		double ThetaBack = 0.0;
		TestTrue(*FString::Printf(TEXT("KB inverse in domain at theta=%.2f"), Theta),
			FKannalaBrandtDistortion::SolveInverseDistortion(ThetaD, 0.05, 0.01, 0.0, 0.0, RoundTripThetaMax, RoundTripThetaDMax, ThetaBack));
		Near(*FString::Printf(TEXT("KB round trip at theta=%.2f"), Theta), ThetaBack, Theta);
	}

	// FOutput for a full equidistant image = (W/2) / (HFOV/2 in radians).
	// For W=1000, HFOV=180deg: 500 / (PI/2) ~= 318.3099.
	const FKannalaBrandtDistortion KB(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
	Near(TEXT("KB FOutput for 180deg/1000px"),
		KB.ComputeFOutputForFullImage(FIntPoint(1000, 1000), 180.0), 500.0 / (UE_PI / 2.0));

	return true;
}

namespace
{
	// TerraVerse's fisheye seed (dirt config/calibrations/seeds/common/cam_f_f.pbtxt). Its polynomial
	// peaks at theta 2.0081 rad (theta_d 1.6852), short of the sensor corners (theta_d ~1.724).
	FTempoLensParameters MakeSeedFisheyeLens(const FVector2D& PrincipalPoint)
	{
		FTempoLensParameters Params;
		Params.LensModel = ETempoLensModel::KannalaBrandt;
		Params.K1 = 0.00585150048f;
		Params.K2 = -0.0050607579f;
		Params.K3 = -0.00198001581f;
		Params.K4 = 0.000104815609f;
		Params.PrincipalPoint = PrincipalPoint;
		return Params;
	}

	// Top/bottom tiles of a tall fisheye split vertically, as UTempoCamera::SyncTiles lays them out.
	TArray<FFisheyeTileLayout> ComputeVerticalSplit(const FTempoLensParameters& Params, const FIntPoint& SizeXY,
		double FOutput, int32 Feather)
	{
		const TUniquePtr<FLensModel> Model = CreateLensModel(Params, 0.0, 0.0);
		FIntPoint AtlasSize;
		return ComputeFisheyeTileLayouts(*Model, FOutput, SizeXY, Params.GetClampedPrincipalPoint(), Feather,
			/*bSplitHorizontal=*/ false, /*bSplitVertical=*/ true, AtlasSize);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLensKannalaBrandtDomainTest,
	"Tempo.Sensors.LensModels.KannalaBrandtDomain", TempoLensTestFlags)
bool FTempoLensKannalaBrandtDomainTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = LensTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.8f, got %.8f"), What, B, A));
		}
	};

	const FTempoLensParameters Seed = MakeSeedFisheyeLens(FVector2D::ZeroVector);
	const double K1 = Seed.K1, K2 = Seed.K2, K3 = Seed.K3, K4 = Seed.K4;

	// A monotonic polynomial's branch runs to the PI cap.
	{
		double ThetaMax = 0.0, ThetaDMax = 0.0;
		FKannalaBrandtDistortion::ComputeMaxTheta(0.0, 0.0, 0.0, 0.0, ThetaMax, ThetaDMax);
		Near(TEXT("KB equidistant ThetaMax"), ThetaMax, UE_DOUBLE_PI);
		Near(TEXT("KB equidistant ThetaDMax"), ThetaDMax, UE_DOUBLE_PI);
	}

	// The seed's branch ends where its slope reaches zero.
	double ThetaMax = 0.0, ThetaDMax = 0.0;
	FKannalaBrandtDistortion::ComputeMaxTheta(K1, K2, K3, K4, ThetaMax, ThetaDMax);
	Near(TEXT("KB seed ThetaMax"), ThetaMax, 2.0081, 1e-3);
	Near(TEXT("KB seed ThetaDMax"), ThetaDMax, 1.6852, 1e-3);

	// Round trips hold all the way up the branch, including close to the peak where the slope vanishes.
	for (const double Theta : { 0.1, 1.0, 1.5, 1.9, 2.0 })
	{
		double ThetaBack = 0.0;
		const bool bOk = FKannalaBrandtDistortion::SolveInverseDistortion(
			FKannalaBrandtDistortion::SolveDistortion(Theta, K1, K2, K3, K4), K1, K2, K3, K4, ThetaMax, ThetaDMax, ThetaBack);
		TestTrue(*FString::Printf(TEXT("KB seed inverse in domain at theta=%.2f"), Theta), bOk);
		Near(*FString::Printf(TEXT("KB seed round trip at theta=%.2f"), Theta), ThetaBack, Theta, 1e-6);
	}

	// Beyond the peak there is no physical inverse: the solve reports it, and the distortion map gets
	// the sentinel instead of a spurious root's scene content.
	for (const double ThetaD : { ThetaDMax, 1.70, 1.7239, 2.5 })
	{
		double Theta = 0.0;
		TestFalse(*FString::Printf(TEXT("KB seed inverse rejects theta_d=%.4f"), ThetaD),
			FKannalaBrandtDistortion::SolveInverseDistortion(ThetaD, K1, K2, K3, K4, ThetaMax, ThetaDMax, Theta));
		const FKannalaBrandtDistortion KB(K1, K2, K3, K4, 0.0, 0.0);
		FVector2D Render;
		TestFalse(*FString::Printf(TEXT("KB seed TryOutputToRender rejects theta_d=%.4f"), ThetaD),
			KB.TryOutputToRender(ThetaD * 0.6, ThetaD * 0.8, Render));
		TestTrue(*FString::Printf(TEXT("KB seed OutputToRender sentinel at theta_d=%.4f"), ThetaD),
			KB.OutputToRender(ThetaD * 0.6, ThetaD * 0.8).Equals(FVector2D(1e6, 1e6)));
	}

	// cam_f_f at camera_downsample_factor 4: 540x960, 1277.85 px/rad / 4, which the camera splits
	// top/bottom, with its default 16 px feather. Every covered-rect corner lies beyond the peak.
	const FIntPoint SizeXY(540, 960);
	const double FOutput = 1277.85 / 4.0;
	constexpr int32 Feather = 16;
	const double MaxTanBound = FMath::Tan(FMath::DegreesToRadians(85.0));

	// A top/bottom split would leave each tile's outer corners behind its camera (~93 degrees from
	// its aim). No frustum covers the pixels approaching them, so a tile in that state opens those
	// sides to the cap...
	{
		const TUniquePtr<FLensModel> Model = CreateLensModel(Seed, 0.0, 0.0);
		const int32 TopCoveredH = SizeXY.Y / 2 + Feather;
		const double TopPCDy = TopCoveredH * 0.5 - SizeXY.Y * 0.5;
		const FFisheyeTileAim Aim = ComputeFisheyeTileAim(*Model, FOutput,
			-SizeXY.X * 0.5, SizeXY.X * 0.5, -SizeXY.Y * 0.5, TopCoveredH - SizeXY.Y * 0.5, 0.0, TopPCDy);
		const TUniquePtr<FLensModel> TileModel = CreateLensModel(Seed, Aim.YawDeg, Aim.PitchDeg, Aim.AxisShiftXRd, Aim.AxisShiftYRd);
		const FDistortionRenderConfig Config = TileModel->ComputeRenderConfig(FIntPoint(SizeXY.X, TopCoveredH), FOutput, FVector2D::ZeroVector);
		Near(TEXT("KB seed half tile opens left to the cap"), Config.TanLeft, -MaxTanBound, 1e-9);
		Near(TEXT("KB seed half tile opens right to the cap"), Config.TanRight, MaxTanBound, 1e-9);
		Near(TEXT("KB seed half tile opens top to the cap"), Config.TanTop, -MaxTanBound, 1e-9);
		TestTrue(TEXT("KB seed half tile bottom stays tight"), Config.TanBottom < 1.0);
	}

	// ...which is why the layout promotes it to quadrants, each with its corners in front.
	const TArray<FFisheyeTileLayout> Centered = ComputeVerticalSplit(Seed, SizeXY, FOutput, Feather);
	if (!TestEqual(TEXT("KB seed vertical split promoted to quadrants"), Centered.Num(), 4))
	{
		return false;
	}
	Near(TEXT("KB seed quadrant yaws mirror"), Centered[0].Aim.YawDeg, -Centered[1].Aim.YawDeg, 1e-9);
	Near(TEXT("KB seed quadrant pitches mirror"), Centered[0].Aim.PitchDeg, -Centered[2].Aim.PitchDeg, 1e-9);
	Near(TEXT("KB seed top quadrants share pitch"), Centered[0].Aim.PitchDeg, Centered[1].Aim.PitchDeg, 1e-9);
	TestTrue(TEXT("KB seed TL quadrant aims up and left"), Centered[0].Aim.PitchDeg > 10.0 && Centered[0].Aim.YawDeg < -10.0);
	TArray<FDistortionRenderConfig> Configs;
	for (const FFisheyeTileLayout& Layout : Centered)
	{
		const FFisheyeTileAim& Aim = Layout.Aim;
		const TUniquePtr<FLensModel> TileModel = CreateLensModel(Seed, Aim.YawDeg, Aim.PitchDeg, Aim.AxisShiftXRd, Aim.AxisShiftYRd);
		const FDistortionRenderConfig& Config = Configs.Add_GetRef(TileModel->ComputeRenderConfig(Layout.CoveredSizeXY, FOutput, FVector2D::ZeroVector));
		TestTrue(TEXT("KB seed quadrant frustum stays clear of the cap"),
			FMath::Max(FMath::Max(-Config.TanLeft, Config.TanRight), FMath::Max(-Config.TanTop, Config.TanBottom)) < 0.5 * MaxTanBound);
	}
	Near(TEXT("KB seed TL/TR frustums mirror"), Configs[0].TanLeft, -Configs[1].TanRight, 1e-9);
	Near(TEXT("KB seed TL/BL frustums mirror"), Configs[0].TanTop, -Configs[2].TanBottom, 1e-9);

	// A milder lens keeps the top/bottom split: equidistant at 100 degrees HFOV on the same image
	// reaches ~78 degrees from each tile's aim.
	{
		FTempoLensParameters Mild = MakeSeedFisheyeLens(FVector2D::ZeroVector);
		Mild.K1 = Mild.K2 = Mild.K3 = Mild.K4 = 0.0f;
		const double MildFOutput = (SizeXY.X / 2.0) / FMath::DegreesToRadians(50.0);
		const TArray<FFisheyeTileLayout> Halves = ComputeVerticalSplit(Mild, SizeXY, MildFOutput, Feather);
		if (TestEqual(TEXT("KB mild vertical split keeps two tiles"), Halves.Num(), 2))
		{
			const FFisheyeTileAim& Aim = Halves[0].Aim;
			const TUniquePtr<FLensModel> TileModel = CreateLensModel(Mild, Aim.YawDeg, Aim.PitchDeg, Aim.AxisShiftXRd, Aim.AxisShiftYRd);
			const FDistortionRenderConfig Config = TileModel->ComputeRenderConfig(Halves[0].CoveredSizeXY, MildFOutput, FVector2D::ZeroVector);
			Near(TEXT("KB mild top tile TanLeft == -TanRight"), Config.TanLeft, -Config.TanRight, 1e-9);
			TestTrue(TEXT("KB mild top tile frustum stays clear of the cap"), Config.TanRight < 0.5 * MaxTanBound && Config.TanTop > -0.5 * MaxTanBound);
		}
	}

	// The seed's real principal point; a 1e-9 relative change in FOutput (as float FOV round-off
	// produces) must barely move the aims. An unguarded Newton solve at these corners jumps between
	// spurious roots and moves the aims by tens of degrees. The quadrants' inner corners sit just
	// inside the peak, where the inverse is steep, so the aims do drift smoothly (~1e-4 degrees per
	// 1e-7 of FOutput) — far below a jump.
	const FTempoLensParameters SeedOffCenter = MakeSeedFisheyeLens(
		FVector2D((1060.8 - 1080.0) / 2160.0, (1923.54 - 1920.0) / 3840.0));
	const TArray<FFisheyeTileLayout> Base = ComputeVerticalSplit(SeedOffCenter, SizeXY, FOutput, Feather);
	for (const double Scale : { 1.0 + 1e-9, 1.0 - 1e-9, 1.0 + 1e-7 })
	{
		const TArray<FFisheyeTileLayout> Perturbed = ComputeVerticalSplit(SeedOffCenter, SizeXY, FOutput * Scale, Feather);
		if (!TestEqual(*FString::Printf(TEXT("KB seed tile count stable (scale %.9f)"), Scale), Perturbed.Num(), Base.Num()))
		{
			continue;
		}
		for (int32 I = 0; I < Base.Num(); ++I)
		{
			Near(*FString::Printf(TEXT("KB seed tile %d yaw stable (scale %.9f)"), I, Scale), Perturbed[I].Aim.YawDeg, Base[I].Aim.YawDeg, 1e-2);
			Near(*FString::Printf(TEXT("KB seed tile %d pitch stable (scale %.9f)"), I, Scale), Perturbed[I].Aim.PitchDeg, Base[I].Aim.PitchDeg, 1e-2);
		}
	}
	if (TestEqual(TEXT("KB seed off-center split promoted to quadrants"), Base.Num(), 4))
	{
		Near(TEXT("KB seed off-center top yaws near mirror"), Base[0].Aim.YawDeg, -Base[1].Aim.YawDeg, 1.0);
		Near(TEXT("KB seed off-center left pitches near mirror"), Base[0].Aim.PitchDeg, -Base[2].Aim.PitchDeg, 1.0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLensEquidistantTest,
	"Tempo.Sensors.LensModels.Equidistant", TempoLensTestFlags)
bool FTempoLensEquidistantTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = LensTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.8f, got %.8f"), What, B, A));
		}
	};

	const FEquidistantDistortion Eq;

	// Origin maps to origin.
	const FVector2D Origin = Eq.OutputToRender(0.0, 0.0);
	Near(TEXT("Equidistant origin X"), Origin.X, 0.0);
	Near(TEXT("Equidistant origin Y"), Origin.Y, 0.0);

	// Along the horizontal axis (elevation 0): RenderX = tan(azimuth), RenderY = 0.
	const FVector2D Horiz = Eq.OutputToRender(0.2, 0.0);
	Near(TEXT("Equidistant horiz X = tan(az)"), Horiz.X, FMath::Tan(0.2));
	Near(TEXT("Equidistant horiz Y = 0"), Horiz.Y, 0.0);

	// Along the vertical axis (azimuth 0): RenderY = tan(elevation) * sec(0) = tan(elevation).
	const FVector2D Vert = Eq.OutputToRender(0.0, 0.15);
	Near(TEXT("Equidistant vert X = 0"), Vert.X, 0.0);
	Near(TEXT("Equidistant vert Y = tan(el)"), Vert.Y, FMath::Tan(0.15));

	// The render frustum is symmetric about the optical axis.
	const FDistortionRenderConfig Config = Eq.ComputeRenderConfig(FIntPoint(800, 600), 300.0, FVector2D::ZeroVector);
	Near(TEXT("Equidistant TanLeft == -TanRight"), Config.TanLeft, -Config.TanRight);
	Near(TEXT("Equidistant TanTop == -TanBottom"), Config.TanTop, -Config.TanBottom);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoLensDoubleSphereTest,
	"Tempo.Sensors.LensModels.DoubleSphere", TempoLensTestFlags)
bool FTempoLensDoubleSphereTest::RunTest(const FString& Parameters)
{
	auto Near = [this](const TCHAR* What, double A, double B, double Tol = LensTol)
	{
		if (!FMath::IsNearlyEqual(A, B, Tol))
		{
			AddError(FString::Printf(TEXT("%s: expected %.8f, got %.8f"), What, B, A));
		}
	};

	// Xi=0, Alpha=0 reduces Double Sphere to a pinhole projection: (x, y, z) -> (x/z, y/z).
	{
		double Mx = 0.0, My = 0.0;
		const bool bOk = FDoubleSphereDistortion::ProjectRay(0.3, 0.2, 1.0, 0.0, 0.0, Mx, My);
		TestTrue(TEXT("DS pinhole ProjectRay succeeds"), bOk);
		Near(TEXT("DS pinhole Mx = x/z"), Mx, 0.3);
		Near(TEXT("DS pinhole My = y/z"), My, 0.2);
	}

	// RadialProject(0) == 0 for any parameters.
	Near(TEXT("DS RadialProject(0) == 0"), FDoubleSphereDistortion::RadialProject(0.0, -0.2, 0.6), 0.0);

	// Project then unproject returns a ray parallel to the original (closed-form inverse).
	auto CheckRoundTrip = [&](double X, double Y, double Z, double Xi, double Alpha)
	{
		double Mx = 0.0, My = 0.0;
		if (!FDoubleSphereDistortion::ProjectRay(X, Y, Z, Xi, Alpha, Mx, My))
		{
			AddError(FString::Printf(TEXT("DS ProjectRay failed for ray (%.2f,%.2f,%.2f)"), X, Y, Z));
			return;
		}
		double Ux = 0.0, Uy = 0.0, Uz = 0.0;
		if (!FDoubleSphereDistortion::UnprojectPoint(Mx, My, Xi, Alpha, Ux, Uy, Uz))
		{
			AddError(FString::Printf(TEXT("DS UnprojectPoint failed for (%.4f,%.4f)"), Mx, My));
			return;
		}
		// Compare normalized directions.
		const FVector In = FVector(X, Y, Z).GetSafeNormal();
		const FVector Out = FVector(Ux, Uy, Uz).GetSafeNormal();
		if (!In.Equals(Out, LensTol))
		{
			AddError(FString::Printf(TEXT("DS round trip direction mismatch: in %s out %s (Xi=%.2f Alpha=%.2f)"),
				*In.ToString(), *Out.ToString(), Xi, Alpha));
		}
	};

	CheckRoundTrip(0.3, 0.2, 1.0, 0.0, 0.0);   // pinhole-equivalent
	CheckRoundTrip(0.3, 0.1, 1.0, -0.2, 0.6);  // genuine double sphere
	CheckRoundTrip(-0.25, 0.2, 1.0, 0.1, 0.4); // negative azimuth

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
