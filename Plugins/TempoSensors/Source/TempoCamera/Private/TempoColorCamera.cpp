// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoColorCamera.h"

#include "TempoCoreUtils.h"

#include "Engine/TextureRenderTarget2D.h"

int32 UTempoColorCameraBase::QualityFromCompressionLevel(TempoCamera::ImageCompressionLevel CompressionLevel)
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

UTempoColorCameraBase::UTempoColorCameraBase()
{
	MeasurementTypes = { EMeasurementType::COLOR_IMAGE, EMeasurementType::LABEL_IMAGE};
	RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
}

void UTempoColorCameraBase::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	Super::UpdateSceneCaptureContents(Scene);

	ITempoMeasurementRequestingInterface* RequestingSubsystem = Cast<ITempoMeasurementRequestingInterface>(
		UTempoCoreUtils::GetSubsystemImplementingInterface(this, UTempoMeasurementRequestingInterface::StaticClass()));
	if (RequestingSubsystem && RequestingSubsystem->HasPendingRequestForSensor(SensorId))
	{
		RequestingSubsystem->RequestedMeasurementReady_Color(SensorId, SequenceId, TextureTarget->GameThread_GetRenderTargetResource());
	}
}
