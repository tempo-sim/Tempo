// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCamera.h"

#include "TempoCameraModule.h"

#include "TempoSensorsSettings.h"

#include "Engine/TextureRenderTarget2D.h"
#include "TempoLabelTypes.h"

namespace
{
	// This is the largest float less than the largest uint32 (2^32 - 1).
	// We use it to discretize the depth buffer into a uint32 pixel.
	constexpr float kMaxDiscreteDepth = 4294967040.0;
}

UTempoCamera::UTempoCamera()
{
	MeasurementTypes = { EMeasurementType::COLOR_IMAGE, EMeasurementType::LABEL_IMAGE, EMeasurementType::DEPTH_IMAGE};
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	PostProcessSettings.AutoExposureMethod = AEM_Basic;
	PostProcessSettings.AutoExposureSpeedUp = 20.0;
	PostProcessSettings.AutoExposureSpeedDown = 20.0;
	PostProcessSettings.MotionBlurAmount = 0.0;

	// No depth settings
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8; // Corresponds to PF_B8G8R8A8
	PixelFormatOverride = EPixelFormat::PF_Unknown;
}

void UTempoCamera::BeginPlay()
{
	Super::BeginPlay();

	ApplyDepthEnabled();
}

void UTempoCamera::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	Super::UpdateSceneCaptureContents(Scene);

	check(GetWorld());

	if (bDepthEnabled)
	{
		if (TextureReadQueueWithDepth.GetNumPendingTextureReads() > GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize())
		{
			UE_LOG(LogTempoCamera, Warning, TEXT("Fell behind while rendering images from camera %s owner %s. Dropping image."), *GetSensorName(), *GetOwnerName());
			return;
		}
		TextureReadQueueWithDepth.EnqueuePendingTextureRead(EnqueueTextureRead<FCameraPixelWithDepth>());
	}
	else
	{
		if (TextureReadQueueNoDepth.GetNumPendingTextureReads() > GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize())
		{
			UE_LOG(LogTempoCamera, Warning, TEXT("Fell behind while rendering images from camera %s owner %s. Dropping image."), *GetSensorName(), *GetOwnerName());
			return;
		}
		TextureReadQueueNoDepth.EnqueuePendingTextureRead(EnqueueTextureRead<FCameraPixelNoDepth>());
	}
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

	SetDepthEnabled(true);
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
			PostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDiscreteDepth"), kMaxDiscreteDepth);
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
	}
	else
	{
		UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialInstance is not set."));
	}

	InitRenderTarget();
}

template <typename PixelType>
TFuture<void> UTempoCamera::DecodeAndRespond(TUniquePtr<TTextureRead<PixelType>> TextureRead)
{
	const double TransmissionTime = GetWorld()->GetTimeSeconds();

	TFuture<void> Future = Async(EAsyncExecution::TaskGraph, [
		TextureRead = MoveTemp(TextureRead),
		TransmissionTime, MinDepthCpy = MinDepth, MaxDepthCpy = MaxDepth,
		ColorImageRequests = PendingColorImageRequests,
		LabelImageRequests = PendingLabelImageRequests,
		DepthImageRequests = PendingDepthImageRequests
		]
		{
			if (!ColorImageRequests.IsEmpty())
			{
				TempoCamera::ColorImage ColorImage;
				ColorImage.set_width(TextureRead->ImageSize.X);
				ColorImage.set_height(TextureRead->ImageSize.Y);
				std::vector<char> ImageData;
				ImageData.reserve(TextureRead->Image.Num() * 3);
				for (const PixelType& Pixel : TextureRead->Image)
				{
					ImageData.push_back(Pixel.B());
					ImageData.push_back(Pixel.G());
					ImageData.push_back(Pixel.R());
				}
				ColorImage.mutable_data()->assign(ImageData.begin(), ImageData.end());
				ColorImage.mutable_header()->set_sequence_id(TextureRead->SequenceId);
				ColorImage.mutable_header()->set_capture_time(TextureRead->CaptureTime);
				ColorImage.mutable_header()->set_transmission_time(TransmissionTime);
		
				for (auto ColorImageRequestIt = ColorImageRequests.CreateConstIterator(); ColorImageRequestIt; ++ColorImageRequestIt)
				{
					ColorImageRequestIt->ResponseContinuation.ExecuteIfBound(ColorImage, grpc::Status_OK);
				}
			}
			
			if (!LabelImageRequests.IsEmpty())
			{
				TempoCamera::LabelImage LabelImage;
				LabelImage.set_width(TextureRead->ImageSize.X);
				LabelImage.set_height(TextureRead->ImageSize.Y);
				std::vector<char> ImageData;
				ImageData.reserve(TextureRead->Image.Num());
				for (const PixelType& Pixel : TextureRead->Image)
				{
					ImageData.push_back(Pixel.Label());
				}
				LabelImage.mutable_data()->assign(ImageData.begin(), ImageData.end());
				LabelImage.mutable_header()->set_sequence_id(TextureRead->SequenceId);
				LabelImage.mutable_header()->set_capture_time(TextureRead->CaptureTime);
				LabelImage.mutable_header()->set_transmission_time(TransmissionTime);
		
				for (auto LabelImageRequestIt = LabelImageRequests.CreateConstIterator(); LabelImageRequestIt; ++LabelImageRequestIt)
				{
					LabelImageRequestIt->ResponseContinuation.ExecuteIfBound(LabelImage, grpc::Status_OK);
				}
			}
		
			if (!DepthImageRequests.IsEmpty() && PixelType::bSupportsDepth)
			{
				TempoCamera::DepthImage DepthImage;
				DepthImage.set_width(TextureRead->ImageSize.X);
				DepthImage.set_height(TextureRead->ImageSize.Y);
				DepthImage.mutable_depths()->Reserve(TextureRead->ImageSize.X * TextureRead->ImageSize.Y);
				for (const PixelType& Pixel : TextureRead->Image)
				{
					DepthImage.add_depths(Pixel.Depth(MinDepthCpy, MaxDepthCpy, kMaxDiscreteDepth));
				}
				DepthImage.mutable_header()->set_sequence_id(TextureRead->SequenceId);
				DepthImage.mutable_header()->set_capture_time(TextureRead->CaptureTime);
				DepthImage.mutable_header()->set_transmission_time(TransmissionTime);
		
				for (auto DepthImageRequestIt = DepthImageRequests.CreateConstIterator(); DepthImageRequestIt; ++DepthImageRequestIt)
				{
					DepthImageRequestIt->ResponseContinuation.ExecuteIfBound(DepthImage, grpc::Status_OK);
				}
			}
		});

	PendingColorImageRequests.Empty();
	PendingLabelImageRequests.Empty();
	if (PixelType::bSupportsDepth)
	{
		PendingDepthImageRequests.Empty();
	}
	
	return Future;
}

TOptional<TFuture<void>> UTempoCamera::FlushMeasurementResponses()
{
	if (TUniquePtr<TTextureRead<FCameraPixelNoDepth>> TextureReadNoDepth = TextureReadQueueNoDepth.DequeuePendingTextureRead())
	{
		return DecodeAndRespond(MoveTemp(TextureReadNoDepth));
	}

	if (TUniquePtr<TTextureRead<FCameraPixelWithDepth>> TextureReadWithDepth = TextureReadQueueWithDepth.DequeuePendingTextureRead())
	{
		if (PendingDepthImageRequests.IsEmpty())
		{
			// If no client is requesting depth, stop rendering it.
			SetDepthEnabled(false);
		}
		
		return DecodeAndRespond(MoveTemp(TextureReadWithDepth));
	}
	
	return TOptional<TFuture<void>>();
}

void UTempoCamera::FlushPendingRenderingCommands() const
{
	TextureReadQueueNoDepth.FlushPendingTextureReads();
	TextureReadQueueWithDepth.FlushPendingTextureReads();
}
