// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCamera/Camera.pb.h"
#include "TempoSensorInterface.h"
#include "TempoSceneCaptureComponent2D.h"
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
// render is driven entirely from UTempoCamera::MaybeCapture via TempoMultiViewCapture::RenderTiles.
USTRUCT()
struct FTempoCameraTile
{
	GENERATED_BODY()

	// Rotation relative to the parent camera (UE convention: positive yaw=right, positive pitch=up).
	UPROPERTY(VisibleAnywhere)
	FRotator RelativeRotation = FRotator::ZeroRotator;

	// The output tile dimensions in the final stitched image.
	UPROPERTY(VisibleAnywhere)
	FIntPoint TileOutputSizeXY = FIntPoint::ZeroValue;

	// The destination offset of this tile within the atlas.
	UPROPERTY(VisibleAnywhere)
	FIntPoint TileDestOffset = FIntPoint::ZeroValue;

	// The equidistant tile's horizontal FOV in radians (only used for equidistant distortion model).
	double EquidistantTileHFOVRad = 0.0;

	// The tile's perspective render size (set by InitTileDistortionMap; may differ from TileOutputSizeXY).
	UPROPERTY(VisibleAnywhere)
	FIntPoint SizeXY = FIntPoint::ZeroValue;

	// The tile's perspective render horizontal FOV in degrees (set by InitTileDistortionMap).
	UPROPERTY(VisibleAnywhere)
	float FOVAngle = 90.0f;

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
class TEMPOCAMERA_API UTempoCamera : public UTempoSceneCaptureComponent2D, public ITempoSensorInterface
{
	GENERATED_BODY()

public:
	UTempoCamera();

	virtual void OnRegister() override;

	virtual void BeginPlay() override;

	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// UTempoSceneCaptureComponent2D hooks. UTempoCamera drives its own timer and manages its own
	// readback from SharedFinalTextureTarget (not the inherited TextureTarget, which is used as
	// the LDR output of the proxy tonemap capture).
	virtual bool ShouldManageOwnReadback() const override { return false; }
	virtual bool ShouldManageOwnTimer() const override { return false; }
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
	virtual FString GetOwnerName() const override;
	virtual FString GetSensorName() const override;
	virtual float GetRate() const override { return RateHz; }
	virtual const TArray<TEnumAsByte<EMeasurementType>>& GetMeasurementTypes() const override { return MeasurementTypes; }
	virtual bool IsAwaitingRender() override;
	virtual void OnRenderCompleted() override;
	virtual void BlockUntilMeasurementsReady() const override;
	virtual TOptional<TFuture<void>> SendMeasurements() override;
	// End ITempoSensorInterface

	bool HasPendingCameraRequests() const;

protected:
	// UTempoSceneCaptureComponent2D hooks.
	virtual bool HasPendingRequests() const override { return HasPendingCameraRequests(); }
	virtual FTextureRead* MakeTextureRead() const override;
	virtual int32 GetMaxTextureQueueSize() const override;

	TFuture<void> DecodeAndRespond(TUniquePtr<FTextureRead> TextureRead);

	// Starts or restarts the timer that calls MaybeCapture.
	void RestartCaptureTimer();

	// Called by the capture timer. Issues CaptureScene() on each active tile, then enqueues a
	// single render command that stitches tile RTs into the shared RT and initiates a single readback.
	void MaybeCapture();

	// Returns the next staging texture from the ring buffer and advances the index.
	FTextureRHIRef AcquireNextStagingTexture();

	// Per-tile configuration / state management. Tiles live in Tiles[] as plain USTRUCTs.
	void SyncTiles();
	void ConfigureTile(FTempoCameraTile& Tile, double YawOffset, double PitchOffset, double PerspectiveFOV, const FIntPoint& TileSizeXY, const FIntPoint& TileDestOffset, bool bActivate);
	void ApplyTilePostProcess(FTempoCameraTile& Tile);
	void SetTileDepthEnabled(FTempoCameraTile& Tile, bool bTileDepthEnabled);
	void InitTileDistortionMap(FTempoCameraTile& Tile);
	void AllocateTileViewState(FTempoCameraTile& Tile);
	void DeactivateTile(FTempoCameraTile& Tile);

	// Returns true iff any watched property differs from its _Internal mirror.
	bool HasDetectedParameterChange() const;

	// Apply any pending reconfigure iff it is safe to do so (no in-flight reads). No-op otherwise.
	void TryApplyPendingReconfigure();

	// Deactivate all active tiles, then re-sync. Callers should ensure no reads are in flight.
	void ReconfigureTilesNow();

	// Snapshot the watched properties into their _Internal mirrors.
	void UpdateInternalMirrors();

	void SetDepthEnabled(bool bDepthEnabledIn);
	void ApplyDepthEnabled();

	void ValidateFOV() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// The measurement types supported.
	UPROPERTY(VisibleAnywhere)
	TArray<TEnumAsByte<EMeasurementType>> MeasurementTypes;

	// Lens distortion parameters. Used by BrownConrady (K1-K3) and Rational (K1-K6) models.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	FTempoLensDistortionParameters LensParameters;

	// FOVAngle (horizontal), SizeXY, RateHz, SequenceId, PostProcessSettings, ShowFlagSettings,
	// and bUseRayTracingIfEnabled are inherited from UTempoSceneCaptureComponent2D /
	// USceneCaptureComponent(2D).

	// Whether this camera can measure depth. Disabled when not requested to optimize performance.
	UPROPERTY(VisibleAnywhere, Category = "Depth")
	bool bDepthEnabled = false;

	// The minimum depth this camera can measure (if depth is enabled). Will be set to the global near clip plane.
	UPROPERTY(VisibleAnywhere, Category = "Depth")
	float MinDepth = 10.0;

	// The maximum depth this camera can measure (if depth is enabled). Will be set to UTempoSensorsSettings::MaxCameraDepth.
	UPROPERTY(VisibleAnywhere, Category = "Depth")
	float MaxDepth = 100000.0;

	int32 NumResponded = 0;

	TArray<FColorImageRequest> PendingColorImageRequests;
	TArray<FLabelImageRequest> PendingLabelImageRequests;
	TArray<FDepthImageRequest> PendingDepthImageRequests;
	TArray<FBoundingBoxesRequest> PendingBoundingBoxesRequests;

	// Tile slots (fixed size: TL, TR, BL, BR). Stable storage — indices are never invalidated
	// and held addresses remain valid across reconfigures. Transient: runtime-only state.
	UPROPERTY(Transient)
	TArray<FTempoCameraTile> Tiles;

	// Internal tracking for runtime change detection. Mirrors the watched properties after
	// they have been pushed to tiles via ReconfigureTilesNow.
	FTempoLensDistortionParameters LensParameters_Internal;
	float FOVAngle_Internal = -1.0f;
	FIntPoint SizeXY_Internal = FIntPoint(-1, -1);

	// Set when HasDetectedParameterChange() sees a diff; cleared after a successful apply.
	uint8 bReconfigurePending = false;

	// Shared render target holding the stitched output from all active tiles.
	UPROPERTY(Transient, VisibleAnywhere)
	UTextureRenderTarget2D* SharedTextureTarget = nullptr;

	// Shared render target holding the stitched aux output (label + depth bytes) from all
	// active tiles. Merged with the proxy capture's tonemapped color into SharedFinalTextureTarget.
	UPROPERTY(Transient, VisibleAnywhere)
	UTextureRenderTarget2D* SharedAuxTextureTarget = nullptr;

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
	// SharedTextureTarget — one full-screen Canvas draw samples SharedTextureTarget.a over the
	// whole atlas and unpacks label+depth bits into SharedAuxTextureTarget. Shares the same
	// material as the legacy per-tile aux pass; only the source texture (full atlas vs per-tile
	// RT) and Canvas extent (whole atlas vs tile rect) differ.
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* AuxAtlasMID = nullptr;

	// Create AuxAtlasMID if needed and (re)bind its TileRT parameter to SharedTextureTarget.
	// Returns nullptr if the aux material is not configured or SharedTextureTarget is missing.
	UMaterialInstanceDynamic* GetOrCreateAuxAtlasMID();

	// Dynamic instance of the proxy tonemap post-process material, with its "HDRColorRT"
	// parameter bound to SharedTextureTarget. Created lazily and appended to this component's
	// PostProcessSettings.WeightedBlendables so it runs during the proxy scene capture.
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* ProxyTonemapMID = nullptr;

	// Create ProxyTonemapMID if needed, (re)bind its HDRColorRT parameter to SharedTextureTarget,
	// and ensure it is present in this component's PostProcessSettings.WeightedBlendables.
	UMaterialInstanceDynamic* GetOrCreateProxyTonemapMID();

	// Shared pre-exposure (EV stops) pushed onto every tile's AutoExposureBias before each capture.
	// Driven by a P-controller fed from the proxy's AE-reported luminance. Lifts tile HDR values
	// into a range where TAA's neighborhood clamp can actually reject stale history — without which
	// low-light scenes ghost for seconds. See MaybeCapture for the update.
	float SharedExposureBias = 0.0f;
	bool bHasValidSharedExposure = false;

	// Ring buffer of staging textures for GPU->CPU readback of the shared RT.
	TArray<FTextureRHIRef> StagingTextures;
	FCriticalSection StagingTexturesMutex;
	int32 NextStagingIndex = 0;

	// Queue of pending texture reads for the shared RT (one entry per capture).
	FTextureReadQueue TextureReadQueue;

	// Fence indicating that staging texture init has completed on the render thread.
	FRenderCommandFence TextureInitFence;

	FTimerHandle TimerHandle;
};
