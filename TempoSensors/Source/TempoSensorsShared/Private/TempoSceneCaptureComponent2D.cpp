// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSceneCaptureComponent2D.h"

#include "TempoSensorsSettings.h"

#include "Engine/TextureRenderTarget2D.h"

UTempoSceneCaptureComponent2D::UTempoSceneCaptureComponent2D()
{
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
	
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &UTempoSceneCaptureComponent2D::MaybeCapture, 1.0 / RateHz, true);
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

void UTempoSceneCaptureComponent2D::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	Super::UpdateSceneCaptureContents(Scene);

	SequenceId++;
}

void UTempoSceneCaptureComponent2D::MaybeCapture()
{
	if (!HasPendingRequests())
	{
		return;
	}

	CaptureSceneDeferred();
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

#if PLATFORM_LINUX
	// Create the TextureRHICopy, where we will copy our TextureTarget's resource before reading it on the CPU, due to a Vulkan limitation.
	ETextureCreateFlags TexCreateFlags = ETextureCreateFlags::Shared | ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource | ETextureCreateFlags::CPUReadback;
	
	const FRHITextureCreateDesc Desc =
		FRHITextureCreateDesc::Create2D(*FString::Printf(TEXT("%s TextureRHICopy"), *GetName()))
		.SetExtent(TextureTarget->SizeX, TextureTarget->SizeY)
		.SetFormat(TextureTarget->GetFormat())
		.SetFlags(TexCreateFlags);
	
	TextureRHICopy = RHICreateTexture(Desc);
#endif
}
