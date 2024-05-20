// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCamera/Camera.pb.h"

#include "TempoMeasurementRequestingInterface.h"
#include "TempoSceneCaptureComponent2D.h"

#include "CoreMinimal.h"

#include "TempoDepthCamera.generated.h"

struct FDepthImageRequest : FMeasurementRequest<TempoCamera::DepthImage>
{
	using FMeasurementRequest::FMeasurementRequest;
	void ExtractAndRespond(const TTextureRead<FLinearColor>* TextureRead, double TransmissionTime) const
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
		Image.mutable_header()->set_transmission_time(TransmissionTime);

		ResponseContinuation.ExecuteIfBound(Image, grpc::Status_OK);
	}
};

UCLASS(Blueprintable, BlueprintType, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOCAMERA_API UTempoDepthCamera : public UTempoSceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UTempoDepthCamera();

	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override;
};
