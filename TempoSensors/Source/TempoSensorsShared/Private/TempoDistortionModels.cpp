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

double FBrownConradyDistortion::SolveInverseDistortion(double TargetRadius, double K1, double K2, double K3)
{
	if (TargetRadius < 1e-6)
	{
		return TargetRadius;
	}

	return Solve(
		[K1, K2, K3, TargetRadius](double R) {
			double R2 = R * R;
			double R4 = R2 * R2;
			double R6 = R4 * R2;
			return (R * (1.0 + K1*R2 + K2*R4 + K3*R6)) - TargetRadius;
		},
		[K1, K2, K3](double R) {
			double R2 = R * R;
			double R4 = R2 * R2;
			double R6 = R4 * R2;
			return 1.0 + 3.0*K1*R2 + 5.0*K2*R4 + 7.0*K3*R6;
		},
		TargetRadius,
		40,
		1e-6
	);
}

FVector2D FBrownConradyDistortion::DistortedToSource(double TargetX, double TargetY) const
{
	const double TargetRadius = FMath::Sqrt(TargetX * TargetX + TargetY * TargetY);
	const double SourceRadius = SolveInverseDistortion(TargetRadius, K1, K2, K3);
	const double Scale = (TargetRadius > 1e-6) ? (SourceRadius / TargetRadius) : 1.0;
	return FVector2D(TargetX * Scale, TargetY * Scale);
}

FVector2D FEquidistantDistortion::DistortedToSource(double TargetX, double TargetY) const
{
	// TargetX = azimuth in radians, TargetY = elevation in radians
	// (when normalized by equidistant focal length, pixel position is linear in angle)
	//
	// Maps to perspective projection coordinates:
	//   SourceX = tan(azimuth)
	//   SourceY = tan(elevation) * sec(azimuth)
	//
	// This accounts for the coupling between azimuth and elevation in perspective projection:
	// a point at (azimuth, elevation) projects to perspective image plane at
	// (tan(azimuth), tan(elevation) * sqrt(tan(azimuth)^2 + 1)).
	const double TanAzimuth = FMath::Tan(TargetX);
	const double TanElevation = FMath::Tan(TargetY);
	return FVector2D(TanAzimuth, TanElevation * FMath::Sqrt(TanAzimuth * TanAzimuth + 1.0));
}
