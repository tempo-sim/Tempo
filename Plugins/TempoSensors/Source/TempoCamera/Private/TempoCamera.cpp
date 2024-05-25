// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCamera.h"

#include "TempoCameraModule.h"

#include "TempoSensorsSettings.h"

#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"

namespace
{
	// This is the largest float less than the largest uint32 (2^32 - 1).
	// We use it to discretize the depth buffer into a uint32 pixel.
	constexpr float kMaxDiscreteDepth = 4294967040.0;
}

int32 QualityFromCompressionLevel(TempoCamera::ImageCompressionLevel CompressionLevel)
{
	// See FJpegImageWrapper::Compress() for an explanation of these levels
	switch (CompressionLevel)
	{
	case TempoCamera::MIN:
		{
			return 100;
		}
	case TempoCamera::LOW:
		{
			return 85;
		}
	case TempoCamera::MID:
		{
			return 70;
		}
	case TempoCamera::HIGH:
		{
			return 55;
		}
	case TempoCamera::MAX:
		{
			return 40;
		}
	default:
		{
			checkf(false, TEXT("Unhandled compression level"));
			return 0;
		}
	}
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
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	PixelFormatOverride = EPixelFormat::PF_Unknown;
}

void UTempoCamera::BeginPlay()
{
	Super::BeginPlay();
	
	if (const TObjectPtr<UMaterialInterface> PostProcessMaterialWithDepth = GetDefault<UTempoSensorsSettings>()->GetCameraPostProcessMaterialWithDepth())
	{
		DepthPostProcessMaterialInstance = UMaterialInstanceDynamic::Create(PostProcessMaterialWithDepth.Get(), this);
		MinDepth = GEngine->NearClipPlane;
		DepthPostProcessMaterialInstance->SetScalarParameterValue(TEXT("MinDepth"), MinDepth);
		DepthPostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDepth"), MaxDepth);
		DepthPostProcessMaterialInstance->SetScalarParameterValue(TEXT("MaxDiscreteDepth"), kMaxDiscreteDepth);
	}
	else
	{
		UE_LOG(LogTempoCamera, Error, TEXT("PostProcessMaterialWithDepth is not set in TempoSensors settings"));
	}

	ApplyDepthEnabled();
}

void UTempoCamera::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	Super::UpdateSceneCaptureContents(Scene);

	check(GetWorld());

	if (TextureReadQueueWithDepth.GetNumPendingTextureReads() > GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize())
	{
		UE_LOG(LogTempoCamera, Warning, TEXT("Fell behind while rendering images from camera %s owner %s. Dropping image."), *GetSensorName(), *GetOwnerName());
		return;
	}

	if (bDepthEnabled)
	{
		TextureReadQueueWithDepth.EnqueuePendingTextureRead(EnqueueTextureRead<FCameraPixelWithDepth>());
	}
	else
	{
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
	PostProcessSettings.WeightedBlendables.Array.Empty();
	if (bDepthEnabled)
	{
		RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
		PixelFormatOverride = EPixelFormat::PF_A16B16G16R16;
		if (DepthPostProcessMaterialInstance)
		{
			PostProcessSettings.WeightedBlendables.Array.Init(FWeightedBlendable(1.0, DepthPostProcessMaterialInstance), 1);
		}
		else
		{
			UE_LOG(LogTempoCamera, Error, TEXT("DepthPostProcessMaterialInstance is not set. Disabling depth."));
			bDepthEnabled = false;
			ApplyDepthEnabled();
		}
	}
	else
	{
		RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		PixelFormatOverride = EPixelFormat::PF_R8G8B8A8;
		if (const TObjectPtr<UMaterialInterface> PostProcessMaterialNoDepth = GetDefault<UTempoSensorsSettings>()->GetCameraPostProcessMaterialNoDepth())
		{
			PostProcessSettings.WeightedBlendables.Array.Init(FWeightedBlendable(1.0, PostProcessMaterialNoDepth.Get()), 1);
		}
	}

	InitRenderTarget();
}

template <typename PixelType>
void UTempoCamera::DecodeAndRespond(const TTextureRead<PixelType>* TextureRead)
{
	check(GetWorld());

	if (!PendingColorImageRequests.IsEmpty())
	{
		// Cache to deduplicate compression for requests with the same quality.
		TMap<int32, TOptional<TempoCamera::ColorImage>> ColorImageCache;

		for (auto ColorImageRequestIt = PendingColorImageRequests.CreateIterator(); ColorImageRequestIt; ++ColorImageRequestIt)
		{
			const int32 Quality = QualityFromCompressionLevel(ColorImageRequestIt->Request.compression_level());
			TOptional<TempoCamera::ColorImage>& ColorImage = ColorImageCache.FindOrAdd(Quality);
			if (!ColorImage.IsSet())
			{
				ColorImage = TempoCamera::ColorImage();
				ColorImage->set_width(TextureRead->ImageSize.X);
				ColorImage->set_height(TextureRead->ImageSize.Y);
				TArray64<uint8> ImageData;
				TArray<FColor> ColorData;
				ColorData.Reserve(TextureRead->ImageSize.X* TextureRead->ImageSize.Y);
				for (const PixelType& Pixel : TextureRead->Image)
				{
					ColorData.Add(Pixel.Color());
				}
				FImageUtils::CompressImage(ImageData, TEXT("jpg"),
					FImageView(ColorData.GetData(), TextureRead->ImageSize.X, TextureRead->ImageSize.Y), Quality);
				ColorImage->set_data(ImageData.GetData(), ImageData.Num());
				ColorImage->mutable_header()->set_sequence_id(TextureRead->SequenceId);
				ColorImage->mutable_header()->set_capture_time(TextureRead->CaptureTime);
				ColorImage->mutable_header()->set_transmission_time(GetWorld()->GetTimeSeconds());
			}
			ColorImageRequestIt->ResponseContinuation.ExecuteIfBound(ColorImage.GetValue(), grpc::Status_OK);
			ColorImageRequestIt.RemoveCurrent();
		}
	}
	
	if (!PendingLabelImageRequests.IsEmpty())
	{
		TempoCamera::LabelImage LabelImage;	
		LabelImage.set_width(TextureRead->ImageSize.X);
		LabelImage.set_height(TextureRead->ImageSize.Y);
		TArray<uint8> ImageData;
		ImageData.Reserve(TextureRead->Image.Num());
		for (const PixelType& Pixel : TextureRead->Image)
		{
			ImageData.Add(Pixel.Label());
		}
		LabelImage.set_data(ImageData.GetData(), ImageData.Num());
		LabelImage.mutable_header()->set_sequence_id(TextureRead->SequenceId);
		LabelImage.mutable_header()->set_capture_time(TextureRead->CaptureTime);
		LabelImage.mutable_header()->set_transmission_time(GetWorld()->GetTimeSeconds());
		
		for (auto LabelImageRequestIt = PendingLabelImageRequests.CreateIterator(); LabelImageRequestIt; ++LabelImageRequestIt)
		{
			LabelImageRequestIt->ResponseContinuation.ExecuteIfBound(LabelImage, grpc::Status_OK);
			LabelImageRequestIt.RemoveCurrent();
		}
	}

	if (!PendingDepthImageRequests.IsEmpty() && PixelType::bSupportsDepth)
	{
		TempoCamera::DepthImage DepthImage;	
		DepthImage.set_width(TextureRead->ImageSize.X);
		DepthImage.set_height(TextureRead->ImageSize.Y);
		DepthImage.mutable_depths()->Reserve(TextureRead->ImageSize.X * TextureRead->ImageSize.Y);
		for (const PixelType& Pixel : TextureRead->Image)
		{
			DepthImage.add_depths(Pixel.Depth(MinDepth, MaxDepth, kMaxDiscreteDepth));
		}
		DepthImage.mutable_header()->set_sequence_id(TextureRead->SequenceId);
		DepthImage.mutable_header()->set_capture_time(TextureRead->CaptureTime);
		DepthImage.mutable_header()->set_transmission_time(GetWorld()->GetTimeSeconds());
		
		for (auto DepthImageRequestIt = PendingDepthImageRequests.CreateIterator(); DepthImageRequestIt; ++DepthImageRequestIt)
		{
			DepthImageRequestIt->ResponseContinuation.ExecuteIfBound(DepthImage, grpc::Status_OK);
			DepthImageRequestIt.RemoveCurrent();
		}
	}
}

void UTempoCamera::FlushMeasurementResponses()
{
	if (const TUniquePtr<TTextureRead<FCameraPixelNoDepth>> TextureRead = TextureReadQueueNoDepth.DequeuePendingTextureRead())
	{
		DecodeAndRespond(TextureRead.Get());
	}
	
	if (const TUniquePtr<TTextureRead<FCameraPixelWithDepth>> TextureRead = TextureReadQueueWithDepth.DequeuePendingTextureRead())
	{
		if (PendingDepthImageRequests.IsEmpty())
		{
			// If no client is requesting depth, stop rendering it.
			SetDepthEnabled(false);
		}
		DecodeAndRespond(TextureRead.Get());
	}
}
