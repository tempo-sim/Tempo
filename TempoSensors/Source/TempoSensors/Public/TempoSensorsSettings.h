// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSensorsTypes.h"

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "TempoSensorsSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FTempoSensorsLabelSettingsChanged);

/**
 * TempoSensors Plugin Settings.
 */
UCLASS(Config=Plugins, DefaultConfig)
class TEMPOSENSORS_API UTempoSensorsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTempoSensorsSettings();

	virtual void PostInitProperties() override;

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif

	// Labels
	TObjectPtr<UDataTable> GetSemanticLabelTable() const { return SemanticLabelTable.LoadSynchronous(); }
	ELabelType GetLabelType() const { return LabelType; }
	bool GetGloballyUniqueInstanceLabels() const { return bGloballyUniqueInstanceLabels; }
	bool GetInstantaneouslyUniqueInstanceLabels() const { return bInstantaneouslyUniqueInstanceLabels; }

	// Camera
	TObjectPtr<UMaterialInterface> GetCameraPostProcessMaterialNoDepth() const { return CameraPostProcessMaterialNoDepth.LoadSynchronous(); }
	TObjectPtr<UMaterialInterface> GetCameraPostProcessMaterialWithDepth() const { return CameraPostProcessMaterialWithDepth.LoadSynchronous(); }
	TObjectPtr<UMaterialInterface> GetCameraStitchAuxMaterial() const { return CameraStitchAuxMaterial.LoadSynchronous(); }
	TObjectPtr<UMaterialInterface> GetCameraStitchColorFeatherMaterial() const { return CameraStitchColorFeatherMaterial.LoadSynchronous(); }
	TObjectPtr<UMaterialInterface> GetCameraStitchMergeMaterialWithDepth() const { return CameraStitchMergeMaterialWithDepth.LoadSynchronous(); }
	TObjectPtr<UMaterialInterface> GetCameraStitchMergeMaterialNoDepth() const { return CameraStitchMergeMaterialNoDepth.LoadSynchronous(); }
	TObjectPtr<UMaterialInterface> GetCameraProxyTonemapMaterial() const { return CameraProxyTonemapMaterial.LoadSynchronous(); }
	EColorImageEncoding GetColorImageEncoding() const { return ColorImageEncoding; }
	float GetMaxCameraDepth() const { return MaxCameraDepth; }
	float GetSceneCaptureGamma() const { return SceneCaptureGamma; }
	FName GetOverridableLabelRowName() const { return OverridableLabelRowName; }
	FName GetOverridingLabelRowName() const { return OverridingLabelRowName; }
	int32 GetMaxRenderBufferSize() const { return MaxRenderBufferSize; }
	bool GetPipelinedRendering() const { return bPipelinedRendering; }
	FTempoSensorsLabelSettingsChanged TempoSensorsLabelSettingsChangedEvent;

	// Lidar
	float GetMaxLidarDepth() const { return MaxLidarDepth; }
	TObjectPtr<UMaterialInterface> GetLidarPostProcessMaterial() const { return LidarPostProcessMaterial.LoadSynchronous(); }
	TObjectPtr<UMaterialInterface> GetLidarPostProcessMaterialWithColor() const { return LidarPostProcessMaterialWithColor.LoadSynchronous(); }

	// RayTracingScene Buffer Overrun Workaround
	bool GetRayTracingSceneReadbackBuffersOverrunWorkaroundEnabled() const { return bEnableRayTracingSceneReadbackBuffersOverrunWorkaround; }
	uint32 GetRayTracingSceneMaxReadbackBuffersOverride() const { return RayTracingSceneMaxReadbackBuffersOverride; }

	// RayTracing Geometry Residency Workaround
	bool GetRayTracingGeometryResidencyWorkaroundEnabled() const { return bEnableRayTracingGeometryResidencyWorkaround; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	// The data table from which we will load the mapping from Actor classes or static meshes to semantic labels.
	// In Instance label mode we will label all instances of objects that appear in the semantic label table.
	UPROPERTY(EditAnywhere, Config, Category="Labels")
	TSoftObjectPtr<UDataTable> SemanticLabelTable;

	// The global label type to use.
	UPROPERTY(EditAnywhere, Config, Category="Labels")
	TEnumAsByte<ELabelType> LabelType = ELabelType::Semantic;

	// Whether to reclaim instance labels for later use when labeled objects are destroyed.
	UPROPERTY(EditAnywhere, Config, Category="Labels", meta=(EditCondition="LabelType == ELabelType::Instance"))
	bool bGloballyUniqueInstanceLabels = false;

	// Whether to reuse instance labels after exhausting our 256 unique labels.
	UPROPERTY(EditAnywhere, Config, Category="Labels", meta=(EditCondition="LabelType == ELabelType::Instance"))
	bool bInstantaneouslyUniqueInstanceLabels = false;

	// The post process material that should be used by TempoCamera when not capturing the depth image.
	UPROPERTY(EditAnywhere, Config, Category="Advanced", meta=( AllowedClasses="/Script/Engine.BlendableInterface", Keywords="PostProcess" ))
	TSoftObjectPtr<UMaterialInterface> CameraPostProcessMaterialNoDepth;

	// The post process material that should be used by TempoCamera when capturing the depth image.
	UPROPERTY(EditAnywhere, Config, Category="Advanced", meta=( AllowedClasses="/Script/Engine.BlendableInterface", Keywords="PostProcess" ))
	TSoftObjectPtr<UMaterialInterface> CameraPostProcessMaterialWithDepth;

	// The material used to resolve the per-tile distorted atlas alpha into label+depth bytes in
	// the shared aux render target. Run as a full-screen Canvas pass over the equidistant output.
	// Must have Texture2D parameters "TileRT" (the atlas), "OutputResolveMap" (RGBA fp16 with the
	// two contributing tiles' atlas UVs), and "OutputResolveWeight" (R fp16 with tile-A weight in
	// [0,1]). Picks atlas UV by ownership (Weight >= 0.5 → UV_A else UV_B) — label is integer and
	// depth is bit-packed, neither is safely averageable across a feather band — then point-samples
	// TileRT.a there and unpacks the bit-packed label+depth bytes.
	UPROPERTY(EditAnywhere, Config, Category="Advanced", meta=( AllowedClasses="/Script/Engine.MaterialInterface" ))
	TSoftObjectPtr<UMaterialInterface> CameraStitchAuxMaterial;

	// The material used to resolve the per-tile distorted atlas into the final equidistant HDR output
	// with feathered seams between adjacent tiles. Run as a full-screen Canvas pass that writes to the
	// stitch HDR RT (which the proxy tonemap PPM then reads as scene color). Must have Texture2D
	// parameters "AtlasRT" (the atlas of per-tile distorted outputs), "OutputResolveMap" (RGBA fp16 with
	// channels (AtlasU_A, AtlasV_A, AtlasU_B, AtlasV_B)), and "OutputResolveWeight" (R fp16, weight of
	// tile A in [0,1]). Output color = lerp(sample(AtlasRT, UV_B), sample(AtlasRT, UV_A), Weight).
	UPROPERTY(EditAnywhere, Config, Category="Advanced", meta=( AllowedClasses="/Script/Engine.MaterialInterface" ))
	TSoftObjectPtr<UMaterialInterface> CameraStitchColorFeatherMaterial;

	// Merge material used when depth is enabled. Output RT is PF_A16B16G16R16 (16-bit UNORM per
	// channel, 8 bytes/pixel) matching FCameraPixelWithDepth. Must have Texture2D parameters
	// "ColorRT" (fp16 HDR linear) and "AuxRT" (RGBA8 with label in R and 24-bit depth in GBA).
	// Packs (saturated LDR color bytes, label byte, 24-bit depth bytes) into the 4 UNORM16 channels.
	UPROPERTY(EditAnywhere, Config, Category="Advanced", meta=( AllowedClasses="/Script/Engine.MaterialInterface" ))
	TSoftObjectPtr<UMaterialInterface> CameraStitchMergeMaterialWithDepth;

	// Merge material used when depth is disabled. Output RT is RGBA8 (4 bytes/pixel) matching
	// FCameraPixelNoDepth. Same texture parameters as the WithDepth variant; packs (BGR color
	// bytes, label byte) into the RGBA8 output. AuxRT's G/B/A channels (depth bytes) are ignored.
	UPROPERTY(EditAnywhere, Config, Category="Advanced", meta=( AllowedClasses="/Script/Engine.MaterialInterface" ))
	TSoftObjectPtr<UMaterialInterface> CameraStitchMergeMaterialNoDepth;

	// Post-process material used by UTempoCamera's proxy scene capture. Runs at blendable location
	// "Scene Color Before Bloom" so its output replaces scene color before Bloom/AE/Tonemapper.
	// Must have a Texture2D parameter "HDRColorRT" (Linear Color sampler) bound to the stitched
	// HDR render target; the sampled RGB is written to Emissive Color so the tonemapper operates
	// on the stitched-tile color rather than the (empty) scene render.
	UPROPERTY(EditAnywhere, Config, Category="Advanced", meta=( AllowedClasses="/Script/Engine.BlendableInterface", Keywords="PostProcess" ))
	TSoftObjectPtr<UMaterialInterface> CameraProxyTonemapMaterial;

	// Encoding to use for color images.
	UPROPERTY(EditAnywhere, Config, Category="Camera")
	TEnumAsByte<EColorImageEncoding> ColorImageEncoding = EColorImageEncoding::BGR8;

	// The expected maximum required depth for a camera depth image.
	UPROPERTY(EditAnywhere, Config, Category="Camera")
	float MaxCameraDepth = 100000.0; // 1km

	// Gamma to use for simulated scene captures.
	UPROPERTY(EditAnywhere, Config, Category="Camera")
	float SceneCaptureGamma = 2.2f;

	// The max number of frames per camera to buffer before dropping.
	UPROPERTY(EditAnywhere, Config, Category="Advanced")
	int32 MaxRenderBufferSize = 4;

	// This special row can be overriden by a value passed through the subsurface color.
	UPROPERTY(EditAnywhere, Config, Category="Camera")
	FName OverridableLabelRowName = NAME_None;

	// Anywhere a non-zero subsurface color is found on an object of type OverridableLabelRowName, this label will be used instead.
	UPROPERTY(EditAnywhere, Config, Category="Camera")
	FName OverridingLabelRowName = NAME_None;

	// The expected maximum required depth for a Lidar return.
	UPROPERTY(EditAnywhere, Config, Category="Lidar")
	float MaxLidarDepth = 40000.0; // 400m

	// The lidar PPM used when bColorEnabled is false. Writes scene-color-independent aux channels
	// (normal, label, discretized depth) into a PF_A16B16G16R16 atlas matching FLidarPixel.
	UPROPERTY(EditAnywhere, Config, Category="Advanced", meta=( AllowedClasses="/Script/Engine.BlendableInterface", Keywords="PostProcess" ))
	TSoftObjectPtr<UMaterialInterface> LidarPostProcessMaterial;

	// The lidar PPM used when bColorEnabled is true. Additionally samples scene color and packs
	// the BGR bytes into the wider PF_R32G32B32A32_UINT atlas matching FLidarPixelWithColor.
	UPROPERTY(EditAnywhere, Config, Category="Advanced", meta=( AllowedClasses="/Script/Engine.BlendableInterface", Keywords="PostProcess" ))
	TSoftObjectPtr<UMaterialInterface> LidarPostProcessMaterialWithColor;

	// Whether to enable a hack to work around a buffer overrun bug in FRayTracingScene.
	UPROPERTY(EditAnywhere, Config, Category="Advanced")
	bool bEnableRayTracingSceneReadbackBuffersOverrunWorkaround = true;

	// The size of the FRayTracingScene readback rings (StatsReadback/FeedbackReadback) to use as an
	// override, if enabled. Each ray-tracing scene render advances the ring by one and reuses a slot
	// MaxReadbackBuffers later without checking that the prior copy completed; reusing an in-flight
	// slot trips an assert in FRHIGPUBufferReadback::EnqueueCopy. So this must exceed the number of
	// ray-tracing scene renders in flight: (RT renders per frame) x (GPU frames in flight + margin).
	UPROPERTY(EditAnywhere, Config, Category="Advanced", meta=(EditCondition=bEnableRayTracingSceneReadbackBuffersOverrunWorkaround))
	uint32 RayTracingSceneMaxReadbackBuffersOverride = 128;

	// Whether to force ray-tracing geometry resident (disable reference-based residency) while
	// ray-traced camera captures are active, to work around an engine crash:
	// checkf(bIsCachedRayTracingInstanceValid) in RayTracing::GatherRelevantStaticPrimitives.
	// With the engine default (r.RayTracing.UseReferenceBasedResidency=1), a Nanite mesh's fallback
	// RT geometry is evicted/streamed based on what's referenced in the TLAS. Our many capture views
	// (each a different frustum / hide list) churn that referenced set, so geometry is repeatedly
	// evicted and re-requested. In the window where it is non-resident, RefreshCachedRayTracingData /
	// the cache pass commits an invalid instance (GeometryRHI==null) as clean, and the next gather —
	// ours or the main viewport's — asserts instead of skipping it (the Nanite-fallback branch has no
	// skip, unlike the streamed-Nanite branch). Keeping geometry resident closes that eviction window.
	// Cost: higher RT-geometry memory (all referenced geometry stays resident); does not close the
	// one-time first-build window on load. The proper fix is engine-side (skip invalid instances in
	// the gather); this is a no-engine-build mitigation. Set once when the first RT camera renders and
	// left in effect for the session (toggling the cvar recreates all render state).
	UPROPERTY(EditAnywhere, Config, Category="Advanced")
	bool bEnableRayTracingGeometryResidencyWorkaround = true;

	// When true, FixedStep mode allows the game thread to advance without waiting for sensor
	// readback to complete. Sensor images may arrive 1-2 frames late, but throughput increases
	// because the game, render, and readback pipelines run in parallel. Each image's
	// MeasurementHeader carries the correct CaptureTime and SequenceId so clients know which
	// simulation frame the data corresponds to.
	UPROPERTY(EditAnywhere, Config, Category="Advanced")
	bool bPipelinedRendering = false;
};
