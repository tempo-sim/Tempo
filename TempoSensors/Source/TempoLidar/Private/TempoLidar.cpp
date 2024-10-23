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

	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	if (const TObjectPtr<UMaterialInterface> PostProcessMaterial = GetDefault<UTempoSensorsSettings>()->GetLidarPostProcessMaterial())
	{
		PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterial.Get(), this);
		const float VerticalFOVAngle = 2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(FOVAngle) / 2.0) * (SizeXY.Y / SizeXY.X));
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("VerticalFOV"), VerticalFOVAngle);
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

	return static_cast<FTextureRead*>(new TTextureRead<FDepthPixel>(SizeXY, SequenceId, GetWorld()->GetTimeSeconds(), GetOwnerName(), GetSensorName(), MinRange, MaxRange));
}

void TTextureRead<FDepthPixel>::RespondToRequests(const TArray<FLidarScanRequest>& Requests, float TransmissionTime) const
{
	TempoLidar::LidarScanSegment ScanSegment;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeColor);
		std::vector<char> ImageData;
		ImageData.reserve(Image.Num() * 3);
		for (const auto& Pixel : Image)
		{
			TempoLidar::LidarReturn Return;
			// Return.set_allocated_point()
			// ScanSegment.add_returns(Pixel.GetDepth(MinRange, MaxRange));
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
