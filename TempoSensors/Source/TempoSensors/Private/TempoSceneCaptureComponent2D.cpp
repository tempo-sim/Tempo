// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSceneCaptureComponent2D.h"

#include "TempoSensors.h"
#include "TempoSensorsSettings.h"

#include "TempoSensors/Common.pb.h"

#include "TempoCoreSettings.h"
#include "TempoCoreUtils.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"

#if RHI_RAYTRACING && ENGINE_MAJOR_VERSION == 5 && ((ENGINE_MINOR_VERSION == 5 && STATS) || ENGINE_MINOR_VERSION > 5)
#if PLATFORM_WINDOWS
// An upstream include leaks the Win32 Interlocked* macros, which mangle
// FPlatformAtomics::InterlockedIncrement -> ::_InterlockedIncrement inside RenderCore headers
// (RenderTargetPool.h) that ScenePrivate.h transitively pulls in. Allow+Hide is a no-op pair
// that re-asserts and then clears those macros, so ScenePrivate.h parses against the real
// FPlatformAtomics API.
#include "Windows/AllowWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformAtomics.h"
#endif
// Hack to get access to private members of FRayTracingScene. See comment in UpdateSceneCaptureContents for more detail.
// The macro must be undef'd after the parse — leaving it active flips access on every
// `private:` section parsed later in the unity TU (e.g. the protobuf-generated Sensors.pb.h
// pulled in by TempoSensorServiceSubsystem.cpp), which changes MSVC name mangling and
// produces LNK2019 mismatches against the corresponding .pb.cc TUs.
#define private public
#include "RayTracing/RayTracingScene.h"
#include "ScenePrivate.h"
#undef private
#endif

void FTextureRead::ExtractMeasurementHeader(float TransmissionTime, TempoSensors::MeasurementHeader* MeasurementHeaderOut) const
{
	MeasurementHeaderOut->set_sequence_id(SequenceId);
	MeasurementHeaderOut->set_capture_time_s(CaptureTime);
	MeasurementHeaderOut->set_transmission_time_s(TransmissionTime);
	MeasurementHeaderOut->set_owner(TCHAR_TO_UTF8(*OwnerName));
	MeasurementHeaderOut->set_sensor(TCHAR_TO_UTF8(*SensorName));
	const FVector SensorLocation = QuantityConverter<CM2M, L2R>::Convert(SensorTransform.GetLocation());
	const FRotator SensorRotation = QuantityConverter<Deg2Rad, L2R>::Convert(SensorTransform.Rotator());
	MeasurementHeaderOut->mutable_capture_transform()->mutable_location()->set_x(SensorLocation.X);
	MeasurementHeaderOut->mutable_capture_transform()->mutable_location()->set_y(SensorLocation.Y);
	MeasurementHeaderOut->mutable_capture_transform()->mutable_location()->set_z(SensorLocation.Z);
	MeasurementHeaderOut->mutable_capture_transform()->mutable_rotation()->set_p(SensorRotation.Pitch);
	MeasurementHeaderOut->mutable_capture_transform()->mutable_rotation()->set_r(SensorRotation.Roll);
	MeasurementHeaderOut->mutable_capture_transform()->mutable_rotation()->set_y(SensorRotation.Yaw);
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

void UTempoSceneCaptureComponent2D::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	if (ProxyMeshComponent)
	{
		ProxyMeshComponent->SetVisibility(false);
	}
#endif
}

void UTempoSceneCaptureComponent2D::Activate(bool bReset)
{
	Super::Activate(bReset);

	if (UTempoCoreUtils::IsGameWorld(this))
	{
		InitRenderTarget();
		if (ShouldManageOwnTimer())
		{
			RestartCaptureTimer();
		}
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

// FRayTracingScene's StatsReadback / FeedbackReadback are fixed-size rings (MaxReadbackBuffers, 4
// by default). The main viewport doesn't overrun them, but running many scene captures per frame
// does — FinishTracingFeedback in particular writes unconditionally without checking
// `< MaxReadbackBuffers` (see the TODO in RayTracingScene.cpp), so an in-flight readback gets
// overwritten and EnqueueCopy / Lock later trips. Expand the rings to a configurable size.
//
// All FRayTracingScene state lives on the render thread; the expansion must happen there too. We
// enqueue a render command rather than mutating from the game thread. The hack is idempotent —
// repeated calls early-return once the rings are already at the target size, so issuing it from
// every TempoSceneCaptureComponent2D's UpdateSceneCaptureContents is fine.
#if RHI_RAYTRACING && ENGINE_MAJOR_VERSION == 5 && ((ENGINE_MINOR_VERSION == 5 && STATS) || ENGINE_MINOR_VERSION > 5)
	const UTempoSensorsSettings* TempoSensorsSettings = GetDefault<UTempoSensorsSettings>();
	if (TempoSensorsSettings && TempoSensorsSettings->GetRayTracingSceneReadbackBuffersOverrunWorkaroundEnabled())
	{
		const uint32 NewMaxReadbackBuffers = TempoSensorsSettings->GetRayTracingSceneMaxReadbackBuffersOverride();
		FSceneInterface* SceneCapture = Scene;
		ENQUEUE_RENDER_COMMAND(TempoExpandRayTracingReadbackBuffers)(
			[SceneCapture, NewMaxReadbackBuffers](FRHICommandListImmediate&)
			{
				FRayTracingScene* RayTracingScene = &SceneCapture->GetRenderScene()->RayTracingScene;
				if (RayTracingScene->MaxReadbackBuffers < NewMaxReadbackBuffers)
				{
					// MaxReadbackBuffers is declared `const uint32`; bypass via offsetof. The
					// original write was one byte, which only happened to work for small values on
					// little-endian — write the full uint32 here.
					const size_t MaxReadbackBuffersOffset = offsetof(FRayTracingScene, MaxReadbackBuffers);
					*reinterpret_cast<uint32*>(reinterpret_cast<char*>(RayTracingScene) + MaxReadbackBuffersOffset) = NewMaxReadbackBuffers;
				}
#if ENGINE_MINOR_VERSION > 6
				const uint32 PrevStatsReadbackBuffersSize = RayTracingScene->StatsReadback.Num();
				if (PrevStatsReadbackBuffersSize < NewMaxReadbackBuffers)
				{
					RayTracingScene->StatsReadback.SetNum(NewMaxReadbackBuffers);
					for (uint32 Index = PrevStatsReadbackBuffersSize; Index < NewMaxReadbackBuffers; ++Index)
					{
						RayTracingScene->StatsReadback[Index].ReadbackBuffer = new FRHIGPUBufferReadback(TEXT("FRayTracingScene::StatsReadbackBuffer"));
						// Don't preset MaxNumViews here — FinishStats sets it at enqueue time
						// (RayTracingScene.cpp:884), and the GT-time value of ActiveViews.GetMaxIndex()
						// can be INDEX_NONE (-1) which becomes 0xFFFFFFFF and overflows the read-side
						// size calc if the entry is ever Lock'd before being written.
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
			});
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
	if (!ensureMsgf(RenderTarget && RenderTarget->IsInitialized(), TEXT("RenderTarget was not initialized. Skipping capture.")))
	{
		return;
	}

	if (ShouldManageOwnReadback())
	{
		if (!ensureMsgf(StagingTextures.Num() > 0 && StagingTextures[0].IsValid() && StagingTextures[0]->IsValid(), TEXT("StagingTextures were not valid. Skipping capture.")) ||
			!ensureMsgf(StagingTextures[0]->GetFormat() == TextureTarget->GetFormat(), TEXT("RenderTarget and StagingTextures did not have same format. Skipping Capture.")))
		{
			return;
		}

		const int32 MaxTextureQueueSize = GetMaxTextureQueueSize();
		while (MaxTextureQueueSize > 0 && TextureReadQueue.Num() >= MaxTextureQueueSize)
		{
			UE_LOG(LogTempoSensors, Warning, TEXT("Fell behind while reading frames from sensor %s. Evicting oldest frame."), *GetName());
			TextureReadQueue.EvictOldest();
		}
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	Super::UpdateSceneCaptureContents(Scene);
#else
	Super::UpdateSceneCaptureContents(Scene, SceneRenderBuilder);
#endif

	if (!ShouldManageOwnReadback())
	{
		return;
	}

	SequenceId++;

	TSharedPtr<FTextureRead> NewRead(MakeTextureRead());
	NewRead->StagingTexture = AcquireNextStagingTexture();

	ENQUEUE_RENDER_COMMAND(SetTempoSceneCaptureRenderFence)(
	[NewRead](FRHICommandList& RHICmdList)
	{
		NewRead->RenderFence = RHICreateGPUFence(TEXT("TempoCameraRenderFence"));
		RHICmdList.WriteGPUFence(NewRead->RenderFence);
	});

	TextureReadQueue.Enqueue(MoveTemp(NewRead));
}

bool UTempoSceneCaptureComponent2D::IsAnyReadAwaitingRender() const
{
	return TextureReadQueue.IsAnyAwaitingRender();
}

void UTempoSceneCaptureComponent2D::ReadNextIfAvailable()
{
	if (!TextureReadQueue.IsAnyAwaitingRender())
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

TSharedPtr<FTextureRead> UTempoSceneCaptureComponent2D::DequeueIfReadComplete()
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

void UTempoSceneCaptureComponent2D::CreateOrResizeDistortionMapTexture(UTexture2D*& OutTexture, const FIntPoint& TextureSizeXY)
{
	if (TextureSizeXY.X <= 0 || TextureSizeXY.Y <= 0)
	{
		return;
	}

	OutTexture = UTexture2D::CreateTransient(TextureSizeXY.X, TextureSizeXY.Y, PF_G16R16F);
	OutTexture->CompressionSettings = TC_HDR;
	OutTexture->Filter = TF_Bilinear;
	OutTexture->AddressX = TA_Clamp;
	OutTexture->AddressY = TA_Clamp;
	OutTexture->SRGB = 0;
	// No UpdateResource() here — the caller (e.g. FillDistortionMap) writes BulkData and then
	// calls UpdateResource. Initializing the resource here would race: BeginInitResource queues
	// a render-thread InitRHI that reads via FBulkData::GetCopy, while the game thread is about
	// to Lock the same BulkData to write the distortion map. Concurrent Lock + GetCopy mutates
	// FBulkData internals and corrupts the heap, producing intermittent tbbmalloc crashes.
}

void UTempoSceneCaptureComponent2D::ApplyDistortionMapToMaterial(UMaterialInstanceDynamic* MaterialInstance, UTexture2D* DistortionMap)
{
	if (MaterialInstance && DistortionMap)
	{
		MaterialInstance->SetTextureParameterValue(FName("DistortionMap"), DistortionMap);
	}
}

void UTempoSceneCaptureComponent2D::FillDistortionMap(UTexture2D* DistortionMap, const FLensModel& Model, const FIntPoint& OutputSizeXY,
	double FOutput, const FIntPoint& RenderSizeXY,
	double TanLeft, double TanRight, double TanTop, double TanBottom)
{
	if (!DistortionMap || OutputSizeXY.X <= 0 || OutputSizeXY.Y <= 0)
	{
		return;
	}

	FTexture2DMipMap& Mip = DistortionMap->GetPlatformData()->Mips[0];
	uint16* MipData = static_cast<uint16*>(Mip.BulkData.Lock(LOCK_READ_WRITE));

	if (!MipData)
	{
		Mip.BulkData.Unlock();
		return;
	}

	// Output image center (for pixel-to-normalized conversion).
	const double OutputCx = OutputSizeXY.X * 0.5;
	const double OutputCy = OutputSizeXY.Y * 0.5;

	// UV linearly spans [TanLeft, TanRight] and [TanTop, TanBottom]. For symmetric frustums
	// (TanLeft = -TanRight) this reduces to the legacy formula Render*FRender / RenderSize + 0.5.
	const double InvTanWidth = 1.0 / (TanRight - TanLeft);
	const double InvTanHeight = 1.0 / (TanBottom - TanTop);

	for (int V = 0; V < OutputSizeXY.Y; ++V)
	{
		uint16* Row = &MipData[V * OutputSizeXY.X * 2];
		const double OutputY = (V + 0.5 - OutputCy) / FOutput;

		for (int U = 0; U < OutputSizeXY.X; ++U)
		{
			const double OutputX = (U + 0.5 - OutputCx) / FOutput;
			const FVector2D Render = Model.OutputToRender(OutputX, OutputY);
			const float FinalU = static_cast<float>((Render.X - TanLeft) * InvTanWidth);
			const float FinalV = static_cast<float>((Render.Y - TanTop) * InvTanHeight);
			Row[U * 2 + 0] = FFloat16(FinalU).Encoded;
			Row[U * 2 + 1] = FFloat16(FinalV).Encoded;
		}
	}

	Mip.BulkData.Unlock();
	DistortionMap->UpdateResource();
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

	if (!ShouldManageOwnReadback())
	{
		// Outer owner manages staging and readback; nothing else for us to do here.
		InitDistortionMap();
		return;
	}

	AllocateStagingTextures(TextureTarget->SizeX, TextureTarget->SizeY, TextureTarget->GetFormat());

	// Any pending texture reads might have the wrong pixel format.
	TextureReadQueue.Empty();

	InitDistortionMap();
}

void UTempoSceneCaptureComponent2D::AllocateStagingTextures(int32 SizeX, int32 SizeY, EPixelFormat PixelFormat)
{
	// Wait for any previous staging texture init render command to complete before modifying
	// StagingTextures, since the render command accesses the array via raw pointer.
	TextureInitFence.Wait();

	// Determine how many staging textures to create. We need at least as many as the max
	// texture queue size to ensure each in-flight read gets its own staging texture.
	const int32 MaxQueueSize = GetMaxTextureQueueSize();
	const int32 NumStagingTextures = FMath::Max(2, MaxQueueSize > 0 ? MaxQueueSize + 1 : 2);

	{
		FScopeLock StagingTexturesLock(&StagingTexturesMutex);
		if (NumStagingTextures != StagingTextures.Num())
		{
			StagingTextures.SetNum(NumStagingTextures);
			NextStagingIndex = 0;
		}
	}

	struct FInitStagingContext {
		FString NameBase;
		int32 SizeX;
		int32 SizeY;
		EPixelFormat PixelFormat;
		int32 NumTextures;
		TArray<FTextureRHIRef>* StagingTextures;
		FCriticalSection* StagingTexturesMutex;
	};

	FInitStagingContext Context = {
		GetName(),
		SizeX,
		SizeY,
		PixelFormat,
		NumStagingTextures,
		&StagingTextures,
		&StagingTexturesMutex
	};

	ENQUEUE_RENDER_COMMAND(InitTempoSceneCaptureStagingTextures)(
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

FTextureRHIRef UTempoSceneCaptureComponent2D::AcquireNextStagingTexture()
{
	// AllocateStagingTextures recreates the staging textures on the render thread asynchronously and
	// only BeginFence()s. Block here until that completes so we never hand out a slot still holding a
	// previous-generation texture. Otherwise a capture built against the new ImageSize/PixelType (e.g.
	// after a SizeXY resize or the lidar's 8B->16B color-mode format change) could be paired with a
	// stale, smaller staging texture, and FTextureRead::Read's staging-surface memcpy would over-read
	// and crash in _platform_memmove. UpdateSceneCaptureContents already waits before its own readback;
	// the tiled RenderCapture paths reach staging acquisition only through here, so this one wait covers
	// every caller. Must run on the game thread (all callers do).
	check(IsInGameThread());
	TextureInitFence.Wait();
	check(StagingTextures.Num() > 0);
	const FTextureRHIRef& Texture = StagingTextures[NextStagingIndex];
	NextStagingIndex = (NextStagingIndex + 1) % StagingTextures.Num();
	return Texture;
}
