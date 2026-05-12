// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoBrightnessMeterComponent.h"

#include "Engine/TextureRenderTarget2D.h"

#include "TempoWorldSettings.h"

UTempoBrightnessMeterComponent::UTempoBrightnessMeterComponent()
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

float UTempoBrightnessMeterComponent::GetBrightness() const
{
    FScopeLock Lock(&Mutex);
    return Brightness;
}

void UTempoBrightnessMeterComponent::BeginPlay()
{
    Super::BeginPlay();

    // If the user chose an AutoExposureBias, use that. Otherwise, pull it from the world settings.
    if (!PostProcessSettings.bOverride_AutoExposureBias)
    {
        if (ATempoWorldSettings* TempoWorldSettings = Cast<ATempoWorldSettings>(GetWorld()->GetWorldSettings()))
        {
            PostProcessSettings.bOverride_AutoExposureBias = true;
            PostProcessSettings.AutoExposureBias = TempoWorldSettings->GetDefaultAutoExposureBias();
        }
    }

    TextureTarget = NewObject<UTextureRenderTarget2D>(this);
    TextureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    TextureTarget->InitAutoFormat(SizeXY.X, SizeXY.Y);

    check(GetWorld());
    GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &UTempoBrightnessMeterComponent::UpdateBrightness, UpdatePeriod, true, FMath::RandRange(0.0f, UpdatePeriod));
}

void UTempoBrightnessMeterComponent::UpdateBrightness()
{
    CaptureSceneDeferred();

    RenderFence = FRenderCommandFence();
    RenderFence->BeginFence();
}

void UTempoBrightnessMeterComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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
