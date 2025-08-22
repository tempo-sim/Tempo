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

	// We allow up to 120 degrees horizontal FOV per capture component
	if (HorizontalFOV <= 120.0)
	{
		AddCaptureComponent(0.0, HorizontalFOV, HorizontalBeams);
	}
	else if (HorizontalFOV <= 240.0)
	{
		const double BeamGapSize = HorizontalFOV / (HorizontalBeams - 1);
		const int32 LeftSegmentBeams = FMath::CeilToInt32(HorizontalBeams / 2.0);
		const int32 RightSegmentBeams = HorizontalBeams - LeftSegmentBeams;
		const double LeftSegmentFOV = BeamGapSize * (LeftSegmentBeams - 1);
		const double RightSegmentFOV = BeamGapSize * (RightSegmentBeams - 1);
		AddCaptureComponent(-(LeftSegmentFOV + BeamGapSize) / 2.0, LeftSegmentFOV, LeftSegmentBeams);
		AddCaptureComponent((RightSegmentFOV + BeamGapSize) / 2.0, RightSegmentFOV, RightSegmentBeams);
	}
	else
	{
		const double BeamGapSize = HorizontalFOV / (HorizontalFOV < 360.0 ? HorizontalBeams - 1 : HorizontalBeams);
		const int32 SideSegmentBeams = FMath::CeilToInt32(HorizontalBeams / 3.0);
		const int32 CenterSegmentBeams = HorizontalBeams - 2 * SideSegmentBeams;
		const double SideSegmentFOV = BeamGapSize * (SideSegmentBeams - 1);
		const double CenterSegmentFOV = BeamGapSize * (CenterSegmentBeams - 1);
		AddCaptureComponent(0.0, CenterSegmentFOV, CenterSegmentBeams);
		AddCaptureComponent(-BeamGapSize - (CenterSegmentFOV + SideSegmentFOV) / 2.0, SideSegmentFOV, SideSegmentBeams);
		AddCaptureComponent(BeamGapSize + (CenterSegmentFOV + SideSegmentFOV) / 2.0, SideSegmentFOV, SideSegmentBeams);
	}
}

void UTempoLidar::AddCaptureComponent(double YawOffset, double SubHorizontalFOV, int32 SubHorizontalBeams)
{
	UTempoLidarCaptureComponent* LidarCaptureComponent = Cast<UTempoLidarCaptureComponent>(GetOwner()->AddComponentByClass(UTempoLidarCaptureComponent::StaticClass(), true, FTransform::Identity, true));
	LidarCaptureComponent->LidarOwner = this;
	LidarCaptureComponent->RateHz = RateHz;
	LidarCaptureComponent->HorizontalFOV = SubHorizontalFOV;
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

FVector2D SphericalToPerspective(double AzimuthDeg, double ElevationDeg)
{
	const double TanAzimuth = FMath::Tan(FMath::DegreesToRadians(AzimuthDeg));
	const double TanElevation = FMath::Tan(FMath::DegreesToRadians(ElevationDeg));
	return FVector2D(TanAzimuth, TanElevation * FMath::Sqrt(TanAzimuth * TanAzimuth + 1));
}

void PerspectiveToSpherical(const FVector2D& PerspectiveImagePlaneLocation, double& AzimuthDeg, double& ElevationDeg)
{
	const double Azimuth = FMath::Atan(PerspectiveImagePlaneLocation.X);
	const double TanAzimuth = FMath::Tan(Azimuth);
	const double Elevation = FMath::Atan(PerspectiveImagePlaneLocation.Y / FMath::Sqrt(TanAzimuth * TanAzimuth + 1));

	AzimuthDeg = FMath::RadiansToDegrees(Azimuth);
	ElevationDeg = FMath::RadiansToDegrees(Elevation);
}

double DepthToDistance(double AzimuthDeg, double ElevationDeg, double Depth)
{
	const double CosAzimuth = FMath::Cos(FMath::DegreesToRadians(AzimuthDeg));
	const double CosElevation = FMath::Cos(FMath::DegreesToRadians(ElevationDeg));
	return Depth / (CosAzimuth * CosElevation);
}

FVector SphericalToCartesian(double AzimuthDeg, double ElevationDeg, double Distance)
{
	const double Azimuth = FMath::DegreesToRadians(AzimuthDeg);
	const double Elevation = FMath::DegreesToRadians(ElevationDeg);
	return Distance * FVector(FMath::Cos(-Elevation) * FMath::Cos(Azimuth), FMath::Cos(-Elevation) * FMath::Sin(Azimuth), FMath::Sin(-Elevation));
}

void UTempoLidarCaptureComponent::BeginPlay()
{
	Super::BeginPlay();

	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	const double BeamGapSize = HorizontalFOV / (HorizontalBeams - 1);
	FOVAngle = HorizontalFOV + 2 * BeamGapSize;

	const double UndistortedVerticalImagePlaneSize = 2.0 * FMath::Tan(LidarOwner->VerticalFOV / 2.0);
	const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(HorizontalFOV / 2.0, LidarOwner->VerticalFOV / 2.0);

	const double AspectRatio = ImagePlaneSize.Y / ImagePlaneSize.X;
	SizeXYFOV = TempoSensorsSettings->GetLidarUpsamplingFactor() * FVector2D(HorizontalBeams, AspectRatio * HorizontalBeams);

	SizeXY = FIntPoint(FMath::CeilToInt32(SizeXYFOV.X) + 2, FMath::CeilToInt32(SizeXYFOV.Y) + 2);
	DistortionFactor = UndistortedVerticalImagePlaneSize / ImagePlaneSize.Y;
	DistortedVerticalFOV = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(LidarOwner->VerticalFOV) / 2.0) * DistortionFactor));

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

		const FIntPoint& SizeXY = CaptureComponent->SizeXY;
		const FVector2D& SizeXYFOV = CaptureComponent->SizeXYFOV;
		const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(CaptureComponent->HorizontalFOV / 2.0, TempoLidar->VerticalFOV / 2.0);
		const FVector2D SizeXYOffset = (FVector2D(SizeXY) - SizeXYFOV) / 2.0;

		const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
		if (!TempoSensorsSettings)
		{
			return;
		}
		ELidarInterpolationMethod InterpolationMethod = TempoSensorsSettings->GetLidarInterpolationMethod();

		auto DistanceAtBeam = [this, &ImagePlaneSize, &SizeXY, &SizeXYFOV, &SizeXYOffset, InterpolationMethod] (int32 HorizontalBeam, int32 VerticalBeam) -> double
		{
			auto ImagePlaneLocationToPixelCoordinate = [&ImagePlaneSize, &SizeXYFOV, &SizeXYOffset](const FVector2D& ImagePlaneLocation)
			{
				return (FVector2D::UnitVector / 2.0 + ImagePlaneLocation / ImagePlaneSize) * (SizeXYFOV - FVector2D::UnitVector) + SizeXYOffset;
			};

			auto PixelCoordinateToImagePlaneLocation = [&ImagePlaneSize, &SizeXYFOV, &SizeXYOffset](const FVector2D& PixelCoordinate)
			{
				return ((PixelCoordinate - SizeXYOffset) / (SizeXYFOV - FVector2D::UnitVector) - (FVector2D::UnitVector / 2.0)) * ImagePlaneSize;
			};

			const double AzimuthDeg = (-0.5 + static_cast<double>(HorizontalBeam) / CaptureComponent->HorizontalBeams) * CaptureComponent->HorizontalFOV;
			const double ElevationDeg = (-0.5 + static_cast<double>(VerticalBeam) / TempoLidar->VerticalBeams) * TempoLidar->VerticalFOV;
			const FVector2D ImagePlaneLocation = SphericalToPerspective(AzimuthDeg, ElevationDeg);
			const FVector2D PixelCoordinate = ImagePlaneLocationToPixelCoordinate(ImagePlaneLocation);

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

					const FVector2D ImagePlaneLocationAboveLeft = PixelCoordinateToImagePlaneLocation(FVector2D(ColLeft, RowAbove));
					const FVector2D ImagePlaneLocationAboveRight = PixelCoordinateToImagePlaneLocation(FVector2D(ColRight, RowAbove));
					const FVector2D ImagePlaneLocationBelowLeft = PixelCoordinateToImagePlaneLocation(FVector2D(ColLeft, RowBelow));
					const FVector2D ImagePlaneLocationBelowRight = PixelCoordinateToImagePlaneLocation(FVector2D(ColRight, RowBelow));

					double AzimuthDegAboveLeft, ElevationDegAboveLeft;
					PerspectiveToSpherical(ImagePlaneLocationAboveLeft, AzimuthDegAboveLeft, ElevationDegAboveLeft);
					double AzimuthDegAboveRight, ElevationDegAboveRight;
					PerspectiveToSpherical(ImagePlaneLocationAboveRight, AzimuthDegAboveRight, ElevationDegAboveRight);
					double AzimuthDegBelowLeft, ElevationDegBelowLeft;
					PerspectiveToSpherical(ImagePlaneLocationBelowLeft, AzimuthDegBelowLeft, ElevationDegBelowLeft);
					double AzimuthDegBelowRight, ElevationDegBelowRight;
					PerspectiveToSpherical(ImagePlaneLocationBelowRight, AzimuthDegBelowRight, ElevationDegBelowRight);

					const double DistanceAboveLeft = DepthToDistance(AzimuthDegAboveLeft, ElevationDegAboveLeft, DepthAboveLeft);
					const double DistanceAboveRight = DepthToDistance(AzimuthDegAboveRight, ElevationDegAboveRight, DepthAboveRight);
					const double DistanceBelowLeft = DepthToDistance(AzimuthDegBelowLeft, ElevationDegBelowLeft, DepthBelowLeft);
					const double DistanceBelowRight = DepthToDistance(AzimuthDegBelowRight, ElevationDegBelowRight, DepthBelowRight);

					const FVector PointAboveLeft = SphericalToCartesian(AzimuthDegAboveLeft, ElevationDegAboveLeft, DistanceAboveLeft);
					const FVector PointAboveRight = SphericalToCartesian(AzimuthDegAboveRight, ElevationDegAboveRight, DistanceAboveRight);
					const FVector PointBelowLeft = SphericalToCartesian(AzimuthDegBelowLeft, ElevationDegBelowLeft, DistanceBelowLeft);
					const FVector PointBelowRight = SphericalToCartesian(AzimuthDegBelowRight, ElevationDegBelowRight, DistanceBelowRight);

					const FVector RayDirection = SphericalToCartesian(AzimuthDeg, ElevationDeg, TempoLidar->MaxRange);

					if (ColLeft == ColRight)
					{
						FVector Intersection1, Intersection2;
						FMath::SegmentDistToSegment(PointAboveLeft, PointBelowLeft, FVector::ZeroVector, RayDirection, Intersection1, Intersection2);
						ensureMsgf(FVector::PointsAreNear(Intersection1, Intersection2, UE_KINDA_SMALL_NUMBER), TEXT("Segment between points with matching columns did not intersect ray"));
						return Intersection2.Length();
					}
					if (RowAbove == RowBelow)
					{
						FVector Intersection1, Intersection2;
						FMath::SegmentDistToSegment(PointAboveLeft, PointAboveRight, FVector::ZeroVector, RayDirection, Intersection1, Intersection2);
						if (!FVector::PointsAreNear(Intersection1, Intersection2, 0.1))
						ensureMsgf(FVector::PointsAreNear(Intersection1, Intersection2, UE_KINDA_SMALL_NUMBER), TEXT("Segment between points with matching rows did not intersect ray"));
						return Intersection2.Length();
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

		// Intermediate "Returns" to allow the parallel for (DistanceAtBeam is somewhat expensive)
		std::vector<TempoLidar::LidarReturn> Returns(CaptureComponent->HorizontalBeams * TempoLidar->VerticalBeams);
		ParallelFor(CaptureComponent->HorizontalBeams, [this, &Returns, &DistanceAtBeam](int32 HorizontalBeam)
		{
			for (int32 VerticalBeam = 0; VerticalBeam < TempoLidar->VerticalBeams; ++VerticalBeam)
			{
				double Distance = DistanceAtBeam(HorizontalBeam, VerticalBeam);
				if (Distance > TempoLidar->MaxRange)
				{
					Distance = 0.0;
				}
				Distance = FMath::Max(TempoLidar->MinRange, Distance);
				Returns[HorizontalBeam + CaptureComponent->HorizontalBeams * VerticalBeam].set_distance(QuantityConverter<CM2M>::Convert(Distance));
			}
		});

		ScanSegment.mutable_returns()->Reserve(CaptureComponent->HorizontalBeams * TempoLidar->VerticalBeams);
		for (int32 HorizontalBeam = 0; HorizontalBeam < CaptureComponent->HorizontalBeams; ++HorizontalBeam)
		{
			for (int32 VerticalBeam = 0; VerticalBeam < TempoLidar->VerticalBeams; ++VerticalBeam)
			{
				if ((HorizontalBeam + SizeXY.X * VerticalBeam) % 1000 == 0)
				{
					const double AzimuthDeg = (-0.5 + static_cast<double>(HorizontalBeam) / CaptureComponent->HorizontalBeams) * CaptureComponent->HorizontalFOV;
					const double ElevationDeg = (-0.5 + static_cast<double>(VerticalBeam) / TempoLidar->VerticalBeams) * TempoLidar->VerticalFOV;
					const FVector HitPoint = SphericalToCartesian(AzimuthDeg, ElevationDeg, Returns[HorizontalBeam + CaptureComponent->HorizontalBeams * VerticalBeam].distance() * 100.0);
					DrawDebugPoint(CaptureComponent->GetWorld(), CaptureComponent->GetComponentTransform().TransformPosition(HitPoint), 8.0, FColor::Green, false, 0.1, 0);
				}
				*ScanSegment.add_returns() = Returns[HorizontalBeam + CaptureComponent->HorizontalBeams * VerticalBeam];
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
		ScanSegment.mutable_azimuth_range()->set_max(FMath::DegreesToRadians(CaptureComponent->HorizontalFOV / 2.0 + CaptureComponent->GetRelativeRotation().Yaw));
		ScanSegment.mutable_azimuth_range()->set_min(FMath::DegreesToRadians(-CaptureComponent->HorizontalFOV / 2.0 + CaptureComponent->GetRelativeRotation().Yaw));
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
