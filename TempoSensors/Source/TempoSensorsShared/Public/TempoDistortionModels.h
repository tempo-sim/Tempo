// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "Math/MathFwd.h"

// Base class for distortion models. Given a pixel position in the output (distorted/remapped)
// image, computes the normalized UV coordinates in the source (perspective) image.
// All coordinates are relative to the optical center and normalized by focal length.
struct TEMPOSENSORSSHARED_API FDistortionModel
{
	virtual ~FDistortionModel() = default;

	// For a point at (TargetX, TargetY) in the distorted image plane (normalized by destination
	// focal length), return the corresponding (SourceX, SourceY) in the perspective image plane
	// (normalized by source focal length).
	virtual FVector2D DistortedToSource(double TargetX, double TargetY) const = 0;
};

// Brown-Conrady radial distortion model.
// Distortion: r_distorted = r * (1 + K1*r^2 + K2*r^4 + K3*r^6)
// The inverse (distorted -> source) requires Newton-Raphson root finding.
struct TEMPOSENSORSSHARED_API FBrownConradyDistortion : FDistortionModel
{
	double K1 = 0.0;
	double K2 = 0.0;
	double K3 = 0.0;

	FBrownConradyDistortion(double InK1, double InK2, double InK3)
		: K1(InK1), K2(InK2), K3(InK3) {}

	virtual FVector2D DistortedToSource(double TargetX, double TargetY) const override;

	// Solve for the undistorted radius given a distorted radius.
	// Uses Newton-Raphson to invert: R * (1 + K1*R^2 + K2*R^4 + K3*R^6) = TargetRadius
	static double SolveInverseDistortion(double TargetRadius, double K1, double K2, double K3);
};

// Equidistant (spherical) projection model for Lidar.
// Maps from an equidistant output where pixel position is proportional to angle
// (azimuth horizontally, elevation vertically) to perspective projection coordinates.
// The mapping accounts for the coupling between azimuth and elevation in perspective projection:
//   SourceX = tan(azimuth)
//   SourceY = tan(elevation) * sec(azimuth)
// This is trivially computed (no Newton-Raphson needed).
struct TEMPOSENSORSSHARED_API FEquidistantDistortion : FDistortionModel
{
	virtual FVector2D DistortedToSource(double TargetX, double TargetY) const override;
};
