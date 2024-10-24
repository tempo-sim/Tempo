// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSceneCaptureComponent2D.h"

#include "TempoLidar/Lidar.pb.h"

#include "CoreMinimal.h"
#include "TempoScriptingServer.h"
#include "TempoLidar.generated.h"

struct FDepthPixel
{
	float GetDepth(float MinRange, float MaxRange) const { return FMath::Min(MaxRange, FMath::Max(MinRange, Depth)); }

private:
	float Depth = 0.0;
};

struct FLidarScanRequest
{
	TempoLidar::LidarScanRequest Request;
	TResponseDelegate<TempoLidar::LidarScanSegment> ResponseContinuation;
};

template <>
struct TTextureRead<FDepthPixel> : TTextureReadBase<FDepthPixel>
{
	TTextureRead(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn, const FString& OwnerNameIn,
		const FString& SensorNameIn, float MinRangeIn, float MaxRangeIn, int32 HorizontalBeamsIn, int32 VerticalBeamsIn,
		float DistortedVerticalFOVIn, float VerticalFOVIn, int32 UpsamplingFactorIn)
	   : TTextureReadBase(ImageSizeIn, SequenceIdIn, CaptureTimeIn, OwnerNameIn, SensorNameIn),
		 MinRange(MinRangeIn), MaxRange(MaxRangeIn), HorizontalBeams(HorizontalBeamsIn), VerticalBeams(VerticalBeamsIn),
		 DistortedVerticalFOV(DistortedVerticalFOVIn), VerticalFOV(VerticalFOVIn), UpsamplingFactor(UpsamplingFactorIn)
	{
	}

	virtual FName GetType() const override { return TEXT("Depth"); }

	void RespondToRequests(const TArray<FLidarScanRequest>& Requests, float TransmissionTime) const;

	float MinRange;
	float MaxRange;
	int32 HorizontalBeams;
	int32 VerticalBeams;
	float DistortedVerticalFOV;
	float VerticalFOV;
	int32 UpsamplingFactor;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOLIDAR_API UTempoLidar : public UTempoSceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UTempoLidar();

	void RequestMeasurement(const TempoLidar::LidarScanRequest& Request, const TResponseDelegate<TempoLidar::LidarScanSegment>& ResponseContinuation);

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
	virtual bool HasPendingRequests() const override;

	virtual FTextureRead* MakeTextureRead() const override;

	virtual TFuture<void> DecodeAndRespond(TUniquePtr<FTextureRead> TextureRead) override;

	virtual int32 GetMaxTextureQueueSize() const override;

	void UpdateComputedProperties();

	UPROPERTY(VisibleAnywhere)
	UMaterialInstanceDynamic* PostProcessMaterialInstance= nullptr;

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
	UPROPERTY(EditAnywhere)
	float HorizontalFOV = 170.0;

	// The number of vertical beams.
	UPROPERTY(EditAnywhere)
	int32 VerticalBeams = 64.0;
	
	// The number of horizontal beams.
	UPROPERTY(EditAnywhere)
	int32 HorizontalBeams = 1800.0;

	// The number of horizontal beams.
	UPROPERTY(EditAnywhere)
	int32 UpsamplingFactor = 1.0;

	// The resulting spacing between vertical beams in degrees.
	UPROPERTY(VisibleAnywhere)
	float VerticalSpacing = 30.0 / 64.0;
	
	// The resulting spacing between horizontal beams in degrees.
	UPROPERTY(VisibleAnywhere)
	float HorizontalSpacing = 170.0 / 1800.0;

	// The resulting distortion factor.
	UPROPERTY(VisibleAnywhere)
	float DistortionFactor = 1.0;

	// The resulting distortion factor.
	UPROPERTY(VisibleAnywhere)
	float DistortedVerticalFOV = 30.0;

	TArray<FLidarScanRequest> PendingRequests;
};
