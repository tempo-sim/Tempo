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
	DoubleSphere  UMETA(DisplayName="DoubleSphere (Fisheye)", ToolTip="Double Sphere fisheye projection (Usenko et al. 2018). Closed-form unprojection. Supports >180 degree FOV using multiple captures."),
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

	// Double Sphere parameters. Xi controls the offset of the second sphere; Alpha blends between
	// pinhole-like (0) and pure unit-sphere (1) projection.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens", meta = (ToolTip = "Only used by the DoubleSphere distortion model. Range [-1, 1]."))
	float Xi = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens", meta = (ToolTip = "Only used by the DoubleSphere distortion model. Range [0, 1]."))
	float Alpha = 0.5f;

	bool operator==(const FTempoLensDistortionParameters& Other) const
	{
		return DistortionModel == Other.DistortionModel
			&& K1 == Other.K1 && K2 == Other.K2 && K3 == Other.K3
			&& K4 == Other.K4 && K5 == Other.K5 && K6 == Other.K6
			&& Xi == Other.Xi && Alpha == Other.Alpha;
	}
	bool operator!=(const FTempoLensDistortionParameters& Other) const { return !(*this == Other); }
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

	// Compute the perspective render configuration for a tile of the given output pixel size,
	// given the global output focal length (pixels per the model's output unit — this unit
	// differs by model: r_d in normalized image plane, theta_d in radians, etc.). The returned
	// FOutput equals the input FOutput; the model fills in RenderFOVAngle / RenderSizeXY / FRender.
	virtual FDistortionRenderConfig ComputeRenderConfig(const FIntPoint& OutputSizeXY, double FOutput) const = 0;

	// Compute the global output focal length (pixels per output unit) for a full image with the
	// given pixel size and physical horizontal FOV. Tile-aware (fisheye) models override this;
	// single-capture models return -1 to signal "not applicable" (the camera doesn't ask).
	virtual double ComputeFOutputForFullImage(const FIntPoint& FullImageSizeXY, double FullImageHFOVDeg) const { return -1.0; }

	// For multi-tile rendering: convert a horizontal pixel offset (signed, from the full output
	// image's optical center) into the yaw angle (degrees, UE convention: positive=right) at
	// which a tile centered there should aim its perspective render. Default 0 = single-capture.
	virtual double PixelOffsetToYawDeg(double DxPixels, double FOutput) const { return 0.0; }

	// As above, for vertical pixel offset → pitch (degrees, UE convention: positive=up). Note:
	// image-Y increases downward, so positive DyPixels (below the optical center) maps to a
	// NEGATIVE UE pitch.
	virtual double PixelOffsetToPitchDeg(double DyPixels, double FOutput) const { return 0.0; }
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

	virtual FDistortionRenderConfig ComputeRenderConfig(const FIntPoint& OutputSizeXY, double FOutput) const override;

	// Single-capture models compute FOutput from physical FOV via Distort(tan(half-FOV)).
	virtual double ComputeFOutputForFullImage(const FIntPoint& FullImageSizeXY, double FullImageHFOVDeg) const override;
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
	virtual FDistortionRenderConfig ComputeRenderConfig(const FIntPoint& OutputSizeXY, double FOutput) const override;
};

// Kannala-Brandt fisheye projection model for camera tiles.
// Forward projection:
//   theta_d = theta * (1 + K1*theta^2 + K2*theta^4 + K3*theta^6 + K4*theta^8)
//   r_image = FOutput * theta_d
// where theta is the physical angle from the optical axis and theta_d is the
// distorted angle that's linear in pixel position. When K1=K2=K3=K4=0 this
// reduces to a pure equidistant fisheye (r = f*theta).
//
// Output unit: theta_d (radians). FOutput is pixels per radian of theta_d.
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
	virtual FDistortionRenderConfig ComputeRenderConfig(const FIntPoint& OutputSizeXY, double FOutput) const override;
	virtual double ComputeFOutputForFullImage(const FIntPoint& FullImageSizeXY, double FullImageHFOVDeg) const override;
	virtual double PixelOffsetToYawDeg(double DxPixels, double FOutput) const override;
	virtual double PixelOffsetToPitchDeg(double DyPixels, double FOutput) const override;

	// Forward K-B: physical angle theta -> distorted angle theta_d.
	static double SolveDistortion(double Theta, double K1, double K2, double K3, double K4);

	// Inverse K-B: distorted angle theta_d -> physical angle theta (Newton-Raphson).
	static double SolveInverseDistortion(double ThetaD, double K1, double K2, double K3, double K4);
};

// Double Sphere fisheye projection model for camera tiles.
// (Usenko, Demmel, Cremers, "The Double Sphere Camera Model", 2018.)
//
// Forward projection of a 3D point (x, y, z):
//   d1 = sqrt(x^2 + y^2 + z^2)
//   d2 = sqrt(x^2 + y^2 + (xi*d1 + z)^2)
//   m_x = x / (alpha*d2 + (1-alpha)*(xi*d1 + z))
//   m_y = y / (alpha*d2 + (1-alpha)*(xi*d1 + z))
//
// Output unit: r_d (normalized image plane). FOutput is pixels per unit r_d.
//
// Inverse projection (closed form — the key advantage over Kannala-Brandt):
//   r^2 = m_x^2 + m_y^2
//   m_z = (1 - alpha^2*r^2) / (alpha*sqrt(1 - (2*alpha-1)*r^2) + 1 - alpha)
//   beta = (m_z*xi + sqrt(m_z^2 + (1-xi^2)*r^2)) / (m_z^2 + r^2)
//   ray = (beta*m_x, beta*m_y, beta*m_z - xi)
//
// Multi-tile composition mirrors FKannalaBrandtDistortion: tile-local output coords get
// shifted by the tile center's position in the parent's output plane (computed by forward-
// projecting the child's optical axis ray through the parent's DS), unprojected to a 3D
// ray in the parent frame, rotated into the child frame, then perspective-projected.
//
// Coordinate system / sign conventions: same as FKannalaBrandtDistortion.
struct TEMPOSENSORSSHARED_API FDoubleSphereDistortion : FDistortionModel
{
	// xi: offset of the second sphere along the optical axis. Range [-1, 1].
	double Xi = 0.0;
	// alpha: blend factor between the perspective center on the second sphere (alpha=0,
	// pinhole-like) and the unit sphere (alpha=1, ultra-wide). Range [0, 1].
	double Alpha = 0.5;

	// Same convention as FKannalaBrandtDistortion: tile axis offset from parent optical axis.
	double AzimuthOffset = 0.0;
	double ElevationOffset = 0.0;

	FDoubleSphereDistortion(double InXi, double InAlpha, double InAzimuthOffset, double InElevationOffset)
		: Xi(InXi), Alpha(InAlpha), AzimuthOffset(InAzimuthOffset), ElevationOffset(InElevationOffset) {}

	virtual FVector2D OutputToRender(double OutputX, double OutputY) const override;
	virtual FDistortionRenderConfig ComputeRenderConfig(const FIntPoint& OutputSizeXY, double FOutput) const override;
	virtual double ComputeFOutputForFullImage(const FIntPoint& FullImageSizeXY, double FullImageHFOVDeg) const override;
	virtual double PixelOffsetToYawDeg(double DxPixels, double FOutput) const override;
	virtual double PixelOffsetToPitchDeg(double DyPixels, double FOutput) const override;

	// Forward DS: 3D ray (x, y, z) -> (m_x, m_y) in the normalized image plane. Returns false
	// if the point lies outside the model's valid forward-projection region.
	static bool ProjectRay(double X, double Y, double Z, double Xi, double Alpha, double& OutMx, double& OutMy);

	// Inverse DS: (m_x, m_y) in normalized image plane -> 3D ray (closed form). Returns false
	// if the point lies outside the model's valid unprojection region.
	static bool UnprojectPoint(double Mx, double My, double Xi, double Alpha,
		double& OutX, double& OutY, double& OutZ);

	// Forward DS for a radially-symmetric ray at angle theta from the optical axis:
	// r_d = sin(theta) / (alpha*sqrt(1 + 2*xi*cos(theta) + xi^2) + (1-alpha)*(xi + cos(theta))).
	static double RadialProject(double Theta, double Xi, double Alpha);
};

// Factory: construct the distortion model implementation matching the requested parameters.
// YawDegrees/PitchDegrees are the capture component's relative rotation (UE convention: positive
// yaw=right, positive pitch=up) and are only consulted by tile-aware (fisheye) models.
TEMPOSENSORSSHARED_API TUniquePtr<FDistortionModel> CreateDistortionModel(
	const FTempoLensDistortionParameters& LensParameters,
	double YawDegrees,
	double PitchDegrees);
