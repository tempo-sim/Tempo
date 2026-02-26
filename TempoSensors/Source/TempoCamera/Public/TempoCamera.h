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

UENUM(BlueprintType)
enum class EDistortionModel : uint8
{
	Polynomial UMETA(DisplayName = "Polynomial (Brown-Conrady)"),
	Rational   UMETA(DisplayName = "Rational (OpenCV)")
};

USTRUCT(BlueprintType)
struct FTempoLensDistortionParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	EDistortionModel Model = EDistortionModel::Polynomial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	float K1 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	float K2 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	float K3 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens", meta = (EditCondition = "Model == EDistortionModel::Rational"))
	float P1 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens", meta = (EditCondition = "Model == EDistortionModel::Rational"))
	float P2 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens", meta = (EditCondition = "Model == EDistortionModel::Rational"))
	float K4 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens", meta = (EditCondition = "Model == EDistortionModel::Rational"))
	float K5 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens", meta = (EditCondition = "Model == EDistortionModel::Rational"))
	float K6 = 0.0f;

	bool operator==(const FTempoLensDistortionParameters& Other) const
	{
		return Model == Other.Model &&
			   K1 == Other.K1 && K2 == Other.K2 && K3 == Other.K3 &&
			   P1 == Other.P1 && P2 == Other.P2 &&
			   K4 == Other.K4 && K5 == Other.K5 && K6 == Other.K6;
	}

	bool operator!=(const FTempoLensDistortionParameters& Other) const
	{
		return !(*this == Other);
	}
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

	void RequestMeasurement(const TempoCamera::BoundingBoxesRequest& Request, const TResponseDelegate<TempoCamera::BoundingBoxes>& ResponseContinuation);

	FTempoCameraIntrinsics GetIntrinsics() const;

	virtual FString GetOwnerName() const override;
	virtual FString GetSensorName() const override;
	virtual float GetRate() const override { return RateHz; }
	virtual const TArray<TEnumAsByte<EMeasurementType>>& GetMeasurementTypes() const override { return MeasurementTypes; }
	virtual bool IsAwaitingRender() override;
	virtual void OnRenderCompleted() override;
	virtual void BlockUntilMeasurementsReady() const override;
	virtual TOptional<TFuture<void>> SendMeasurements() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens", meta = (ClampMin = "1.0", ClampMax = "170.0"))
	float DistortedFOV = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	FTempoLensDistortionParameters LensParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo|Lens")
	float CroppingFactor = 0.0f;

protected:
	virtual bool HasPendingRequests() const override;

	virtual FTextureRead* MakeTextureRead() const override;
	
	virtual TFuture<void> DecodeAndRespond(TUniquePtr<FTextureRead> TextureRead);

	virtual int32 GetMaxTextureQueueSize() const override;

	void SetDepthEnabled(bool bDepthEnabledIn);

	void ApplyDepthEnabled();
	
	virtual void InitRenderTarget() override;
	
	void InitDistortionMap();

	void UpdateLensParameters();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(VisibleAnywhere)
	TArray<TEnumAsByte<EMeasurementType>> MeasurementTypes;

	UPROPERTY(VisibleAnywhere, Category="Depth")
	bool bDepthEnabled = false;

	UPROPERTY(VisibleAnywhere, Category="Depth")
	float MinDepth = 10.0;

	UPROPERTY(VisibleAnywhere, Category="Depth")
	float MaxDepth = 100000.0;

	UPROPERTY(VisibleAnywhere)
	UMaterialInstanceDynamic* PostProcessMaterialInstance = nullptr;

	UPROPERTY(Transient)
	UTexture2D* DistortionMapTexture = nullptr;

	TArray<FColorImageRequest> PendingColorImageRequests;
	TArray<FLabelImageRequest> PendingLabelImageRequests;
	TArray<FDepthImageRequest> PendingDepthImageRequests;
	TArray<FBoundingBoxesRequest> PendingBoundingBoxesRequests;

	FTempoLensDistortionParameters LastLensParameters;
	float LastDistortedFOV = -1.0f;
	FIntPoint LastSizeXY = FIntPoint(0, 0);
};