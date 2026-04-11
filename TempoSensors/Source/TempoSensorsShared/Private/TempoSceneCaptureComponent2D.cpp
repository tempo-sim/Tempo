// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSceneCaptureComponent2D.h"

#include "TempoSensorsSettings.h"
#include "TempoSensorsShared.h"

#include "TempoSensorsShared/Common.pb.h"

#include "TempoCoreSettings.h"
#include "TempoCoreUtils.h"

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

	if (UTempoCoreUtils::IsGameWorld(this))
	{
		InitRenderTarget();
		RestartCaptureTimer();	
	}
}

void UTempoSceneCaptureComponent2D::Deactivate()
{
	Super::Deactivate();

	if (UTempoCoreUtils::IsGameWorld(this))
	{
		GetWorld()->GetTimerManager().ClearTimer(TimerHandle);
		TextureReadQueue.Empty();
	}
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UTempoSceneCaptureComponent2D::UpdateSceneCaptureContents(FSceneInterface* Scene)
#else
void UTempoSceneCaptureComponent2D::UpdateSceneCaptureContents(FSceneInterface* Scene, ISceneRenderBuilder& SceneRenderBuilder)
#endif
{
	TextureInitFence.Wait();

// FRayTracingScene includes buffers, StatsReadbackBuffers (StatsReadback in 5.7) and FeedbackReadback, of fixed size.
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
#if ENGINE_MINOR_VERSION > 6
		const uint32 PrevStatsReadbackBuffersSize = RayTracingScene->StatsReadback.Num();
		if (PrevStatsReadbackBuffersSize < NewMaxReadbackBuffers)
		{
			RayTracingScene->StatsReadback.SetNum(NewMaxReadbackBuffers);
			for (uint32 Index = PrevStatsReadbackBuffersSize; Index < NewMaxReadbackBuffers; ++Index)
			{
				RayTracingScene->StatsReadback[Index].ReadbackBuffer = new FRHIGPUBufferReadback(TEXT("FRayTracingScene::StatsReadbackBuffer"));
				RayTracingScene->StatsReadback[Index].MaxNumViews = RayTracingScene->ActiveViews.GetMaxIndex();
			}
		}
#else
		const uint32 PrevStatsReadbackBuffersSize = RayTracingScene->StatsReadbackBuffers.Num();
		if (PrevStatsReadbackBuffersSize < NewMaxReadbackBuffers)
		{
			RayTracingScene->StatsReadbackBuffers.SetNum(NewMaxReadbackBuffers);
			for (uint32 Index = PrevStatsReadbackBuffersSize; Index < NewMaxReadbackBuffers; ++Index)
			{
				RayTracingScene->StatsReadbackBuffers[Index] = new FRHIGPUBufferReadback(TEXT("FRayTracingScene::StatsReadbackBuffer"));
			}
		}
#endif

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
		!ensureMsgf(StagingTextures.Num() > 0 && StagingTextures[0].IsValid() && StagingTextures[0]->IsValid(), TEXT("StagingTextures were not valid. Skipping capture.")) ||
		!ensureMsgf(StagingTextures[0]->GetFormat() == TextureTarget->GetFormat(), TEXT("RenderTarget and StagingTextures did not have same format. Skipping Capture.")))
	{
		return;
	}

	const int32 MaxTextureQueueSize = GetMaxTextureQueueSize();
	if (MaxTextureQueueSize > 0 && TextureReadQueue.Num() > MaxTextureQueueSize)
	{
		UE_LOG(LogTempoSensorsShared, Warning, TEXT("Fell behind while reading frames from sensor %s. Skipping capture."), *GetName());
		return;
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	Super::UpdateSceneCaptureContents(Scene);
#else
	Super::UpdateSceneCaptureContents(Scene, SceneRenderBuilder);
#endif
	
	SequenceId++;

	FTextureRead* NewRead = MakeTextureRead();
	NewRead->StagingTexture = AcquireNextStagingTexture();

	ENQUEUE_RENDER_COMMAND(SetTempoSceneCaptureRenderFence)(
	[NewRead](FRHICommandList& RHICmdList)
	{
		NewRead->RenderFence = RHICreateGPUFence(TEXT("TempoCameraRenderFence"));
		RHICmdList.WriteGPUFence(NewRead->RenderFence);
	});

	TextureReadQueue.Enqueue(NewRead);
}

bool UTempoSceneCaptureComponent2D::IsNextReadAwaitingRender() const
{
	return TextureReadQueue.IsNextAwaitingRender();
}

void UTempoSceneCaptureComponent2D::ReadNextIfAvailable()
{
	if (!TextureReadQueue.IsNextAwaitingRender())
	{
		return;
	}

	const FRenderTarget* RenderTarget = TextureTarget->GetRenderTargetResource();
	if (!ensureMsgf(RenderTarget, TEXT("RenderTarget was not initialized. Skipping texture read.")))
	{
		TextureReadQueue.SkipNext();
		return;
	}

	const bool bShouldBlock = GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep
		&& !GetDefault<UTempoSensorsSettings>()->GetPipelinedRendering();

	TextureReadQueue.ReadAllAvailable(RenderTarget, bShouldBlock);
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

void UTempoSceneCaptureComponent2D::CreateOrResizeDistortionMapTexture(const FIntPoint& TextureSizeXY)
{
	if (TextureSizeXY.X <= 0 || TextureSizeXY.Y <= 0)
	{
		return;
	}

	DistortionMapTexture = UTexture2D::CreateTransient(TextureSizeXY.X, TextureSizeXY.Y, PF_G16R16F);
	DistortionMapTexture->CompressionSettings = TC_HDR;
	DistortionMapTexture->Filter = TF_Bilinear;
	DistortionMapTexture->AddressX = TA_Clamp;
	DistortionMapTexture->AddressY = TA_Clamp;
	DistortionMapTexture->SRGB = 0;
	DistortionMapTexture->UpdateResource();
}

void UTempoSceneCaptureComponent2D::ApplyDistortionMapToMaterial(UMaterialInstanceDynamic* MaterialInstance) const
{
	if (MaterialInstance && DistortionMapTexture)
	{
		MaterialInstance->SetTextureParameterValue(FName("DistortionMap"), DistortionMapTexture);
	}
}

void UTempoSceneCaptureComponent2D::FillDistortionMap(const FDistortionModel& Model,
	const FIntPoint& OutputSizeXY, double FxOutput, double FyOutput,
	const FIntPoint& RenderSizeXY, double FxRender, double FyRender) const
{
	if (!DistortionMapTexture || OutputSizeXY.X <= 0 || OutputSizeXY.Y <= 0)
	{
		return;
	}

	FTexture2DMipMap& Mip = DistortionMapTexture->GetPlatformData()->Mips[0];
	uint16* MipData = static_cast<uint16*>(Mip.BulkData.Lock(LOCK_READ_WRITE));

	if (!MipData)
	{
		Mip.BulkData.Unlock();
		return;
	}

	// Output image center (for pixel-to-normalized conversion).
	const double OutputCx = OutputSizeXY.X * 0.5;
	const double OutputCy = OutputSizeXY.Y * 0.5;

	// Render image center (for normalized-to-UV conversion).
	const double RenderCx = RenderSizeXY.X * 0.5;
	const double RenderCy = RenderSizeXY.Y * 0.5;

	for (int V = 0; V < OutputSizeXY.Y; ++V)
	{
		uint16* Row = &MipData[V * OutputSizeXY.X * 2];
		const double OutputY = (V + 0.5 - OutputCy) / FyOutput;

		for (int U = 0; U < OutputSizeXY.X; ++U)
		{
			const double OutputX = (U + 0.5 - OutputCx) / FxOutput;
			const FVector2D Render = Model.OutputToRender(OutputX, OutputY);
			const float FinalU = static_cast<float>(Render.X * FxRender + RenderCx) / static_cast<float>(RenderSizeXY.X);
			const float FinalV = static_cast<float>(Render.Y * FyRender + RenderCy) / static_cast<float>(RenderSizeXY.Y);
			Row[U * 2 + 0] = FFloat16(FinalU).Encoded;
			Row[U * 2 + 1] = FFloat16(FinalV).Encoded;
		}
	}

	Mip.BulkData.Unlock();
	DistortionMapTexture->UpdateResource();
}

void UTempoSceneCaptureComponent2D::InitRenderTarget()
{
	if (SizeXY.X == 0 || SizeXY.Y == 0)
	{
		return;
	}

	// Wait for any previous staging texture init render command to complete before
	// modifying StagingTextures, since the render command accesses the array via raw pointer.
	TextureInitFence.Wait();

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

	// Determine how many staging textures to create. We need at least as many as the max
	// texture queue size to ensure each in-flight read gets its own staging texture.
	const int32 MaxQueueSize = GetMaxTextureQueueSize();
	const int32 NumStagingTextures = FMath::Max(2, MaxQueueSize > 0 ? MaxQueueSize + 1 : 2);

	struct FInitStagingContext {
		FString NameBase;
		int32 SizeX;
		int32 SizeY;
		EPixelFormat PixelFormat;
		int32 NumTextures;
		TArray<FTextureRHIRef>* StagingTextures;
		FCriticalSection* StagingTexturesMutex;
	};

	{
		FScopeLock StagingTexturesLock(&StagingTexturesMutex);
		if (NumStagingTextures != StagingTextures.Num())
		{
			StagingTextures.SetNum(NumStagingTextures);
			NextStagingIndex = 0;
		}
	}

	FInitStagingContext Context = {
		GetName(),
		TextureTarget->SizeX,
		TextureTarget->SizeY,
		TextureTarget->GetFormat(),
		NumStagingTextures,
		&StagingTextures,
		&StagingTexturesMutex
	};

	ENQUEUE_RENDER_COMMAND(InitTempoSceneCaptureTextureCopy)(
		[Context](FRHICommandListImmediate& RHICmdList)
		{
			constexpr ETextureCreateFlags TexCreateFlags = ETextureCreateFlags::Shared | ETextureCreateFlags::CPUReadback;

			for (int32 I = 0; I < Context.NumTextures; ++I)
			{
				const FRHITextureCreateDesc Desc =
					FRHITextureCreateDesc::Create2D(*FString::Printf(TEXT("%s StagingTexture %d"), *Context.NameBase, I))
					.SetExtent(Context.SizeX, Context.SizeY)
					.SetFormat(Context.PixelFormat)
					.SetFlags(TexCreateFlags);

				{
					FScopeLock StagingTexturesLock(Context.StagingTexturesMutex);
					(*Context.StagingTextures)[I] = RHICreateTexture(Desc);
				}
			}
		});

	TextureInitFence.BeginFence();

	// Any pending texture reads might have the wrong pixel format.
	TextureReadQueue.Empty();

	InitDistortionMap();
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
