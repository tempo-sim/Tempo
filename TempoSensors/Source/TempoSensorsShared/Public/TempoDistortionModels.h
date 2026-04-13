// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "Math/MathFwd.h"
#include "Math/IntPoint.h"

// Configuration for the perspective render needed to produce a distorted output image.
struct TEMPOSENSORSSHARED_API FDistortionRenderConfig
{
	// The horizontal FOV for UE's perspective projection (degrees).
	float RenderFOVAngle = 90.0f;
	// The render target size.
	FIntPoint RenderSizeXY = FIntPoint::ZeroValue;
	// Focal length for the output (distorted) image, in pixels.
	double FOutput = 0.0;
	// Focal length for the render (perspective) image, in pixels.
	double FRender = 0.0;
};

// Base class for distortion models. Given a pixel position in the output (distorted)
// image, computes the normalized UV coordinates in the render (perspective) image.
// All coordinates are relative to the optical center and normalized by focal length.
struct TEMPOSENSORSSHARED_API FDistortionModel
{
	virtual ~FDistortionModel() = default;

	// For a point at (OutputX, OutputY) in the output image plane (normalized by output
	// focal length), return the corresponding (RenderX, RenderY) in the render image plane
	// (normalized by render focal length).
	virtual FVector2D OutputToRender(double OutputX, double OutputY) const = 0;

	// Compute the perspective render configuration needed for the given output image.
	// OutputSizeXY: the user-specified output image dimensions.
	// OutputHFOVDeg: the desired horizontal FOV of the output (distorted) image, in degrees.
	virtual FDistortionRenderConfig ComputeRenderConfig(const FIntPoint& OutputSizeXY, double OutputHFOVDeg) const = 0;
};

// Brown-Conrady radial distortion model.
// Distortion: r_output = r * (1 + K1*r^2 + K2*r^4 + K3*r^6)
// The inverse (output -> render) requires Newton-Raphson root finding.
struct TEMPOSENSORSSHARED_API FBrownConradyDistortion : FDistortionModel
{
	double K1 = 0.0;
	double K2 = 0.0;
	double K3 = 0.0;

	FBrownConradyDistortion(double InK1, double InK2, double InK3)
		: K1(InK1), K2(InK2), K3(InK3) {}

	virtual FVector2D OutputToRender(double OutputX, double OutputY) const override;
	virtual FDistortionRenderConfig ComputeRenderConfig(const FIntPoint& OutputSizeXY, double OutputHFOVDeg) const override;

	// Forward distortion: given a render (undistorted) radius, compute the output (distorted) radius.
	static double SolveDistortion(double RenderRadius, double K1, double K2, double K3);

	// Inverse distortion: given an output (distorted) radius, solve for the render (undistorted) radius.
	// Uses Newton-Raphson to invert: R * (1 + K1*R^2 + K2*R^4 + K3*R^6) = OutputRadius
	static double SolveInverseDistortion(double OutputRadius, double K1, double K2, double K3);

	// Compute the maximum output (distorted) radius for barrel distortion (K1 < 0).
	// Returns -1.0 if distortion is not barrel (K1 >= 0) or has no finite limit.
	static double ComputeMaxOutputRadius(double K1, double K2, double K3);
};

// Equidistant (spherical) projection model for Lidar.
// Maps from an equidistant output where pixel position is proportional to angle
// (azimuth horizontally, elevation vertically) to perspective render coordinates.
// The mapping accounts for the coupling between azimuth and elevation in perspective projection:
//   RenderX = tan(azimuth)
//   RenderY = tan(elevation) * sec(azimuth)
// This is trivially computed (no Newton-Raphson needed).
// NOTE: This is an axis-separable model used by the lidar, not a true radial fisheye.
struct TEMPOSENSORSSHARED_API FEquidistantDistortion : FDistortionModel
{
	virtual FVector2D OutputToRender(double OutputX, double OutputY) const override;
	virtual FDistortionRenderConfig ComputeRenderConfig(const FIntPoint& OutputSizeXY, double OutputHFOVDeg) const override;
};

// Kannala-Brandt fisheye projection model for camera tiles.
// Forward projection:
//   theta_d = theta * (1 + K1*theta^2 + K2*theta^4 + K3*theta^6 + K4*theta^8)
//   r_image = FOutput * theta_d
// where theta is the physical angle from the optical axis and theta_d is the
// distorted angle that's linear in pixel position. When K1=K2=K3=K4=0 this
// reduces to a pure equidistant fisheye (r = f*theta).
//
// For multi-tile rendering, the child capture has a different optical axis than
// the parent camera. OutputToRender:
//   1. Converts tile-local output coordinates (theta_d units) to parent-frame output coords
//   2. Inverts the K-B polynomial (Newton-Raphson) to recover the physical angle theta
//   3. Computes the 3D ray direction from theta and the output plane direction
//   4. Rotates the ray from parent frame to child frame
//   5. Projects to child's perspective coordinates
//
// Coordinate system: X=right, Y=down, Z=forward (optical axis). Right-handed.
// AzimuthOffset: positive = child looks right. ElevationOffset: positive = child looks down.
// NOTE: UE Pitch is positive=up, so callers must negate Pitch when constructing ElevationOffset.
struct TEMPOSENSORSSHARED_API FKannalaBrandtDistortion : FDistortionModel
{
	// Kannala-Brandt polynomial coefficients. All zero = pure equidistant.
	double K1 = 0.0;
	double K2 = 0.0;
	double K3 = 0.0;
	double K4 = 0.0;

	// The child capture's angular offset from the parent camera's optical axis, in radians.
	// AzimuthOffset: positive = right. ElevationOffset: positive = down in image.
	double AzimuthOffset = 0.0;
	double ElevationOffset = 0.0;

	FKannalaBrandtDistortion(double InK1, double InK2, double InK3, double InK4,
		double InAzimuthOffset, double InElevationOffset)
		: K1(InK1), K2(InK2), K3(InK3), K4(InK4)
		, AzimuthOffset(InAzimuthOffset), ElevationOffset(InElevationOffset) {}

	virtual FVector2D OutputToRender(double OutputX, double OutputY) const override;
	virtual FDistortionRenderConfig ComputeRenderConfig(const FIntPoint& OutputSizeXY, double OutputHFOVDeg) const override;

	// Forward K-B: physical angle theta -> distorted angle theta_d.
	static double SolveDistortion(double Theta, double K1, double K2, double K3, double K4);

	// Inverse K-B: distorted angle theta_d -> physical angle theta (Newton-Raphson).
	static double SolveInverseDistortion(double ThetaD, double K1, double K2, double K3, double K4);
};
