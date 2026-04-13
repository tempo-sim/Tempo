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

	virtual FTextureRead* MakeTextureRead() const override;

	virtual int32 GetMaxTextureQueueSize() const override;

	virtual void InitDistortionMap() override;

	UPROPERTY(VisibleAnywhere)
	UMaterialInstanceDynamic* PostProcessMaterialInstance = nullptr;

	UPROPERTY(VisibleAnywhere)
	const UTempoCamera* CameraOwner = nullptr;

	UPROPERTY(VisibleAnywhere, Category="Depth")
	float MinDepth = 10.0;

	UPROPERTY(VisibleAnywhere, Category="Depth")
	float MaxDepth = 100000.0;

	friend UTempoCamera;
};

struct FCameraTileTextureRead
{
	TUniquePtr<FTextureRead> TextureRead;
	FIntPoint TileDestOffset;
	FIntPoint TileOutputSizeXY;
	int32 TileOutputOffsetY;
};

UCLASS(Blueprintable, BlueprintType, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOCAMERA_API UTempoCamera : public USceneComponent, public ITempoSensorInterface
{
	GENERATED_BODY()

public:
	UTempoCamera();

	virtual void OnRegister() override;

	virtual void BeginPlay() override;

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
	TFuture<void> DecodeAndRespond(TUniquePtr<FTextureRead> TextureRead);

	TFuture<void> StitchAndRespond(TArray<FCameraTileTextureRead> TileReads);

	TMap<FName, UTempoCameraCaptureComponent*> GetAllCaptureComponents() const;
	TMap<FName, UTempoCameraCaptureComponent*> GetOrCreateCaptureComponents();
	TArray<UTempoCameraCaptureComponent*> GetActiveCaptureComponents() const;

	void SyncCaptureComponents();

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

	// Output image resolution.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo")
	FIntPoint SizeXY = FIntPoint(960, 540);

	// The rate in Hz this camera updates at.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo", meta = (UIMin = 0.0, ClampMin = 0.0))
	float RateHz = 10.0;

	// Post-process settings applied to the managed capture components.
	// The distortion/label post-process material is appended to WeightedBlendables automatically.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Rendering", meta = (ShowOnlyInnerProperties))
	FPostProcessSettings PostProcessSettings;

	// Show flag overrides applied to the managed capture components.
	// Each entry toggles a single named show flag; unset flags keep their engine defaults.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Rendering")
	TArray<FEngineShowFlagsSetting> ShowFlagSettings;

	// Whether this camera can measure depth. Disabled when not requested to optimize performance.
	UPROPERTY(VisibleAnywhere, Category = "Depth")
	bool bDepthEnabled = false;

	// The minimum depth this camera can measure (if depth is enabled). Will be set to the global near clip plane.
	UPROPERTY(VisibleAnywhere, Category = "Depth")
	float MinDepth = 10.0;

	// The maximum depth this camera can measure (if depth is enabled). Will be set to UTempoSensorsSettings::MaxCameraDepth.
	UPROPERTY(VisibleAnywhere, Category = "Depth")
	float MaxDepth = 100000.0;

	// Monotonically increasing counter of frames captured.
	UPROPERTY(VisibleAnywhere)
	int32 SequenceId = 0;

	int32 NumResponded = 0;

	TArray<FColorImageRequest> PendingColorImageRequests;
	TArray<FLabelImageRequest> PendingLabelImageRequests;
	TArray<FDepthImageRequest> PendingDepthImageRequests;
	TArray<FBoundingBoxesRequest> PendingBoundingBoxesRequests;

	// Internal tracking for change detection
	FTempoLensDistortionParameters LensParameters_Internal;
	float HorizontalFOV_Internal = -1.0f;
	FIntPoint SizeXY_Internal = FIntPoint(-1, -1);

	friend class UTempoCameraCaptureComponent;
};
