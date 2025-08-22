// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSceneCaptureComponent2D.h"

#include "TempoLidar/Lidar.pb.h"

#include "CoreMinimal.h"
#include "TempoScriptingServer.h"
#include "TempoLidar.generated.h"

struct FLidarScanRequest
{
	TempoLidar::LidarScanRequest Request;
	TResponseDelegate<TempoLidar::LidarScanSegment> ResponseContinuation;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOLIDAR_API UTempoLidar : public USceneComponent, public ITempoSensorInterface
{
	GENERATED_BODY()

public:
	UTempoLidar();

	virtual void BeginPlay() override;

	// Begin ITempoSensorInterface
	virtual FString GetOwnerName() const override;
	virtual FString GetSensorName() const override;
	virtual float GetRate() const override { return RateHz; }
	virtual const TArray<TEnumAsByte<EMeasurementType>>& GetMeasurementTypes() const override { return MeasurementTypes; }
	virtual bool IsAwaitingRender() override;
	virtual void OnRenderCompleted() override;
	virtual void BlockUntilMeasurementsReady() const override;
	virtual TArray<TFuture<void>> SendMeasurements() override;
	// End ITempoSensorInterface

	void RequestMeasurement(const TempoLidar::LidarScanRequest& Request, const TResponseDelegate<TempoLidar::LidarScanSegment>& ResponseContinuation);

protected:
	void AddCaptureComponent(float YawOffset, float SubHorizontalFOV, int32 SubHorizontalBeams);

	virtual TFuture<void> DecodeAndRespond(TArray<TUniquePtr<FTextureRead>> TextureReads);

	// The rate in Hz this Lidar updates at.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, ClampMin=0.0))
	float RateHz = 10.0;

	// The measurement types supported. Should be set in constructor of derived classes.
	UPROPERTY(VisibleAnywhere)
	TArray<TEnumAsByte<EMeasurementType>> MeasurementTypes;

	// The minimum depth this Lidar can measure.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, ClampMin=0.0))
	float MinRange = 10.0; // 10cm
	
	// The maximum depth this Lidar can measure.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, ClampMin=0.0))
	float MaxRange = 10000.0; // 100m

	// The standard deviation of range noise to apply to measurements.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, ClampMin=0.0))
	float RangeStdDev = 1.0; // 1cm

	// The vertical field of view in degrees.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0001, UIMax=180.0, ClampMin=0.0001, ClampMax=180.0))
	float VerticalFOV = 30.0;
	
	// The horizontal field of view in degrees.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0001, UIMax=360.0, ClampMin=0.0001, ClampMax=360.0))
	float HorizontalFOV = 120.0;

	// The number of vertical beams.
	UPROPERTY(EditAnywhere, meta=(UIMin=1, ClampMin=1))
	int32 VerticalBeams = 64;

	// The number of horizontal beams.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0001, UIMax=359.9999, ClampMin=0.0001, ClampMax=359.9999))
	int32 HorizontalBeams = 200;

	int32 NumResponded = 0;

	UPROPERTY()
	TArray<UTempoLidarCaptureComponent*> LidarCaptureComponents;

	TArray<FLidarScanRequest> PendingRequests;

	friend class UTempoLidarCaptureComponent;

	friend struct TTextureRead<float>;
};

template <>
struct TTextureRead<float> : TTextureReadBase<float>
{
	TTextureRead(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn,
		const UTempoLidarCaptureComponent* CaptureComponentIn, const UTempoLidar* TempoLidarIn)
	   : TTextureReadBase(ImageSizeIn, SequenceIdIn, CaptureTimeIn, TempoLidarIn->GetOwnerName(), TempoLidarIn->GetSensorName()), CaptureTransform(TempoLidarIn->GetComponentTransform()), CaptureComponent(CaptureComponentIn), TempoLidar(TempoLidarIn)
	{
	}

	virtual FName GetType() const override { return TEXT("Depth"); }

	void RespondToRequests(const TArray<FLidarScanRequest>& Requests, float TransmissionTime) const;

	const FTransform CaptureTransform;
	const UTempoLidarCaptureComponent* CaptureComponent;
	const UTempoLidar* TempoLidar;
};

UCLASS(ClassGroup=(Custom))
class TEMPOLIDAR_API UTempoLidarCaptureComponent : public UTempoSceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UTempoLidarCaptureComponent();

protected:
	virtual void BeginPlay() override;
	
	virtual bool HasPendingRequests() const override;

	virtual FTextureRead* MakeTextureRead() const override;

	virtual int32 GetMaxTextureQueueSize() const override;

	// The number of horizontal beams.
	UPROPERTY(EditAnywhere)
	int32 HorizontalBeams = 200.0;

	// The resulting distortion factor.
	UPROPERTY(VisibleAnywhere)
	float DistortionFactor = 1.0;

	// The resulting distorted vertical FOV.
	UPROPERTY(VisibleAnywhere)
	float DistortedVerticalFOV = 30.0;

	UPROPERTY()
	UTempoLidar* LidarOwner = nullptr;

	friend UTempoLidar;

	friend TTextureRead<float>;
};
