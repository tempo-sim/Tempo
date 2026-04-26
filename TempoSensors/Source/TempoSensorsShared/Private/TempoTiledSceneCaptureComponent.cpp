// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoTiledSceneCaptureComponent.h"

#include "TempoCoreSettings.h"
#include "TempoCoreUtils.h"
#include "TempoSensorsSettings.h"

#include "Materials/MaterialInstanceDynamic.h"

FString UTempoTiledSceneCaptureComponent::GetOwnerName() const
{
	check(GetOwner());
	return GetOwner()->GetActorNameOrLabel();
}

FString UTempoTiledSceneCaptureComponent::GetSensorName() const
{
	return GetName();
}

bool UTempoTiledSceneCaptureComponent::IsAwaitingRender()
{
	return TextureReadQueue.IsNextAwaitingRender();
}

void UTempoTiledSceneCaptureComponent::OnRenderCompleted()
{
	UTextureRenderTarget2D* ReadbackTarget = GetReadbackTextureTarget();
	if (!TextureReadQueue.IsNextAwaitingRender() || !ReadbackTarget)
	{
		return;
	}

	const FRenderTarget* RenderTarget = ReadbackTarget->GetRenderTargetResource();
	if (!ensureMsgf(RenderTarget, TEXT("Readback render target was not initialized. Skipping texture read.")))
	{
		return;
	}

	const bool bShouldBlock = GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep
		&& !GetDefault<UTempoSensorsSettings>()->GetPipelinedRendering();

	TextureReadQueue.ReadAllAvailable(RenderTarget, bShouldBlock);
}

void UTempoTiledSceneCaptureComponent::BlockUntilMeasurementsReady() const
{
	TextureReadQueue.BlockUntilNextReadComplete();
}

void UTempoTiledSceneCaptureComponent::BeginPlay()
{
	Super::BeginPlay();

	// Don't configure tiles during cooking or for template/archetype objects (e.g. Blueprint
	// editor previews where GetOwner() is not a properly-packaged actor).
	if (IsRunningCommandlet() || IsTemplate())
	{
		return;
	}

	InitTileSlots();
	SyncTiles();
	UpdateInternalMirrors();

	if (UTempoCoreUtils::IsGameWorld(this))
	{
		// Activate() runs when added to a live world, but may be skipped in some registration
		// paths. Init render targets here so they are ready for the first capture regardless.
		InitRenderTarget();
	}
}

void UTempoTiledSceneCaptureComponent::Activate(bool bReset)
{
	// Super::Activate calls InitRenderTarget() (the subclass override) in game worlds.
	// It does not start its own capture timer because ShouldManageOwnTimer() returns false.
	Super::Activate(bReset);

	if (UTempoCoreUtils::IsGameWorld(this))
	{
		RestartCaptureTimer();
	}
}

void UTempoTiledSceneCaptureComponent::Deactivate()
{
	Super::Deactivate();

	if (UTempoCoreUtils::IsGameWorld(this))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(TimerHandle);
		}
		TextureReadQueue.Empty();
	}
}

void UTempoTiledSceneCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (HasDetectedParameterChange())
	{
		bReconfigurePending = true;
	}
	TryApplyPendingReconfigure();
}

void UTempoTiledSceneCaptureComponent::RestartCaptureTimer()
{
	if (UWorld* World = GetWorld())
	{
		const float TimerPeriod = 1.0f / FMath::Max(UE_KINDA_SMALL_NUMBER, RateHz);
		World->GetTimerManager().SetTimer(TimerHandle, this, &UTempoTiledSceneCaptureComponent::MaybeCapture, TimerPeriod, true);
	}
}

int32 UTempoTiledSceneCaptureComponent::GetMaxTextureQueueSize() const
{
	return GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize();
}

void UTempoTiledSceneCaptureComponent::TryApplyPendingReconfigure()
{
	// Don't reconfigure during cooking or for template/archetype objects.
	if (IsRunningCommandlet() || IsTemplate())
	{
		return;
	}
	if (!bReconfigurePending)
	{
		return;
	}
	// Only reconfigure when no readback is in flight.
	if (TextureReadQueue.Num() > 0)
	{
		return;
	}
	ReconfigureTilesNow();
	bReconfigurePending = false;
}

void UTempoTiledSceneCaptureComponent::RetirePPM(UMaterialInstanceDynamic* PPM)
{
	if (PPM)
	{
		RetainedPPMs.AddUnique(PPM);
	}
}
