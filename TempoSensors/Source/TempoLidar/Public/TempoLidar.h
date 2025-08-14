// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSceneCaptureComponent2D.h"

#include "TempoLidar/Lidar.pb.h"

#include "CoreMinimal.h"
#include "TempoScriptingServer.h"
#include "TempoLidar.generated.h"

struct FDepthPixel
{
	float GetDepth(float MinRange, float MaxRange) const
	{
		if (Depth > MaxRange)
		{
			return 0.0;
		}
		return FMath::Max(MinRange, Depth);
	}

private:
	float Depth = 0.0;
};

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
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
	void UpdateComputedProperties();

	void AddCaptureComponent(float YawOffset, float SubHorizontalFOV, int32 SubHorizontalBeams);

	virtual TFuture<void> DecodeAndRespond(TArray<TUniquePtr<FTextureRead>> TextureReads);

	// The rate in Hz this Lidar updates at.
	UPROPERTY(EditAnywhere)
	float RateHz = 10.0;

	// The measurement types supported. Should be set in constructor of derived classes.
	UPROPERTY(VisibleAnywhere)
	TArray<TEnumAsByte<EMeasurementType>> MeasurementTypes;

	// The minimum depth this Lidar can measure.
	UPROPERTY(EditAnywhere)
	float MinRange = 10.0; // 10cm
	
	// The maximum depth this Lidar can measure.
	UPROPERTY(EditAnywhere)
	float MaxRange = 10000.0; // 100m

	// The vertical field of view in degrees.
	UPROPERTY(EditAnywhere)
	float VerticalFOV = 30.0;
	
	// The horizontal field of view in degrees.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, UIMax=360.0, ClampMin=0.0, ClampMax=360.0))
	float HorizontalFOV = 120.0;

	// The number of vertical beams.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, UIMax=180.0, ClampMin=0.0, ClampMax=180.0))
	int32 VerticalBeams = 64.0;
	
	// The number of horizontal beams.
	UPROPERTY(EditAnywhere)
	int32 HorizontalBeams = 200.0;

	// The upsampling factor.
	UPROPERTY(EditAnywhere)
	int32 UpsamplingFactor = 1.0;

	// The resulting spacing between vertical beams in degrees.
	UPROPERTY(VisibleAnywhere)
	float VerticalSpacing = 30.0 / 64.0;
	
	// The resulting spacing between horizontal beams in degrees.
	UPROPERTY(VisibleAnywhere)
	float HorizontalSpacing = 120.0 / 200.0;

	UPROPERTY(VisibleAnywhere)
	FIntPoint SizeXY = FIntPoint::ZeroValue;

	int32 NumResponded = 0;
	
	UPROPERTY()
	TArray<UTempoLidarCaptureComponent*> LidarCaptureComponents;

	TArray<FLidarScanRequest> PendingRequests;

	friend class UTempoLidarCaptureComponent;

	friend struct TTextureRead<FDepthPixel>;
};

template <>
struct TTextureRead<FDepthPixel> : TTextureReadBase<FDepthPixel>
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
	
	UPROPERTY(VisibleAnywhere)
	UMaterialInstanceDynamic* PostProcessMaterialInstance= nullptr;

	UPROPERTY()
	UTempoLidar* LidarOwner = nullptr;

	friend UTempoLidar;

	friend TTextureRead<FDepthPixel>;
};
