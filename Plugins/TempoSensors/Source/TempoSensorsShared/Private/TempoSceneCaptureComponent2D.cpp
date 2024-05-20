// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSceneCaptureComponent2D.h"

#include "TempoMeasurementRequestingInterface.h"

#include "TempoCoreSettings.h"
#include "TempoCoreUtils.h"

#include "Engine/TextureRenderTarget2D.h"

UTempoSceneCaptureComponent2D::UTempoSceneCaptureComponent2D()
{
	SensorId = ITempoSensorInterface::AllocateSensorId();
	PrimaryComponentTick.bStartWithTickEnabled = false;
	ShowFlags.SetAntiAliasing(true);
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;
	bTickInEditor = false;
	bAlwaysPersistRenderingState = true;
}

void UTempoSceneCaptureComponent2D::BeginPlay()
{
	Super::BeginPlay();
	
	InitRenderTarget();
	
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &UTempoSceneCaptureComponent2D::MaybeCapture, 1.0 / RateHz, true);
}

void UTempoSceneCaptureComponent2D::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	Super::UpdateSceneCaptureContents(Scene);

	SequenceId++;
}

void UTempoSceneCaptureComponent2D::MaybeCapture()
{
	const ITempoMeasurementRequestingInterface* RequestingSubsystem = Cast<ITempoMeasurementRequestingInterface>(
		UTempoCoreUtils::GetSubsystemImplementingInterface(this, UTempoMeasurementRequestingInterface::StaticClass()));
	if (TextureTarget && RequestingSubsystem && RequestingSubsystem->HasPendingRequestForSensor(SensorId))
	{
		if (GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep)
		{
			// In fixed step mode we block the game thread to render the image immediately.
			// It will then be read and sent before the end of the current frame.
			CaptureScene();
		}
		else
		{
			// Otherwise, we render the frame along with the main render pass.
			// It will get read and sent one or two frames after this one.
			CaptureSceneDeferred();
		}
	}
}

void UTempoSceneCaptureComponent2D::InitRenderTarget()
{
	UTextureRenderTarget2D* RenderTarget2D = NewObject<UTextureRenderTarget2D>(this);
	
	RenderTarget2D->TargetGamma = GEngine->GetDisplayGamma();
	RenderTarget2D->InitAutoFormat(SizeXY.X, SizeXY.Y);
	RenderTarget2D->RenderTargetFormat = RenderTargetFormat;
	RenderTarget2D->bGPUSharedFlag = true;
	
	TextureTarget = RenderTarget2D;
}
