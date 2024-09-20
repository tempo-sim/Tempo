// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSceneCaptureComponent2D.h"

#include "TempoSensorsSettings.h"
#include "TempoSensorsShared.h"

#include "TempoCoreSettings.h"

#include "Engine/TextureRenderTarget2D.h"

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

void UTempoSceneCaptureComponent2D::BeginPlay()
{
	Super::BeginPlay();

	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &UTempoSceneCaptureComponent2D::MaybeCapture, 1.0 / RateHz, true);
}

void UTempoSceneCaptureComponent2D::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	TextureInitFence.Wait();

	const FTextureRenderTargetResource* RenderTarget = TextureTarget->GameThread_GetRenderTargetResource();
	if (!ensureMsgf(RenderTarget && RenderTarget->IsInitialized(), TEXT("RenderTarget was not initialized. Skipping capture.")) ||
		!ensureMsgf(TextureRHICopy.IsValid() && TextureRHICopy->IsValid(), TEXT("TextureRHICopy was not valid. Skipping capture.")) ||
		!ensureMsgf(TextureRHICopy->GetFormat() == TextureTarget->GetFormat(), TEXT("RenderTarget and TextureRHICopy did not have same format. Skipping Capture.")))
	{
		return;
	}

	const int32 MaxTextureQueueSize = GetMaxTextureQueueSize();
	if (MaxTextureQueueSize > 0 && TextureReadQueue.GetNum() > MaxTextureQueueSize)
	{
		UE_LOG(LogTempoSensorsShared, Warning, TEXT("Fell behind while reading frames from sensor %s owner %s. Skipping capture."), *GetSensorName(), *GetOwnerName());
		return;
	}

	Super::UpdateSceneCaptureContents(Scene);
	
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

FString UTempoSceneCaptureComponent2D::GetOwnerName() const
{
	check(GetOwner());

	return GetOwner()->GetActorNameOrLabel();
}

FString UTempoSceneCaptureComponent2D::GetSensorName() const
{
	return GetName();
}

bool UTempoSceneCaptureComponent2D::IsAwaitingRender()
{
	return TextureReadQueue.IsNextAwaitingRender();
}

void UTempoSceneCaptureComponent2D::OnRenderCompleted()
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

	TextureReadQueue.BeginNextRead(RenderTarget, TextureRHICopy);
}

void UTempoSceneCaptureComponent2D::BlockUntilMeasurementsReady() const
{
	TextureReadQueue.BlockUntilNextReadyToSend();
}

TOptional<TFuture<void>> UTempoSceneCaptureComponent2D::SendMeasurements()
{
	if (TUniquePtr<FTextureRead> TextureRead = TextureReadQueue.DequeueIfReadyToSend())
	{
		return DecodeAndRespond(MoveTemp(TextureRead));
	}
	
	return TOptional<TFuture<void>>();
}

void UTempoSceneCaptureComponent2D::InitRenderTarget()
{
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

void UTempoSceneCaptureComponent2D::MaybeCapture()
{
	if (!HasPendingRequests())
	{
		return;
	}

	CaptureSceneDeferred();
}
