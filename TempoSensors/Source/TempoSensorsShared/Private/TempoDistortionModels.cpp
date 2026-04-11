// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDistortionModels.h"

// Root-Finding Solver (Newton-Raphson)
static double Solve(const TFunction<double(double)>& Objective, const TFunction<double(double)>& Derivative, const double InitialGuess, const int32 MaxIter, const double Threshold)
{
	double X = InitialGuess;
	for (int I = 0; I < MaxIter; ++I)
	{
		const double FVal = Objective(X);
		if (FMath::Abs(FVal) < Threshold)
		{
			break;
		}
		double Deriv = Derivative(X);
		if (FMath::Abs(Deriv) < 0.001)
		{
			Deriv = (Deriv < 0) ? -0.001 : 0.001;
		}
		X -= FVal / Deriv;
	}
	return X;
}

double FBrownConradyDistortion::SolveInverseDistortion(double OutputRadius, double K1, double K2, double K3)
{
	if (OutputRadius < 1e-6)
	{
		return OutputRadius;
	}

	return Solve(
		[K1, K2, K3, OutputRadius](double R) {
			double R2 = R * R;
			double R4 = R2 * R2;
			double R6 = R4 * R2;
			return (R * (1.0 + K1*R2 + K2*R4 + K3*R6)) - OutputRadius;
		},
		[K1, K2, K3](double R) {
			double R2 = R * R;
			double R4 = R2 * R2;
			double R6 = R4 * R2;
			return 1.0 + 3.0*K1*R2 + 5.0*K2*R4 + 7.0*K3*R6;
		},
		OutputRadius,
		40,
		1e-6
	);
}

FVector2D FBrownConradyDistortion::OutputToRender(double OutputX, double OutputY) const
{
	const double OutputRadius = FMath::Sqrt(OutputX * OutputX + OutputY * OutputY);
	const double RenderRadius = SolveInverseDistortion(OutputRadius, K1, K2, K3);
	const double Scale = (OutputRadius > 1e-6) ? (RenderRadius / OutputRadius) : 1.0;
	return FVector2D(OutputX * Scale, OutputY * Scale);
}

double FBrownConradyDistortion::SolveDistortion(double RenderRadius, double K1, double K2, double K3)
{
	double R2 = RenderRadius * RenderRadius;
	double R4 = R2 * R2;
	double R6 = R4 * R2;
	return RenderRadius * (1.0 + K1*R2 + K2*R4 + K3*R6);
}

double FBrownConradyDistortion::ComputeMaxOutputRadius(double K1, double K2, double K3)
{
	if (K1 >= 0.0)
	{
		return -1.0;
	}

	if (FMath::IsNearlyZero(K2) && FMath::IsNearlyZero(K3))
	{
		const double RCrit = FMath::Sqrt(-1.0 / (3.0 * K1));
		const double RCrit2 = RCrit * RCrit;
		return RCrit * (1.0 + K1 * RCrit2);
	}

	// Find the critical radius where the derivative of the distortion function is zero.
	const double RCrit = Solve(
		[K1, K2, K3](double R) {
			double R2 = R * R;
			double R4 = R2 * R2;
			double R6 = R4 * R2;
			return 1.0 + 3.0*K1*R2 + 5.0*K2*R4 + 7.0*K3*R6;
		},
		[K1, K2, K3](double R) {
			double R2 = R * R;
			double R3 = R2 * R;
			double R5 = R3 * R2;
			return 6.0*K1*R + 20.0*K2*R3 + 42.0*K3*R5;
		},
		0.5, 40, 1e-6
	);
	const double R2 = RCrit * RCrit;
	const double R4 = R2 * R2;
	const double R6 = R4 * R2;
	const double Scale = 1.0 + K1*R2 + K2*R4 + K3*R6;
	return RCrit * Scale;
}

FDistortionRenderConfig FBrownConradyDistortion::ComputeRenderConfig(const FIntPoint& OutputSizeXY, float OutputHFOV) const
{
	FDistortionRenderConfig Config;

	// Feasibility check for barrel distortion
	const double MaxOutputRadius = ComputeMaxOutputRadius(K1, K2, K3);
	if (MaxOutputRadius > 0.0 && OutputHFOV > 0.0f)
	{
		const double MaxPossibleFOV = FMath::RadiansToDegrees(FMath::Atan(MaxOutputRadius)) * 2.0;
		ensureMsgf(OutputHFOV <= MaxPossibleFOV, TEXT("HorizontalFOV %.2f exceeds limit %.2f for K1=%.3f. Artifacts expected."), OutputHFOV, MaxPossibleFOV, K1);
	}

	const double AspectRatio = static_cast<double>(OutputSizeXY.Y) / static_cast<double>(OutputSizeXY.X);
	const double HalfFOVRad = FMath::DegreesToRadians(OutputHFOV / 2.0);
	const double OutputRadiusHoriz = FMath::Tan(HalfFOVRad);
	const double OutputRadiusVert = AspectRatio * OutputRadiusHoriz;

	// Compute the render (undistorted) radii needed to cover the output (distorted) extents.
	const double RenderRadiusHoriz = SolveDistortion(OutputRadiusHoriz, K1, K2, K3);
	const double RenderRadiusVert = SolveDistortion(OutputRadiusVert, K1, K2, K3);
	const double RenderAspectRatio = RenderRadiusVert / RenderRadiusHoriz;

	// Render size: X matches output, Y adjusted for the render aspect ratio.
	Config.RenderSizeXY.X = OutputSizeXY.X;
	Config.RenderSizeXY.Y = FMath::RoundToInt32(RenderAspectRatio * OutputSizeXY.X);

	// Output focal lengths (the distorted image has square pixels).
	Config.FxOutput = (OutputSizeXY.X / 2.0) / OutputRadiusHoriz;
	Config.FyOutput = Config.FxOutput;

	// Compute render FOV: must cover both the direct render extent and the inverse-mapped diagonal.
	const double RenderRadiusDiag = FMath::Sqrt(RenderRadiusHoriz * RenderRadiusHoriz + RenderRadiusVert * RenderRadiusVert);
	const double EndRadiusDiag = SolveInverseDistortion(RenderRadiusDiag, K1, K2, K3);
	const double EndRadiusHoriz = EndRadiusDiag / FMath::Sqrt(1.0 + RenderAspectRatio * RenderAspectRatio);
	Config.RenderFOVAngle = FMath::Clamp(
		FMath::Max(
			FMath::RadiansToDegrees(FMath::Atan(RenderRadiusHoriz) * 2.0),
			FMath::RadiansToDegrees(FMath::Atan(EndRadiusHoriz) * 2.0)),
		1.0f, 170.0f);

	// Render focal lengths (perspective projection has square pixels).
	Config.FxRender = (Config.RenderSizeXY.X / 2.0) / FMath::Tan(FMath::DegreesToRadians(Config.RenderFOVAngle / 2.0));
	Config.FyRender = Config.FxRender;

	return Config;
}

FDistortionRenderConfig FEquidistantDistortion::ComputeRenderConfig(const FIntPoint& OutputSizeXY, float OutputHFOV) const
{
	// The Lidar equidistant model does not use distortion maps for camera rendering.
	// Provide a basic config that matches the output directly.
	FDistortionRenderConfig Config;
	Config.RenderSizeXY = OutputSizeXY;
	const double HalfFOVRad = FMath::DegreesToRadians(OutputHFOV / 2.0);
	Config.RenderFOVAngle = FMath::Clamp(OutputHFOV, 1.0f, 170.0f);
	const double F = OutputSizeXY.X / (2.0 * FMath::Tan(HalfFOVRad));
	Config.FxOutput = F;
	Config.FyOutput = F;
	Config.FxRender = F;
	Config.FyRender = F;
	return Config;
}

FVector2D FEquidistantDistortion::OutputToRender(double OutputX, double OutputY) const
{
	// OutputX = azimuth in radians, OutputY = elevation in radians
	// (when normalized by equidistant focal length, pixel position is linear in angle)
	//
	// Maps to perspective render coordinates:
	//   RenderX = tan(azimuth)
	//   RenderY = tan(elevation) * sec(azimuth)
	//
	// This accounts for the coupling between azimuth and elevation in perspective projection:
	// a point at (azimuth, elevation) projects to the render image plane at
	// (tan(azimuth), tan(elevation) * sqrt(tan(azimuth)^2 + 1)).
	const double TanAzimuth = FMath::Tan(OutputX);
	const double TanElevation = FMath::Tan(OutputY);
	return FVector2D(TanAzimuth, TanElevation * FMath::Sqrt(TanAzimuth * TanAzimuth + 1.0));
}

FVector2D FEquidistantTileDistortion::OutputToRender(double OutputX, double OutputY) const
{
	// OutputX/OutputY are equidistant angles (radians) from this tile's center.
	// Convert to global equidistant angles relative to parent camera's optical axis.
	const double GlobalH = OutputX + AzimuthOffset;
	const double GlobalV = OutputY + ElevationOffset;

	// Radial angle from parent's optical axis (equidistant: r_image = f * theta).
	const double Theta = FMath::Sqrt(GlobalH * GlobalH + GlobalV * GlobalV);

	// Compute 3D ray direction in parent frame.
	// Coordinate system: X=right, Y=down, Z=forward.
	double RayX, RayY, RayZ;
	if (Theta < 1e-10)
	{
		RayX = 0.0;
		RayY = 0.0;
		RayZ = 1.0;
	}
	else
	{
		const double SincTheta = FMath::Sin(Theta) / Theta;
		RayX = SincTheta * GlobalH;
		RayY = SincTheta * GlobalV;
		RayZ = FMath::Cos(Theta);
	}

	// Rotate from parent frame to child frame: R_X(ElevationOffset) * R_Y(-AzimuthOffset).
	const double CosAz = FMath::Cos(AzimuthOffset);
	const double SinAz = FMath::Sin(AzimuthOffset);
	const double CosEl = FMath::Cos(ElevationOffset);
	const double SinEl = FMath::Sin(ElevationOffset);

	// Apply R_Y(-AzimuthOffset):
	const double Rx1 = CosAz * RayX - SinAz * RayZ;
	const double Ry1 = RayY;
	const double Rz1 = SinAz * RayX + CosAz * RayZ;

	// Apply R_X(ElevationOffset):
	const double ChildX = Rx1;
	const double ChildY = CosEl * Ry1 - SinEl * Rz1;
	const double ChildZ = SinEl * Ry1 + CosEl * Rz1;

	// Perspective projection in child frame.
	if (ChildZ <= 1e-10)
	{
		return FVector2D(1e6, 1e6);
	}

	return FVector2D(ChildX / ChildZ, ChildY / ChildZ);
}

FDistortionRenderConfig FEquidistantTileDistortion::ComputeRenderConfig(const FIntPoint& OutputSizeXY, float OutputHFOV) const
{
	FDistortionRenderConfig Config;

	const double TileHFOVRad = FMath::DegreesToRadians(OutputHFOV);
	const double TileHHalfRad = TileHFOVRad / 2.0;
	const double TileVHalfRad = TileHHalfRad * OutputSizeXY.Y / OutputSizeXY.X;

	// Sample corners and edge midpoints to find the max perspective extent needed.
	double MaxTanH = 0.0;
	double MaxTanV = 0.0;
	const double SX[] = {-TileHHalfRad, TileHHalfRad, -TileHHalfRad, TileHHalfRad, 0.0, 0.0, -TileHHalfRad, TileHHalfRad};
	const double SY[] = {-TileVHalfRad, -TileVHalfRad, TileVHalfRad, TileVHalfRad, -TileVHalfRad, TileVHalfRad, 0.0, 0.0};
	for (int32 I = 0; I < 8; ++I)
	{
		const FVector2D Render = OutputToRender(SX[I], SY[I]);
		MaxTanH = FMath::Max(MaxTanH, FMath::Abs(Render.X));
		MaxTanV = FMath::Max(MaxTanV, FMath::Abs(Render.Y));
	}

	// Render FOV must cover the max perspective extent.
	// FOVAngle is horizontal. Need: tan(hHalf) >= MaxTanH and tan(hHalf) * tileH/tileW >= MaxTanV.
	const double OutputAspectRatio = (OutputSizeXY.Y > 0)
		? static_cast<double>(OutputSizeXY.X) / static_cast<double>(OutputSizeXY.Y)
		: 1.0;
	const double RequiredTanHHalf = FMath::Max(MaxTanH, MaxTanV * OutputAspectRatio);
	Config.RenderFOVAngle = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(RequiredTanHHalf)) * 2.0, 1.0, 170.0);

	// For equidistant tiles, render size matches output size (FOV expansion is handled by RenderFOVAngle).
	Config.RenderSizeXY = OutputSizeXY;

	// Output (equidistant) focal length: pixels per radian.
	Config.FxOutput = static_cast<double>(OutputSizeXY.X) / TileHFOVRad;
	Config.FyOutput = Config.FxOutput;

	// Render (perspective) focal length.
	const double HalfRenderFOVRad = FMath::DegreesToRadians(Config.RenderFOVAngle / 2.0);
	Config.FxRender = OutputSizeXY.X / (2.0 * FMath::Tan(HalfRenderFOVRad));
	Config.FyRender = Config.FxRender;

	return Config;
}
