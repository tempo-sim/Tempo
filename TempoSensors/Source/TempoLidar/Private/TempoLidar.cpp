// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "TempoConversion.h"

#include "TempoLidar/Lidar.pb.h"

#include "TempoSensorsSettings.h"

UTempoLidar::UTempoLidar()
{
	MeasurementTypes = { EMeasurementType::LIDAR_SCAN };
	bAutoActivate = true;
}

FString UTempoLidar::GetOwnerName() const
{
	check(GetOwner());

	return GetOwner()->GetActorNameOrLabel();
}

FString UTempoLidar::GetSensorName() const
{
	return GetName();
}

bool UTempoLidar::IsAwaitingRender()
{
	for (const UTempoLidarCaptureComponent* LidarCaptureComponent : LidarCaptureComponents)
	{
		if (LidarCaptureComponent->IsNextReadAwaitingRender())
		{
			return true;
		}
	}
	return false;
}

void UTempoLidar::OnRenderCompleted()
{
	for (UTempoLidarCaptureComponent* LidarCaptureComponent : LidarCaptureComponents)
	{
		LidarCaptureComponent->ReadNextIfAvailable();
	}
}

void UTempoLidar::BlockUntilMeasurementsReady() const
{
	for (const UTempoLidarCaptureComponent* LidarCaptureComponent : LidarCaptureComponents)
	{
		LidarCaptureComponent->BlockUntilNextReadComplete();
	}
}

TArray<TFuture<void>> UTempoLidar::SendMeasurements()
{
	TArray<TFuture<void>> Futures;

	TArray<TUniquePtr<FTextureRead>> TextureReads;
	const int32 NumCompletedReads = LidarCaptureComponents.FilterByPredicate(
			[](const UTempoLidarCaptureComponent* LidarCaptureComponent) { return LidarCaptureComponent->NextReadComplete(); }
		).Num();
	if (NumCompletedReads == LidarCaptureComponents.Num())
	{
		for (UTempoLidarCaptureComponent* LidarCaptureComponent : LidarCaptureComponents)
		{
			TextureReads.Add(LidarCaptureComponent->DequeueIfReadComplete());
		}
	}

	Futures.Add(DecodeAndRespond(MoveTemp(TextureReads)));

	if (NumResponded == LidarCaptureComponents.Num())
	{
		PendingRequests.Empty();
		NumResponded = 0;
	}

	// if (UTempoLidarCaptureComponent* LidarCaptureComponent = NextCaptureComponent.Key)
	// {
	// 	Futures.Add(DecodeAndRespond(LidarCaptureComponent->DequeueIfReadComplete()));
	// }

	return Futures;
}

void UTempoLidar::BeginPlay()
{
	Super::BeginPlay();

	SizeXY = FIntPoint::ZeroValue;

	UpdateComputedProperties();

	if (HorizontalFOV <= 120.0)
	{
		AddCaptureComponent(0.0, HorizontalFOV, HorizontalBeams);
	}
	else if (HorizontalFOV <= 240.0)
	{
		AddCaptureComponent(HorizontalFOV / 4.0, HorizontalFOV / 2.0, HorizontalBeams / 2.0);
		AddCaptureComponent(-HorizontalFOV / 4.0, HorizontalFOV / 2.0, HorizontalBeams / 2.0);
	}
	else
	{
		AddCaptureComponent(0.0, HorizontalFOV / 3.0, HorizontalBeams / 3.0);
		AddCaptureComponent(HorizontalFOV / 3.0, HorizontalFOV / 3.0, HorizontalBeams / 3.0);
		AddCaptureComponent(-HorizontalFOV / 3.0, HorizontalFOV / 3.0, HorizontalBeams / 3.0);
	}
}

void UTempoLidar::AddCaptureComponent(float YawOffset, float SubHorizontalFOV, int32 SubHorizontalBeams)
{
	UTempoLidarCaptureComponent* LidarCaptureComponent = Cast<UTempoLidarCaptureComponent>(GetOwner()->AddComponentByClass(UTempoLidarCaptureComponent::StaticClass(), true, FTransform::Identity, true));
	LidarCaptureComponent->LidarOwner = this;
	LidarCaptureComponent->FOVAngle = SubHorizontalFOV;
	LidarCaptureComponent->HorizontalBeams = SubHorizontalBeams;
	LidarCaptureComponent->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetIncludingScale);
	LidarCaptureComponents.Add(LidarCaptureComponent);
	GetOwner()->FinishAddComponent(LidarCaptureComponent, true, FTransform::Identity);
	LidarCaptureComponent->SetRelativeRotation(FRotator(0.0, YawOffset, 0.0));
	GetOwner()->AddInstanceComponent(LidarCaptureComponent);
}

UTempoLidarCaptureComponent::UTempoLidarCaptureComponent()
{
	CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	ShowFlags.SetAntiAliasing(false);
	ShowFlags.SetTemporalAA(false);
	ShowFlags.SetMotionBlur(false);

	RenderTargetFormat = ETextureRenderTargetFormat::RTF_R32f;
	PixelFormatOverride = EPixelFormat::PF_Unknown;
	bAutoActivate = true;
}

void UTempoLidar::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UpdateComputedProperties();
}

void UTempoLidar::UpdateComputedProperties()
{
	HorizontalSpacing = HorizontalFOV / HorizontalBeams;
	VerticalSpacing = VerticalFOV / VerticalBeams;
}

FVector2D SphericalToPerspective(float AzimuthDeg, float ElevationDeg)
{
	const float TanAzimuth = FMath::Tan(FMath::DegreesToRadians(AzimuthDeg));
	const float TanElevation = FMath::Tan(FMath::DegreesToRadians(ElevationDeg));
	return FVector2D(TanAzimuth, TanElevation * FMath::Sqrt(TanAzimuth * TanAzimuth + 1));
}

void UTempoLidarCaptureComponent::BeginPlay()
{
	Super::BeginPlay();

	const float UndistortedVerticalImagePlaneSize = 2.0 * FMath::Tan(LidarOwner->VerticalFOV / 2.0);
	const FVector2D DistortedImagePlaneSize = 2.0 * SphericalToPerspective(LidarOwner->HorizontalFOV / 2.0, LidarOwner->VerticalFOV / 2.0);

	const float AspectRatio = DistortedImagePlaneSize.Y / DistortedImagePlaneSize.X;
	const FVector2D SizeXYContinuous = LidarOwner->UpsamplingFactor * FVector2D(LidarOwner->HorizontalBeams, AspectRatio * LidarOwner->HorizontalBeams);

	SizeXY = FIntPoint(FMath::CeilToInt32(SizeXYContinuous.X), FMath::CeilToInt32(SizeXYContinuous.Y));
	DistortionFactor = UndistortedVerticalImagePlaneSize / DistortedImagePlaneSize.Y;
	DistortedVerticalFOV = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(LidarOwner->VerticalFOV) / 2.0) * DistortionFactor));

	LidarOwner->SizeXY.X += SizeXY.X;
	LidarOwner->SizeXY.Y = SizeXY.Y;

	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	InitRenderTarget();
}

bool UTempoLidarCaptureComponent::HasPendingRequests() const
{
	return !LidarOwner->PendingRequests.IsEmpty();
}

FTextureRead* UTempoLidarCaptureComponent::MakeTextureRead() const
{
	check(GetWorld());

	return new TTextureRead<FDepthPixel>(SizeXY, SequenceId, GetWorld()->GetTimeSeconds(), this, LidarOwner);
}

void TTextureRead<FDepthPixel>::RespondToRequests(const TArray<FLidarScanRequest>& Requests, float TransmissionTime) const
{
	TempoLidar::LidarScanSegment ScanSegment;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarDecode);

		auto RangeAtBeam = [this] (int32 HorizontalBeam, int32 VerticalBeam) -> float
		{
			const FVector2D DistortedImagePlaneSize = 2.0 * SphericalToPerspective(TempoLidar->HorizontalFOV / 2.0, TempoLidar->VerticalFOV / 2.0);
			const float Azimuth = (-0.5 + static_cast<float>(HorizontalBeam) / TempoLidar->HorizontalBeams) * TempoLidar->HorizontalFOV;
			const float Elevation = (-0.5 + static_cast<float>(VerticalBeam) / TempoLidar->VerticalBeams) * TempoLidar->VerticalFOV;

			const FVector2D ImagePlaneLocation = SphericalToPerspective(Azimuth, Elevation);
			const FVector2D SizeXYFloat(CaptureComponent->SizeXY.X, CaptureComponent->SizeXY.Y);
			const FVector2D ImageCenter = 0.5 * SizeXYFloat;
			const FVector2D Ratio = ImagePlaneLocation / DistortedImagePlaneSize;
			const FVector2D Coordinate = Ratio * SizeXYFloat;
			const FVector2D PixelLocationContinuous = ImageCenter + Coordinate;

			const float Depth = Image[FMath::RoundToInt32(PixelLocationContinuous.X) + CaptureComponent->SizeXY.X * FMath::RoundToInt32(PixelLocationContinuous.Y)].GetDepth(TempoLidar->MinRange, TempoLidar->MaxRange);
			const float TanAzimuth = FMath::Tan(FMath::DegreesToRadians(Azimuth));
			const float TanElevation = FMath::Tan(FMath::DegreesToRadians(Elevation));
			return FMath::Sqrt((TanAzimuth * TanAzimuth + 1) * (TanElevation * TanElevation + 1)) * Depth;
		};

		ScanSegment.mutable_returns()->Reserve(CaptureComponent->HorizontalBeams * TempoLidar->VerticalBeams);
		for (int32 HorizontalBeam = 0; HorizontalBeam < CaptureComponent->HorizontalBeams; ++HorizontalBeam)
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
		const FVector SensorLocation = QuantityConverter<CM2M, L2R>::Convert(CaptureTransform.GetLocation());
		const FRotator SensorRotation = QuantityConverter<Deg2Rad, L2R>::Convert(CaptureTransform.Rotator());
		ScanSegment.mutable_header()->mutable_sensor_transform()->mutable_location()->set_x(SensorLocation.X);
		ScanSegment.mutable_header()->mutable_sensor_transform()->mutable_location()->set_y(SensorLocation.Y);
		ScanSegment.mutable_header()->mutable_sensor_transform()->mutable_location()->set_z(SensorLocation.Z);
		ScanSegment.mutable_header()->mutable_sensor_transform()->mutable_rotation()->set_p(SensorRotation.Pitch);
		ScanSegment.mutable_header()->mutable_sensor_transform()->mutable_rotation()->set_r(SensorRotation.Roll);
		ScanSegment.mutable_header()->mutable_sensor_transform()->mutable_rotation()->set_y(SensorRotation.Yaw);
  
		ScanSegment.set_scan_count(TempoLidar->LidarCaptureComponents.Num());
		ScanSegment.set_horizontal_beams(CaptureComponent->HorizontalBeams);
		ScanSegment.set_vertical_beams(TempoLidar->VerticalBeams);
		ScanSegment.mutable_azimuth_range()->set_max(FMath::DegreesToRadians(CaptureComponent->FOVAngle / 2.0 + CaptureComponent->GetRelativeRotation().Yaw));
		ScanSegment.mutable_azimuth_range()->set_min(FMath::DegreesToRadians(-CaptureComponent->FOVAngle / 2.0 + CaptureComponent->GetRelativeRotation().Yaw));
		ScanSegment.mutable_elevation_range()->set_max(FMath::DegreesToRadians(TempoLidar->VerticalFOV / 2.0));
        ScanSegment.mutable_elevation_range()->set_min(FMath::DegreesToRadians(-TempoLidar->VerticalFOV / 2.0));
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarRespond);

	for (auto RequestIt = Requests.CreateConstIterator(); RequestIt; ++RequestIt)
	{
		if (!RequestIt->ResponseContinuation.ExecuteIfBound(ScanSegment, grpc::Status_OK))
		{
			UE_LOG(LogTemp, Warning, TEXT("Was not bound at %d"), SequenceId);
		}
	}
}

TFuture<void> UTempoLidar::DecodeAndRespond(TArray<TUniquePtr<FTextureRead>> TextureReads)
{
	const double TransmissionTime = GetWorld()->GetTimeSeconds();

	NumResponded += TextureReads.Num();

	TFuture<void> Future = Async(EAsyncExecution::TaskGraph, [
		TextureReads = MoveTemp(TextureReads),
		Requests = PendingRequests,
		TransmissionTimeCpy = TransmissionTime
		]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarDecodeAndRespond);

		for (const TUniquePtr<FTextureRead>& TextureRead : TextureReads)
		{
			static_cast<TTextureRead<FDepthPixel>*>(TextureRead.Get())->RespondToRequests(Requests, TransmissionTimeCpy);
		}
	});

	
	return Future;
}

void UTempoLidar::RequestMeasurement(const TempoLidar::LidarScanRequest& Request, const TResponseDelegate<TempoLidar::LidarScanSegment>& ResponseContinuation)
{
	PendingRequests.Add({ Request, ResponseContinuation});
}

int32 UTempoLidarCaptureComponent::GetMaxTextureQueueSize() const
{
	return GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize();
}
