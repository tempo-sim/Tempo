// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSceneCaptureComponent2D.h"

#include "TempoSensorsSettings.h"
#include "TempoSensorsShared.h"

#include "TempoSensorsShared/Common.pb.h"

#include "TempoCoreSettings.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"

#if RHI_RAYTRACING && ENGINE_MAJOR_VERSION == 5 && ((ENGINE_MINOR_VERSION == 5 && STATS) || ENGINE_MINOR_VERSION > 5)
// Hack to get access to private members of FRayTracingScene. See comment in UpdateSceneCaptureContents for more detail.
#define private public
#include "RayTracing/RayTracingScene.h"
#include "ScenePrivate.h"
#endif

void FTextureRead::ExtractMeasurementHeader(float TransmissionTime, TempoSensorsShared::MeasurementHeader* MeasurementHeaderOut) const
{
	MeasurementHeaderOut->set_sequence_id(SequenceId);
	MeasurementHeaderOut->set_capture_time(CaptureTime);
	MeasurementHeaderOut->set_transmission_time(TransmissionTime);
	MeasurementHeaderOut->set_sensor_name(TCHAR_TO_UTF8(*FString::Printf(TEXT("%s/%s"), *OwnerName, *SensorName)));
	const FVector SensorLocation = QuantityConverter<CM2M, L2R>::Convert(SensorTransform.GetLocation());
	const FRotator SensorRotation = QuantityConverter<Deg2Rad, L2R>::Convert(SensorTransform.Rotator());
	MeasurementHeaderOut->mutable_sensor_transform()->mutable_location()->set_x(SensorLocation.X);
	MeasurementHeaderOut->mutable_sensor_transform()->mutable_location()->set_y(SensorLocation.Y);
	MeasurementHeaderOut->mutable_sensor_transform()->mutable_location()->set_z(SensorLocation.Z);
	MeasurementHeaderOut->mutable_sensor_transform()->mutable_rotation()->set_p(SensorRotation.Pitch);
	MeasurementHeaderOut->mutable_sensor_transform()->mutable_rotation()->set_r(SensorRotation.Roll);
	MeasurementHeaderOut->mutable_sensor_transform()->mutable_rotation()->set_y(SensorRotation.Yaw);
}

UTempoSceneCaptureComponent2D::UTempoSceneCaptureComponent2D()
{
	PrimaryComponentTick.bStartWithTickEnabled = false;
	ShowFlags.SetAntiAliasing(false);
	ShowFlags.SetMotionBlur(false);
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;
	bTickInEditor = false;
	bAlwaysPersistRenderingState = true;
}

void UTempoSceneCaptureComponent2D::Activate(bool bReset)
{
	Super::Activate(bReset);

	InitRenderTarget();
	RestartCaptureTimer();
}

void UTempoSceneCaptureComponent2D::Deactivate()
{
	Super::Deactivate();

	GetWorld()->GetTimerManager().ClearTimer(TimerHandle);
	TextureReadQueue.Empty();
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UTempoSceneCaptureComponent2D::UpdateSceneCaptureContents(FSceneInterface* Scene)
#else
void UTempoSceneCaptureComponent2D::UpdateSceneCaptureContents(FSceneInterface* Scene, ISceneRenderBuilder& SceneRenderBuilder)
#endif
{
	TextureInitFence.Wait();

// FRayTracingScene includes buffers, StatsReadbackBuffers and FeedbackReadback, of fixed size.
// When only rendering the main viewport the default size (4) is sufficient.
// But when running potentially many scene captures per frame they can easily be overrun, leading to reuse of in-use readbacks and crashes.
// Here we are "hacking" into the persistent RayTracingScene of the scene and increasing the size of these buffers.
#if RHI_RAYTRACING && ENGINE_MAJOR_VERSION == 5 && ((ENGINE_MINOR_VERSION == 5 && STATS) || ENGINE_MINOR_VERSION > 5)
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	if (TempoSensorsSettings && TempoSensorsSettings->GetRayTracingSceneReadbackBuffersOverrunWorkaroundEnabled())
	{
		const uint32 NewMaxReadbackBuffers = TempoSensorsSettings->GetRayTracingSceneMaxReadbackBuffersOverride();
		FRayTracingScene* RayTracingScene = &Scene->GetRenderScene()->RayTracingScene;
		if (RayTracingScene->MaxReadbackBuffers < NewMaxReadbackBuffers)
		{
			// MaxReadbackBuffers is not only private but const, so we need an additional trick.
			size_t MaxReadbackBuffersOffset = offsetof(FRayTracingScene, MaxReadbackBuffers);
			*(reinterpret_cast<char*>(RayTracingScene) + MaxReadbackBuffersOffset) = NewMaxReadbackBuffers;
		}

		const uint32 PrevStatsReadbackBuffersSize = RayTracingScene->StatsReadbackBuffers.Num();
		if (PrevStatsReadbackBuffersSize < NewMaxReadbackBuffers)
		{
			RayTracingScene->StatsReadbackBuffers.SetNum(NewMaxReadbackBuffers);
			for (uint32 Index = PrevStatsReadbackBuffersSize; Index < NewMaxReadbackBuffers; ++Index)
			{
				RayTracingScene->StatsReadbackBuffers[Index] = new FRHIGPUBufferReadback(TEXT("FRayTracingScene::StatsReadbackBuffer"));
			}
		}

		// FeedbackReadback added in 5.6
#if ENGINE_MINOR_VERSION > 5
		const uint32 PrevFeedbackReadbackBuffersSize = RayTracingScene->FeedbackReadback.Num();
		if (PrevFeedbackReadbackBuffersSize < NewMaxReadbackBuffers)
		{
			RayTracingScene->FeedbackReadback.SetNum(NewMaxReadbackBuffers);
			for (uint32 Index = PrevFeedbackReadbackBuffersSize; Index < NewMaxReadbackBuffers; ++Index)
			{
				RayTracingScene->FeedbackReadback[Index].GeometryHandleReadbackBuffer = new FRHIGPUBufferReadback(TEXT("FRayTracingScene::FeedbackReadbackBuffer::GeometryHandles"));
				RayTracingScene->FeedbackReadback[Index].GeometryCountReadbackBuffer = new FRHIGPUBufferReadback(TEXT("FRayTracingScene::FeedbackReadbackBuffer::GeometryCount"));			
			}
		}
#endif
	}
#endif

	if (!TextureTarget)
	{
		return;
	}

	if (TextureTarget->SizeX != SizeXY.X || TextureTarget->SizeY != SizeXY.Y)
	{
		InitRenderTarget();
		return;
	}

	const FTextureRenderTargetResource* RenderTarget = TextureTarget->GameThread_GetRenderTargetResource();
	if (!ensureMsgf(RenderTarget && RenderTarget->IsInitialized(), TEXT("RenderTarget was not initialized. Skipping capture.")) ||
		!ensureMsgf(TextureRHICopy.IsValid() && TextureRHICopy->IsValid(), TEXT("TextureRHICopy was not valid. Skipping capture.")) ||
		!ensureMsgf(TextureRHICopy->GetFormat() == TextureTarget->GetFormat(), TEXT("RenderTarget and TextureRHICopy did not have same format. Skipping Capture.")))
	{
		return;
	}

	const int32 MaxTextureQueueSize = GetMaxTextureQueueSize();
	while (MaxTextureQueueSize > 0 && TextureReadQueue.Num() >= MaxTextureQueueSize)
	{
		UE_LOG(LogTempoSensorsShared, Warning, TEXT("Fell behind while reading frames from sensor %s. Evicting oldest frame."), *GetName());
		TextureReadQueue.EvictOldest();
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	Super::UpdateSceneCaptureContents(Scene);
#else
	Super::UpdateSceneCaptureContents(Scene, SceneRenderBuilder);
#endif
	
	ENQUEUE_RENDER_COMMAND(SetTempoSceneCaptureRenderFence)(
	[this](FRHICommandList& RHICmdList)
	{
		if (!RenderFence.IsValid())
		{
			RenderFence = RHICreateGPUFence(TEXT("TempoCameraRenderFence"));
			RHICmdList.WriteGPUFence(RenderFence);
		}
	});
	
	SequenceId++;

	TextureReadQueue.Enqueue(MakeTextureRead());
}

bool UTempoSceneCaptureComponent2D::IsNextReadAwaitingRender() const
{
	return TextureReadQueue.IsNextAwaitingRender();
}

void UTempoSceneCaptureComponent2D::ReadNextIfAvailable()
{
	if (!TextureReadQueue.IsNextAwaitingRender() || !RenderFence.IsValid())
	{
		return;
	}

	if (GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep)
	{
		while (!RenderFence->Poll())
		{
			FPlatformProcess::Sleep(1e-4);
		}
	}
	else if (!RenderFence->Poll())
	{
		return;
	}

	RenderFence.SafeRelease();

	const FRenderTarget* RenderTarget = TextureTarget->GetRenderTargetResource();
	if (!ensureMsgf(RenderTarget, TEXT("RenderTarget was not initialized. Skipping texture read.")) ||
		!ensureMsgf(TextureRHICopy.IsValid() && TextureRHICopy->IsValid(), TEXT("TextureRHICopy was not valid. Skipping texture read.")) ||
		!ensureMsgf(TextureRHICopy->GetFormat() == TextureTarget->GetFormat(), TEXT("RenderTarget and TextureRHICopy did not have same format. Skipping texture read.")))
	{
		TextureReadQueue.SkipNext();
		return;
	}

	TextureReadQueue.ReadNext(RenderTarget, TextureRHICopy);
}

void UTempoSceneCaptureComponent2D::BlockUntilNextReadComplete() const
{
	TextureReadQueue.BlockUntilNextReadComplete();
}

TUniquePtr<FTextureRead> UTempoSceneCaptureComponent2D::DequeueIfReadComplete()
{	
	return TextureReadQueue.DequeueIfReadComplete();
}

TOptional<int32> UTempoSceneCaptureComponent2D::SequenceIDOfNextCompleteRead() const
{	
	return TextureReadQueue.SequenceIdOfNextCompleteRead();
}

bool UTempoSceneCaptureComponent2D::NextReadComplete() const
{
	return TextureReadQueue.NextReadComplete();
}

void UTempoSceneCaptureComponent2D::InitRenderTarget()
{
	if (SizeXY.X == 0 || SizeXY.Y == 0)
	{
		return;
	}

	TextureTarget = NewObject<UTextureRenderTarget2D>(this);

	TextureTarget->TargetGamma = GetDefault<UTempoSensorsSettings>()->GetSceneCaptureGamma();
	TextureTarget->RenderTargetFormat = RenderTargetFormat;
	TextureTarget->bGPUSharedFlag = true;
	if (PixelFormatOverride == EPixelFormat::PF_Unknown)
	{
		TextureTarget->InitAutoFormat(SizeXY.X, SizeXY.Y);
	}
	else
	{
		TextureTarget->InitCustomFormat(SizeXY.X, SizeXY.Y, PixelFormatOverride, true);
	}

	struct FInitCPUCopyContext {
		FString Name;
		int32 SizeX;
		int32 SizeY;
		EPixelFormat PixelFormat;
		FTextureRHIRef* TextureRHICopy;
	};

	FInitCPUCopyContext Context = {
		FString::Printf(TEXT("%s TextureRHICopy"), *GetName()),
		TextureTarget->SizeX,
		TextureTarget->SizeY,
		TextureTarget->GetFormat(),
		&TextureRHICopy
	};

	ENQUEUE_RENDER_COMMAND(InitTempoSceneCaptureTextureCopy)(
		[Context](FRHICommandListImmediate& RHICmdList)
		{
			// Create the TextureRHICopy, where we will copy our TextureTarget's resource before reading it on the CPU.
			constexpr ETextureCreateFlags TexCreateFlags = ETextureCreateFlags::Shared | ETextureCreateFlags::CPUReadback;

			const FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create2D(*Context.Name)
				.SetExtent(Context.SizeX, Context.SizeY)
				.SetFormat(Context.PixelFormat)
				.SetFlags(TexCreateFlags);

			*Context.TextureRHICopy = RHICreateTexture(Desc);
		});

	TextureInitFence.BeginFence();

	// Any pending texture reads might have the wrong pixel format.
	TextureReadQueue.Empty();
}

float GetTimerPeriod(float RateHz)
{
	// Don't allow a negative or zero rate.
	return 1.0 / FMath::Max(UE_KINDA_SMALL_NUMBER, RateHz);
}

void UTempoSceneCaptureComponent2D::RestartCaptureTimer()
{
	const float TimerPeriod = GetTimerPeriod(RateHz);
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &UTempoSceneCaptureComponent2D::MaybeCapture, TimerPeriod, true);
}

void UTempoSceneCaptureComponent2D::MaybeCapture()
{
	const float TimerPeriod = GetTimerPeriod(RateHz);
	if (!FMath::IsNearlyEqual(GetWorld()->GetTimerManager().GetTimerRate(TimerHandle), TimerPeriod))
	{
		RestartCaptureTimer();
	}

	if (!HasPendingRequests())
	{
		return;
	}

	bool bWorldRenderingDisabled = false;
	if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (const ULocalPlayer* ClientPlayer = PlayerController->GetLocalPlayer())
		{
			if (const UGameViewportClient* ViewportClient = ClientPlayer->ViewportClient)
			{
				bWorldRenderingDisabled = ViewportClient->bDisableWorldRendering;
			}
		}
	}

	// If world rendering is enabled, we'll capture the scene with the main render. Otherwise, we'll capture it now.
	if (bWorldRenderingDisabled)
	{
		CaptureScene();
	}
	else
	{
		CaptureSceneDeferred();
	}
}
