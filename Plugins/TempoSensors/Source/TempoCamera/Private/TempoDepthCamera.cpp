// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDepthCamera.h"

#include "TempoCoreUtils.h"

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

	ITempoMeasurementRequestingInterface* RequestingSubsystem = Cast<ITempoMeasurementRequestingInterface>(
		UTempoCoreUtils::GetSubsystemImplementingInterface(this, UTempoMeasurementRequestingInterface::StaticClass()));
	if (RequestingSubsystem && RequestingSubsystem->HasPendingRequestForSensor(SensorId))
	{
		RequestingSubsystem->RequestedMeasurementReady_LinearColor(SensorId, SequenceId, TextureTarget->GameThread_GetRenderTargetResource());
	}
}
