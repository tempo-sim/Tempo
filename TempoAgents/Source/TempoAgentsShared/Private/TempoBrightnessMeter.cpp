// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoBrightnessMeter.h"

#include "Engine/TextureRenderTarget2D.h"

UTempoBrightnessMeter::UTempoBrightnessMeter()
{
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;
	bTickInEditor = false;
	bAlwaysPersistRenderingState = false;
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	// Only manual exposure adjustment.
	PostProcessSettings.bOverride_AutoExposureMethod = true;
	PostProcessSettings.AutoExposureMethod = AEM_Manual;
	ShowFlags.SetAntiAliasing(false);
	ShowFlags.SetTemporalAA(false);
	ShowFlags.SetMotionBlur(false);
}

float UTempoBrightnessMeter::GetBrightness() const
{
	FScopeLock Lock(&Mutex);
	return Brightness;
}

void UTempoBrightnessMeter::BeginPlay()
{
	Super::BeginPlay();

	TextureTarget = NewObject<UTextureRenderTarget2D>(this);
	TextureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	TextureTarget->InitAutoFormat(SizeXY.X, SizeXY.Y);

	check(GetWorld());
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &UTempoBrightnessMeter::UpdateBrightness, UpdatePeriod, true);
}

void UTempoBrightnessMeter::UpdateBrightness()
{
	CaptureSceneDeferred();

	RenderFence = FRenderCommandFence();
	RenderFence->BeginFence();
}

void UTempoBrightnessMeter::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (RenderFence && RenderFence->IsFenceComplete())
	{
		struct FBrightnessMeterReadContext {
			float* Brightness;
			FCriticalSection* Mutex;
			FRHITexture* Texture;
			FIntRect Rect;
		};

		TextureTarget->GameThread_GetRenderTargetResource();

		FBrightnessMeterReadContext Context = {
			&Brightness,
			&Mutex,
			TextureTarget->GameThread_GetRenderTargetResource()->GetTextureRHI(),
			FIntRect(0, 0, SizeXY.X, SizeXY.Y)
		};

		ENQUEUE_RENDER_COMMAND(TempoBrightnessMeterRead)(
	[Context](FRHICommandListImmediate& RHICmdList)
		{
			TArray<FColor> OutData;
			OutData.SetNumUninitialized(Context.Rect.Area());
			RHICmdList.ReadSurfaceData(Context.Texture, Context.Rect, OutData, FReadSurfaceDataFlags());
			FScopeLock Lock(Context.Mutex);
			uint32 Sum = 0;
		    for (const FColor& Color : OutData)
		    {
		    	Sum += Color.R + Color.G + Color.B;
		    }
			*Context.Brightness = Sum / (3 * 255.0 * OutData.Num());
		});

		RenderFence.Reset();
	}
}
