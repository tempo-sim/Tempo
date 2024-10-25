// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "TempoConversion.h"
#include "TempoLidarModule.h"

#include "TempoLidar/Lidar.pb.h"

#include "TempoSensorsSettings.h"

UTempoLidar::UTempoLidar()
{
	MeasurementTypes = { EMeasurementType::COLOR_IMAGE, EMeasurementType::LABEL_IMAGE, EMeasurementType::DEPTH_IMAGE};
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
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
	SizeXY = FIntPoint(UpsamplingFactor * HorizontalBeams, UpsamplingFactor * FMath::CeilToInt32(DistortionFactorContinuous * UndistortedAspectRatio * HorizontalBeams));
	DistortionFactor = SizeXY.Y / (UndistortedAspectRatio * UpsamplingFactor * HorizontalBeams);
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
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("VerticalFOV"), DistortedVerticalFOV);
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("HorizontalFOV"), FOVAngle);
	}
	else
	{
		UE_LOG(LogTempoLidar, Error, TEXT("LidarPostProcessMaterial is not set in TempoSensors settings"));
	}

	if (PostProcessMaterialInstance)
	{
		PostProcessSettings.WeightedBlendables.Array.Empty();
		PostProcessSettings.WeightedBlendables.Array.Init(FWeightedBlendable(1.0, PostProcessMaterialInstance), 1);
		PostProcessMaterialInstance->EnsureIsComplete();
	}
	else
	{
		UE_LOG(LogTempoLidar, Error, TEXT("PostProcessMaterialInstance is not set."));
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
		GetOwnerName(), GetSensorName(), this);
}

void TTextureRead<FDepthPixel>::RespondToRequests(const TArray<FLidarScanRequest>& Requests, float TransmissionTime) const
{
	TempoLidar::LidarScanSegment ScanSegment;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeColor);
		std::vector<char> ImageData;
		ImageData.reserve(Image.Num() * 3);

		const int32 NumRows = Image.Num() / (TempoLidar->UpsamplingFactor * TempoLidar->UpsamplingFactor) / TempoLidar->HorizontalBeams;
		const float VerticalDegreesPerPixel = TempoLidar->DistortedVerticalFOV / (NumRows - 1.0 / TempoLidar->UpsamplingFactor);

		const float VerticalOffset = (TempoLidar->DistortedVerticalFOV / 2.0 - TempoLidar->VerticalFOV / 2.0) / VerticalDegreesPerPixel;
		const float VerticalSpacingPixels = (TempoLidar->VerticalFOV / (TempoLidar->VerticalBeams - 1)) / VerticalDegreesPerPixel;
		auto RangeAtBeam = [this, VerticalOffset, VerticalSpacingPixels, NumRows] (int32 HorizontalBeam, int32 VerticalBeam) -> float
		{
			const int32 Column = TempoLidar->UpsamplingFactor * HorizontalBeam;
			const float Row = TempoLidar->UpsamplingFactor * (VerticalOffset + VerticalBeam * VerticalSpacingPixels);
			const float RangeAbove = Image[Column + TempoLidar->UpsamplingFactor * TempoLidar->HorizontalBeams * FMath::FloorToInt32(Row)].GetDepth(TempoLidar->MinRange, TempoLidar->MaxRange);
			float RangeBelow = RangeAbove;
			if (Column + TempoLidar->UpsamplingFactor * TempoLidar->HorizontalBeams * FMath::CeilToInt32(Row) < NumRows)
			{
				RangeBelow = Image[Column + TempoLidar->UpsamplingFactor * TempoLidar->HorizontalBeams * FMath::CeilToInt32(Row)].GetDepth(TempoLidar->MinRange, TempoLidar->MaxRange);
			}
			const float Alpha = Row - FMath::FloorToInt32(Row);
			if (FMath::Abs(RangeAbove - RangeBelow) > 10.0)
			{
				return Alpha > 0.5 ? RangeBelow : RangeAbove;
			}
			return RangeAbove * (1.0 - Alpha) + RangeBelow * Alpha;
		};

		ScanSegment.mutable_returns()->Reserve(TempoLidar->HorizontalBeams * TempoLidar->VerticalBeams);
		for (int32 HorizontalBeam = 0; HorizontalBeam < TempoLidar->HorizontalBeams; ++HorizontalBeam)
		{
			for (int32 VerticalBeam = 0; VerticalBeam < TempoLidar->VerticalBeams; ++VerticalBeam)
			{
				auto* Return = ScanSegment.add_returns();
				Return->set_distance(RangeAtBeam(HorizontalBeam, VerticalBeam) / 100.0);
			}
		}

		ScanSegment.mutable_header()->set_sequence_id(SequenceId);
		ScanSegment.mutable_header()->set_capture_time(CaptureTime);
		ScanSegment.mutable_header()->set_transmission_time(TransmissionTime);
		ScanSegment.mutable_header()->set_sensor_name(TCHAR_TO_UTF8(*FString::Printf(TEXT("%s/%s"), *OwnerName, *SensorName)));
		const FVector SensorLocation = QuantityConverter<CM2M, L2R>::Convert(TempoLidar->GetComponentLocation());
		const FRotator SensorRotation = QuantityConverter<Deg2Rad, L2R>::Convert(TempoLidar->GetComponentRotation());
		ScanSegment.mutable_header()->mutable_sensor_transform()->mutable_location()->set_x(SensorLocation.X);
		ScanSegment.mutable_header()->mutable_sensor_transform()->mutable_location()->set_y(SensorLocation.Y);
		ScanSegment.mutable_header()->mutable_sensor_transform()->mutable_location()->set_z(SensorLocation.Z);
		ScanSegment.mutable_header()->mutable_sensor_transform()->mutable_rotation()->set_p(SensorRotation.Pitch);
		ScanSegment.mutable_header()->mutable_sensor_transform()->mutable_rotation()->set_r(SensorRotation.Roll);
		ScanSegment.mutable_header()->mutable_sensor_transform()->mutable_rotation()->set_y(SensorRotation.Yaw);

		ScanSegment.set_horizontal_beams(TempoLidar->HorizontalBeams);
		ScanSegment.set_vertical_beams(TempoLidar->VerticalBeams);
		ScanSegment.mutable_azimuth_range()->set_max(FMath::DegreesToRadians(TempoLidar->HorizontalFOV / 2.0));
		ScanSegment.mutable_azimuth_range()->set_min(-FMath::DegreesToRadians(TempoLidar->HorizontalFOV / 2.0));
		ScanSegment.mutable_elevation_range()->set_max(FMath::DegreesToRadians(TempoLidar->VerticalFOV / 2.0));
        ScanSegment.mutable_elevation_range()->set_min(-FMath::DegreesToRadians(TempoLidar->VerticalFOV / 2.0));
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
