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

// Radial equidistant (fisheye) projection model for camera tiles.
// True equidistant fisheye: r_image = f * theta, where theta is the angle from
// the optical axis and r_image is the radial distance from the image center.
//
// For multi-tile rendering, the child capture has a different optical axis than
// the parent camera. OutputToRender:
//   1. Converts tile-local equidistant coordinates to parent-frame equidistant angles
//   2. Computes the 3D ray direction from the radial equidistant mapping
//   3. Rotates the ray from parent frame to child frame
//   4. Projects to child's perspective coordinates
//
// Coordinate system: X=right, Y=down, Z=forward (optical axis). Right-handed.
// AzimuthOffset: positive = child looks right. ElevationOffset: positive = child looks down.
// NOTE: UE Pitch is positive=up, so callers must negate Pitch when constructing ElevationOffset.
struct TEMPOSENSORSSHARED_API FEquidistantTileDistortion : FDistortionModel
{
	// The child capture's offset from the parent camera's optical axis, in radians.
	// AzimuthOffset: positive = right. ElevationOffset: positive = down in image.
	double AzimuthOffset = 0.0;
	double ElevationOffset = 0.0;

	FEquidistantTileDistortion(double InAzimuthOffset, double InElevationOffset)
		: AzimuthOffset(InAzimuthOffset), ElevationOffset(InElevationOffset) {}

	virtual FVector2D OutputToRender(double OutputX, double OutputY) const override;
	virtual FDistortionRenderConfig ComputeRenderConfig(const FIntPoint& OutputSizeXY, double OutputHFOVDeg) const override;
};
