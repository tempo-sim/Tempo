// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCamera/Camera.pb.h"

#include "TempoMeasurementRequestingInterface.h"
#include "TempoSceneCaptureComponent2D.h"

#include "CoreMinimal.h"
#include "ImageUtils.h"

#include "TempoColorCamera.generated.h"

struct FColorImageRequest : FMeasurementRequest<TempoCamera::ColorImage>
{
	FColorImageRequest(const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuationIn, int32 QualityIn)
		: FMeasurementRequest(ResponseContinuationIn), Quality(QualityIn) {}

	int32 Quality;

	static void CompressImage(TArray64<uint8>& OutData, const TTextureRead<FColor>* TextureRead, int32 Quality);
	
	void ExtractAndRespond(const TTextureRead<FColor>* TextureRead, double TransmissionTime) const
	{
		TArray64<uint8> ImageData;
		FImageUtils::CompressImage(ImageData, TEXT("jpg"),
			FImageView(TextureRead->Image.GetData(), TextureRead->ImageSize.X, TextureRead->ImageSize.Y), Quality);
		TempoCamera::ColorImage Image;	
		Image.set_width(TextureRead->ImageSize.X);
		Image.set_height(TextureRead->ImageSize.Y);
		Image.set_data(ImageData.GetData(), ImageData.Num());
		Image.mutable_header()->set_sequence_id(TextureRead->SequenceId);
		Image.mutable_header()->set_capture_time(TextureRead->CaptureTime);
		Image.mutable_header()->set_transmission_time(TransmissionTime);

		ResponseContinuation.ExecuteIfBound(Image, grpc::Status_OK);
	}
};

struct FLabelImageRequest : FMeasurementRequest<TempoCamera::LabelImage>
{
	using FMeasurementRequest::FMeasurementRequest;
	
	void ExtractAndRespond(const TTextureRead<FColor>* TextureRead, double TransmissionTime) const
	{
		TempoCamera::LabelImage Image;	
		Image.set_width(TextureRead->ImageSize.X);
		Image.set_height(TextureRead->ImageSize.Y);
		TArray<uint8> ImageData;
		ImageData.Reserve(TextureRead->Image.Num());
		for (const FColor& Pixel : TextureRead->Image)
		{
			ImageData.Add(Pixel.A);
		}
		Image.set_data(ImageData.GetData(), ImageData.Num());
		Image.mutable_header()->set_sequence_id(TextureRead->SequenceId);
		Image.mutable_header()->set_capture_time(TextureRead->CaptureTime);
		Image.mutable_header()->set_transmission_time(TransmissionTime);

		ResponseContinuation.ExecuteIfBound(Image, grpc::Status_OK);
	}
};

// TempoColorCamera is abstract and named "base" because it needs to have a
// post process material added in a derived Blueprint.
UCLASS(Abstract, Blueprintable, BlueprintType, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOCAMERA_API UTempoColorCameraBase : public UTempoSceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UTempoColorCameraBase();

	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override;

	static int32 QualityFromCompressionLevel(TempoCamera::ImageCompressionLevel CompressionLevel);
};
