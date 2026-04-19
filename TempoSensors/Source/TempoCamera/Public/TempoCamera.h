// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCamera/Camera.pb.h"
#include "TempoSensorInterface.h"
#include "TempoSceneCaptureComponent2D.h"
#include "TempoScriptingServer.h"

#include "CoreMinimal.h"
#include "Engine/Scene.h"
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

UCLASS(ClassGroup=(Custom), NotPlaceable, NotBlueprintable)
class TEMPOCAMERA_API UTempoCameraCaptureComponent : public UTempoSceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UTempoCameraCaptureComponent();

	virtual void OnRegister() override;

	// Configure this capture component for a tile of the camera's output.
	// YawOffset/PitchOffset: orientation relative to parent camera (UE convention: positive yaw=right, positive pitch=up).
	// EquidistantTileFOV: the horizontal FOV this tile covers in the equidistant output (degrees).
	// TileSizeXY: the output tile dimensions in the final stitched image.
	void Configure(double YawOffset, double PitchOffset, double EquidistantTileFOV, const FIntPoint& TileSizeXY, const FIntPoint& TileDestOffset);

	void SetDepthEnabled(bool bDepthEnabled);

	// Apply PostProcessSettings and ShowFlagSettings from the owning UTempoCamera onto this component.
	void ApplyRenderSettings();

	// The output tile dimensions.
	UPROPERTY(VisibleAnywhere)
	FIntPoint TileOutputSizeXY = FIntPoint::ZeroValue;

	// The vertical offset in pixels from the top of the render target to the start of the tile output.
	UPROPERTY(VisibleAnywhere)
	int32 TileOutputOffsetY = 0;

	// The equidistant tile's horizontal FOV in radians (only used for equidistant distortion model).
	double EquidistantTileHFOVRad = 0.0;

	// The destination offset of this tile within the final stitched output image.
	UPROPERTY(VisibleAnywhere)
	FIntPoint TileDestOffset = FIntPoint::ZeroValue;

	virtual void Activate(bool bReset = false) override;

protected:
	virtual bool HasPendingRequests() const override;

	// Tiles never allocate their own staging textures or FTextureReads — the owning UTempoCamera
	// stitches tile render targets into a shared RT and issues a single readback for the sensor.
	virtual bool ShouldManageOwnReadback() const override { return false; }

	// Tiles never start their own capture timer — the owning UTempoCamera drives CaptureScene()
	// on each active tile from its own timer callback.
	virtual bool ShouldManageOwnTimer() const override { return false; }

	virtual FTextureRead* MakeTextureRead() const override;

	virtual int32 GetMaxTextureQueueSize() const override;

	virtual void InitDistortionMap() override;

	UPROPERTY(VisibleAnywhere)
	UMaterialInstanceDynamic* PostProcessMaterialInstance = nullptr;

	// Dynamic instance of the stitch passthrough material, with its "TileRT" parameter bound to
	// this tile's TextureTarget. Created lazily; owned by this component.
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* StitchMID = nullptr;

	// Dynamic instance of the stitch aux material, with its "TileRT" parameter bound to this tile's
	// TextureTarget. Created lazily; owned by this component.
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* StitchAuxMID = nullptr;

	// Create StitchMID if needed and (re)bind its TileRT parameter to the current TextureTarget.
	// Safe to call before TextureTarget exists — in that case, returns nullptr and does nothing.
	UMaterialInstanceDynamic* GetOrCreateStitchMID();

	// Same as GetOrCreateStitchMID but for the aux material.
	UMaterialInstanceDynamic* GetOrCreateStitchAuxMID();

	UPROPERTY(VisibleAnywhere)
	const UTempoCamera* CameraOwner = nullptr;

	UPROPERTY(VisibleAnywhere, Category="Depth")
	float MinDepth = 10.0;

	UPROPERTY(VisibleAnywhere, Category="Depth")
	float MaxDepth = 100000.0;

	friend UTempoCamera;
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

	TMap<FName, UTempoCameraCaptureComponent*> GetAllCaptureComponents() const;
	TMap<FName, UTempoCameraCaptureComponent*> GetOrCreateCaptureComponents();
	TArray<UTempoCameraCaptureComponent*> GetActiveCaptureComponents() const;
	void SyncCaptureComponents();

	// Returns true iff any watched property differs from its _Internal mirror.
	bool HasDetectedParameterChange() const;

	// Returns true iff any active capture component has a texture read still in flight.
	bool AnyCaptureReadsInFlight() const;

	// Apply any pending reconfigure iff it is safe to do so (no in-flight reads). No-op otherwise.
	void TryApplyPendingReconfigure();

	// Deactivate all active tiles (draining their texture read queues) then re-sync.
	// Caller must have confirmed no reads are in flight.
	void ReconfigureCaptureComponentsNow();

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

	// The horizontal field of view of the output image in degrees.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens", meta = (ClampMin = "1.0", ClampMax = "240.0"))
	float HorizontalFOV = 90.0f;

	// Lens distortion parameters. Used by BrownConrady (K1-K3) and Rational (K1-K6) models.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	FTempoLensDistortionParameters LensParameters;

	// Note: SizeXY, RateHz, SequenceId, PostProcessSettings, ShowFlagSettings, and
	// bUseRayTracingIfEnabled are inherited from UTempoSceneCaptureComponent2D /
	// USceneCaptureComponent(2D). UTempoCamera propagates PostProcessSettings/ShowFlagSettings
	// to its tile components (with AutoExposureMethod forced to AEM_Manual on tiles) and uses
	// them for its own proxy scene capture (with AEM_Histogram). The proxy PPM is appended to
	// the inherited PostProcessSettings.WeightedBlendables at runtime.

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

	// Internal tracking for runtime change detection. Mirrors the watched properties after
	// they have been pushed to the capture components via ReconfigureCaptureComponentsNow.
	FTempoLensDistortionParameters LensParameters_Internal;
	float HorizontalFOV_Internal = -1.0f;
	FIntPoint SizeXY_Internal = FIntPoint(-1, -1);

	// Set when HasDetectedParameterChange() sees a diff; cleared after a successful apply.
	uint8 bReconfigurePending = false;

	// Shared render target holding the stitched output from all active tiles.
	UPROPERTY(Transient, VisibleAnywhere)
	UTextureRenderTarget2D* SharedTextureTarget = nullptr;

	// Shared render target holding the stitched aux output (label + depth) from all active tiles.
	// In Phase 2 this is written but not consumed; in Phase 3 it will be merged with the proxy
	// capture's LDR color output into the staging textures for readback.
	UPROPERTY(Transient, VisibleAnywhere)
	UTextureRenderTarget2D* SharedAuxTextureTarget = nullptr;

	// Final merged render target. A Canvas pass using the merge material combines SharedTextureTarget
	// and SharedAuxTextureTarget into this RT, and the staging copy reads from here. In Phase 3a-i
	// the merge is a passthrough of SharedTextureTarget, so output is bit-identical to Phase 2.
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

	// Dynamic instance of the proxy tonemap post-process material, with its "HDRColorRT"
	// parameter bound to SharedTextureTarget. Created lazily and appended to this component's
	// PostProcessSettings.WeightedBlendables so it runs during the proxy scene capture.
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* ProxyTonemapMID = nullptr;

	// Create ProxyTonemapMID if needed, (re)bind its HDRColorRT parameter to SharedTextureTarget,
	// and ensure it is present in this component's PostProcessSettings.WeightedBlendables.
	UMaterialInstanceDynamic* GetOrCreateProxyTonemapMID();

	// Ring buffer of staging textures for GPU->CPU readback of the shared RT.
	TArray<FTextureRHIRef> StagingTextures;
	FCriticalSection StagingTexturesMutex;
	int32 NextStagingIndex = 0;

	// Queue of pending texture reads for the shared RT (one entry per capture).
	FTextureReadQueue TextureReadQueue;

	// Fence indicating that staging texture init has completed on the render thread.
	FRenderCommandFence TextureInitFence;

	FTimerHandle TimerHandle;

	friend class UTempoCameraCaptureComponent;
};
