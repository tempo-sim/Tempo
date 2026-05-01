// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCamera/Camera.pb.h"
#include "TempoTiledSceneCaptureComponent.h"
#include "TempoScriptingServer.h"

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "SceneTypes.h"
#include "ShowFlags.h"

#include "TempoCamera.generated.h"

// 4-byte pixel format where first 3 bytes are color, 4th byte is label.
struct FCameraPixelNoDepth
{
	static constexpr bool bSupportsDepth = false;

	uint8 B() const { return U1; }
	uint8 G() const { return U2; }
	uint8 R() const { return U3; }

	uint8 Label() const { return U4; }

private:
	uint8 U1 = 0;
	uint8 U2 = 0;
	uint8 U3 = 0;
	uint8 U4 = 0;
};

// 8-byte pixel format where first 3 bytes are color, 4th byte is label.
// 5th-8th bytes are a uint32 representing discrete depth.
struct FCameraPixelWithDepth
{
	static constexpr bool bSupportsDepth = true;

	uint8 B() const { return U3; }
	uint8 G() const { return U2; }
	uint8 R() const { return U1; }

	uint8 Label() const { return U4; }

	float Depth(float MinDepth, float MaxDepth, float MaxDiscretizedDepth) const
	{
	   // We discretize inverse depth to give more consistent precision vs depth.
		const float InverseDepthFraction = static_cast<float>(U5) / MaxDiscretizedDepth;
		const float InverseDepth = InverseDepthFraction * (1.0 / MinDepth - 1.0 / MaxDepth) + 1.0 / MaxDepth;
		return 1.0 / InverseDepth;
	}

private:
	uint8 U1 = 0;
	uint8 U2 = 0;
	uint8 U3 = 0;
	uint8 U4 = 0;
	uint32 U5 = 0;
};

struct FColorImageRequest
{
	TempoCamera::ColorImageRequest Request;
	TResponseDelegate<TempoCamera::ColorImage> ResponseContinuation;
};

struct FLabelImageRequest
{
	TempoCamera::LabelImageRequest Request;
	TResponseDelegate<TempoCamera::LabelImage> ResponseContinuation;
};

struct FDepthImageRequest
{
	TempoCamera::DepthImageRequest Request;
	TResponseDelegate<TempoCamera::DepthImage> ResponseContinuation;
};

struct FBoundingBoxesRequest
{
	TempoCamera::BoundingBoxesRequest Request;
	TResponseDelegate<TempoCamera::BoundingBoxes> ResponseContinuation;
};

template <>
struct TTextureRead<FCameraPixelWithDepth> : TTextureReadBase<FCameraPixelWithDepth>
{
	TTextureRead(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn, const FString& OwnerNameIn,
	   const FString& SensorNameIn, const FTransform& SensorTransformIn, float MinDepthIn, float MaxDepthIn,
	   TMap<uint8, uint8>&& InstanceToSemanticMapIn)
	   : TTextureReadBase(ImageSizeIn, SequenceIdIn, CaptureTimeIn, OwnerNameIn, SensorNameIn, SensorTransformIn),
	   MinDepth(MinDepthIn), MaxDepth(MaxDepthIn), InstanceToSemanticMap(MoveTemp(InstanceToSemanticMapIn))
	{
	}

	virtual FName GetType() const override { return TEXT("WithDepth"); }

	void RespondToRequests(const TArray<FColorImageRequest>& Requests, float TransmissionTime) const;
	void RespondToRequests(const TArray<FLabelImageRequest>& Requests, float TransmissionTime) const;
	void RespondToRequests(const TArray<FDepthImageRequest>& Requests, float TransmissionTime) const;
	void RespondToRequests(const TArray<FBoundingBoxesRequest>& Requests, float TransmissionTime) const;

	float MinDepth;
	float MaxDepth;
	TMap<uint8, uint8> InstanceToSemanticMap;
};

template <>
struct TTextureRead<FCameraPixelNoDepth> : TTextureReadBase<FCameraPixelNoDepth>
{
	TTextureRead(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn, const FString& OwnerNameIn,
	   const FString& SensorNameIn, const FTransform& SensorTransformIn, TMap<uint8, uint8>&& InstanceToSemanticMapIn)
	   : TTextureReadBase(ImageSizeIn, SequenceIdIn, CaptureTimeIn, OwnerNameIn, SensorNameIn, SensorTransformIn),
	   InstanceToSemanticMap(MoveTemp(InstanceToSemanticMapIn))
	{
	}

	virtual FName GetType() const override { return TEXT("NoDepth"); }

	void RespondToRequests(const TArray<FColorImageRequest>& Requests, float TransmissionTime) const;
	void RespondToRequests(const TArray<FLabelImageRequest>& Requests, float TransmissionTime) const;
	void RespondToRequests(const TArray<FBoundingBoxesRequest>& Requests, float TransmissionTime) const;

	TMap<uint8, uint8> InstanceToSemanticMap;
};

struct TEMPOCAMERA_API FTempoCameraIntrinsics
{
	FTempoCameraIntrinsics(const FIntPoint& SizeXY, float HorizontalFOV);
	const float Fx;
	const float Fy;
	const float Cx;
	const float Cy;
};

class UTempoCamera;

// Per-tile state for the multi-view atlas render. A tile is just a set of view parameters + its own
// view state + distortion assets; no USceneCaptureComponent and no child scene component. The atlas
// render is driven entirely from UTempoCamera::RenderCapture via TempoMultiViewCapture::RenderTiles.
USTRUCT()
struct FTempoCameraTile
{
	GENERATED_BODY()

	// Rotation relative to the parent camera (UE convention: positive yaw=right, positive pitch=up).
	UPROPERTY(VisibleAnywhere)
	FRotator RelativeRotation = FRotator::ZeroRotator;

	// The tile's distorted output extent — width/height in the atlas, *including* any feather
	// margin growth on shared edges. Equals the tile's perspective render output size, which is
	// what the distortion PPM produces and writes into the atlas.
	UPROPERTY(VisibleAnywhere)
	FIntPoint TileOutputSizeXY = FIntPoint::ZeroValue;

	// The tile's destination offset in the atlas. The tile occupies the rect
	// [TileDestOffset, TileDestOffset + TileOutputSizeXY) in atlas pixels. Atlas tile rects are
	// always non-overlapping; with FeatherPixels > 0 the atlas itself is wider than the
	// equidistant output to fit the per-tile feather margins disjointly.
	UPROPERTY(VisibleAnywhere)
	FIntPoint TileDestOffset = FIntPoint::ZeroValue;

	// The slice of the equidistant output image this tile "owns" (centerline pick rule). This is
	// the tile's pre-feather coverage in output space — adjacent tiles' OwnedOutput rects partition
	// the output rect exactly with no overlap. Used by the resolve-map fill to decide which tile
	// is primary at each output pixel and to measure distance to the nearest seam.
	UPROPERTY(VisibleAnywhere)
	FIntPoint OwnedOutputOffset = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere)
	FIntPoint OwnedOutputSize = FIntPoint::ZeroValue;

	// Output focal length for this tile's distortion map fill, in pixels per the model's output
	// unit (r_d for radial / Double Sphere; theta_d radians for Kannala-Brandt). All tiles share
	// the same value — it is the global pixel scale of the full distorted output image.
	double OutputFocalLength = 0.0;

	// The tile's perspective render size (set by InitTileDistortionMap; may differ from TileOutputSizeXY).
	UPROPERTY(VisibleAnywhere)
	FIntPoint SizeXY = FIntPoint::ZeroValue;

	// The tile's perspective render horizontal FOV in degrees (set by InitTileDistortionMap).
	UPROPERTY(VisibleAnywhere)
	float FOVAngle = 90.0f;

	// Signed tan-space frustum bounds (set by InitTileDistortionMap from FDistortionRenderConfig).
	// Drive an off-axis projection matrix that tightly bounds the tile's used render quadrant —
	// for symmetric tiles TanLeft = -TanRight and TanTop = -TanBottom (legacy behavior); for
	// corner / off-axis tiles the bounds are asymmetric, which removes the wasted regions of a
	// symmetric frustum and effectively raises sampling density at the tile's outer edges.
	double TanLeft = -1.0;
	double TanRight = 1.0;
	double TanTop = -1.0;
	double TanBottom = 1.0;

	// Re-aim correction in parent r_d units: how far the optical axis sits from the tile's
	// pixel-rect center. Zero for radial/single-capture tiles and for centered fisheye tiles;
	// non-zero for fisheye corner tiles where the angular centroid lies inside the tile rect.
	double AxisShiftXRd = 0.0;
	double AxisShiftYRd = 0.0;

	UPROPERTY(VisibleAnywhere, Category="Depth")
	float MinDepth = 10.0;

	UPROPERTY(VisibleAnywhere, Category="Depth")
	float MaxDepth = 100000.0;

	// Whether this tile is participating in the current multi-view family.
	UPROPERTY(VisibleAnywhere)
	bool bActive = false;

	// Distortion/label PPM active on this tile's view. Its DistortionMap parameter is bound to
	// DistortionMap below.
	UPROPERTY(VisibleAnywhere)
	UMaterialInstanceDynamic* PostProcessMaterialInstance = nullptr;

	// Per-tile distortion map (PF_G16R16F of output→render UV samples).
	UPROPERTY(VisibleAnywhere)
	UTexture2D* DistortionMap = nullptr;

	// Per-tile post-process settings — carries the distortion PPM blendable + AE bias override.
	UPROPERTY()
	FPostProcessSettings PostProcessSettings;

	// Per-tile scene view state (TAA/AE history). Not a UPROPERTY — manages its own lifetime.
	FSceneViewStateReference ViewState;

	// One-shot camera-cut flag consumed by the next multi-view render.
	bool bCameraCut = false;
};

UCLASS(Blueprintable, BlueprintType, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOCAMERA_API UTempoCamera : public UTempoTiledSceneCaptureComponent
{
	GENERATED_BODY()

public:
	UTempoCamera();

	// UTempoSceneCaptureComponent2D hooks. UTempoCamera drives its own timer and manages its own
	// readback from SharedFinalTextureTarget (not the inherited TextureTarget, which is used as
	// the LDR output of the proxy tonemap capture).
	virtual void InitRenderTarget() override;

	// Rebuild only the depth-dependent resources: SharedFinalTextureTarget (format varies with
	// depth) and the staging texture ring. Leaves the inherited TextureTarget, SharedTextureTarget,
	// and SharedAuxTextureTarget intact so the proxy's persistent view state (TAA/AE history)
	// stays consistent across depth toggles.
	void InitFinalRenderTargetAndStaging();

	void RequestMeasurement(const TempoCamera::ColorImageRequest& Request, const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuation);
	void RequestMeasurement(const TempoCamera::LabelImageRequest& Request, const TResponseDelegate<TempoCamera::LabelImage>& ResponseContinuation);
	void RequestMeasurement(const TempoCamera::DepthImageRequest& Request, const TResponseDelegate<TempoCamera::DepthImage>& ResponseContinuation);
	void RequestMeasurement(const TempoCamera::BoundingBoxesRequest& Request, const TResponseDelegate<TempoCamera::BoundingBoxes>& ResponseContinuation);

	FTempoCameraIntrinsics GetIntrinsics() const;

	// Begin ITempoSensorInterface
	virtual TOptional<TFuture<void>> SendMeasurements() override;
	// End ITempoSensorInterface

	bool HasPendingCameraRequests() const;

protected:
	virtual bool HasPendingRequests() const override { return HasPendingCameraRequests(); }
	virtual int32 GetNumActiveTiles() const override;
	virtual void RenderCapture() override;

	TFuture<void> DecodeAndRespond(TUniquePtr<FTextureRead> TextureRead);

	// Begin UTempoTiledSceneCaptureComponent tile interface
	virtual void SyncTiles() override;
	virtual bool HasDetectedParameterChange() const override;
	virtual void ReconfigureTilesNow() override;
	virtual void UpdateInternalMirrors() override;
	// End UTempoTiledSceneCaptureComponent tile interface

	// Returns SharedFinalTextureTarget so OnRenderCompleted reads from the merged output.
	virtual UTextureRenderTarget2D* GetReadbackTextureTarget() const override { return SharedFinalTextureTarget; }

	void ConfigureTile(FTempoCameraTile& Tile, double YawOffset, double PitchOffset, double FOutput, const FIntPoint& TileSizeXY, const FIntPoint& TileDestOffset, const FIntPoint& OwnedOffset, const FIntPoint& OwnedSize, double AxisShiftXRd, double AxisShiftYRd, bool bActivate);

	// Recompute OutputResolveMap and OutputResolveWeight from the current tile layout. Must run
	// after SyncTiles whenever tile geometry changes. Cheap with FeatherPixels=0 (writes the
	// identity map) and bounded by SizeXY.X * SizeXY.Y otherwise.
	void RebuildResolveMaps();
	void ApplyTilePostProcess(FTempoCameraTile& Tile);
	void SetTileDepthEnabled(FTempoCameraTile& Tile, bool bTileDepthEnabled);
	void InitTileDistortionMap(FTempoCameraTile& Tile);
	void AllocateTileViewState(FTempoCameraTile& Tile);
	void DeactivateTile(FTempoCameraTile& Tile);

	void SetDepthEnabled(bool bDepthEnabledIn);
	void ApplyDepthEnabled();

	void ValidateFOV() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// Lens distortion parameters. Used by BrownConrady (K1-K3) and Rational (K1-K6) models.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo")
	FTempoLensDistortionParameters LensParameters;

	// Feather width (in equidistant-output pixels) used to blend across tile seams in multi-tile
	// configurations. Each tile renders an extra FeatherPixels of angular margin on every shared
	// edge into a correspondingly larger atlas region; the stitch pass then linearly blends across
	// the 2*FeatherPixels-wide overlap straddling each seam, hiding the per-tile TAA/TSR history
	// discontinuity. Zero disables feathering — atlas equals the equidistant output exactly.
	// Single-tile (Brown-Conrady / Rational / single-capture equidistant) configurations have no
	// seams and ignore this setting. Default 16 px is wide enough to mask per-tile temporal-history
	// differences while keeping perimeter overdraw under a few percent.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo", meta=(UIMin=0, UIMax=64, ClampMin=0, ClampMax=128))
	int32 FeatherPixels = 16;

	// When true, each tile rasterizes at ScreenPercentage% of its view-rect size and TSR (or
	// TAAU, or spatial upscale, depending on the tile's AA method) upsamples to the full view
	// rect. Tunes rasterization-vs-view-rect ratio only — the perspective render's resolution
	// (the view rect, which is what the distortion PPM samples as scene color) is unchanged.
	// Use UpsamplingFactor to change that. Off by default to match the engine's stock
	// scene-capture behavior; turn on to trade some shading detail for substantial GPU savings.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo")
	bool bEnableScreenPercentage = false;

	// Per-tile primary screen percentage (rasterization fraction, in percent of the tile's view
	// rect). Applied via the multi-view family's FLegacyScreenPercentageDriver and only honored
	// when bEnableScreenPercentage is true. 100 = no upscale. <100 = render smaller, upsample
	// (TSR when AA is TSR/TAA — the default for our tiles — else spatial). >100 = supersample.
	// Independent of UpsamplingFactor: this scales rasterization relative to the view rect; the
	// view rect itself is set by SizeXY * UpsamplingFactor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo", meta=(EditCondition="bEnableScreenPercentage", UIMin=25, UIMax=200, ClampMin=25, ClampMax=200))
	float ScreenPercentage = 100.0f;

	// Scales the perspective render's view-rect resolution (and hence the resolution of the
	// scene color the distortion PPM resamples) by this factor. The equidistant output stays
	// at SizeXY — the K× distorted atlas is bilinearly downsampled to SizeXY by the existing
	// stitch+feather pass. Useful when distortion is concentrated in a small angular region
	// (wide-FOV / fisheye) and 1:1 perspective:output sampling produces visibly fuzzy or
	// pixelated regions in the resampled output. Independent of bEnableScreenPercentage. Atlas
	// and aux RT memory grow by K². K=1 disables (byte-identical to no-upsampling behavior).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo", meta=(UIMin=1.0, UIMax=4.0, ClampMin=1.0, ClampMax=4.0))
	float UpsamplingFactor = 1.0f;

	// Whether this camera can measure depth. Disabled when not requested to optimize performance.
	UPROPERTY(VisibleAnywhere, Category = "Tempo")
	bool bDepthEnabled = false;

	// The minimum depth this camera can measure (if depth is enabled). Will be set to the global near clip plane.
	UPROPERTY(VisibleAnywhere, Category = "Tempo")
	float MinDepth = 10.0;

	// The maximum depth this camera can measure (if depth is enabled). Will be set to UTempoSensorsSettings::MaxCameraDepth.
	UPROPERTY(VisibleAnywhere, Category = "Tempo")
	float MaxDepth = 100000.0;

	int32 NumResponded = 0;

	TArray<FColorImageRequest> PendingColorImageRequests;
	TArray<FLabelImageRequest> PendingLabelImageRequests;
	TArray<FDepthImageRequest> PendingDepthImageRequests;
	TArray<FBoundingBoxesRequest> PendingBoundingBoxesRequests;

	// Tile slots (fixed size: TL, TR, BL, BR). Stable storage — indices are never invalidated
	// and held addresses remain valid across reconfigures. Transient: runtime-only state.
	UPROPERTY(Transient)
	FTempoCameraTile Tiles[4];

	// Internal tracking for runtime change detection. Mirrors the watched properties after
	// they have been pushed to tiles via ReconfigureTilesNow.
	FTempoLensDistortionParameters LensParameters_Internal;
	float FOVAngle_Internal = -1.0f;
	FIntPoint SizeXY_Internal = FIntPoint(-1, -1);
	int32 FeatherPixels_Internal = -1;
	float UpsamplingFactor_Internal = -1.0f;

	// Shared render target holding the stitched aux output (label + depth bytes) from all
	// active tiles. Merged with the proxy capture's tonemapped color into SharedFinalTextureTarget.
	UPROPERTY(Transient, VisibleAnywhere)
	UTextureRenderTarget2D* SharedAuxTextureTarget = nullptr;

	// HDR, sized to SizeXY (the equidistant output). Produced by the stitch+feather Canvas pass
	// from SharedTextureTarget (the per-tile atlas) via OutputResolveMap/OutputResolveWeight, and
	// consumed by the proxy tonemap PPM as scene color (HDRColorRT). Size matches the proxy
	// capture's TextureTarget so the canvas UV mapping is identity.
	UPROPERTY(Transient, VisibleAnywhere)
	UTextureRenderTarget2D* SharedStitchHDRTextureTarget = nullptr;

	// Per-output-pixel atlas UVs of the two contributing tiles (RGBA fp16: AtlasU_A, AtlasV_A,
	// AtlasU_B, AtlasV_B). Sized to SizeXY. With FeatherPixels=0 only the A pair is meaningful and
	// equals output UV remapped through the (identity) atlas layout.
	UPROPERTY(Transient, VisibleAnywhere)
	UTexture2D* OutputResolveMap = nullptr;

	// Per-output-pixel blend weight (R fp16). Encodes how much tile A contributes; tile B
	// contributes 1-Weight. Pixels outside any feather band have Weight=1 (only A contributes).
	// At a seam centerline Weight=0.5. Sized to SizeXY.
	UPROPERTY(Transient, VisibleAnywhere)
	UTexture2D* OutputResolveWeight = nullptr;

	// Atlas size in pixels. Equals SizeXY when there are no inter-tile seams (single-tile or
	// FeatherPixels=0); otherwise grows by 2*FeatherPixels per shared seam axis to give each
	// tile's covered output a disjoint atlas region.
	FIntPoint AtlasSize = FIntPoint::ZeroValue;

	// Final merged render target. A Canvas pass using the merge material combines the proxy's
	// tonemapped TextureTarget with SharedAuxTextureTarget into this RT; the staging copy reads
	// from here.
	UPROPERTY(Transient, VisibleAnywhere)
	UTextureRenderTarget2D* SharedFinalTextureTarget = nullptr;

	// Dynamic instance of the stitch merge material, with its "ColorRT" and "AuxRT" parameters
	// bound to the tonemapped (inherited TextureTarget) and aux render targets respectively.
	// Created lazily.
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* MergeMID = nullptr;

	// Create MergeMID if needed and (re)bind its ColorRT/AuxRT parameters. Returns nullptr if
	// the merge material is not configured or either source RT is missing.
	UMaterialInstanceDynamic* GetOrCreateStitchMergeMID();

	// Dynamic instance of the aux unpack material with its "TileRT" parameter bound to
	// SharedTextureTarget. The material samples atlas alpha through the resolve map (using the
	// owning-tile UV when Weight >= 0.5, else the secondary tile UV) and unpacks label+depth
	// bits into SharedAuxTextureTarget. Pick-by-ownership (rather than blending) is intentional —
	// label is integer and depth is bit-packed, neither is safely averageable.
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* AuxAtlasMID = nullptr;

	// Create AuxAtlasMID if needed and (re)bind TileRT/OutputResolveMap/OutputResolveWeight.
	// Returns nullptr if the aux material is not configured or SharedTextureTarget is missing.
	UMaterialInstanceDynamic* GetOrCreateAuxAtlasMID();

	// Dynamic instance of the stitch+feather material. Run as a full-screen Canvas pass over
	// SharedStitchHDRTextureTarget. Samples SharedTextureTarget at the two atlas UVs from the
	// resolve map and lerps by the weight, producing the linear HDR equidistant output that the
	// proxy tonemap PPM then consumes as scene color.
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* StitchColorMID = nullptr;

	// Create StitchColorMID if needed and (re)bind AtlasRT/OutputResolveMap/OutputResolveWeight.
	UMaterialInstanceDynamic* GetOrCreateStitchColorMID();

	// Dynamic instance of the proxy tonemap post-process material, with its "HDRColorRT"
	// parameter bound to SharedTextureTarget. Created lazily and appended to this component's
	// PostProcessSettings.WeightedBlendables so it runs during the proxy scene capture.
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* ProxyTonemapMID = nullptr;

	// Create ProxyTonemapMID if needed, (re)bind its HDRColorRT parameter to SharedTextureTarget,
	// and ensure it is present in this component's PostProcessSettings.WeightedBlendables.
	UMaterialInstanceDynamic* GetOrCreateProxyTonemapMID();

	// Shared pre-exposure (EV stops) pushed onto every tile's AutoExposureBias before each capture.
	// Driven by a P-controller fed from the proxy's AE-reported luminance.
	float SharedExposureBias = 0.0f;
	bool bHasValidSharedExposure = false;

	// Tracks whether the previous capture used the single-tile fast path. When this flips, the
	// active tile's TAA/AE history was conditioned on a different post-process configuration;
	// force a camera cut on transition so TAA doesn't sample stale history.
	bool bWasSingleTileFastPath = false;

	// Retention list for distortion textures that have been replaced but may still be referenced
	// by in-flight render commands.
	UPROPERTY(Transient)
	TArray<UTexture2D*> RetainedDistortionMaps;

	// Retire a distortion map that's being replaced: moves it into the retention list so it
	// can't be GC'd while render commands from prior captures still reference it.
	void RetireDistortionMap(UTexture2D* DistortionMap);
};
