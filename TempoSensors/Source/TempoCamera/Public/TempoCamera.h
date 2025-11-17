// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCamera/Camera.pb.h"

#include "TempoSensorInterface.h"
#include "TempoSceneCaptureComponent2D.h"

#include "TempoScriptingServer.h"

#include "CoreMinimal.h"

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
		// See https://developer.nvidia.com/content/depth-precision-visualized
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

struct FColorImageWithBoundingBoxesRequest
{
	TempoCamera::ColorImageRequest Request;
	TResponseDelegate<TempoCamera::ColorImageWithBoundingBoxes> ResponseContinuation;
};

template <>
struct TTextureRead<FCameraPixelWithDepth> : TTextureReadBase<FCameraPixelWithDepth>
{
	TTextureRead(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn, const FString& OwnerNameIn,
		const FString& SensorNameIn, const FTransform& SensorTransformIn, float MinDepthIn, float MaxDepthIn)
	   : TTextureReadBase(ImageSizeIn, SequenceIdIn, CaptureTimeIn, OwnerNameIn, SensorNameIn, SensorTransformIn), MinDepth(MinDepthIn), MaxDepth(MaxDepthIn)
	{
	}
	
	virtual FName GetType() const override { return TEXT("WithDepth"); }

	void RespondToRequests(const TArray<FColorImageRequest>& Requests, float TransmissionTime) const;
	void RespondToRequests(const TArray<FLabelImageRequest>& Requests, float TransmissionTime) const;
	void RespondToRequests(const TArray<FDepthImageRequest>& Requests, float TransmissionTime) const;
	void RespondToRequests(const TArray<FColorImageWithBoundingBoxesRequest>& Requests, float TransmissionTime) const;

	float MinDepth;
	float MaxDepth;
};

template <>
struct TTextureRead<FCameraPixelNoDepth> : TTextureReadBase<FCameraPixelNoDepth>
{
	using TTextureReadBase::TTextureReadBase;
	
	virtual FName GetType() const override { return TEXT("NoDepth"); }

	void RespondToRequests(const TArray<FColorImageRequest>& Requests, float TransmissionTime) const;
	void RespondToRequests(const TArray<FLabelImageRequest>& Requests, float TransmissionTime) const;
	void RespondToRequests(const TArray<FColorImageWithBoundingBoxesRequest>& Requests, float TransmissionTime) const;
};

struct TEMPOCAMERA_API FTempoCameraIntrinsics
{
	FTempoCameraIntrinsics(const FIntPoint& SizeXY, float HorizontalFOV);
	const float Fx;
	const float Fy;
	const float Cx;
	const float Cy;
};

UCLASS(Blueprintable, BlueprintType, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOCAMERA_API UTempoCamera : public UTempoSceneCaptureComponent2D, public ITempoSensorInterface
{
	GENERATED_BODY()

public:
	UTempoCamera();

	virtual void BeginPlay() override;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override;
#else
	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene, ISceneRenderBuilder& SceneRenderBuilder) override;
#endif

	void RequestMeasurement(const TempoCamera::ColorImageRequest& Request, const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuation);

	void RequestMeasurement(const TempoCamera::LabelImageRequest& Request, const TResponseDelegate<TempoCamera::LabelImage>& ResponseContinuation);

	void RequestMeasurement(const TempoCamera::DepthImageRequest& Request, const TResponseDelegate<TempoCamera::DepthImage>& ResponseContinuation);

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

protected:
	virtual bool HasPendingRequests() const override;

	virtual FTextureRead* MakeTextureRead() const override;
	
	virtual TFuture<void> DecodeAndRespond(TUniquePtr<FTextureRead> TextureRead);

	virtual int32 GetMaxTextureQueueSize() const override;

	void SetDepthEnabled(bool bDepthEnabledIn);

	void ApplyDepthEnabled();

	// The measurement types supported. Should be set in constructor of derived classes.
	UPROPERTY(VisibleAnywhere)
	TArray<TEnumAsByte<EMeasurementType>> MeasurementTypes;

	// Whether this camera can measure depth. Disabled when not requested to optimize performance.
	UPROPERTY(VisibleAnywhere, Category="Depth")
	bool bDepthEnabled = false;

	// The minimum depth this camera can measure (if depth is enabled). Will be set to the global near clip plane.
	UPROPERTY(VisibleAnywhere, Category="Depth")
	float MinDepth = 10.0; // 10cm

	// The maximum depth this camera can measure (if depth is enabled). Will be set to UTempoSensorsSettings::MaxCameraDepth.
	UPROPERTY(VisibleAnywhere, Category="Depth")
	float MaxDepth = 100000.0; // 1km

	UPROPERTY(VisibleAnywhere)
	UMaterialInstanceDynamic* PostProcessMaterialInstance= nullptr;

	TArray<FColorImageRequest> PendingColorImageRequests;
	TArray<FLabelImageRequest> PendingLabelImageRequests;
	TArray<FDepthImageRequest> PendingDepthImageRequests;
	TArray<FColorImageWithBoundingBoxesRequest> PendingColorImageWithBoundingBoxesRequests;
};
