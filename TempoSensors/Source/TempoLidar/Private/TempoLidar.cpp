// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "TempoLidarModule.h"

#include "TempoLidar/Lidar.pb.h"

#include "TempoSensorsSettings.h"

UTempoLidar::UTempoLidar()
{
	MeasurementTypes = { EMeasurementType::COLOR_IMAGE, EMeasurementType::LABEL_IMAGE, EMeasurementType::DEPTH_IMAGE};
	CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	ShowFlags.SetAntiAliasing(false);
	ShowFlags.SetTemporalAA(false);
	ShowFlags.SetMotionBlur(false);

	RenderTargetFormat = ETextureRenderTargetFormat::RTF_R32f;
	PixelFormatOverride = EPixelFormat::PF_Unknown;
}

void UTempoLidar::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UpdateComputedProperties();
}

void UTempoLidar::UpdateComputedProperties()
{
	// Special case for 1 vertical beam? Special lidar?

	FOVAngle = HorizontalFOV;
	HorizontalSpacing = HorizontalFOV / HorizontalBeams;
	VerticalSpacing = VerticalFOV / VerticalBeams;
	const float TanHFOVOverTwo = FMath::Tan(FMath::DegreesToRadians(HorizontalFOV / 2.0));
	const float DistortionFactorContinuous = FMath::Sqrt((TanHFOVOverTwo * TanHFOVOverTwo) + 1);
	const float UndistortedAspectRatio = FMath::Tan(FMath::DegreesToRadians(VerticalFOV / 2.0)) / FMath::Tan(FMath::DegreesToRadians(HorizontalFOV / 2.0));
	SizeXY = FIntPoint(HorizontalBeams, FMath::CeilToInt32(DistortionFactorContinuous * UndistortedAspectRatio * HorizontalBeams));
	DistortionFactor = SizeXY.Y / (UndistortedAspectRatio * HorizontalBeams);
	DistortedVerticalFOV = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalFOV) / 2.0) * DistortionFactor));
}

void UTempoLidar::BeginPlay()
{
	Super::BeginPlay();

	UpdateComputedProperties();

	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	if (const TObjectPtr<UMaterialInterface> PostProcessMaterial = GetDefault<UTempoSensorsSettings>()->GetLidarPostProcessMaterial())
	{
		PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterial.Get(), this);
		const float VerticalFOVAngle = 2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(FOVAngle) / 2.0) * (SizeXY.Y / SizeXY.X));
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("VerticalFOV"), DistortedVerticalFOV);
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("HorizontalFOV"), FOVAngle);
	}
	else
	{
		UE_LOG(LogTempoLidar, Error, TEXT("PostProcessMaterialWithDepth is not set in TempoSensors settings"));
	}

	InitRenderTarget();
}

bool UTempoLidar::HasPendingRequests() const
{
	return !PendingRequests.IsEmpty();
}

FTextureRead* UTempoLidar::MakeTextureRead() const
{
	check(GetWorld());

	return new TTextureRead<FDepthPixel>(SizeXY, SequenceId, GetWorld()->GetTimeSeconds(),
		GetOwnerName(), GetSensorName(), MinRange, MaxRange, HorizontalBeams, VerticalBeams,
		DistortedVerticalFOV, VerticalFOV, UpsamplingFactor);
}

void TTextureRead<FDepthPixel>::RespondToRequests(const TArray<FLidarScanRequest>& Requests, float TransmissionTime) const
{
	TempoLidar::LidarScanSegment ScanSegment;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeColor);
		std::vector<char> ImageData;
		ImageData.reserve(Image.Num() * 3);

		const float VerticalOffset = DistortedVerticalFOV / 2.0 - VerticalFOV / 2.0;
		const float VerticalSpacing = VerticalFOV / (VerticalBeams - 1);
		auto RangeAtBeam = [this, VerticalOffset, VerticalSpacing] (int32 HorizontalBeam, int32 VerticalBeam) -> float
		{
			const int32 Column = HorizontalBeam;
			const float Row = VerticalOffset + VerticalBeam * VerticalSpacing;
			const float RangeAbove = Image[Column + HorizontalBeams * FMath::FloorToInt32(Row)].GetDepth(MinRange, MaxRange);
			const float RangeBelow = Image[Column + HorizontalBeams * FMath::CeilToInt32(Row)].GetDepth(MinRange, MaxRange);
			const float Alpha = Row - FMath::FloorToInt32(Row);
			if (FMath::Abs(RangeAbove - RangeBelow) > 10.0)
			{
				return Alpha > 0.5 ? RangeBelow : RangeAbove;
			}
			else
			{
				return RangeAbove * (1.0 - Alpha) + RangeBelow * Alpha;
			}
		};

		ScanSegment.mutable_returns()->Reserve(HorizontalBeams * VerticalBeams);
		for (int32 HorizontalBeam = 0; HorizontalBeam < HorizontalBeams; ++HorizontalBeam)
		{
			for (int32 VerticalBeam = 0; VerticalBeam < VerticalBeams; ++VerticalBeam)
			{
				auto* Return = ScanSegment.add_returns();
				Return->set_range(RangeAtBeam(HorizontalBeam, VerticalBeam) / 100.0);
			}
		}

		ScanSegment.mutable_header()->set_sequence_id(SequenceId);
		ScanSegment.mutable_header()->set_capture_time(CaptureTime);
		ScanSegment.mutable_header()->set_transmission_time(TransmissionTime);
		ScanSegment.mutable_header()->set_sensor_name(TCHAR_TO_UTF8(*FString::Printf(TEXT("%s/%s"), *OwnerName, *SensorName)));
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraRespondColor);
	for (auto ColorImageRequestIt = Requests.CreateConstIterator(); ColorImageRequestIt; ++ColorImageRequestIt)
	{
		ColorImageRequestIt->ResponseContinuation.ExecuteIfBound(ScanSegment, grpc::Status_OK);
	}
}

TFuture<void> UTempoLidar::DecodeAndRespond(TUniquePtr<FTextureRead> TextureRead)
{
	const double TransmissionTime = GetWorld()->GetTimeSeconds();

	const bool bSupportsDepth = TextureRead->GetType() == TEXT("WithDepth");

	TFuture<void> Future = Async(EAsyncExecution::TaskGraph, [
		TextureRead = MoveTemp(TextureRead),
		Requests = PendingRequests,
		TransmissionTimeCpy = TransmissionTime
		]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarDecodeAndRespond);

		static_cast<TTextureRead<FDepthPixel>*>(TextureRead.Get())->RespondToRequests(Requests, TransmissionTimeCpy);
	});

	PendingRequests.Empty();
	
	return Future;
}

void UTempoLidar::RequestMeasurement(const TempoLidar::LidarScanRequest& Request, const TResponseDelegate<TempoLidar::LidarScanSegment>& ResponseContinuation)
{
	PendingRequests.Add({ Request, ResponseContinuation});
}

int32 UTempoLidar::GetMaxTextureQueueSize() const
{
	return GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize();
}
