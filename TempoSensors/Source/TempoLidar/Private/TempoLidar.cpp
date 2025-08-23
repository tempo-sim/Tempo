// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "TempoSensorsConstants.h"

#include "TempoConversion.h"
#include "TempoLabelTypes.h"
#include "TempoLidarModule.h"

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
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	PixelFormatOverride = EPixelFormat::PF_A16B16G16R16;
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

	FOVAngle = HorizontalFOV;

	const double UndistortedVerticalImagePlaneSize = 2.0 * FMath::Tan(LidarOwner->VerticalFOV / 2.0);
	const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(HorizontalFOV / 2.0, LidarOwner->VerticalFOV / 2.0);

	const double AspectRatio = ImagePlaneSize.Y / ImagePlaneSize.X;
	SizeXYFOV = TempoSensorsSettings->GetLidarUpsamplingFactor() * FVector2D(HorizontalBeams, AspectRatio * HorizontalBeams);

	SizeXY = FIntPoint(FMath::CeilToInt32(SizeXYFOV.X), FMath::CeilToInt32(SizeXYFOV.Y));
	DistortionFactor = UndistortedVerticalImagePlaneSize / ImagePlaneSize.Y;
	DistortedVerticalFOV = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(LidarOwner->VerticalFOV) / 2.0) * DistortionFactor));

	if (const TObjectPtr<UMaterialInterface> PostProcessMaterial = GetDefault<UTempoSensorsSettings>()->GetLidarPostProcessMaterial())
	{
		PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterial.Get(), this);
		PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterial.Get(), this);
		MinDepth = GEngine->NearClipPlane;
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MinDepth"), MinDepth);
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDepth"), LidarOwner->MaxRange);
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDiscreteDepth"), GTempo_Max_Discrete_Depth);
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

	UDataTable* SemanticLabelTable = GetDefault<UTempoSensorsSettings>()->GetSemanticLabelTable();
	FName OverridableLabelRowName = TempoSensorsSettings->GetOverridableLabelRowName();
	FName OverridingLabelRowName = TempoSensorsSettings->GetOverridingLabelRowName();
	TOptional<int32> OverridableLabel;
	TOptional<int32> OverridingLabel;
	if (!OverridableLabelRowName.IsNone())
	{
		SemanticLabelTable->ForeachRow<FSemanticLabel>(TEXT(""),
			[&OverridableLabelRowName,
				&OverridingLabelRowName,
				&OverridableLabel,
				&OverridingLabel](const FName& Key, const FSemanticLabel& Value)
		{
			if (Key == OverridableLabelRowName)
			{
				OverridableLabel = Value.Label;
			}
			if (Key == OverridingLabelRowName)
			{
				OverridingLabel = Value.Label;
			}
		});
	}

	if (PostProcessMaterialInstance)
	{
		if (OverridableLabel.IsSet() && OverridingLabel.IsSet())
		{
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("OverridableLabel"), OverridableLabel.GetValue());
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("OverridingLabel"), OverridingLabel.GetValue());
		}
		else
		{
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("OverridingLabel"), 0.0);
		}
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

bool UTempoLidarCaptureComponent::HasPendingRequests() const
{
	return !LidarOwner->PendingRequests.IsEmpty();
}

FTextureRead* UTempoLidarCaptureComponent::MakeTextureRead() const
{
	check(GetWorld());

	return new TTextureRead<FLidarPixel>(SizeXY, SequenceId, GetWorld()->GetTimeSeconds(), this, LidarOwner, MinDepth, LidarOwner->MaxRange);
}

void TTextureRead<FLidarPixel>::RespondToRequests(const TArray<FLidarScanRequest>& Requests, float TransmissionTime) const
{
	TempoLidar::LidarScanSegment ScanSegment;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarDecode);

		const FIntPoint& SizeXY = CaptureComponent->SizeXY;
		const FVector2D& SizeXYFOV = CaptureComponent->SizeXYFOV;
		const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(CaptureComponent->HorizontalFOV / 2.0, LidarOwner->VerticalFOV / 2.0);
		const FVector2D SizeXYOffset = (FVector2D(SizeXY) - SizeXYFOV) / 2.0;

		auto LidarReturnFromBeam = [this, &ImagePlaneSize, &SizeXY, &SizeXYFOV, &SizeXYOffset] (int32 HorizontalBeam, int32 VerticalBeam) -> TempoLidar::LidarReturn
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
			const double ElevationDeg = (-0.5 + static_cast<double>(VerticalBeam) / LidarOwner->VerticalBeams) * LidarOwner->VerticalFOV;
			const FVector2D ImagePlaneLocation = SphericalToPerspective(AzimuthDeg, ElevationDeg);
			const FVector2D PixelCoordinate = ImagePlaneLocationToPixelCoordinate(ImagePlaneLocation);
			const FIntPoint Coord(FMath::RoundToInt32(PixelCoordinate.X), FMath::RoundToInt32(PixelCoordinate.Y));
			const float NearestDepth = Image[Coord.X + SizeXY.X * Coord.Y].Depth(MinDepth, LidarOwner->MaxRange, GTempo_Max_Discrete_Depth);
			const FVector2D ImagePlaneLocationAboveLeft = PixelCoordinateToImagePlaneLocation(FVector2D(Coord.X, Coord.Y));
			double AzimuthDegNearest, ElevationDegNearest;
			PerspectiveToSpherical(ImagePlaneLocationAboveLeft, AzimuthDegNearest, ElevationDegNearest);
			const float NearestDistance =  DepthToDistance(AzimuthDegNearest, ElevationDegNearest, NearestDepth);
			const FVector NearestPoint = SphericalToCartesian(AzimuthDegNearest, ElevationDegNearest, NearestDistance);
			const FVector WorldNormal = Image[Coord.X + SizeXY.X * Coord.Y].Normal();
			const FVector LocalNormal = CaptureComponent->GetComponentTransform().InverseTransformVector(WorldNormal);
			const FVector RayDirection = SphericalToCartesian(AzimuthDeg, ElevationDeg, LidarOwner->MaxRange);
			const FPlane SurfacePlane( NearestPoint, LocalNormal );
			const FVector HitPoint = FMath::LinePlaneIntersection(FVector::ZeroVector, RayDirection, SurfacePlane);

			double Distance = HitPoint.Length();
			TempoLidar::LidarReturn LidarReturn;
			if (Distance > LidarOwner->MaxRange)
			{
				Distance = 0.0;
			}
			Distance = FMath::Max(LidarOwner->MinRange, Distance);
			LidarReturn.set_distance(QuantityConverter<CM2M>::Convert(Distance));
			LidarReturn.set_intensity(0.0);
			LidarReturn.set_label(Image[Coord.X + SizeXY.X * Coord.Y].Label());
			return LidarReturn;
		};

		// Intermediate "Returns" to allow the parallel for (DistanceAtBeam is somewhat expensive)
		std::vector<TempoLidar::LidarReturn> LidarReturns(CaptureComponent->HorizontalBeams * LidarOwner->VerticalBeams);
		ParallelFor(CaptureComponent->HorizontalBeams, [this, &LidarReturns, &LidarReturnFromBeam](int32 HorizontalBeam)
		{
			for (int32 VerticalBeam = 0; VerticalBeam < LidarOwner->VerticalBeams; ++VerticalBeam)
			{
				LidarReturns[HorizontalBeam + CaptureComponent->HorizontalBeams * VerticalBeam] = LidarReturnFromBeam(HorizontalBeam, VerticalBeam);
			}
		});

		ScanSegment.mutable_returns()->Reserve(CaptureComponent->HorizontalBeams * LidarOwner->VerticalBeams);
		for (int32 HorizontalBeam = 0; HorizontalBeam < CaptureComponent->HorizontalBeams; ++HorizontalBeam)
		{
			for (int32 VerticalBeam = 0; VerticalBeam < LidarOwner->VerticalBeams; ++VerticalBeam)
			{
				*ScanSegment.add_returns() = LidarReturns[HorizontalBeam + CaptureComponent->HorizontalBeams * VerticalBeam];
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

		ScanSegment.set_scan_count(LidarOwner->LidarCaptureComponents.Num());
		ScanSegment.set_horizontal_beams(CaptureComponent->HorizontalBeams);
		ScanSegment.set_vertical_beams(LidarOwner->VerticalBeams);
		ScanSegment.mutable_azimuth_range()->set_max(FMath::DegreesToRadians(CaptureComponent->HorizontalFOV / 2.0 + CaptureComponent->GetRelativeRotation().Yaw));
		ScanSegment.mutable_azimuth_range()->set_min(FMath::DegreesToRadians(-CaptureComponent->HorizontalFOV / 2.0 + CaptureComponent->GetRelativeRotation().Yaw));
		ScanSegment.mutable_elevation_range()->set_max(FMath::DegreesToRadians(LidarOwner->VerticalFOV / 2.0));
		ScanSegment.mutable_elevation_range()->set_min(FMath::DegreesToRadians(-LidarOwner->VerticalFOV / 2.0));
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
			static_cast<TTextureRead<FLidarPixel>*>(TextureRead.Get())->RespondToRequests(Requests, TransmissionTimeCpy);
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
