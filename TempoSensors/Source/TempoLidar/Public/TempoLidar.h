// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSceneCaptureComponent2D.h"

#include "TempoLidar/Lidar.pb.h"

#include "CoreMinimal.h"
#include "TempoScriptingServer.h"
#include "TempoLidar.generated.h"

// 8-byte pixel format where first 3 bytes are normal, 4th byte is label.
// 5th-8th bytes are a uint32 representing discrete depth.
struct FLidarPixel
{
	static constexpr bool bSupportsDepth = false;

	FVector Normal() const
	{
		return FVector( 2.0 * U1 / 255.0 - 1.0, 2.0 * U2 / 255.0 - 1.0, 2.0 * U3 / 255.0 - 1.0 );
	}

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

	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

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

	void RequestMeasurement(const TempoLidar::LidarScanRequest& Request, const TResponseDelegate<TempoLidar::LidarScanSegment>& ResponseContinuation);

protected:
	TFuture<void> DecodeAndRespond(TArray<TUniquePtr<FTextureRead>> TextureReads);

	TMap<FName, UTempoLidarCaptureComponent*> GetAllCaptureComponents() const ;
	TMap<FName, UTempoLidarCaptureComponent*> GetOrCreateCaptureComponents();
	TArray<UTempoLidarCaptureComponent*> GetActiveCaptureComponents() const;

	void SyncCaptureComponents();

	static void SyncCaptureComponent(UTempoLidarCaptureComponent* LidarCaptureComponent, bool bActive, double YawOffset, double SubHorizontalFOV, double SubHorizontalBeams);

	// The rate in Hz this Lidar updates at.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, ClampMin=0.0))
	float RateHz = 10.0;

	// The measurement types supported. Should be set in constructor of derived classes.
	UPROPERTY(VisibleAnywhere)
	TArray<TEnumAsByte<EMeasurementType>> MeasurementTypes;

	// The minimum distance this Lidar can measure. Note that GEngine->NearClipPlane must be less than this value.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, ClampMin=0.0))
	double MinDistance = 10.0; // 10cm

	// The maximum distance this Lidar can measure.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, ClampMin=0.0))
	double MaxDistance = 10000.0; // 100m

	// The distance closer than which perpendicular surfaces will have saturated (1.0) intensity.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, ClampMin=0.0))
	double IntensitySaturationDistance = 1000.0; // 10m

	// The vertical field of view in degrees.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0001, UIMax=180.0, ClampMin=0.0001, ClampMax=180.0))
	double VerticalFOV = 30.0;

	// The horizontal field of view in degrees.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0001, UIMax=360.0, ClampMin=0.0001, ClampMax=360.0))
	double HorizontalFOV = 120.0;

	// The number of vertical beams.
	UPROPERTY(EditAnywhere, meta=(UIMin=1, ClampMin=1))
	int32 VerticalBeams = 64;

	// The number of horizontal beams.
	UPROPERTY(EditAnywhere, meta=(UIMin=0, ClampMin=0))
	int32 HorizontalBeams = 200;

	// The max angle of incidence in degrees from which this Lidar will get a return.
	UPROPERTY(EditAnywhere, meta=(UIMin=0.0, UIMax=90.0, ClampMin=0.0, ClampMax=90.0))
	float MaxAngleOfIncidence = 87.5;

	// Monotonically increasing counter of scans captured.
	UPROPERTY(VisibleAnywhere)
	int32 SequenceId = 0;

	int32 NumResponded = 0;

	TArray<FLidarScanRequest> PendingRequests;

	friend class UTempoLidarCaptureComponent;

	friend struct TTextureRead<FLidarPixel>;
};

template <>
struct TTextureRead<FLidarPixel> : TTextureReadBase<FLidarPixel>
{
	TTextureRead(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn, const FString& OwnerNameIn,
		const FString& SensorNameIn, const FTransform& SensorTransformIn, const FTransform& CaptureTransform,
		double HorizontalFOVIn, double VerticalFOVIn, int32 HorizontalBeamsIn, int32 VerticalBeamsIn, const FVector2D& SizeXYFOVIn, double IntensitySaturationDistanceIn, double MaxAngleOfIncidenceIn,
		int32 NumCaptureComponentsIn, double RelativeYawIn, float MinDepthIn, float MaxDepthIn, double MinDistanceIn, double MaxDistanceIn)
		: TTextureReadBase(ImageSizeIn, SequenceIdIn, CaptureTimeIn, OwnerNameIn, SensorNameIn),
			SensorTransform(SensorTransformIn), CaptureTransform(CaptureTransform), HorizontalFOV(HorizontalFOVIn),
			VerticalFOV(VerticalFOVIn), HorizontalBeams(HorizontalBeamsIn), VerticalBeams(VerticalBeamsIn),
			SizeXYFOV(SizeXYFOVIn), IntensitySaturationDistance(IntensitySaturationDistanceIn), MaxAngleOfIncidence(MaxAngleOfIncidenceIn),
			NumCaptureComponents(NumCaptureComponentsIn), RelativeYaw(RelativeYawIn), MinDepth(MinDepthIn), MaxDepth(MaxDepthIn), MinDistance(MinDistanceIn), MaxDistance(MaxDistanceIn)
	{
	}

	virtual FName GetType() const override { return TEXT("Lidar"); }

	void Decode(float TransmissionTime, TempoLidar::LidarScanSegment& ScanSegmentOut) const;

	const FTransform SensorTransform;
	const FTransform CaptureTransform;
	double HorizontalFOV;
	double VerticalFOV;
	int32 HorizontalBeams;
	int32 VerticalBeams;
	const FVector2D SizeXYFOV;
	double IntensitySaturationDistance;
	double MaxAngleOfIncidence;
	int32 NumCaptureComponents;
	double RelativeYaw;
	float MinDepth;
	float MaxDepth;
	double MinDistance;
	double MaxDistance;
};

UCLASS(ClassGroup=(Custom), NotPlaceable, NotBlueprintable)
class TEMPOLIDAR_API UTempoLidarCaptureComponent : public UTempoSceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UTempoLidarCaptureComponent();

protected:
	virtual void Activate(bool bReset = false) override;

	virtual bool HasPendingRequests() const override;

	virtual FTextureRead* MakeTextureRead() const override;

	virtual int32 GetMaxTextureQueueSize() const override;

	void Configure(double YawOffset, double SubHorizontalFOV, double SubHorizontalBeams);

	// The number of horizontal beams.
	UPROPERTY(EditAnywhere)
	int32 HorizontalBeams = 200.0;

	// The resulting distortion factor.
	UPROPERTY(VisibleAnywhere)
	double DistortionFactor = 1.0;

	// The resulting distorted vertical FOV.
	UPROPERTY(VisibleAnywhere)
	double DistortedVerticalFOV = 30.0;

	// The size of the image plane encompassing the Lidar FOV in pixels. SizeXY is the ceil of this.
	UPROPERTY(VisibleAnywhere)
	FVector2D SizeXYFOV = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere)
	UMaterialInstanceDynamic* PostProcessMaterialInstance= nullptr;

	UPROPERTY(VisibleAnywhere)
	const UTempoLidar* LidarOwner = nullptr;

	// The minimum depth this Lidar can measure. Will be set to the global near clip plane.
	UPROPERTY(VisibleAnywhere)
	float MinDepth = 10.0; // 10cm

	// The maximum depth this Lidar can measure. Will be set to UTempoSensorsSettings::MaxLidarDepth.
	UPROPERTY(VisibleAnywhere, Category="Depth")
	float MaxDepth = 40000.0; // 400m

	friend UTempoLidar;
};
