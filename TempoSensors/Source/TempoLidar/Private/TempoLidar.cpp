// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "TempoConversion.h"
#include "TempoCoreUtils.h"

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
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_R32f;
	PixelFormatOverride = EPixelFormat::PF_Unknown;
	ShowFlags.SetAntiAliasing(false);
	ShowFlags.SetTemporalAA(false);
	ShowFlags.SetMotionBlur(false);
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

void PerspectiveToSpherical(const FVector2D& PerspectiveImagePlaneLocation, float& AzimuthDeg, float& ElevationDeg)
{
	const float Azimuth = FMath::Atan(PerspectiveImagePlaneLocation.X);
	const float TanAzimuth = FMath::Tan(Azimuth);
	const float Elevation = FMath::Atan(PerspectiveImagePlaneLocation.Y / FMath::Sqrt(TanAzimuth * TanAzimuth + 1));

	AzimuthDeg = FMath::RadiansToDegrees(Azimuth);
	ElevationDeg = FMath::RadiansToDegrees(Elevation);
}

float DepthToDistance(float AzimuthDeg, float ElevationDeg, float Depth)
{
	const float TanAzimuth = FMath::Tan(FMath::DegreesToRadians(AzimuthDeg));
	const float TanElevation = FMath::Tan(FMath::DegreesToRadians(ElevationDeg));
	return FMath::Sqrt((TanAzimuth * TanAzimuth + 1) * (TanElevation * TanElevation + 1)) * Depth;
}

FVector SphericalToCartesian(float AzimuthDeg, float ElevationDeg, float Distance)
{
	const float Azimuth = FMath::DegreesToRadians(AzimuthDeg);
	const float Elevation = FMath::DegreesToRadians(ElevationDeg);
	return Distance * FVector(FMath::Cos(Elevation) * FMath::Cos(Azimuth), FMath::Cos(Elevation) * FMath::Sin(Azimuth), FMath::Sin(Elevation));
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

	return new TTextureRead<float>(SizeXY, SequenceId, GetWorld()->GetTimeSeconds(), this, LidarOwner);
}

void TTextureRead<float>::RespondToRequests(const TArray<FLidarScanRequest>& Requests, float TransmissionTime) const
{
	TempoLidar::LidarScanSegment ScanSegment;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarDecode);

		const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(TempoLidar->HorizontalFOV / 2.0, TempoLidar->VerticalFOV / 2.0);

		const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
		if (!TempoSensorsSettings)
		{
			return;
		}
		ELidarInterpolationMethod InterpolationMethod = TempoSensorsSettings->GetLidarInterpolationMethod();

		auto DistanceAtBeam = [this, &ImagePlaneSize, InterpolationMethod] (int32 HorizontalBeam, int32 VerticalBeam) -> float
		{
			const FIntPoint& SizeXY = CaptureComponent->SizeXY;
			const double AzimuthDeg = (-0.5 + static_cast<double>(HorizontalBeam) / TempoLidar->HorizontalBeams) * TempoLidar->HorizontalFOV;
			const double ElevationDeg = (-0.5 + static_cast<double>(VerticalBeam) / TempoLidar->VerticalBeams) * TempoLidar->VerticalFOV;
			const FVector2D ImagePlaneLocation = SphericalToPerspective(AzimuthDeg, ElevationDeg);
			const FVector2D PixelCoordinate = (FVector2D(0.5, 0.5) + ImagePlaneLocation / ImagePlaneSize) * SizeXY;

			switch (InterpolationMethod)
			{
				case ELidarInterpolationMethod::Nearest:
				{
					const float Depth = Image[FMath::RoundToInt32(PixelCoordinate.X) + SizeXY.X * FMath::RoundToInt32(PixelCoordinate.Y)];
					return DepthToDistance(AzimuthDeg, ElevationDeg, Depth);
				}
				case ELidarInterpolationMethod::PlanarFit:
				{
					const int32 ColLeft = FMath::FloorToInt32(PixelCoordinate.X);
					const int32 ColRight = FMath::CeilToInt32(PixelCoordinate.X);
					const int32 RowAbove = FMath::FloorToInt32(PixelCoordinate.Y);
					const int32 RowBelow = FMath::CeilToInt32(PixelCoordinate.Y);

					if (ColLeft == ColRight && RowAbove == RowBelow)
					{
						const float Depth = Image[ColLeft + SizeXY.X * RowAbove];
						return DepthToDistance(AzimuthDeg, ElevationDeg, Depth);
					}

					const float DepthAboveLeft = Image[ColLeft + SizeXY.X * RowAbove];
					const float DepthAboveRight = Image[ColRight + SizeXY.X * RowAbove];
					const float DepthBelowLeft = Image[ColLeft + SizeXY.X * RowBelow];
					const float DepthBelowRight = Image[ColRight + SizeXY.X * RowBelow];

					const FVector2D ImagePlaneLocationAboveLeft = (FVector2D(ColLeft, RowAbove) / SizeXY - FVector2D(0.5, 0.5)) * ImagePlaneSize;
					const FVector2D ImagePlaneLocationAboveRight = (FVector2D(ColRight, RowAbove) / SizeXY - FVector2D(0.5, 0.5)) * ImagePlaneSize;
					const FVector2D ImagePlaneLocationBelowLeft = (FVector2D(ColLeft, RowBelow) / SizeXY - FVector2D(0.5, 0.5)) * ImagePlaneSize;
					const FVector2D ImagePlaneLocationBelowRight = (FVector2D(ColRight, RowBelow) / SizeXY - FVector2D(0.5, 0.5)) * ImagePlaneSize;

					float AzimuthDegAboveLeft, ElevationDegAboveLeft;
					PerspectiveToSpherical(ImagePlaneLocationAboveLeft, AzimuthDegAboveLeft, ElevationDegAboveLeft);
					float AzimuthDegAboveRight, ElevationDegAboveRight;
					PerspectiveToSpherical(ImagePlaneLocationAboveRight, AzimuthDegAboveRight, ElevationDegAboveRight);
					float AzimuthDegBelowLeft, ElevationDegBelowLeft;
					PerspectiveToSpherical(ImagePlaneLocationBelowLeft, AzimuthDegBelowLeft, ElevationDegBelowLeft);
					float AzimuthDegBelowRight, ElevationDegBelowRight;
					PerspectiveToSpherical(ImagePlaneLocationBelowRight, AzimuthDegBelowRight, ElevationDegBelowRight);

					const float DistanceAboveLeft = DepthToDistance(AzimuthDegAboveLeft, ElevationDegAboveLeft, DepthAboveLeft);
					const float DistanceAboveRight = DepthToDistance(AzimuthDegAboveRight, ElevationDegAboveRight, DepthAboveRight);
					const float DistanceBelowLeft = DepthToDistance(AzimuthDegBelowLeft, ElevationDegBelowLeft, DepthBelowLeft);
					const float DistanceBelowRight = DepthToDistance(AzimuthDegBelowRight, ElevationDegBelowRight, DepthBelowRight);

					const FVector PointAboveLeft = SphericalToCartesian(AzimuthDegAboveLeft, ElevationDegAboveLeft, DistanceAboveLeft);
					const FVector PointAboveRight = SphericalToCartesian(AzimuthDegAboveRight, ElevationDegAboveRight, DistanceAboveRight);
					const FVector PointBelowLeft = SphericalToCartesian(AzimuthDegBelowLeft, ElevationDegBelowLeft, DistanceBelowLeft);
					const FVector PointBelowRight = SphericalToCartesian(AzimuthDegBelowRight, ElevationDegBelowRight, DistanceBelowRight);

					const FVector RayDirection = SphericalToCartesian(AzimuthDeg, ElevationDeg, UE_BIG_NUMBER);

					if (ColLeft == ColRight)
					{
						FVector Intersection1, Intersection2;
						FMath::SegmentDistToSegment(PointAboveLeft, PointBelowLeft, FVector::ZeroVector, RayDirection, Intersection1, Intersection2);
						return Intersection1.Length();
					}
					if (RowAbove == RowBelow)
					{
						FVector Intersection1, Intersection2;
						FMath::SegmentDistToSegment(PointAboveLeft, PointAboveRight, FVector::ZeroVector, RayDirection, Intersection1, Intersection2);
						return Intersection1.Length();
					}

					FPlane BestFitPlane = UTempoCoreUtils::BestFitPlaneFromFourPoints(PointAboveRight, PointBelowRight, PointBelowLeft, PointAboveLeft);
					FVector HitPoint = FMath::LinePlaneIntersection(FVector::ZeroVector, RayDirection, BestFitPlane);
					return HitPoint.Length();
				}
				default:
				{
					return 0.0;
				}
			}
		};

		// Intermediate Returns to allow the parallel for (DistanceAtBeam can be somewhat expensive)
		std::vector<TempoLidar::LidarReturn> Returns(CaptureComponent->HorizontalBeams * TempoLidar->VerticalBeams);
		ParallelFor(CaptureComponent->HorizontalBeams, [this, &Returns, &DistanceAtBeam](int32 HorizontalBeam)
		{
			for (int32 VerticalBeam = 0; VerticalBeam < TempoLidar->VerticalBeams; ++VerticalBeam)
			{
				float Distance = DistanceAtBeam(HorizontalBeam, VerticalBeam);
				if (Distance > TempoLidar->MaxRange)
				{
					Distance = 0.0;
				}
				Distance = FMath::Max(TempoLidar->MinRange, Distance);
				Returns[HorizontalBeam + TempoLidar->HorizontalBeams * VerticalBeam].set_distance(QuantityConverter<CM2M>::Convert(Distance));
			}
		});

		ScanSegment.mutable_returns()->Reserve(CaptureComponent->HorizontalBeams * TempoLidar->VerticalBeams);
		for (int32 HorizontalBeam = 0; HorizontalBeam < CaptureComponent->HorizontalBeams; ++HorizontalBeam)
		{
			for (int32 VerticalBeam = 0; VerticalBeam < TempoLidar->VerticalBeams; ++VerticalBeam)
			{
				*ScanSegment.add_returns() = Returns[HorizontalBeam + TempoLidar->HorizontalBeams * VerticalBeam];
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
			static_cast<TTextureRead<float>*>(TextureRead.Get())->RespondToRequests(Requests, TransmissionTimeCpy);
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
