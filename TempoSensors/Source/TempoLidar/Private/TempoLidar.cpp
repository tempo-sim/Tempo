// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "TempoSensorsConstants.h"

#include "TempoConversion.h"
#include "TempoLabelTypes.h"
#include "TempoLidarModule.h"

#include "TempoLidar/Lidar.pb.h"

#include "TempoSensorsSettings.h"
#include "TempoSensorsUtils.h"

namespace
{
	const FName LeftCaptureComponentName(TEXT("LeftTempoLidarCaptureComponent"));
	const FName CenterCaptureComponentName(TEXT("CenterTempoLidarCaptureComponent"));
	const FName RightCaptureComponentName(TEXT("RightTempoLidarCaptureComponent"));
}

UTempoLidar::UTempoLidar()
{
	PrimaryComponentTick.bCanEverTick = true;
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
	for (const UTempoLidarCaptureComponent* LidarCaptureComponent : GetActiveCaptureComponents())
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
	for (UTempoLidarCaptureComponent* LidarCaptureComponent : GetActiveCaptureComponents())
	{
		LidarCaptureComponent->ReadNextIfAvailable();
	}
}

void UTempoLidar::BlockUntilMeasurementsReady() const
{
	for (const UTempoLidarCaptureComponent* LidarCaptureComponent : GetActiveCaptureComponents())
	{
		LidarCaptureComponent->BlockUntilNextReadComplete();
	}
}

TOptional<TFuture<void>> UTempoLidar::SendMeasurements()
{
	TArray<TUniquePtr<FTextureRead>> TextureReads;
	const TArray<UTempoLidarCaptureComponent*> ActiveCaptureComponents = GetActiveCaptureComponents();
	const int32 NumCompletedReads = ActiveCaptureComponents.FilterByPredicate(
			[](const UTempoLidarCaptureComponent* CaptureComponent) { return CaptureComponent->NextReadComplete(); }
		).Num();
	if (NumCompletedReads == ActiveCaptureComponents.Num())
	{
		for (UTempoLidarCaptureComponent* CaptureComponent : ActiveCaptureComponents)
		{
			TextureReads.Add(CaptureComponent->DequeueIfReadComplete());
		}
	}

	TOptional<TFuture<void>> Future = DecodeAndRespond(MoveTemp(TextureReads));

	if (NumResponded == ActiveCaptureComponents.Num())
	{
		PendingRequests.Empty();
		NumResponded = 0;
		SequenceId++;

		SyncCaptureComponents();
	}

	return Future;
}

void UTempoLidar::SyncCaptureComponents()
{
	// We allow up to 120 degrees horizontal FOV per capture component
	const TMap<FName, UTempoLidarCaptureComponent*> CaptureComponents = GetOrCreateCaptureComponents();
	if (HorizontalFOV <= 120.0)
	{
		SyncCaptureComponent(CaptureComponents[LeftCaptureComponentName], false, 0.0, 0.0, 0);
		SyncCaptureComponent(CaptureComponents[CenterCaptureComponentName], true, 0.0, HorizontalFOV, HorizontalBeams);
		SyncCaptureComponent(CaptureComponents[RightCaptureComponentName], false, 0.0, 0.0, 0);
	}
	else if (HorizontalFOV <= 240.0)
	{
		const double BeamGapSize = HorizontalFOV / (HorizontalBeams - 1);
		const int32 LeftSegmentBeams = FMath::CeilToInt32(HorizontalBeams / 2.0);
		const int32 RightSegmentBeams = HorizontalBeams - LeftSegmentBeams;
		const double LeftSegmentFOV = BeamGapSize * (LeftSegmentBeams - 1);
		const double RightSegmentFOV = BeamGapSize * (RightSegmentBeams - 1);
		SyncCaptureComponent(CaptureComponents[LeftCaptureComponentName], true, -(LeftSegmentFOV + BeamGapSize) / 2.0, LeftSegmentFOV, LeftSegmentBeams);
		SyncCaptureComponent(CaptureComponents[CenterCaptureComponentName], false, 0.0, 0.0, 0);
		SyncCaptureComponent(CaptureComponents[RightCaptureComponentName], true, (RightSegmentFOV + BeamGapSize) / 2.0, RightSegmentFOV, RightSegmentBeams);
	}
	else
	{
		const double BeamGapSize = HorizontalFOV / (HorizontalFOV < 360.0 ? HorizontalBeams - 1 : HorizontalBeams);
		const int32 SideSegmentBeams = FMath::CeilToInt32(HorizontalBeams / 3.0);
		const int32 CenterSegmentBeams = HorizontalBeams - 2 * SideSegmentBeams;
		const double SideSegmentFOV = BeamGapSize * (SideSegmentBeams - 1);
		const double CenterSegmentFOV = BeamGapSize * (CenterSegmentBeams - 1);
		SyncCaptureComponent(CaptureComponents[LeftCaptureComponentName], true, -BeamGapSize - (CenterSegmentFOV + SideSegmentFOV) / 2.0, SideSegmentFOV, SideSegmentBeams);
		SyncCaptureComponent(CaptureComponents[CenterCaptureComponentName], true, 0.0, CenterSegmentFOV, CenterSegmentBeams);
		SyncCaptureComponent(CaptureComponents[RightCaptureComponentName], true, BeamGapSize + (CenterSegmentFOV + SideSegmentFOV) / 2.0, SideSegmentFOV, SideSegmentBeams);
	}
}

void UTempoLidar::OnRegister()
{
	Super::OnRegister();

	SyncCaptureComponents();
}

void UTempoLidar::SyncCaptureComponent(UTempoLidarCaptureComponent* LidarCaptureComponent, bool bActive, double YawOffset, double SubHorizontalFOV, double SubHorizontalBeams)
{
	LidarCaptureComponent->Configure(YawOffset, SubHorizontalFOV, SubHorizontalBeams);
	if (bActive && !LidarCaptureComponent->IsActive())
	{
		LidarCaptureComponent->Activate();
	}
	if (!bActive && LidarCaptureComponent->IsActive())
	{
		LidarCaptureComponent->Deactivate();
	}
}

UTempoLidarCaptureComponent::UTempoLidarCaptureComponent()
{
	// Final Color? Necessary to use post-processing, which we need to pack normal, label, and depth very tight.
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	PixelFormatOverride = EPixelFormat::PF_A16B16G16R16;

	// Disable as many unnecessary rendering features as possible.
	OptimizeShowFlagsForNoColor(ShowFlags);
	bUseRayTracingIfEnabled = false;

	// This component will be activated and deactivated by UTempoLidar.
	bAutoActivate = false;
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

void UTempoLidarCaptureComponent::Activate(bool bReset)
{
	Super::Activate(bReset);

	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	if (const TObjectPtr<UMaterialInterface> PostProcessMaterial = GetDefault<UTempoSensorsSettings>()->GetLidarPostProcessMaterial())
	{
		PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterial.Get(), this);
		PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterial.Get(), this);
		MinDepth = GEngine->NearClipPlane;
		MaxDepth = TempoSensorsSettings->GetMaxLidarDepth();
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MinDepth"), MinDepth);
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDepth"), MaxDepth);
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
}

bool UTempoLidarCaptureComponent::HasPendingRequests() const
{
	return !LidarOwner->PendingRequests.IsEmpty();
}

FTextureRead* UTempoLidarCaptureComponent::MakeTextureRead() const
{
	check(GetWorld());

	return new TTextureRead<FLidarPixel>(SizeXY, LidarOwner->SequenceId + NumPendingTextureReads(), GetWorld()->GetTimeSeconds(), LidarOwner->GetOwnerName(),
		LidarOwner->GetSensorName(), LidarOwner->GetComponentTransform(), GetComponentTransform(), FOVAngle,
		LidarOwner->VerticalFOV, HorizontalBeams, LidarOwner->VerticalBeams, SizeXYFOV,
		LidarOwner->IntensitySaturationDistance, LidarOwner->MaxAngleOfIncidence,
		LidarOwner->GetActiveCaptureComponents().Num(), GetRelativeRotation().Yaw, MinDepth, MaxDepth, LidarOwner->MinDistance, LidarOwner->MaxDistance);
}

void TTextureRead<FLidarPixel>::Decode(float TransmissionTime, TempoLidar::LidarScanSegment& ScanSegmentOut) const
{
	const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(HorizontalFOV / 2.0, VerticalFOV / 2.0);
	const FVector2D SizeXYOffset = (FVector2D(ImageSize) - SizeXYFOV) / 2.0;

	auto LidarReturnFromBeam = [this, &ImagePlaneSize, &SizeXYOffset] (int32 HorizontalBeam, int32 VerticalBeam) -> TempoLidar::LidarReturn
	{
		auto ImagePlaneLocationToPixelCoordinate = [this, &ImagePlaneSize, &SizeXYOffset](const FVector2D& ImagePlaneLocation)
		{
			return (FVector2D::UnitVector / 2.0 + ImagePlaneLocation / ImagePlaneSize) * (SizeXYFOV - FVector2D::UnitVector) + SizeXYOffset;
		};

		auto PixelCoordinateToImagePlaneLocation = [this, &ImagePlaneSize, &SizeXYOffset](const FVector2D& PixelCoordinate)
		{
			return ((PixelCoordinate - SizeXYOffset) / (SizeXYFOV - FVector2D::UnitVector) - (FVector2D::UnitVector / 2.0)) * ImagePlaneSize;
		};

		const double AzimuthDeg = (-0.5 + static_cast<double>(HorizontalBeam) / (HorizontalBeams - 1)) * HorizontalFOV;
		const double ElevationDeg = (-0.5 + static_cast<double>(VerticalBeam) / (VerticalBeams - 1)) * VerticalFOV;
		const FVector2D ImagePlaneLocation = SphericalToPerspective(AzimuthDeg, ElevationDeg);
		const FVector2D PixelCoordinate = ImagePlaneLocationToPixelCoordinate(ImagePlaneLocation);
		const FIntPoint Coord(FMath::RoundToInt32(PixelCoordinate.X), FMath::RoundToInt32(PixelCoordinate.Y));
		const float NearestDepth = Image[Coord.X + ImageSize.X * Coord.Y].Depth(MinDepth, MaxDepth, GTempo_Max_Discrete_Depth);
		const FVector2D ImagePlaneLocationAboveLeft = PixelCoordinateToImagePlaneLocation(FVector2D(Coord.X, Coord.Y));
		double AzimuthDegNearest, ElevationDegNearest;
		PerspectiveToSpherical(ImagePlaneLocationAboveLeft, AzimuthDegNearest, ElevationDegNearest);
		const float NearestDistance =  DepthToDistance(AzimuthDegNearest, ElevationDegNearest, NearestDepth);
		const FVector NearestPoint = SphericalToCartesian(AzimuthDegNearest, ElevationDegNearest, NearestDistance);
		const FVector WorldNormal = Image[Coord.X + ImageSize.X * Coord.Y].Normal();
		const FVector LocalNormal = CaptureTransform.InverseTransformVector(WorldNormal);
		const FVector RayDirection = SphericalToCartesian(AzimuthDeg, ElevationDeg, MaxDistance);
		const double CosAngleOfIncidence = FVector::DotProduct(LocalNormal.GetSafeNormal(), -RayDirection.GetSafeNormal());
		const double AngleOfIncidence = FMath::RadiansToDegrees(FMath::Acos(CosAngleOfIncidence));
		double Intensity;
		double Distance;
		if (AngleOfIncidence > MaxAngleOfIncidence)
		{
			Distance = 0.0;
			Intensity = 0.0;
		}
		else
		{
			const FPlane SurfacePlane( NearestPoint, LocalNormal );
			const FVector HitPoint = FMath::LinePlaneIntersection(FVector::ZeroVector, RayDirection, SurfacePlane);

			Distance = HitPoint.Length();
			Intensity = CosAngleOfIncidence * IntensitySaturationDistance / FMath::Max(IntensitySaturationDistance, Distance);

			if (Distance > MaxDistance)
			{
				Distance = 0.0;
				Intensity = 0.0;
			}
			Distance = FMath::Max(MinDistance, Distance);
		}
		TempoLidar::LidarReturn LidarReturn;
		LidarReturn.set_distance(QuantityConverter<CM2M>::Convert(Distance));
		LidarReturn.set_intensity(Intensity);
		LidarReturn.set_label(Image[Coord.X + ImageSize.X * Coord.Y].Label());
		return LidarReturn;
	};

	// Temporary LidarReturns allows use of ParallelFor
	std::vector<TempoLidar::LidarReturn> LidarReturns(HorizontalBeams * VerticalBeams);
	ParallelFor(VerticalBeams, [this, &LidarReturnFromBeam, &LidarReturns](int32 VerticalBeam)
	{
		for (int32 HorizontalBeam = 0; HorizontalBeam < HorizontalBeams; ++HorizontalBeam)
		{
			LidarReturns[HorizontalBeam + HorizontalBeams * VerticalBeam] = LidarReturnFromBeam(HorizontalBeam, VerticalBeam);
		}
	});

	ScanSegmentOut.mutable_returns()->Reserve(HorizontalBeams * VerticalBeams);
	for (int32 HorizontalBeam = 0; HorizontalBeam < HorizontalBeams; ++HorizontalBeam)
	{
		for (int32 VerticalBeam = 0; VerticalBeam < VerticalBeams; ++VerticalBeam)
		{
			*ScanSegmentOut.add_returns() = LidarReturns[HorizontalBeam + HorizontalBeams * VerticalBeam];
		}
	}

	ScanSegmentOut.mutable_header()->set_sequence_id(SequenceId);
	ScanSegmentOut.mutable_header()->set_capture_time(CaptureTime);
	ScanSegmentOut.mutable_header()->set_transmission_time(TransmissionTime);
	ScanSegmentOut.mutable_header()->set_sensor_name(TCHAR_TO_UTF8(*FString::Printf(TEXT("%s/%s"), *OwnerName, *SensorName)));
	const FVector SensorLocation = QuantityConverter<CM2M, L2R>::Convert(CaptureTransform.GetLocation());
	const FRotator SensorRotation = QuantityConverter<Deg2Rad, L2R>::Convert(CaptureTransform.Rotator());
	ScanSegmentOut.mutable_header()->mutable_sensor_transform()->mutable_location()->set_x(SensorLocation.X);
	ScanSegmentOut.mutable_header()->mutable_sensor_transform()->mutable_location()->set_y(SensorLocation.Y);
	ScanSegmentOut.mutable_header()->mutable_sensor_transform()->mutable_location()->set_z(SensorLocation.Z);
	ScanSegmentOut.mutable_header()->mutable_sensor_transform()->mutable_rotation()->set_p(SensorRotation.Pitch);
	ScanSegmentOut.mutable_header()->mutable_sensor_transform()->mutable_rotation()->set_r(SensorRotation.Roll);
	ScanSegmentOut.mutable_header()->mutable_sensor_transform()->mutable_rotation()->set_y(SensorRotation.Yaw);

	ScanSegmentOut.set_scan_count(NumCaptureComponents);
	ScanSegmentOut.set_horizontal_beams(HorizontalBeams);
	ScanSegmentOut.set_vertical_beams(VerticalBeams);
	ScanSegmentOut.mutable_azimuth_range()->set_max(FMath::DegreesToRadians(HorizontalFOV / 2.0 + RelativeYaw));
	ScanSegmentOut.mutable_azimuth_range()->set_min(FMath::DegreesToRadians(-HorizontalFOV / 2.0 + RelativeYaw));
	ScanSegmentOut.mutable_elevation_range()->set_max(FMath::DegreesToRadians(VerticalFOV / 2.0));
	ScanSegmentOut.mutable_elevation_range()->set_min(FMath::DegreesToRadians(-VerticalFOV / 2.0));
}

TFuture<void> UTempoLidar::DecodeAndRespond(TArray<TUniquePtr<FTextureRead>> TextureReads)
{
	const double TransmissionTime = GetWorld()->GetTimeSeconds();

	NumResponded += TextureReads.Num();

	TFuture<void> Future = Async(EAsyncExecution::TaskGraph, [
		this,
		TextureReads = MoveTemp(TextureReads),
		Requests = PendingRequests,
		TransmissionTimeCpy = TransmissionTime
		]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarDecodeAndRespond);

		TArray<TempoLidar::LidarScanSegment> Segments;
		Segments.SetNum(TextureReads.Num());
		if (!Requests.IsEmpty())
		{
			ParallelFor(TextureReads.Num(), [&TextureReads, &Segments, TransmissionTimeCpy](int Index)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarDecode);
				static_cast<TTextureRead<FLidarPixel>*>(TextureReads[Index].Get())->Decode(TransmissionTimeCpy, Segments[Index]);
			});
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(TempoLidarRespond);
		for (const TempoLidar::LidarScanSegment& Segment : Segments)
		{
			for (const auto& Request : Requests)
			{
				Request.ResponseContinuation.ExecuteIfBound(Segment, grpc::Status_OK);
			}
		}
	});

	return Future;
}

TMap<FName, UTempoLidarCaptureComponent*> UTempoLidar::GetAllCaptureComponents() const
{
	TMap<FName, UTempoLidarCaptureComponent*> CaptureComponents;
	TArray<USceneComponent*> ChildrenComponents;
	GetChildrenComponents(false, ChildrenComponents);
	for (USceneComponent* ChildComponent : ChildrenComponents)
	{
		for (const FName& Tag : { LeftCaptureComponentName, CenterCaptureComponentName, RightCaptureComponentName })
		{
			if (UTempoLidarCaptureComponent* LidarCaptureComponent = Cast<UTempoLidarCaptureComponent>(ChildComponent); ChildComponent->ComponentHasTag(Tag))
			{
				CaptureComponents.Add(Tag, LidarCaptureComponent);
			}
		}
	}
	return CaptureComponents;
}

TMap<FName, UTempoLidarCaptureComponent*> UTempoLidar::GetOrCreateCaptureComponents()
{
	TMap<FName, UTempoLidarCaptureComponent*> CaptureComponents = GetAllCaptureComponents();
	for (const FName& Tag : { LeftCaptureComponentName, CenterCaptureComponentName, RightCaptureComponentName })
	{
		if (!CaptureComponents.Contains(Tag))
		{
			UTempoLidarCaptureComponent* CaptureComponent = NewObject<UTempoLidarCaptureComponent>(GetOwner(), UTempoLidarCaptureComponent::StaticClass(), Tag);
			CaptureComponent->LidarOwner = this;
			CaptureComponent->ComponentTags.AddUnique(Tag);
			CaptureComponent->OnComponentCreated();
			CaptureComponent->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			CaptureComponent->RegisterComponent();
			GetOwner()->AddInstanceComponent(CaptureComponent);
			CaptureComponents.Add(Tag, CaptureComponent);
		}
	}
	return CaptureComponents;
}

TArray<UTempoLidarCaptureComponent*> UTempoLidar::GetActiveCaptureComponents() const
{
	TArray<UTempoLidarCaptureComponent*> ActiveCaptureComponents;
	TMap<FName, UTempoLidarCaptureComponent*> CaptureComponents = GetAllCaptureComponents();
	for (const auto& Elem : CaptureComponents)
	{
		if (Elem.Value->IsActive())
		{
			ActiveCaptureComponents.Add(Elem.Value);
		}
	}
	return ActiveCaptureComponents;
}

void UTempoLidar::RequestMeasurement(const TempoLidar::LidarScanRequest& Request, const TResponseDelegate<TempoLidar::LidarScanSegment>& ResponseContinuation)
{
	PendingRequests.Add({ Request, ResponseContinuation});
}

int32 UTempoLidarCaptureComponent::GetMaxTextureQueueSize() const
{
	return GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize();
}

void UTempoLidarCaptureComponent::Configure(double YawOffset, double SubHorizontalFOV, double SubHorizontalBeams)
{
	RateHz = LidarOwner->RateHz;
	FOVAngle = SubHorizontalFOV;
	HorizontalBeams = SubHorizontalBeams;
	SetRelativeRotation(FRotator(0.0, YawOffset, 0.0));

	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);

	const double UndistortedVerticalImagePlaneSize = 2.0 * FMath::Tan(LidarOwner->VerticalFOV / 2.0);
	const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(FOVAngle / 2.0, LidarOwner->VerticalFOV / 2.0);

	const double AspectRatio = ImagePlaneSize.Y / ImagePlaneSize.X;
	SizeXYFOV = TempoSensorsSettings->GetLidarUpsamplingFactor() * FVector2D(HorizontalBeams, AspectRatio * HorizontalBeams);

	SizeXY = FIntPoint(FMath::CeilToInt32(SizeXYFOV.X), FMath::CeilToInt32(SizeXYFOV.Y));
	DistortionFactor = UndistortedVerticalImagePlaneSize / ImagePlaneSize.Y;
	DistortedVerticalFOV = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(LidarOwner->VerticalFOV) / 2.0) * DistortionFactor));
}
