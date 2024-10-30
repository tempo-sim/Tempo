// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoLidar.h"

#include "TempoConversion.h"
#include "TempoLidarModule.h"

#include "TempoLidar/Lidar.pb.h"

#include "TempoSensorsSettings.h"

UTempoLidar::UTempoLidar()
{
	MeasurementTypes = { EMeasurementType::LIDAR_SCAN };
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
	// TPair<UTempoLidarCaptureComponent*, int32> NextCaptureComponent = TPair<UTempoLidarCaptureComponent*, int32>(nullptr, TNumericLimits<int32>::Max());

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
	// for (UTempoLidarCaptureComponent* LidarCaptureComponent : LidarCaptureComponents)
	// {
	// 	if (TOptional<int32> NextSequenceId = LidarCaptureComponent->SequenceIDOfNextCompleteRead())
	// 	{
	// 		if (SequenceID < 0)
	// 		{
	// 			SequenceID = NextSequenceId.GetValue();
	// 		}
	// 		else
	// 		{
	// 			
	// 		}
	// 		if (NextSequenceId.GetValue() < NextCaptureComponent.Value)
	// 		{
	// 			NextCaptureComponent.Key = LidarCaptureComponent;
	// 			NextCaptureComponent.Value = NextSequenceId.GetValue();
	// 		}
	// 	}
	// 	if (TUniquePtr<FTextureRead> TextureRead = LidarCaptureComponent->DequeueIfReadComplete())
	// 	{
	// 		TextureReads.Add(MoveTemp(TextureRead));
	// 	}
	// }

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
	HorizontalSpacing = HorizontalFOV / HorizontalBeams;
	VerticalSpacing = VerticalFOV / VerticalBeams;
}

void UTempoLidarCaptureComponent::BeginPlay()
{
	Super::BeginPlay();

	const float TanHFOVOverTwo = FMath::Tan(FMath::DegreesToRadians(FOVAngle / 2.0));
	const float DistortionFactorContinuous = FMath::Sqrt((TanHFOVOverTwo * TanHFOVOverTwo) + 1);
	const float UndistortedAspectRatio = FMath::Tan(FMath::DegreesToRadians(LidarOwner->VerticalFOV / 2.0)) / FMath::Tan(FMath::DegreesToRadians(FOVAngle / 2.0));
	SizeXY = FIntPoint(LidarOwner->UpsamplingFactor * HorizontalBeams, LidarOwner->UpsamplingFactor * FMath::CeilToInt32(DistortionFactorContinuous * UndistortedAspectRatio * HorizontalBeams));
	DistortionFactor = SizeXY.Y / (UndistortedAspectRatio * LidarOwner->UpsamplingFactor * HorizontalBeams);
	DistortedVerticalFOV = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(LidarOwner->VerticalFOV) / 2.0) * DistortionFactor));
	
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

		const int32 NumRows = Image.Num() / (TempoLidar->UpsamplingFactor * TempoLidar->UpsamplingFactor) / CaptureComponent->HorizontalBeams;
		const float VerticalDegreesPerPixel = CaptureComponent->DistortedVerticalFOV / (NumRows - 1.0 / TempoLidar->UpsamplingFactor);
  
		const float VerticalOffset = (CaptureComponent->DistortedVerticalFOV / 2.0 - TempoLidar->VerticalFOV / 2.0) / VerticalDegreesPerPixel;
		const float VerticalSpacingPixels = (TempoLidar->VerticalFOV / (TempoLidar->VerticalBeams - 1)) / VerticalDegreesPerPixel;
		auto RangeAtBeam = [this, VerticalOffset, VerticalSpacingPixels] (int32 HorizontalBeam, int32 VerticalBeam) -> float
		{
			const int32 Column = TempoLidar->UpsamplingFactor * HorizontalBeam;
			const float Row = TempoLidar->UpsamplingFactor * (VerticalOffset + VerticalBeam * VerticalSpacingPixels);
			// const float RangeAbove = ;
			// const float RangeBelow = ;
			const float Alpha = Row - FMath::FloorToInt32(Row);
			return Alpha > 0.5 ?
				Image[Column + TempoLidar->UpsamplingFactor * CaptureComponent->HorizontalBeams * FMath::CeilToInt32(Row)].GetDepth(TempoLidar->MinRange, TempoLidar->MaxRange) :
				Image[Column + TempoLidar->UpsamplingFactor * CaptureComponent->HorizontalBeams * FMath::FloorToInt32(Row)].GetDepth(TempoLidar->MinRange, TempoLidar->MaxRange);
			// if (FMath::Abs(RangeAbove - RangeBelow) > 10.0)
			// {
			// 	return Alpha > 0.5 ? RangeBelow : RangeAbove;
			// }
			// return RangeAbove * (1.0 - Alpha) + RangeBelow * Alpha;
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
