// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Math/MathFwd.h"
#include "Math/IntPoint.h"

#include "TempoDistortionModels.generated.h"

UENUM(BlueprintType)
enum class ETempoDistortionModel : uint8
{
	BrownConrady  UMETA(DisplayName="Brown-Conrady", ToolTip="Standard radial lens distortion. Single capture, max 170 degree FOV."),
	Rational      UMETA(DisplayName="Rational", ToolTip="Rational radial distortion (numerator K1-K3, denominator K4-K6). Single capture, max 170 degree FOV."),
	KannalaBrandt UMETA(DisplayName="KannalaBrandt (Fisheye)", ToolTip="Equidistant fisheye projection. Supports up to 240 degree FOV using multiple captures."),
};

USTRUCT(BlueprintType)
struct FTempoLensDistortionParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	ETempoDistortionModel DistortionModel = ETempoDistortionModel::BrownConrady;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	float K1 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	float K2 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	float K3 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	float K4 = 0.0f;
	// K5 and K6 are denominator coefficients only used by the Rational distortion model.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens", meta = (ToolTip = "Only used by the Rational distortion model."))
	float K5 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens", meta = (ToolTip = "Only used by the Rational distortion model."))
	float K6 = 0.0f;
};

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

// Base for radial (non-equidistant) distortion models that share the same ComputeRenderConfig
// structure: forward/inverse distortion functions plus a barrel/pincushion determination.
// Subclasses implement Distort, Undistort, IsBarrel, and GetMaxOutputRadius; this base
// provides the common ComputeRenderConfig implementation in terms of those primitives.
struct TEMPOSENSORSSHARED_API FRadialDistortionBase : FDistortionModel
{
	// Forward distortion: undistorted radius -> distorted output radius.
	virtual double Distort(double R) const = 0;

	// Inverse distortion: distorted output radius -> undistorted radius (Newton-Raphson).
	virtual double Undistort(double Rd) const = 0;

	// Returns true for barrel distortion (corners pushed outward), which means the image
	// diagonal — not the horizontal — is the limiting factor for the render FOV.
	virtual bool IsBarrel() const = 0;

	// Maximum distorted output radius for barrel feasibility check. Returns -1 if not applicable.
	virtual double GetMaxOutputRadius() const { return -1.0; }

	// Model name used in log warnings.
	virtual const TCHAR* GetModelName() const = 0;

	virtual FDistortionRenderConfig ComputeRenderConfig(const FIntPoint& OutputSizeXY, double OutputHFOVDeg) const override;
};

// Brown-Conrady radial distortion model.
// Distortion: r_d = r * (1 + K1*r^2 + K2*r^4 + K3*r^6)
// The inverse (output -> render) requires Newton-Raphson root finding.
struct TEMPOSENSORSSHARED_API FBrownConradyDistortion : FRadialDistortionBase
{
	double K1 = 0.0;
	double K2 = 0.0;
	double K3 = 0.0;

	FBrownConradyDistortion(double InK1, double InK2, double InK3)
		: K1(InK1), K2(InK2), K3(InK3) {}

	virtual FVector2D OutputToRender(double OutputX, double OutputY) const override;

	virtual double Distort(double R) const override { return SolveDistortion(R, K1, K2, K3); }
	virtual double Undistort(double Rd) const override { return SolveInverseDistortion(Rd, K1, K2, K3); }
	virtual bool IsBarrel() const override { return K1 <= 0.0; }
	virtual double GetMaxOutputRadius() const override { return ComputeMaxOutputRadius(K1, K2, K3); }
	virtual const TCHAR* GetModelName() const override { return TEXT("Brown-Conrady"); }

	// Forward distortion: given a render (undistorted) radius, compute the output (distorted) radius.
	static double SolveDistortion(double RenderRadius, double K1, double K2, double K3);

	// Inverse distortion: given an output (distorted) radius, solve for the render (undistorted) radius.
	// Uses Newton-Raphson to invert: R * (1 + K1*R^2 + K2*R^4 + K3*R^6) = OutputRadius
	static double SolveInverseDistortion(double OutputRadius, double K1, double K2, double K3);

	// Compute the maximum output (distorted) radius for barrel distortion (K1 < 0).
	// Returns -1.0 if distortion is not barrel (K1 >= 0) or has no finite limit.
	static double ComputeMaxOutputRadius(double K1, double K2, double K3);
};

// Rational radial distortion model.
// Distortion: r_d = r * (1 + K1*r^2 + K2*r^4 + K3*r^6) / (1 + K4*r^2 + K5*r^4 + K6*r^6)
// The inverse (output -> render) requires Newton-Raphson root finding.
// When K4=K5=K6=0 this reduces to the Brown-Conrady model.
struct TEMPOSENSORSSHARED_API FRationalDistortion : FRadialDistortionBase
{
	double K1 = 0.0;
	double K2 = 0.0;
	double K3 = 0.0;
	double K4 = 0.0;
	double K5 = 0.0;
	double K6 = 0.0;

	FRationalDistortion(double InK1, double InK2, double InK3, double InK4, double InK5, double InK6)
		: K1(InK1), K2(InK2), K3(InK3), K4(InK4), K5(InK5), K6(InK6) {}

	virtual FVector2D OutputToRender(double OutputX, double OutputY) const override;

	virtual double Distort(double R) const override { return SolveDistortion(R, K1, K2, K3, K4, K5, K6); }
	virtual double Undistort(double Rd) const override { return SolveInverseDistortion(Rd, K1, K2, K3, K4, K5, K6); }
	// At small r the ratio ≈ 1 + (K1-K4)*r^2 + ..., so the net first-order coefficient drives barrel/pincushion.
	virtual bool IsBarrel() const override { return (K1 - K4) <= 0.0; }
	virtual double GetMaxOutputRadius() const override { return ComputeMaxOutputRadius(K1, K2, K3, K4, K5, K6); }
	virtual const TCHAR* GetModelName() const override { return TEXT("Rational"); }

	// Forward distortion: given a render (undistorted) radius, compute the output (distorted) radius.
	static double SolveDistortion(double R, double K1, double K2, double K3, double K4, double K5, double K6);

	// Inverse distortion: given an output (distorted) radius, solve for the render (undistorted) radius.
	// Uses Newton-Raphson to invert: R * N(R^2) / D(R^2) = Rd
	static double SolveInverseDistortion(double Rd, double K1, double K2, double K3, double K4, double K5, double K6);

	// Numerically finds the critical radius where d(r_d)/dr = 0, then returns the corresponding r_d.
	// Returns -1.0 if the model is pincushion-like ((K1-K4) >= 0) or no critical point is found.
	static double ComputeMaxOutputRadius(double K1, double K2, double K3, double K4, double K5, double K6);
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

// Factory: construct the distortion model implementation matching the requested parameters.
// YawDegrees/PitchDegrees are the capture component's relative rotation (UE convention: positive
// yaw=right, positive pitch=up) and are only consulted by the Kannala-Brandt multi-tile model.
TEMPOSENSORSSHARED_API TUniquePtr<FDistortionModel> CreateDistortionModel(
	const FTempoLensDistortionParameters& LensParameters,
	double YawDegrees,
	double PitchDegrees);
