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

	// Don't activate capture components during cooking - materials and rendering aren't available
	if (IsRunningCommandlet())
	{
		return;
	}

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
		MinDepth = GEngine->NearClipPlane;
		MaxDepth = TempoSensorsSettings->GetMaxLidarDepth();
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MinDepth"), MinDepth);
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDepth"), MaxDepth);
		PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDiscreteDepth"), GTempo_Max_Discrete_Depth);
		ApplyDistortionMapToMaterial(PostProcessMaterialInstance);
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
		LidarOwner->VerticalFOV, HorizontalBeams, LidarOwner->VerticalBeams,
		LidarOwner->IntensitySaturationDistance, LidarOwner->MaxAngleOfIncidence,
		LidarOwner->GetActiveCaptureComponents().Num(), GetRelativeRotation().Yaw, MinDepth, MaxDepth, LidarOwner->MinDistance, LidarOwner->MaxDistance);
}

void TTextureRead<FLidarPixel>::Decode(float TransmissionTime, TempoLidar::LidarScanSegment& ScanSegmentOut) const
{
#if !UE_BUILD_SHIPPING
	{
		static bool bLogged = false;
		if (!bLogged)
		{
			UE_LOG(LogTemp, Warning, TEXT("Lidar Decode: ImageSize=(%d,%d), HBeams=%d, VBeams=%d"),
				ImageSize.X, ImageSize.Y, HorizontalBeams, VerticalBeams);
			for (int32 v = 0; v < FMath::Min(VerticalBeams, 10); ++v)
			{
				const int32 PixelY = FMath::RoundToInt32(static_cast<double>(v) / (VerticalBeams - 1) * (ImageSize.Y - 1));
				const int32 NextPixelY = (v + 1 < VerticalBeams) ? FMath::RoundToInt32(static_cast<double>(v + 1) / (VerticalBeams - 1) * (ImageSize.Y - 1)) : -1;
				UE_LOG(LogTemp, Warning, TEXT("  VBeam %d -> PixelY %d (stride %d)"), v, PixelY, NextPixelY - PixelY);
			}
			for (int32 h = 0; h < FMath::Min(HorizontalBeams, 10); ++h)
			{
				const int32 PixelX = FMath::RoundToInt32(static_cast<double>(h) / (HorizontalBeams - 1) * (ImageSize.X - 1));
				const int32 NextPixelX = (h + 1 < HorizontalBeams) ? FMath::RoundToInt32(static_cast<double>(h + 1) / (HorizontalBeams - 1) * (ImageSize.X - 1)) : -1;
				UE_LOG(LogTemp, Warning, TEXT("  HBeam %d -> PixelX %d (stride %d)"), h, PixelX, NextPixelX - PixelX);
			}
			bLogged = true;
		}
	}
#endif

	// After the equidistant distortion map is applied by the GPU, the output image has pixels
	// at uniform angular intervals. Each beam maps directly to a pixel position.
	auto LidarReturnFromBeam = [this] (int32 HorizontalBeam, int32 VerticalBeam) -> TempoLidar::LidarReturn
	{
		// Beam angles
		const double AzimuthDeg = (-0.5 + static_cast<double>(HorizontalBeam) / (HorizontalBeams - 1)) * HorizontalFOV;
		const double ElevationDeg = (-0.5 + static_cast<double>(VerticalBeam) / (VerticalBeams - 1)) * VerticalFOV;

		// Direct pixel mapping in equidistant output: pixel position is linear in angle
		const int32 PixelX = FMath::RoundToInt32(static_cast<double>(HorizontalBeam) / (HorizontalBeams - 1) * (ImageSize.X - 1));
		const int32 PixelY = FMath::RoundToInt32(static_cast<double>(VerticalBeam) / (VerticalBeams - 1) * (ImageSize.Y - 1));
		const int32 PixelIndex = FMath::Clamp(PixelX, 0, ImageSize.X - 1) + ImageSize.X * FMath::Clamp(PixelY, 0, ImageSize.Y - 1);

		const float NearestDepth = Image[PixelIndex].Depth(MinDepth, MaxDepth, GTempo_Max_Discrete_Depth);
		const float NearestDistance = DepthToDistance(AzimuthDeg, ElevationDeg, NearestDepth);
		const FVector NearestPoint = SphericalToCartesian(AzimuthDeg, ElevationDeg, NearestDistance);
		const FVector WorldNormal = Image[PixelIndex].Normal();
		const FVector LocalNormal = CaptureTransform.InverseTransformVector(WorldNormal);
		const FVector RayDirection = SphericalToCartesian(AzimuthDeg, ElevationDeg, MaxDistance);
		const double CosAngleOfIncidence = FVector::DotProduct(LocalNormal.GetSafeNormal(), -RayDirection.GetSafeNormal());
		const double AngleOfIncidence = FMath::RadiansToDegrees(FMath::Acos(CosAngleOfIncidence));
		double Intensity;
		double Distance;

#if !UE_BUILD_SHIPPING
		{
			static bool bLoggedBeams = false;
			const int32 MidV = VerticalBeams * 3 / 4;
			if (!bLoggedBeams && VerticalBeam == MidV && (HorizontalBeam == 0 || HorizontalBeam == HorizontalBeams / 4 || HorizontalBeam == HorizontalBeams / 2 || HorizontalBeam == HorizontalBeams * 3 / 4 || HorizontalBeam == HorizontalBeams - 1))
			{
				UE_LOG(LogTemp, Warning, TEXT("Beam(%d,%d) Az=%.2f El=%.2f Pixel=(%d,%d) Depth=%.2f Dist=%.2f Pos=(%.2f,%.2f,%.2f) Normal=(%.3f,%.3f,%.3f) AoI=%.1f"),
					HorizontalBeam, VerticalBeam, AzimuthDeg, ElevationDeg, PixelX, PixelY,
					NearestDepth, NearestDistance,
					NearestPoint.X, NearestPoint.Y, NearestPoint.Z,
					LocalNormal.X, LocalNormal.Y, LocalNormal.Z, AngleOfIncidence);
				if (HorizontalBeam == HorizontalBeams - 1) bLoggedBeams = true;
			}
		}
#endif

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
		LidarReturn.set_label(Image[PixelIndex].Label());
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

	ExtractMeasurementHeader(TransmissionTime, ScanSegmentOut.mutable_header());

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

	// Size the render target for equidistant output with uniform angular resolution.
	// The perspective image plane size determines the aspect ratio needed to cover the full FOV.
	const FVector2D ImagePlaneSize = 2.0 * SphericalToPerspective(FOVAngle / 2.0, LidarOwner->VerticalFOV / 2.0);
	const double AspectRatio = ImagePlaneSize.Y / ImagePlaneSize.X;
	const float UpsamplingFactor = TempoSensorsSettings->GetLidarUpsamplingFactor();
	SizeXY = FIntPoint(
		FMath::CeilToInt32(UpsamplingFactor * HorizontalBeams),
		FMath::Max(FMath::CeilToInt32(AspectRatio * UpsamplingFactor * HorizontalBeams), FMath::CeilToInt32(UpsamplingFactor * LidarOwner->VerticalBeams))
	);

	InitDistortionMap();
}

void UTempoLidarCaptureComponent::InitDistortionMap()
{
	if (SizeXY.X <= 0 || SizeXY.Y <= 0 || FOVAngle <= 0.0 || !LidarOwner)
	{
		return;
	}

	CreateOrResizeDistortionMapTexture();

	const double HalfHFOVRad = FMath::DegreesToRadians(FOVAngle / 2.0);
	const double HalfVFOVRad = FMath::DegreesToRadians(LidarOwner->VerticalFOV / 2.0);

	// Equidistant focal lengths: pixels per radian (pixel position linear in angle)
	const double FxDest = (SizeXY.X / 2.0) / HalfHFOVRad;
	const double FyDest = (SizeXY.Y / 2.0) / HalfVFOVRad;

	// Perspective focal lengths: pixels per tan(angle) (must match the perspective render)
	// For square pixels, FxSource == FySource. UE perspective renders use horizontal FOV.
	const double FxSource = (SizeXY.X / 2.0) / FMath::Tan(HalfHFOVRad);
	const double FySource = FxSource;

	const FEquidistantDistortion Model;
	FillDistortionMap(Model, FxDest, FyDest, FxSource, FySource);

#if !UE_BUILD_SHIPPING
	UE_LOG(LogTemp, Warning, TEXT("Lidar DistortionMap: SizeXY=(%d,%d), FxDest=%.4f, FyDest=%.4f, FxSource=%.4f, FySource=%.4f"),
		SizeXY.X, SizeXY.Y, FxDest, FyDest, FxSource, FySource);
	UE_LOG(LogTemp, Warning, TEXT("  Pixel 0 H angle: %.6f rad (expected: %.6f rad)"),
		(0.5 - SizeXY.X / 2.0) / FxDest, -HalfHFOVRad);
	UE_LOG(LogTemp, Warning, TEXT("  Pixel %d H angle: %.6f rad (expected: %.6f rad)"),
		SizeXY.X - 1, (SizeXY.X - 0.5 - SizeXY.X / 2.0) / FxDest, HalfHFOVRad);
	UE_LOG(LogTemp, Warning, TEXT("  Pixel 0 V angle: %.6f rad (expected: %.6f rad)"),
		(0.5 - SizeXY.Y / 2.0) / FyDest, -HalfVFOVRad);
	UE_LOG(LogTemp, Warning, TEXT("  Pixel %d V angle: %.6f rad (expected: %.6f rad)"),
		SizeXY.Y - 1, (SizeXY.Y - 0.5 - SizeXY.Y / 2.0) / FyDest, HalfVFOVRad);
#endif

	if (PostProcessMaterialInstance)
	{
		ApplyDistortionMapToMaterial(PostProcessMaterialInstance);
	}
}
