// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoColorCamera.h"

#include "TempoCamera.h"

#include "TempoSensorsSettings.h"

#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"

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

UTempoColorCamera::UTempoColorCamera()
{
	MeasurementTypes = { EMeasurementType::COLOR_IMAGE, EMeasurementType::LABEL_IMAGE};
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	PostProcessSettings.AutoExposureMethod = AEM_Basic;
	PostProcessSettings.AutoExposureSpeedUp = 20.0;
	PostProcessSettings.AutoExposureSpeedDown = 20.0;
	PostProcessSettings.MotionBlurAmount = 0.0;
	if (const TObjectPtr<UMaterialInterface> PostProcessMaterial = GetDefault<UTempoSensorsSettings>()->GetColorAndLabelPostProcessMaterial())
	{
		PostProcessSettings.WeightedBlendables.Array.Init(FWeightedBlendable(1.0, PostProcessMaterial.Get()), 1);
	}
}

void UTempoColorCamera::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	Super::UpdateSceneCaptureContents(Scene);

	check(GetWorld());

	if (TextureReadQueue.GetNumPendingTextureReads() > GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize())
	{
		UE_LOG(LogTempoCamera, Warning, TEXT("Fell behind while rendering images from camera %s (Id: %d). Dropping image."), *GetName(), SensorId);
		return;
	}
	
	TextureReadQueue.EnqueuePendingTextureRead(EnqueueTextureRead<FColor>());
}

void UTempoColorCamera::RequestMeasurement(const TempoCamera::ColorImageRequest& Request, const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuation)
{
	PendingColorImageRequests.Add({ Request, ResponseContinuation});
}

void UTempoColorCamera::RequestMeasurement(const TempoCamera::LabelImageRequest& Request, const TResponseDelegate<TempoCamera::LabelImage>& ResponseContinuation)
{
	PendingLabelImageRequests.Add({ Request, ResponseContinuation});
}

void UTempoColorCamera::FlushMeasurementResponses()
{
	check(GetWorld());
	
	if (const TUniquePtr<TTextureRead<FColor>> TextureRead = TextureReadQueue.DequeuePendingTextureRead())
	{
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
					FImageUtils::CompressImage(ImageData, TEXT("jpg"),
						FImageView(TextureRead->Image.GetData(), TextureRead->ImageSize.X, TextureRead->ImageSize.Y), Quality);
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
			for (const FColor& Pixel : TextureRead->Image)
			{
				ImageData.Add(Pixel.A);
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
	}
}
