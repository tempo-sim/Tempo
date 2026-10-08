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
	for (const double Theta : { 0.1, 0.5, 1.0, 1.5 })
	{
		const double ThetaD = FKannalaBrandtDistortion::SolveDistortion(Theta, 0.05, 0.01, 0.0, 0.0);
		double ThetaBack = 0.0;
		TestTrue(*FString::Printf(TEXT("KB inverse in domain at theta=%.2f"), Theta),
			FKannalaBrandtDistortion::SolveInverseDistortion(ThetaD, 0.05, 0.01, 0.0, 0.0, ThetaBack));
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

	struct FVerticalSplitAims
	{
		FFisheyeTileAim Top;
		FFisheyeTileAim Bottom;
	};

	// Top/bottom tile aims for a tall fisheye split vertically, mirroring UTempoCamera::SyncTiles.
	FVerticalSplitAims ComputeVerticalSplitAims(const FTempoLensParameters& Params, const FIntPoint& SizeXY,
		double FOutput, int32 Feather)
	{
		const TUniquePtr<FLensModel> Model = CreateLensModel(Params, 0.0, 0.0);
		const FVector2D OpticalCenter = OpticalCenterPixels(SizeXY, Params.GetClampedPrincipalPoint());
		const double LeftExtent = OpticalCenter.X;
		const double RightExtent = SizeXY.X - OpticalCenter.X;
		const double TopExtent = OpticalCenter.Y;
		const double BottomExtent = SizeXY.Y - OpticalCenter.Y;
		const double FullWidthPCDx = SizeXY.X * 0.5 - OpticalCenter.X;
		const int32 TopHeight = FMath::CeilToInt32(SizeXY.Y / 2.0);
		const int32 TopCoveredH = TopHeight + Feather;
		const int32 BottomCoveredH = SizeXY.Y - TopHeight + Feather;
		const double TopPCDy = TopCoveredH * 0.5 - TopExtent;
		const double BottomPCDy = BottomExtent - BottomCoveredH * 0.5;
		return {
			ComputeFisheyeTileAim(*Model, FOutput, -LeftExtent, +RightExtent, -TopExtent, TopPCDy + TopCoveredH * 0.5, FullWidthPCDx, TopPCDy),
			ComputeFisheyeTileAim(*Model, FOutput, -LeftExtent, +RightExtent, BottomPCDy - BottomCoveredH * 0.5, +BottomExtent, FullWidthPCDx, BottomPCDy)
		};
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
			FKannalaBrandtDistortion::SolveDistortion(Theta, K1, K2, K3, K4), K1, K2, K3, K4, ThetaBack);
		TestTrue(*FString::Printf(TEXT("KB seed inverse in domain at theta=%.2f"), Theta), bOk);
		Near(*FString::Printf(TEXT("KB seed round trip at theta=%.2f"), Theta), ThetaBack, Theta, 1e-6);
	}

	// Beyond the peak there is no physical inverse: the solve reports it, and the distortion map gets
	// the sentinel instead of a spurious root's scene content.
	for (const double ThetaD : { ThetaDMax, 1.70, 1.7239, 2.5 })
	{
		double Theta = 0.0;
		TestFalse(*FString::Printf(TEXT("KB seed inverse rejects theta_d=%.4f"), ThetaD),
			FKannalaBrandtDistortion::SolveInverseDistortion(ThetaD, K1, K2, K3, K4, Theta));
		const FKannalaBrandtDistortion KB(K1, K2, K3, K4, 0.0, 0.0);
		const FVector2D Render = KB.OutputToRender(ThetaD * 0.6, ThetaD * 0.8);
		TestTrue(*FString::Printf(TEXT("KB seed OutputToRender sentinel at theta_d=%.4f"), ThetaD),
			Render.Equals(FVector2D(1e6, 1e6)));
	}

	// cam_f_f at camera_downsample_factor 4: 540x960, 1277.85 px/rad / 4, split top/bottom with the
	// camera's default 16 px feather. Every covered-rect corner lies beyond the peak.
	const FIntPoint SizeXY(540, 960);
	const double FOutput = 1277.85 / 4.0;
	constexpr int32 Feather = 16;

	// Centered optical axis: the tiles mirror each other about the horizon.
	const FVerticalSplitAims Centered = ComputeVerticalSplitAims(Seed, SizeXY, FOutput, Feather);
	Near(TEXT("KB seed top tile yaw"), Centered.Top.YawDeg, 0.0, 1e-9);
	Near(TEXT("KB seed bottom tile yaw"), Centered.Bottom.YawDeg, 0.0, 1e-9);
	Near(TEXT("KB seed tile pitches mirror"), Centered.Top.PitchDeg, -Centered.Bottom.PitchDeg, 1e-9);
	TestTrue(TEXT("KB seed top tile pitches up"), Centered.Top.PitchDeg > 10.0 && Centered.Top.PitchDeg < 45.0);

	// The tiles' frustums stay finite and mirror left/right.
	for (const FFisheyeTileAim& Aim : { Centered.Top, Centered.Bottom })
	{
		const TUniquePtr<FLensModel> TileModel = CreateLensModel(Seed, Aim.YawDeg, Aim.PitchDeg, Aim.AxisShiftXRd, Aim.AxisShiftYRd);
		const FDistortionRenderConfig Config = TileModel->ComputeRenderConfig(
			FIntPoint(SizeXY.X, SizeXY.Y / 2 + Feather), FOutput, FVector2D::ZeroVector);
		Near(TEXT("KB seed tile TanLeft == -TanRight"), Config.TanLeft, -Config.TanRight, 1e-9);
		TestTrue(TEXT("KB seed tile frustum is bounded"), Config.TanRight < 1e3 && Config.TanBottom < 1e3 && Config.TanTop > -1e3);
	}

	// The seed's real principal point; a 1e-9 relative change in FOutput (as float FOV round-off
	// produces) must not move the aims. An unguarded Newton solve at these corners jumps between
	// spurious roots and moves the aims by tens of degrees.
	const FTempoLensParameters SeedOffCenter = MakeSeedFisheyeLens(
		FVector2D((1060.8 - 1080.0) / 2160.0, (1923.54 - 1920.0) / 3840.0));
	const FVerticalSplitAims Base = ComputeVerticalSplitAims(SeedOffCenter, SizeXY, FOutput, Feather);
	for (const double Scale : { 1.0 + 1e-9, 1.0 - 1e-9, 1.0 + 1e-7 })
	{
		const FVerticalSplitAims Perturbed = ComputeVerticalSplitAims(SeedOffCenter, SizeXY, FOutput * Scale, Feather);
		Near(*FString::Printf(TEXT("KB seed top yaw stable (scale %.9f)"), Scale), Perturbed.Top.YawDeg, Base.Top.YawDeg, 1e-4);
		Near(*FString::Printf(TEXT("KB seed top pitch stable (scale %.9f)"), Scale), Perturbed.Top.PitchDeg, Base.Top.PitchDeg, 1e-4);
		Near(*FString::Printf(TEXT("KB seed bottom yaw stable (scale %.9f)"), Scale), Perturbed.Bottom.YawDeg, Base.Bottom.YawDeg, 1e-4);
		Near(*FString::Printf(TEXT("KB seed bottom pitch stable (scale %.9f)"), Scale), Perturbed.Bottom.PitchDeg, Base.Bottom.PitchDeg, 1e-4);
	}
	Near(TEXT("KB seed off-center top yaw near 0"), Base.Top.YawDeg, 0.0, 1.0);
	Near(TEXT("KB seed off-center bottom yaw near 0"), Base.Bottom.YawDeg, 0.0, 1.0);

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
