// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDepthCamera.h"

#include "TempoCamera.h"

#include "TempoSensorsSettings.h"

#include "Engine/TextureRenderTarget2D.h"

UTempoDepthCamera::UTempoDepthCamera()
{
	MeasurementTypes = { EMeasurementType::DEPTH_IMAGE };
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_R32f;
	CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
}

void UTempoDepthCamera::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	Super::UpdateSceneCaptureContents(Scene);

	if (TextureReadQueue.GetNumPendingTextureReads() > GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize())
	{
		UE_LOG(LogTempoCamera, Warning, TEXT("Fell behind while rendering images from camera %s owner %s. Dropping image."), *GetSensorName(), *GetOwnerName());
		return;
	}
	
	TextureReadQueue.EnqueuePendingTextureRead(EnqueueTextureRead<FLinearColor>());
}

void UTempoDepthCamera::RequestMeasurement(const TempoCamera::DepthImageRequest& Request, const TResponseDelegate<TempoCamera::DepthImage>& ResponseContinuation)
{
	PendingDepthImageRequests.Add({ Request, ResponseContinuation });
}

void UTempoDepthCamera::FlushMeasurementResponses()
{
	check(GetWorld());
	
	if (const TUniquePtr<TTextureRead<FLinearColor>> TextureRead = TextureReadQueue.DequeuePendingTextureRead())
	{
		if (!PendingDepthImageRequests.IsEmpty())
		{
			TempoCamera::DepthImage Image;	
			Image.set_width(TextureRead->ImageSize.X);
			Image.set_height(TextureRead->ImageSize.Y);
			Image.mutable_depths()->Reserve(TextureRead->Image.Num());
			for (const FLinearColor& Pixel : TextureRead->Image)
			{
				Image.add_depths(Pixel.R);
			}
			Image.mutable_header()->set_sequence_id(TextureRead->SequenceId);
			Image.mutable_header()->set_capture_time(TextureRead->CaptureTime);
			Image.mutable_header()->set_transmission_time(GetWorld()->GetTimeSeconds());

			for (auto DepthImageRequestIt = PendingDepthImageRequests.CreateIterator(); DepthImageRequestIt; ++DepthImageRequestIt)
			{
				DepthImageRequestIt->ResponseContinuation.ExecuteIfBound(Image, grpc::Status_OK);
				DepthImageRequestIt.RemoveCurrent();
			}
		}
	}
}
