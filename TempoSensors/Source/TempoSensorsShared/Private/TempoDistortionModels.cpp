// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDistortionModels.h"

#include "TempoSensorsShared.h"

TUniquePtr<FDistortionModel> CreateDistortionModel(const FTempoLensDistortionParameters& LensParameters, double YawDegrees, double PitchDegrees)
{
	if (LensParameters.DistortionModel == ETempoDistortionModel::BrownConrady)
	{
		return MakeUnique<FBrownConradyDistortion>(
			LensParameters.K1,
			LensParameters.K2,
			LensParameters.K3);
	}
	else if (LensParameters.DistortionModel == ETempoDistortionModel::Rational)
	{
		return MakeUnique<FRationalDistortion>(
			LensParameters.K1,
			LensParameters.K2,
			LensParameters.K3,
			LensParameters.K4,
			LensParameters.K5,
			LensParameters.K6);
	}
	else if (LensParameters.DistortionModel == ETempoDistortionModel::DoubleSphere)
	{
		// Negate pitch: UE positive pitch = up, but image-plane positive Y = down.
		const double AzOffset = FMath::DegreesToRadians(YawDegrees);
		const double ElOffset = -FMath::DegreesToRadians(PitchDegrees);
		return MakeUnique<FDoubleSphereDistortion>(
			LensParameters.Xi,
			LensParameters.Alpha,
			AzOffset, ElOffset);
	}
	else // KannalaBrandt (Equidistant fisheye; K=0 = pure equidistant)
	{
		// Negate pitch: UE positive pitch = up, but image-plane positive Y = down.
		const double AzOffset = FMath::DegreesToRadians(YawDegrees);
		const double ElOffset = -FMath::DegreesToRadians(PitchDegrees);
		return MakeUnique<FKannalaBrandtDistortion>(
			LensParameters.K1,
			LensParameters.K2,
			LensParameters.K3,
			LensParameters.K4,
			AzOffset, ElOffset);
	}
}

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

double FRadialDistortionBase::ComputeFOutputForFullImage(const FIntPoint& FullImageSizeXY, double FullImageHFOVDeg) const
{
	if (FullImageSizeXY.X <= 0 || FullImageHFOVDeg <= 0.0)
	{
		return -1.0;
	}
	const double HalfHFOVRad = FMath::DegreesToRadians(FullImageHFOVDeg / 2.0);
	const double OutputHorizRadius = Distort(FMath::Tan(HalfHFOVRad));
	if (OutputHorizRadius <= 0.0)
	{
		return -1.0;
	}
	return (FullImageSizeXY.X / 2.0) / OutputHorizRadius;
}

FDistortionRenderConfig FRadialDistortionBase::ComputeRenderConfig(const FIntPoint& OutputSizeXY, double FOutput) const
{
	FDistortionRenderConfig Config;
	Config.RenderSizeXY = OutputSizeXY;
	Config.FOutput = FOutput;

	// Tile-local r_d half-extent at the horizontal edge.
	const double OutputHorizRadius = (OutputSizeXY.X / 2.0) / FOutput;
	const double AspectRatio = static_cast<double>(OutputSizeXY.X) / static_cast<double>(OutputSizeXY.Y);

	// Feasibility check: warn if the requested FOV exceeds what the barrel distortion can represent.
	const double MaxOutputRad = GetMaxOutputRadius();
	if (MaxOutputRad > 0.0 && OutputHorizRadius > MaxOutputRad)
	{
		const double RequestedHFOVDeg = FMath::RadiansToDegrees(FMath::Atan(OutputHorizRadius)) * 2.0;
		const double MaxPossibleFOV = FMath::RadiansToDegrees(FMath::Atan(MaxOutputRad)) * 2.0;
		UE_LOG(LogTempoSensorsShared, Warning, TEXT("HorizontalFOV %.2f exceeds limit %.2f for %s model. Artifacts expected."), RequestedHFOVDeg, MaxPossibleFOV, GetModelName());
	}

	if (IsBarrel()) // Barrel - diagonal is the limiting factor
	{
		const double OutputVertRadius = OutputHorizRadius / AspectRatio;
		const double OutputDiagRadius = FMath::Sqrt(OutputHorizRadius * OutputHorizRadius + OutputVertRadius * OutputVertRadius);
		const double RenderDiagRadius = Undistort(OutputDiagRadius);
		const double RenderHorizRadius = RenderDiagRadius / FMath::Sqrt(1.0 + 1.0 / (AspectRatio * AspectRatio));
		Config.RenderFOVAngle = 2.0 * FMath::RadiansToDegrees(FMath::Atan(RenderHorizRadius));
	}
	else // Pincushion - horizontal/vertical is the limiting factor
	{
		Config.RenderFOVAngle = 2.0 * FMath::RadiansToDegrees(FMath::Atan(OutputHorizRadius));
	}

	Config.FRender = (Config.RenderSizeXY.X / 2.0) / FMath::Tan(FMath::DegreesToRadians(Config.RenderFOVAngle / 2.0));

	return Config;
}

FDistortionRenderConfig FEquidistantDistortion::ComputeRenderConfig(const FIntPoint& OutputSizeXY, double FOutput) const
{
	// FOutput here is pixels per radian of physical angle (axis-separable equidistant).
	FDistortionRenderConfig Config;
	Config.RenderSizeXY = OutputSizeXY;
	Config.FOutput = FOutput;
	const double HFOVRad = OutputSizeXY.X / FOutput;
	Config.RenderFOVAngle = FMath::Clamp(static_cast<float>(FMath::RadiansToDegrees(HFOVRad)), 1.0f, 170.0f);
	Config.FRender = OutputSizeXY.X / (2.0 * FMath::Tan(FMath::DegreesToRadians(Config.RenderFOVAngle) / 2.0));
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

double FKannalaBrandtDistortion::SolveDistortion(double Theta, double K1, double K2, double K3, double K4)
{
	const double T2 = Theta * Theta;
	const double T4 = T2 * T2;
	const double T6 = T4 * T2;
	const double T8 = T4 * T4;
	return Theta * (1.0 + K1*T2 + K2*T4 + K3*T6 + K4*T8);
}

double FKannalaBrandtDistortion::SolveInverseDistortion(double ThetaD, double K1, double K2, double K3, double K4)
{
	if (ThetaD < 1e-6)
	{
		return ThetaD;
	}

	return Solve(
		[K1, K2, K3, K4, ThetaD](double T) {
			const double T2 = T * T;
			const double T4 = T2 * T2;
			const double T6 = T4 * T2;
			const double T8 = T4 * T4;
			return T * (1.0 + K1*T2 + K2*T4 + K3*T6 + K4*T8) - ThetaD;
		},
		[K1, K2, K3, K4](double T) {
			const double T2 = T * T;
			const double T4 = T2 * T2;
			const double T6 = T4 * T2;
			const double T8 = T4 * T4;
			return 1.0 + 3.0*K1*T2 + 5.0*K2*T4 + 7.0*K3*T6 + 9.0*K4*T8;
		},
		ThetaD,
		40,
		1e-6
	);
}

double FKannalaBrandtDistortion::ComputeFOutputForFullImage(const FIntPoint& FullImageSizeXY, double FullImageHFOVDeg) const
{
	// FOutput = pixels per radian of theta_d. The full image's theta_d at the right edge equals
	// FullImageHFOVDeg/2 (in radians) — by the convention that the full-image HFOV specifies
	// theta_d at the edge (equals physical FOV when K1=K2=K3=K4=0).
	if (FullImageSizeXY.X <= 0 || FullImageHFOVDeg <= 0.0)
	{
		return -1.0;
	}
	const double HalfHFOVRad = FMath::DegreesToRadians(FullImageHFOVDeg / 2.0);
	return (FullImageSizeXY.X / 2.0) / HalfHFOVRad;
}

void FKannalaBrandtDistortion::PixelOffsetToYawPitchDeg(double DxPixels, double DyPixels, double FOutput,
	double& OutYawDeg, double& OutPitchDeg) const
{
	OutYawDeg = 0.0;
	OutPitchDeg = 0.0;
	if (FOutput <= 0.0)
	{
		return;
	}
	// theta_d = pixel_offset / FOutput. The K-B output plane is treated axis-separably here
	// (yaw from Dx alone, pitch from Dy alone): exact when K1=K2=K3=K4=0 (pure equidistant) and
	// the existing OutputToRender's small-angle approximation cancels with this exactly even at
	// diagonal tile centers. Image-Y grows downward, so positive DyPixels maps to NEGATIVE UE
	// pitch (UE convention: positive pitch = up).
	OutYawDeg = FMath::RadiansToDegrees(DxPixels / FOutput);
	OutPitchDeg = -FMath::RadiansToDegrees(DyPixels / FOutput);
}

FVector2D FKannalaBrandtDistortion::OutputToRender(double OutputX, double OutputY) const
{
	// OutputX/OutputY are tile-local coordinates in distorted-angle (theta_d) units from
	// this tile's center. Convert to parent-frame distorted-output coordinates by adding
	// the tile center's position in the parent's output image plane.
	//
	// The tile center sits on a ray at physical angle theta_axis from the parent's optical
	// axis. Its position in the parent's (distorted) output plane has radius theta_d_axis
	// and the same azimuthal direction as (AzimuthOffset, ElevationOffset).
	const double ThetaAxis = FMath::Sqrt(AzimuthOffset * AzimuthOffset + ElevationOffset * ElevationOffset);
	double TileCenterX = 0.0;
	double TileCenterY = 0.0;
	if (ThetaAxis >= 1e-10)
	{
		const double ThetaDAxis = SolveDistortion(ThetaAxis, K1, K2, K3, K4);
		TileCenterX = ThetaDAxis * AzimuthOffset / ThetaAxis;
		TileCenterY = ThetaDAxis * ElevationOffset / ThetaAxis;
	}

	const double ParentOutputX = TileCenterX + OutputX;
	const double ParentOutputY = TileCenterY + OutputY;

	// Invert K-B to recover the physical radial angle from the parent's optical axis.
	const double ThetaD = FMath::Sqrt(ParentOutputX * ParentOutputX + ParentOutputY * ParentOutputY);
	const double Theta = SolveInverseDistortion(ThetaD, K1, K2, K3, K4);

	// Compute 3D ray direction in parent frame.
	// Coordinate system: X=right, Y=down, Z=forward.
	double RayX, RayY, RayZ;
	if (ThetaD < 1e-10)
	{
		RayX = 0.0;
		RayY = 0.0;
		RayZ = 1.0;
	}
	else
	{
		const double SinTheta = FMath::Sin(Theta);
		RayX = SinTheta * ParentOutputX / ThetaD;
		RayY = SinTheta * ParentOutputY / ThetaD;
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

double FRationalDistortion::SolveDistortion(double R, double K1, double K2, double K3, double K4, double K5, double K6)
{
	const double R2 = R * R;
	const double R4 = R2 * R2;
	const double R6 = R4 * R2;
	const double Num = 1.0 + K1*R2 + K2*R4 + K3*R6;
	const double Den = 1.0 + K4*R2 + K5*R4 + K6*R6;
	return R * Num / Den;
}

double FRationalDistortion::SolveInverseDistortion(double Rd, double K1, double K2, double K3, double K4, double K5, double K6)
{
	if (Rd < 1e-6)
	{
		return Rd;
	}

	return Solve(
		[K1, K2, K3, K4, K5, K6, Rd](double R) {
			const double R2 = R * R;
			const double R4 = R2 * R2;
			const double R6 = R4 * R2;
			const double Num = 1.0 + K1*R2 + K2*R4 + K3*R6;
			const double Den = 1.0 + K4*R2 + K5*R4 + K6*R6;
			return R * Num / Den - Rd;
		},
		[K1, K2, K3, K4, K5, K6](double R) {
			const double R2 = R * R;
			const double R4 = R2 * R2;
			const double R6 = R4 * R2;
			const double Num  = 1.0 + K1*R2  + K2*R4  + K3*R6;
			const double Den  = 1.0 + K4*R2  + K5*R4  + K6*R6;
			const double DNum = 2.0*R*(K1 + 2.0*K2*R2 + 3.0*K3*R4); // d(Num)/dR
			const double DDen = 2.0*R*(K4 + 2.0*K5*R2 + 3.0*K6*R4); // d(Den)/dR
			// Quotient rule: d/dR [R*Num/Den] = (Num*Den + R*DNum*Den - R*Num*DDen) / Den^2
			return (Num*Den + R*DNum*Den - R*Num*DDen) / (Den * Den);
		},
		Rd,
		40,
		1e-6
	);
}

double FRationalDistortion::ComputeMaxOutputRadius(double K1, double K2, double K3, double K4, double K5, double K6)
{
	// Only barrel-like models (K1-K4 < 0) have a finite maximum output radius.
	if ((K1 - K4) >= 0.0)
	{
		return -1.0;
	}

	// Find the critical radius where d(r_d)/dR = 0:
	//   Num*Den + R*DNum*Den - R*Num*DDen = 0
	// Use Newton-Raphson on the derivative expression, starting near 0.5.
	const double RCrit = Solve(
		[K1, K2, K3, K4, K5, K6](double R) {
			const double R2 = R * R;
			const double R4 = R2 * R2;
			const double R6 = R4 * R2;
			const double Num  = 1.0 + K1*R2  + K2*R4  + K3*R6;
			const double Den  = 1.0 + K4*R2  + K5*R4  + K6*R6;
			const double DNum = 2.0*R*(K1 + 2.0*K2*R2 + 3.0*K3*R4);
			const double DDen = 2.0*R*(K4 + 2.0*K5*R2 + 3.0*K6*R4);
			return Num*Den + R*DNum*Den - R*Num*DDen;
		},
		[K1, K2, K3, K4, K5, K6](double R) {
			const double R2 = R * R;
			const double R4 = R2 * R2;
			const double R6 = R4 * R2;
			const double Num   = 1.0 + K1*R2  + K2*R4  + K3*R6;
			const double Den   = 1.0 + K4*R2  + K5*R4  + K6*R6;
			const double DNum  = 2.0*R*(K1 + 2.0*K2*R2 + 3.0*K3*R4);
			const double DDen  = 2.0*R*(K4 + 2.0*K5*R2 + 3.0*K6*R4);
			const double D2Num = 2.0*(K1 + 6.0*K2*R2 + 15.0*K3*R4);  // d^2(Num)/dR^2 / (noting chain rule absorbed)
			const double D2Den = 2.0*(K4 + 6.0*K5*R2 + 15.0*K6*R4);
			// d/dR [Num*Den + R*DNum*Den - R*Num*DDen]
			return DNum*Den + Num*DDen
				 + Den*(DNum + R*D2Num) + R*DNum*DDen
				 - DDen*(Num + R*DNum) - R*Num*D2Den;
		},
		0.5,
		40,
		1e-6
	);

	if (RCrit <= 0.0)
	{
		return -1.0;
	}

	return SolveDistortion(RCrit, K1, K2, K3, K4, K5, K6);
}

FVector2D FRationalDistortion::OutputToRender(double OutputX, double OutputY) const
{
	const double OutputRadius = FMath::Sqrt(OutputX * OutputX + OutputY * OutputY);
	const double RenderRadius = SolveInverseDistortion(OutputRadius, K1, K2, K3, K4, K5, K6);
	const double Scale = (OutputRadius > 1e-6) ? (RenderRadius / OutputRadius) : 1.0;
	return FVector2D(OutputX * Scale, OutputY * Scale);
}

FDistortionRenderConfig FKannalaBrandtDistortion::ComputeRenderConfig(const FIntPoint& OutputSizeXY, double FOutput) const
{
	FDistortionRenderConfig Config;
	Config.RenderSizeXY = OutputSizeXY;
	Config.FOutput = FOutput;

	// For K-B output, "radius" is a distorted angle (theta_d, radians):
	// pixel position = FOutput * theta_d. The tile's horizontal/vertical theta_d half-extents
	// are derived from its pixel size and the global FOutput.
	const double AspectRatio = static_cast<double>(OutputSizeXY.X) / static_cast<double>(OutputSizeXY.Y);
	const double OutputHorizRadius = (OutputSizeXY.X / 2.0) / FOutput;
	const double OutputVertRadius = (OutputSizeXY.Y / 2.0) / FOutput;

	// The K-B->perspective mapping isn't radial in tile space (azimuth/elevation coupling
	// plus radial distortion), so sample corners and edge midpoints to find max render extent.
	double MaxRenderHoriz = 0.0;
	double MaxRenderVert = 0.0;
	const double SX[] = {-OutputHorizRadius, OutputHorizRadius, -OutputHorizRadius, OutputHorizRadius, 0.0, 0.0, -OutputHorizRadius, OutputHorizRadius};
	const double SY[] = {-OutputVertRadius, -OutputVertRadius, OutputVertRadius, OutputVertRadius, -OutputVertRadius, OutputVertRadius, 0.0, 0.0};
	for (int32 I = 0; I < 8; ++I)
	{
		const FVector2D Render = OutputToRender(SX[I], SY[I]);
		MaxRenderHoriz = FMath::Max(MaxRenderHoriz, FMath::Abs(Render.X));
		MaxRenderVert = FMath::Max(MaxRenderVert, FMath::Abs(Render.Y));
	}

	// RenderFOVAngle is horizontal; UE derives vertical FOV from aspect ratio.
	// Need: tan(RFOV/2) >= MaxRenderHoriz AND tan(RFOV/2) / AspectRatio >= MaxRenderVert.
	const double RequiredRenderHorizRadius = FMath::Max(MaxRenderHoriz, MaxRenderVert * AspectRatio);
	Config.RenderFOVAngle = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(RequiredRenderHorizRadius)) * 2.0, 1.0, 170.0);

	Config.FRender = (OutputSizeXY.X / 2.0) / FMath::Tan(FMath::DegreesToRadians(Config.RenderFOVAngle / 2.0));

	return Config;
}

// ----------------------------------------------------------------------------------------
// Double Sphere
// ----------------------------------------------------------------------------------------

double FDoubleSphereDistortion::RadialProject(double Theta, double Xi, double Alpha)
{
	const double SinT = FMath::Sin(Theta);
	const double CosT = FMath::Cos(Theta);
	const double D2 = FMath::Sqrt(1.0 + 2.0 * Xi * CosT + Xi * Xi);
	const double Denom = Alpha * D2 + (1.0 - Alpha) * (Xi + CosT);
	if (FMath::Abs(Denom) < 1e-12)
	{
		return 1e6;
	}
	return SinT / Denom;
}

bool FDoubleSphereDistortion::ProjectRay(double X, double Y, double Z, double Xi, double Alpha, double& OutMx, double& OutMy)
{
	const double D1 = FMath::Sqrt(X*X + Y*Y + Z*Z);
	if (D1 < 1e-12)
	{
		OutMx = 0.0;
		OutMy = 0.0;
		return false;
	}
	const double Inner = Xi * D1 + Z;
	const double D2 = FMath::Sqrt(X*X + Y*Y + Inner * Inner);
	const double Denom = Alpha * D2 + (1.0 - Alpha) * Inner;
	if (Denom <= 1e-12)
	{
		// Outside the forward-projection validity region (point behind/beyond the model's horizon).
		OutMx = 0.0;
		OutMy = 0.0;
		return false;
	}
	OutMx = X / Denom;
	OutMy = Y / Denom;
	return true;
}

bool FDoubleSphereDistortion::UnprojectPoint(double Mx, double My, double Xi, double Alpha,
	double& OutX, double& OutY, double& OutZ)
{
	const double R2 = Mx * Mx + My * My;

	// When Alpha > 0.5, unprojection is only defined for r^2 <= 1 / (2*Alpha - 1).
	if (Alpha > 0.5)
	{
		const double MaxR2 = 1.0 / (2.0 * Alpha - 1.0);
		if (R2 > MaxR2)
		{
			OutX = 0.0; OutY = 0.0; OutZ = 1.0;
			return false;
		}
	}

	const double Inner = 1.0 - (2.0 * Alpha - 1.0) * R2;
	if (Inner < 0.0)
	{
		OutX = 0.0; OutY = 0.0; OutZ = 1.0;
		return false;
	}
	const double SqrtInner = FMath::Sqrt(Inner);
	const double DenomMz = Alpha * SqrtInner + (1.0 - Alpha);
	if (FMath::Abs(DenomMz) < 1e-12)
	{
		OutX = 0.0; OutY = 0.0; OutZ = 1.0;
		return false;
	}
	const double Mz = (1.0 - Alpha * Alpha * R2) / DenomMz;

	const double InnerBeta = Mz * Mz + (1.0 - Xi * Xi) * R2;
	if (InnerBeta < 0.0)
	{
		OutX = 0.0; OutY = 0.0; OutZ = 1.0;
		return false;
	}
	const double DenomBeta = Mz * Mz + R2;
	if (DenomBeta < 1e-20)
	{
		// Optical axis (Mx=My=0): the ray is straight along Z (+1 in our convention).
		OutX = 0.0; OutY = 0.0; OutZ = 1.0;
		return true;
	}
	const double Beta = (Mz * Xi + FMath::Sqrt(InnerBeta)) / DenomBeta;

	OutX = Beta * Mx;
	OutY = Beta * My;
	OutZ = Beta * Mz - Xi;
	return true;
}

double FDoubleSphereDistortion::ComputeFOutputForFullImage(const FIntPoint& FullImageSizeXY, double FullImageHFOVDeg) const
{
	if (FullImageSizeXY.X <= 0 || FullImageHFOVDeg <= 0.0)
	{
		return -1.0;
	}
	const double HalfHFOVRad = FMath::DegreesToRadians(FullImageHFOVDeg / 2.0);
	const double Rd = RadialProject(HalfHFOVRad, Xi, Alpha);
	if (Rd <= 0.0)
	{
		return -1.0;
	}
	return (FullImageSizeXY.X / 2.0) / Rd;
}

void FDoubleSphereDistortion::PixelOffsetToYawPitchDeg(double DxPixels, double DyPixels, double FOutput,
	double& OutYawDeg, double& OutPitchDeg) const
{
	OutYawDeg = 0.0;
	OutPitchDeg = 0.0;
	if (FOutput <= 0.0)
	{
		return;
	}
	// Joint 2D unprojection: find the single 3D ray that DS forward-projects to (Mx, My), then
	// decompose into (yaw, pitch_UE). Doing this 1D-per-axis (treating Dy=0 for yaw and Dx=0 for
	// pitch) yields independent angles whose combined 3D direction projects to a different point
	// than (Mx, My), leaving multi-degree gaps at diagonal tile seams.
	const double Mx = DxPixels / FOutput;
	const double My = DyPixels / FOutput;
	double X, Y, Z;
	if (!UnprojectPoint(Mx, My, Xi, Alpha, X, Y, Z))
	{
		return;
	}
	// (X, Y, Z) = R_Y(yaw) * R_X(-pitch_UE) * (0, 0, 1)
	//           = (sin(yaw)*cos(pitch_UE), -sin(pitch_UE), cos(yaw)*cos(pitch_UE))
	const double CosPitchUE = FMath::Sqrt(X * X + Z * Z);
	OutPitchDeg = FMath::RadiansToDegrees(FMath::Atan2(-Y, CosPitchUE));
	OutYawDeg = FMath::RadiansToDegrees(FMath::Atan2(X, Z));
}

FVector2D FDoubleSphereDistortion::OutputToRender(double OutputX, double OutputY) const
{
	// Tile-local r_d coordinates. Shift by the tile center's position in the parent's output
	// plane to get parent-frame normalized image-plane coordinates.
	//
	// The tile's optical axis in the PARENT frame is the result of applying R_Y(Az) * R_X(-El)
	// (child→parent) to (0, 0, 1). Forward-project that 3D direction through the parent's DS
	// to find its r_d position. The small-angle approximation
	// theta ≈ sqrt(Az² + El²), dir ≈ (Az, El)/sqrt(...) is incorrect at diagonal tiles —
	// the actual 3D axis differs by several degrees and projects to a different r_d.
	double TileCenterX = 0.0;
	double TileCenterY = 0.0;
	if (FMath::Abs(AzimuthOffset) > 1e-10 || FMath::Abs(ElevationOffset) > 1e-10)
	{
		const double CosAz = FMath::Cos(AzimuthOffset);
		const double SinAz = FMath::Sin(AzimuthOffset);
		const double CosEl = FMath::Cos(ElevationOffset);
		const double SinEl = FMath::Sin(ElevationOffset);
		const double AxisX = SinAz * CosEl;
		const double AxisY = SinEl;
		const double AxisZ = CosAz * CosEl;
		if (!ProjectRay(AxisX, AxisY, AxisZ, Xi, Alpha, TileCenterX, TileCenterY))
		{
			TileCenterX = 0.0;
			TileCenterY = 0.0;
		}
	}

	const double ParentMx = TileCenterX + OutputX;
	const double ParentMy = TileCenterY + OutputY;

	// Closed-form unprojection to a 3D ray in parent frame.
	double RayX, RayY, RayZ;
	if (!UnprojectPoint(ParentMx, ParentMy, Xi, Alpha, RayX, RayY, RayZ))
	{
		return FVector2D(1e6, 1e6);
	}

	// Rotate from parent frame to child frame: R_X(ElevationOffset) * R_Y(-AzimuthOffset).
	const double CosAz = FMath::Cos(AzimuthOffset);
	const double SinAz = FMath::Sin(AzimuthOffset);
	const double CosEl = FMath::Cos(ElevationOffset);
	const double SinEl = FMath::Sin(ElevationOffset);

	const double Rx1 = CosAz * RayX - SinAz * RayZ;
	const double Ry1 = RayY;
	const double Rz1 = SinAz * RayX + CosAz * RayZ;

	const double ChildX = Rx1;
	const double ChildY = CosEl * Ry1 - SinEl * Rz1;
	const double ChildZ = SinEl * Ry1 + CosEl * Rz1;

	if (ChildZ <= 1e-10)
	{
		return FVector2D(1e6, 1e6);
	}

	return FVector2D(ChildX / ChildZ, ChildY / ChildZ);
}

FDistortionRenderConfig FDoubleSphereDistortion::ComputeRenderConfig(const FIntPoint& OutputSizeXY, double FOutput) const
{
	FDistortionRenderConfig Config;
	Config.RenderSizeXY = OutputSizeXY;
	Config.FOutput = FOutput;

	// Tile-local r_d half-extents.
	const double AspectRatio = static_cast<double>(OutputSizeXY.X) / static_cast<double>(OutputSizeXY.Y);
	const double OutputHorizRadius = (OutputSizeXY.X / 2.0) / FOutput;
	const double OutputVertRadius = (OutputSizeXY.Y / 2.0) / FOutput;

	// Sample tile corners + edge midpoints to find the max render extent (mapping is non-radial
	// due to azimuth/elevation coupling and the parent-frame composition).
	double MaxRenderHoriz = 0.0;
	double MaxRenderVert = 0.0;
	const double SX[] = {-OutputHorizRadius, OutputHorizRadius, -OutputHorizRadius, OutputHorizRadius, 0.0, 0.0, -OutputHorizRadius, OutputHorizRadius};
	const double SY[] = {-OutputVertRadius, -OutputVertRadius, OutputVertRadius, OutputVertRadius, -OutputVertRadius, OutputVertRadius, 0.0, 0.0};
	for (int32 I = 0; I < 8; ++I)
	{
		const FVector2D Render = OutputToRender(SX[I], SY[I]);
		MaxRenderHoriz = FMath::Max(MaxRenderHoriz, FMath::Abs(Render.X));
		MaxRenderVert = FMath::Max(MaxRenderVert, FMath::Abs(Render.Y));
	}

	const double RequiredRenderHorizRadius = FMath::Max(MaxRenderHoriz, MaxRenderVert * AspectRatio);
	Config.RenderFOVAngle = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(RequiredRenderHorizRadius)) * 2.0, 1.0, 170.0);

	Config.FRender = (OutputSizeXY.X / 2.0) / FMath::Tan(FMath::DegreesToRadians(Config.RenderFOVAngle / 2.0));

	return Config;
}
