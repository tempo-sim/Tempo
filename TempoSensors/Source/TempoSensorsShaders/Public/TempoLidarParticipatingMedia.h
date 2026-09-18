// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "RHIDefinitions.h"
#include "SceneView.h"

class FLocalFogVolumeUniformParameters;

// Fixed-point scale of the optical depth profile texture: optical depth * this, as uint32. Chosen
// so that an optical depth of 1 is exactly representable and the largest value (65535) is far past
// where anything transmits. Also the unit other passes add to the profile in.
constexpr float GTempoLidarMediaOpticalDepthScale = 65536.0f;

// The per-pixel results of the participating media resolve, one 8-byte pixel of
// PF_R16G16B16A16_UINT. The lidar decode reads these next to its normal pixel.
struct FTempoLidarMediaPixel
{
	static constexpr uint16 FlagMediumEcho = 1;
	static constexpr uint16 FlagSurfaceRendered = 2;

	// Range along the ray of the medium's echo, as a fraction of MaxRange in 1/65535 steps. 0 = no echo.
	uint16 MediumRangeCode = 0;
	// Intensity of the medium's echo, in 1/65535 steps of [0, 1].
	uint16 MediumIntensityCode = 0;
	// One-way transmittance from the sensor to the opaque surface, in 1/65535 steps of [0, 1].
	uint16 SurfaceTransmittanceCode = 0;
	uint16 Flags = 0;

	bool HasMediumEcho() const { return (Flags & FlagMediumEcho) != 0 && MediumRangeCode != 0; }
	float MediumRange(float MaxRange) const { return MaxRange * static_cast<float>(MediumRangeCode) / 65535.0f; }
	float MediumIntensity() const { return static_cast<float>(MediumIntensityCode) / 65535.0f; }
	float SurfaceTransmittance() const { return static_cast<float>(SurfaceTransmittanceCode) / 65535.0f; }
};
static_assert(sizeof(FTempoLidarMediaPixel) == 8, "FTempoLidarMediaPixel must match PF_R16G16B16A16_UINT stride");

// The fog the camera's fog pass would compose for this view, mirrored from the renderer's per-view
// fog constants (FViewInfo) and resources. Everything defaults to "no fog".
struct FTempoLidarMediaFogInputs
{
	// FViewInfo::ExponentialFogParameters, 2 and 3; see HeightFogCommon.ush for the packing.
	FVector4f ExponentialFogParameters = FVector4f(0.0f, 1.0f, 0.0f, 0.0f);
	FVector4f ExponentialFogParameters2 = FVector4f(0.0f, 1.0f, 0.0f, 0.0f);
	FVector4f ExponentialFogParameters3 = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	// 1 - FViewInfo::FogMaxOpacity.
	float MinFogTransmittance = 0.0f;
	// FViewInfo::FogEndDistance.
	float EndDistance = 0.0f;
	// The volumetric fog froxel grid rendered for this view, or null when it was not.
	FRDGTextureRef IntegratedLightScattering = nullptr;
	float VolumetricFogStartDistance = 0.0f;
	// The view's local fog volume data when local fog volumes are composed analytically for it, else
	// null. Points at renderer-owned memory valid for the current render.
	const FLocalFogVolumeUniformParameters* LocalFogVolumes = nullptr;
};

// The lidar's participating media model.
struct FTempoLidarMediaSensorInputs
{
	// Number of range bins in the profile.
	int32 NumBins = 64;
	// Far edge of the first bin, cm. Bins are log-spaced from here to MaxRange.
	float FirstBinEdge = 100.0f;
	// The lidar's maximum range, cm. Also the scale of FTempoLidarMediaPixel::MediumRangeCode.
	float MaxRange = 10000.0f;
	// Visual opacity to lidar optical depth.
	float ExtinctionScale = 1.0f;
	// The medium's backscatter toward the sensor, as a fraction of a perpendicular surface's return.
	float Backscatter = 0.1f;
	// The lidar's own range falloff parameter, cm.
	float IntensitySaturationDistance = 1000.0f;
	// Draw each beam's medium echo range at random from the return distribution (true) or report
	// its median (false).
	bool bStochastic = true;
	// Random seed for the draw; the capture's sequence id keeps a scan reproducible.
	uint32 Seed = 0;
};

struct FTempoLidarMediaPassInputs
{
	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::SM5;
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;
	// The view's rect in SceneDepth and in the output texture.
	FIntRect ViewRect;
	// The family's resolved scene depth.
	FRDGTextureRef SceneDepth = nullptr;
	FTempoLidarMediaFogInputs Fog;
	FTempoLidarMediaSensorInputs Sensor;
};

// Build the optical depth profile of the view: a PF_R32_UINT 3D texture, X and Y the view rect's
// size and Z the bins, holding each bin's optical depth times GTempoLidarMediaOpticalDepthScale.
// Filled from the fog the camera renders; other passes may add to it before it is resolved.
TEMPOSENSORSSHADERS_API FRDGTextureRef AddTempoLidarMediaProfilePass(FRDGBuilder& GraphBuilder, const FTempoLidarMediaPassInputs& Inputs);

// Resolve the profile into per-pixel FTempoLidarMediaPixel results, written to Output (a
// PF_R16G16B16A16_UINT texture with a UAV) inside the view rect.
TEMPOSENSORSSHADERS_API void AddTempoLidarMediaResolvePass(FRDGBuilder& GraphBuilder, const FTempoLidarMediaPassInputs& Inputs, FRDGTextureRef OpticalDepthProfile, FRDGTextureRef Output);
