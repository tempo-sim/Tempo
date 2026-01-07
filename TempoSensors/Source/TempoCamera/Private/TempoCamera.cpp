// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCamera.h"

#include "TempoCameraModule.h"

#include "TempoSensorsSettings.h"

#include "TempoLabelTypes.h"

#include "TempoSensorsConstants.h"

#include "TempoActorLabeler.h"

#include "Engine/TextureRenderTarget2D.h"

#include "Math/Box2D.h"

/**
 * Compute 2D bounding boxes from label data.
 * Single-pass algorithm: scan all pixels, maintain min/max coords per instance ID.
 * @param LabelData Array of label values (one per pixel)
 * @param Width Image width in pixels
 * @param Height Image height in pixels
 * @return Map of instance ID to bounding box
 */
static TMap<int32, FBox2D> ComputeBoundingBoxes(const TArray<uint8>& LabelData, uint32 Width, uint32 Height)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ComputeBoundingBoxes);

	TMap<int32, FBox2D> Boxes;
	for (uint32 Y = 0; Y < Height; ++Y)
	{
		for (uint32 X = 0; X < Width; ++X)
		{
			const uint8 InstanceId = LabelData[Y * Width + X];
			if (InstanceId > 0)  // 0 = unlabeled
			{
				Boxes.FindOrAdd(InstanceId) += FUintPoint(X, Y);
			}
		}
	}
	return Boxes;
}

FTempoCameraIntrinsics::FTempoCameraIntrinsics(const FIntPoint& SizeXY, float HorizontalFOV)
	: Fx(SizeXY.X / 2.0 / FMath::Tan(FMath::DegreesToRadians(HorizontalFOV) / 2.0)),
	  Fy(Fx), // Fx == Fy means the sensor's pixels are square, not that the FOV is symmetric.
	  Cx(SizeXY.X / 2.0),
	  Cy(SizeXY.Y / 2.0) {}

template <typename PixelType>
void ExtractPixelData(const PixelType& Pixel, EColorImageEncoding Encoding, char* Dest)
{
	switch (Encoding)
	{
	case EColorImageEncoding::BGR8:
		{
			Dest[0] = Pixel.B();
			Dest[1] = Pixel.G();
			Dest[2] = Pixel.R();
			break;
		}
	case EColorImageEncoding::RGB8:
		{
			Dest[0] = Pixel.R();
			Dest[1] = Pixel.G();
			Dest[2] = Pixel.B();
			break;
		}
	}
}

TempoCamera::ColorEncoding ColorEncodingToProto(EColorImageEncoding Encoding)
{
	switch (Encoding)
	{
		case EColorImageEncoding::BGR8:
		{
			return TempoCamera::ColorEncoding::BGR8;
		}
		case EColorImageEncoding::RGB8:
		default:
		{
			return TempoCamera::ColorEncoding::RGB8;
		}
	}
}

template <typename PixelType>
void RespondToColorRequests(const TTextureRead<PixelType>* TextureRead, const TArray<FColorImageRequest>& Requests, float TransmissionTime)
{
	TempoCamera::ColorImage ColorImage;
	if (!Requests.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeColor);
		ColorImage.set_width(TextureRead->ImageSize.X);
		ColorImage.set_height(TextureRead->ImageSize.Y);
		std::vector<char> ImageData;
		ImageData.resize(TextureRead->Image.Num() * 3);
		const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
		if (!TempoSensorsSettings)
		{
			return;
		}
		const EColorImageEncoding Encoding = TempoSensorsSettings->GetColorImageEncoding();
		ParallelFor(TextureRead->Image.Num(), [&ImageData, &TextureRead, Encoding](int32 Idx){
			ExtractPixelData(TextureRead->Image[Idx], Encoding, &ImageData[Idx * 3]);
		});
		ColorImage.mutable_data()->assign(ImageData.begin(), ImageData.end());
		ColorImage.set_encoding(ColorEncodingToProto(Encoding));
		TextureRead->ExtractMeasurementHeader(TransmissionTime, ColorImage.mutable_header());
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraRespondColor);
	for (auto ColorImageRequestIt = Requests.CreateConstIterator(); ColorImageRequestIt; ++ColorImageRequestIt)
	{
		ColorImageRequestIt->ResponseContinuation.ExecuteIfBound(ColorImage, grpc::Status_OK);
	}
}

template <typename PixelType>
void RespondToLabelRequests(const TTextureRead<PixelType>* TextureRead, const TArray<FLabelImageRequest>& Requests, float TransmissionTime)
{
	TempoCamera::LabelImage LabelImage;
	if (!Requests.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeLabel);
		LabelImage.set_width(TextureRead->ImageSize.X);
		LabelImage.set_height(TextureRead->ImageSize.Y);
		std::vector<char> ImageData;
		ImageData.resize(TextureRead->Image.Num());
		ParallelFor(TextureRead->Image.Num(), [&ImageData, &TextureRead](int32 Idx)
		{
			ImageData[Idx] = TextureRead->Image[Idx].Label();
		});
		LabelImage.mutable_data()->assign(ImageData.begin(), ImageData.end());
		TextureRead->ExtractMeasurementHeader(TransmissionTime, LabelImage.mutable_header());
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraRespondLabel);
	for (auto LabelImageRequestIt = Requests.CreateConstIterator(); LabelImageRequestIt; ++LabelImageRequestIt)
	{
		LabelImageRequestIt->ResponseContinuation.ExecuteIfBound(LabelImage, grpc::Status_OK);
	}
}

template <typename PixelType>
void RespondToBoundingBoxRequests(const TTextureRead<PixelType>* TextureRead, const TArray<FBoundingBoxesRequest>& Requests, float TransmissionTime)
{
	TempoCamera::BoundingBoxes Response;
	if (!Requests.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeBoundingBoxes);

		// Set header and dimensions
		Response.set_width(TextureRead->ImageSize.X);
		Response.set_height(TextureRead->ImageSize.Y);
		TextureRead->ExtractMeasurementHeader(TransmissionTime, Response.mutable_header());

		// Extract label data from pixels
		TArray<uint8> LabelData;
		LabelData.SetNumUninitialized(TextureRead->Image.Num());
		ParallelFor(TextureRead->Image.Num(), [&LabelData, &TextureRead](int32 Idx)
		{
			LabelData[Idx] = TextureRead->Image[Idx].Label();
		});

		// Compute bounding boxes
		TMap<int32, FBox2D> BoundingBoxes = ComputeBoundingBoxes(LabelData, TextureRead->ImageSize.X, TextureRead->ImageSize.Y);

		// Add bounding boxes to proto message using the map captured at render time
		for (const auto& [InstanceId, Box] : BoundingBoxes)
		{
			TempoCamera::BoundingBox2D* BBoxProto = Response.add_bounding_boxes();
			BBoxProto->set_min_x(FMath::RoundToInt32(Box.Min.X));
			BBoxProto->set_min_y(FMath::RoundToInt32(Box.Min.Y));
			BBoxProto->set_max_x(FMath::RoundToInt32(Box.Max.X));
			BBoxProto->set_max_y(FMath::RoundToInt32(Box.Max.Y));
			BBoxProto->set_instance_id(InstanceId);

			// Find semantic ID from mapping captured at render time
			const uint8* SemanticId = TextureRead->InstanceToSemanticMap.Find(InstanceId);
			if (!SemanticId)
			{
				UE_LOG(LogTemp, Warning, TEXT("No semantic ID found for instance ID %d"), InstanceId);
			}
			BBoxProto->set_semantic_id(SemanticId ? *SemanticId : 0);
		}
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraRespondBoundingBoxes);
	for (auto RequestIt = Requests.CreateConstIterator(); RequestIt; ++RequestIt)
	{
		RequestIt->ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
	}
}

void TTextureRead<FCameraPixelNoDepth>::RespondToRequests(const TArray<FColorImageRequest>& Requests, float TransmissionTime) const
{
	RespondToColorRequests(this, Requests, TransmissionTime);
}

void TTextureRead<FCameraPixelNoDepth>::RespondToRequests(const TArray<FLabelImageRequest>& Requests, float TransmissionTime) const
{
	RespondToLabelRequests(this, Requests, TransmissionTime);
}

void TTextureRead<FCameraPixelNoDepth>::RespondToRequests(const TArray<FBoundingBoxesRequest>& Requests, float TransmissionTime) const
{
	RespondToBoundingBoxRequests(this, Requests, TransmissionTime);
}

void TTextureRead<FCameraPixelWithDepth>::RespondToRequests(const TArray<FColorImageRequest>& Requests, float TransmissionTime) const
{
	RespondToColorRequests(this, Requests, TransmissionTime);
}

void TTextureRead<FCameraPixelWithDepth>::RespondToRequests(const TArray<FLabelImageRequest>& Requests, float TransmissionTime) const
{
	RespondToLabelRequests(this, Requests, TransmissionTime);
}

void TTextureRead<FCameraPixelWithDepth>::RespondToRequests(const TArray<FDepthImageRequest>& Requests, float TransmissionTime) const
{
	TempoCamera::DepthImage DepthImage;
	if (!Requests.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeDepth);
		DepthImage.set_width(ImageSize.X);
		DepthImage.set_height(ImageSize.Y);
		DepthImage.mutable_depths()->Resize(ImageSize.X * ImageSize.Y, 0.0);
		ParallelFor(Image.Num(), [&DepthImage, this](int32 Idx)
		{
			DepthImage.set_depths(Idx, Image[Idx].Depth(MinDepth, MaxDepth, GTempo_Max_Discrete_Depth));
		});
		ExtractMeasurementHeader(TransmissionTime, DepthImage.mutable_header());
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraRespondDepth);
	for (auto DepthImageRequestIt = Requests.CreateConstIterator(); DepthImageRequestIt; ++DepthImageRequestIt)
	{
		DepthImageRequestIt->ResponseContinuation.ExecuteIfBound(DepthImage, grpc::Status_OK);
	}
}

void TTextureRead<FCameraPixelWithDepth>::RespondToRequests(const TArray<FBoundingBoxesRequest>& Requests, float TransmissionTime) const
{
	RespondToBoundingBoxRequests(this, Requests, TransmissionTime);
}

UTempoCamera::UTempoCamera()
{
	MeasurementTypes = { EMeasurementType::COLOR_IMAGE, EMeasurementType::LABEL_IMAGE, EMeasurementType::DEPTH_IMAGE, EMeasurementType::BOUNDING_BOXES};
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	PostProcessSettings.AutoExposureMethod = AEM_Basic;
	PostProcessSettings.AutoExposureSpeedUp = 20.0;
	PostProcessSettings.AutoExposureSpeedDown = 20.0;
	// Auto exposure percentages chosen to better match their own recommended settings (see Scene.h).
	PostProcessSettings.AutoExposureLowPercent = 75.0;
	PostProcessSettings.AutoExposureHighPercent = 85.0;
	PostProcessSettings.MotionBlurAmount = 0.0;
	ShowFlags.SetAntiAliasing(true);
	ShowFlags.SetTemporalAA(true);
	ShowFlags.SetMotionBlur(false);

	// Start with no-depth settings
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8; // Corresponds to PF_B8G8R8A8
	PixelFormatOverride = EPixelFormat::PF_Unknown;
}

void UTempoCamera::BeginPlay()
{
	Super::BeginPlay();

	ApplyDepthEnabled();
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UTempoCamera::UpdateSceneCaptureContents(FSceneInterface* Scene)
#else
void UTempoCamera::UpdateSceneCaptureContents(FSceneInterface* Scene, ISceneRenderBuilder& SceneRenderBuilder)
#endif
{
	if (!bDepthEnabled && !PendingDepthImageRequests.IsEmpty())
	{
		// If a client is requesting depth, start rendering it.
		SetDepthEnabled(true);
	}
	
	if (bDepthEnabled && PendingDepthImageRequests.IsEmpty())
	{
		// If no client is requesting depth, stop rendering it.
		SetDepthEnabled(false);
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	Super::UpdateSceneCaptureContents(Scene);
#else
	Super::UpdateSceneCaptureContents(Scene, SceneRenderBuilder);
#endif
}

void UTempoCamera::RequestMeasurement(const TempoCamera::ColorImageRequest& Request, const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuation)
{
	PendingColorImageRequests.Add({ Request, ResponseContinuation});
}

void UTempoCamera::RequestMeasurement(const TempoCamera::LabelImageRequest& Request, const TResponseDelegate<TempoCamera::LabelImage>& ResponseContinuation)
{
	PendingLabelImageRequests.Add({ Request, ResponseContinuation});
}

void UTempoCamera::RequestMeasurement(const TempoCamera::DepthImageRequest& Request, const TResponseDelegate<TempoCamera::DepthImage>& ResponseContinuation)
{
	PendingDepthImageRequests.Add({ Request, ResponseContinuation});
}

void UTempoCamera::RequestMeasurement(const TempoCamera::BoundingBoxesRequest& Request, const TResponseDelegate<TempoCamera::BoundingBoxes>& ResponseContinuation)
{
	PendingBoundingBoxesRequests.Add({ Request, ResponseContinuation});
}

FTempoCameraIntrinsics UTempoCamera::GetIntrinsics() const
{
	return FTempoCameraIntrinsics(SizeXY, FOVAngle);
}

FString UTempoCamera::GetOwnerName() const
{
	check(GetOwner());

	return GetOwner()->GetActorNameOrLabel();
}

FString UTempoCamera::GetSensorName() const
{
	return GetName();
}

bool UTempoCamera::IsAwaitingRender()
{
	return IsNextReadAwaitingRender();
}

void UTempoCamera::OnRenderCompleted()
{
	ReadNextIfAvailable();
}

void UTempoCamera::BlockUntilMeasurementsReady() const
{
	BlockUntilNextReadComplete();
}

TOptional<TFuture<void>> UTempoCamera::SendMeasurements()
{
	TOptional<TFuture<void>> Future;

	if (TUniquePtr<FTextureRead> TextureRead = DequeueIfReadComplete())
	{
		Future = DecodeAndRespond(MoveTemp(TextureRead));
	}

	return Future;
}

bool UTempoCamera::HasPendingRequests() const
{
	return !PendingColorImageRequests.IsEmpty() || !PendingLabelImageRequests.IsEmpty() || !PendingDepthImageRequests.IsEmpty() || !PendingBoundingBoxesRequests.IsEmpty();
}

FTextureRead* UTempoCamera::MakeTextureRead() const
{
	check(GetWorld());

	// Capture instance-to-semantic mapping at render time for bounding box requests
	TMap<uint8, uint8> InstanceToSemanticMap;
	if (UTempoActorLabeler* Labeler = GetWorld()->GetSubsystem<UTempoActorLabeler>())
	{
		InstanceToSemanticMap = Labeler->GetInstanceToSemanticIdMap();
	}

	return bDepthEnabled ?
		static_cast<FTextureRead*>(new TTextureRead<FCameraPixelWithDepth>(SizeXY, SequenceId, GetWorld()->GetTimeSeconds(), GetOwnerName(), GetSensorName(), GetComponentTransform(), MinDepth, MaxDepth, MoveTemp(InstanceToSemanticMap))):
		static_cast<FTextureRead*>(new TTextureRead<FCameraPixelNoDepth>(SizeXY, SequenceId, GetWorld()->GetTimeSeconds(), GetOwnerName(), GetSensorName(), GetComponentTransform(), MoveTemp(InstanceToSemanticMap)));
}

TFuture<void> UTempoCamera::DecodeAndRespond(TUniquePtr<FTextureRead> TextureRead)
{
	const double TransmissionTime = GetWorld()->GetTimeSeconds();

	const bool bSupportsDepth = TextureRead->GetType() == TEXT("WithDepth");

	TFuture<void> Future = Async(EAsyncExecution::TaskGraph, [
		TextureRead = MoveTemp(TextureRead),
		ColorImageRequests = PendingColorImageRequests,
		LabelImageRequests = PendingLabelImageRequests,
		DepthImageRequests = PendingDepthImageRequests,
		BoundingBoxRequests = PendingBoundingBoxesRequests,
		TransmissionTimeCpy = TransmissionTime
		]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoCameraDecodeAndRespond);

		if (TextureRead->GetType() == TEXT("WithDepth"))
		{
			static_cast<TTextureRead<FCameraPixelWithDepth>*>(TextureRead.Get())->RespondToRequests(ColorImageRequests, TransmissionTimeCpy);
			static_cast<TTextureRead<FCameraPixelWithDepth>*>(TextureRead.Get())->RespondToRequests(LabelImageRequests, TransmissionTimeCpy);
			static_cast<TTextureRead<FCameraPixelWithDepth>*>(TextureRead.Get())->RespondToRequests(DepthImageRequests, TransmissionTimeCpy);
			static_cast<TTextureRead<FCameraPixelWithDepth>*>(TextureRead.Get())->RespondToRequests(BoundingBoxRequests, TransmissionTimeCpy);
		}
		else if (TextureRead->GetType() == TEXT("NoDepth"))
		{
			static_cast<TTextureRead<FCameraPixelNoDepth>*>(TextureRead.Get())->RespondToRequests(ColorImageRequests, TransmissionTimeCpy);
			static_cast<TTextureRead<FCameraPixelNoDepth>*>(TextureRead.Get())->RespondToRequests(LabelImageRequests, TransmissionTimeCpy);
			static_cast<TTextureRead<FCameraPixelNoDepth>*>(TextureRead.Get())->RespondToRequests(BoundingBoxRequests, TransmissionTimeCpy);
		}
	});

	PendingColorImageRequests.Empty();
	PendingLabelImageRequests.Empty();
	PendingBoundingBoxesRequests.Empty();
	if (bSupportsDepth)
	{
		PendingDepthImageRequests.Empty();
	}
	
	return Future;
}

int32 UTempoCamera::GetMaxTextureQueueSize() const
{
	return GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize();
}

void UTempoCamera::SetDepthEnabled(bool bDepthEnabledIn)
{
	if (bDepthEnabled == bDepthEnabledIn)
	{
		return;
	}

	UE_LOG(LogTempoCamera, Display, TEXT("Setting owner: %s camera: %s depth enabled: %d"), *GetOwnerName(), *GetSensorName(), bDepthEnabledIn);

	bDepthEnabled = bDepthEnabledIn;
	ApplyDepthEnabled();
}

void UTempoCamera::ApplyDepthEnabled()
{
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	check(TempoSensorsSettings);
	
	if (bDepthEnabled)
	{
		RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
		PixelFormatOverride = EPixelFormat::PF_A16B16G16R16;

		if (const TObjectPtr<UMaterialInterface> PostProcessMaterialWithDepth = GetDefault<UTempoSensorsSettings>()->GetCameraPostProcessMaterialWithDepth())
		{
			PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterialWithDepth.Get(), this);
			MinDepth = GEngine->NearClipPlane;
			MaxDepth = TempoSensorsSettings->GetMaxCameraDepth();
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MinDepth"), MinDepth);
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDepth"), MaxDepth);
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDiscreteDepth"), GTempo_Max_Discrete_Depth);
		}
		else
		{
			UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialWithDepth is not set in TempoSensors settings"));
		}
	}
	else
	{
		if (const TObjectPtr<UMaterialInterface> PostProcessMaterialNoDepth = GetDefault<UTempoSensorsSettings>()->GetCameraPostProcessMaterialNoDepth())
		{
			PostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterialNoDepth.Get(), this);
		}
		else
		{
			UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialWithDepth is not set in TempoSensors settings"));
		}
		
		RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8; // Corresponds to PF_B8G8R8A8
		PixelFormatOverride = EPixelFormat::PF_Unknown;
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
		UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialInstance is not set."));
	}

	InitRenderTarget();
}
