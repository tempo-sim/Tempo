// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "RHIDefinitions.h"
#include "SceneView.h"

class FLocalFogVolumeUniformParameters;
struct FMeshBatch;
class FPrimitiveSceneProxy;
class FScene;
class FSceneUniformBuffer;
class FSceneView;

// Fixed-point scale of the optical depth profile texture: optical depth * this, as uint32. Chosen
// so that an optical depth of 1 is exactly representable and the largest value (65535) is far past
// where anything transmits. Also the unit other passes add to the profile in.
constexpr float GTempoLidarMediaOpticalDepthScale = 65536.0f;

// The per-pixel results of the participating media resolve, one 8-byte pixel of PF_R32G32_UINT.
// The lidar decode reads these next to its normal pixel.
struct FTempoLidarMediaPixel
{
	// Low 16 bits: range along the ray of the medium's echo, as a fraction of MaxRange in 1/65535
	// steps, 0 = no echo. High 16 bits: intensity of the medium's echo, in 1/65535 steps of [0, 1].
	uint32 RangeAndIntensity = 0;
	// Low 16 bits: one-way transmittance from the sensor to the opaque surface, in 1/65535 steps of
	// [0, 1]. Next byte: the medium echo's reflectivity estimate, its return-weighted mean albedo, in
	// 1/255 steps of [0, 1]. High byte: the medium echo's label, the semantic or instance label of
	// whatever contributed most to the bin the echo was drawn from, 0 for unlabeled.
	uint32 TransmittanceAlbedoAndLabel = 0;

	uint16 MediumRangeCode() const { return static_cast<uint16>(RangeAndIntensity & 0xFFFFu); }
	uint16 MediumIntensityCode() const { return static_cast<uint16>(RangeAndIntensity >> 16); }
	uint16 SurfaceTransmittanceCode() const { return static_cast<uint16>(TransmittanceAlbedoAndLabel & 0xFFFFu); }
	bool HasMediumEcho() const { return MediumRangeCode() != 0; }
	uint8 MediumReflectivityByte() const { return static_cast<uint8>((TransmittanceAlbedoAndLabel >> 16) & 0xFFu); }
	uint8 MediumLabel() const { return static_cast<uint8>(TransmittanceAlbedoAndLabel >> 24); }
	float MediumRange(float MaxRange) const { return MaxRange * static_cast<float>(MediumRangeCode()) / 65535.0f; }
	float MediumIntensity() const { return static_cast<float>(MediumIntensityCode()) / 65535.0f; }
	float SurfaceTransmittance() const { return static_cast<float>(SurfaceTransmittanceCode()) / 65535.0f; }
};
static_assert(sizeof(FTempoLidarMediaPixel) == 8, "FTempoLidarMediaPixel must match PF_R32G32_UINT stride");

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
	// The fog's single-scattering albedo, as a luminance: the fraction of what it takes out of the
	// beam that it scatters rather than absorbs. Applied to every fog source.
	float Albedo = 1.0f;
	// The local fog volumes in the scene, each its center in the view's translated world space (as
	// the renderer uploads the instances) with its label in w. An instance the view composes is
	// labeled by the nearest entry. Empty, every fog source carries the sensor's fog label.
	TArray<FVector4f> LabeledVolumes;
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
	// The backscatter of a white medium toward the sensor, as a fraction of a perpendicular surface's
	// return. Each bin's return is this times the bin's albedo.
	float Backscatter = 0.1f;
	// The lidar's own range falloff parameter, cm.
	float IntensitySaturationDistance = 1000.0f;
	// Draw each beam's medium echo range at random from the return distribution (true) or report
	// its median (false).
	bool bStochastic = true;
	// Random seed for the draw; the capture's sequence id keeps a scan reproducible.
	uint32 Seed = 0;
	// The label medium echoes from fog carry: the height fog's, the volumetric fog grid's and, unless
	// they are labeled individually, local fog volumes'. 0 for unlabeled.
	uint32 FogLabel = 0;
};

struct FTempoLidarMediaPassInputs
{
	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::SM5;
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;
	// The view's rect in the scene textures: where the renderer rasterized it, which it quantizes to
	// an 8-pixel boundary, so it need not be where the view lands in the family's render target.
	FIntRect ViewRect;
	// The view's rect in the family's render target, where its post-processed output lands and the
	// results are written so they index the render target the same way. Same size as ViewRect.
	FIntRect OutputRect;
	// The family's resolved scene depth.
	FRDGTextureRef SceneDepth = nullptr;
	FTempoLidarMediaFogInputs Fog;
	FTempoLidarMediaSensorInputs Sensor;
};

// The per-pixel profile of a view: three PF_R32_UINT 3D textures, X and Y the view rect's size and
// Z the bins. OpticalDepth holds each bin's optical depth times GTempoLidarMediaOpticalDepthScale;
// AlbedoOpticalDepth holds the same weighted by the albedo of what contributed it, so a bin's mean
// albedo is the ratio of the two. Label holds the bin's largest single contribution, its optical
// depth times the scale in the high 24 bits over that contributor's label in the low byte, so the
// low byte of the largest value is the label of what dominates the bin.
struct FTempoLidarMediaProfile
{
	FRDGTextureRef OpticalDepth = nullptr;
	FRDGTextureRef AlbedoOpticalDepth = nullptr;
	FRDGTextureRef Label = nullptr;

	bool IsValid() const { return OpticalDepth != nullptr && AlbedoOpticalDepth != nullptr && Label != nullptr; }
};

// Build the profile of the view from the fog the camera renders; other passes may add to it before
// it is resolved. Both textures are null when there is nothing to build.
TEMPOSENSORSSHADERS_API FTempoLidarMediaProfile AddTempoLidarMediaProfilePass(FRDGBuilder& GraphBuilder, const FTempoLidarMediaPassInputs& Inputs);

// A translucent mesh batch visible in the view, to be rasterized into the profile.
struct FTempoLidarMediaTranslucentBatch
{
	const FMeshBatch* Mesh = nullptr;
	const FPrimitiveSceneProxy* Proxy = nullptr;
	uint64 BatchElementMask = ~0ull;
	// The batch's static mesh id, or -1 for a dynamic batch.
	int32 StaticMeshId = -1;
};

// Rasterize the view's translucent primitives with the plugin's own material shaders, which add
// each fragment's optical depth (-ln(1 - opacity)), that weighted by the material's albedo
// estimate, and its primitive's label (the custom depth stencil value the labeler assigned it) to
// the profile at its range. Batches must stay valid until the graph executes; the
// renderer's own live for the whole render. Requires the material shaders this module registers,
// compiled for every translucent material.
TEMPOSENSORSSHADERS_API void AddTempoLidarMediaTranslucencyPass(
	FRDGBuilder& GraphBuilder,
	const FTempoLidarMediaPassInputs& Inputs,
	const FSceneView& View,
	const FScene* Scene,
	FSceneUniformBuffer& SceneUniforms,
	TArrayView<const FTempoLidarMediaTranslucentBatch> Batches,
	const FTempoLidarMediaProfile& Profile);

// Resolve the profile into per-pixel FTempoLidarMediaPixel results, written to Output (a
// PF_R32G32_UINT texture with a UAV) inside the view rect.
TEMPOSENSORSSHADERS_API void AddTempoLidarMediaResolvePass(FRDGBuilder& GraphBuilder, const FTempoLidarMediaPassInputs& Inputs, const FTempoLidarMediaProfile& Profile, FRDGTextureRef Output);
