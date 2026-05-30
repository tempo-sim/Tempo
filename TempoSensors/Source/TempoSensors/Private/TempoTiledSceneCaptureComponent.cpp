// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoTiledSceneCaptureComponent.h"

#include "TempoSensors.h"
#include "TempoSensorsSettings.h"

#include "TempoCoreSettings.h"
#include "TempoCoreUtils.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"

FString UTempoTiledSceneCaptureComponent::GetOwnerName() const
{
	check(GetOwner());
	return UTempoCoreUtils::GetActorIdentifier(GetOwner());
}

FString UTempoTiledSceneCaptureComponent::GetSensorName() const
{
	return GetName();
}

bool UTempoTiledSceneCaptureComponent::IsAwaitingRender()
{
	return TextureReadQueue.IsAnyAwaitingRender();
}

void UTempoTiledSceneCaptureComponent::OnRenderCompleted()
{
	UTextureRenderTarget2D* ReadbackTarget = GetReadbackTextureTarget();
	if (!TextureReadQueue.IsAnyAwaitingRender() || !ReadbackTarget)
	{
		return;
	}

	const FRenderTarget* RenderTarget = ReadbackTarget->GetRenderTargetResource();
	if (!ensureMsgf(RenderTarget, TEXT("Readback render target was not initialized. Skipping texture read.")))
	{
		return;
	}

	// Always non-blocking on the render thread. Blocking here (spinning on the GPU fence) deadlocks
	// on platforms where the render thread is also the GPU submission thread (Metal): the thread
	// waiting on the fence is the one that must submit the work that signals it. In
	// fixed-step/non-pipelined mode the game thread guarantees same-frame completion in
	// BlockUntilMeasurementsReady (via FlushRenderingCommands); in pipelined mode these opportunistic
	// reads drain the queue across frames.
	TextureReadQueue.ReadAllAvailable(RenderTarget, /*bBlock=*/false);
}

void UTempoTiledSceneCaptureComponent::BlockUntilMeasurementsReady() const
{
	UTextureRenderTarget2D* ReadbackTarget = GetReadbackTextureTarget();
	if (!ReadbackTarget)
	{
		return;
	}

	// Submit and complete all pending render work (the staging copies + fence writes enqueued by
	// RenderCapture) here on the game thread rather than by blocking the render thread. On
	// single-submission-thread platforms (Metal) a render thread spinning on a GPU fence can never
	// submit the work that signals it — that is the deadlock this replaces. FlushRenderingCommands
	// drains the render thread and flushes the RHI thread, so the staging fences are in flight on
	// the GPU once it returns.
	FlushRenderingCommands();

	// Use the game-thread-safe accessor here. GetRenderTargetResource() asserts IsInRenderingThread();
	// this function runs on the game thread.
	const FRenderTarget* RenderTarget = ReadbackTarget->GameThread_GetRenderTargetResource();
	if (!RenderTarget)
	{
		return;
	}

	// Perform the GPU->CPU readback. Read() asserts it runs on the render thread, so enqueue it and
	// flush again to wait for completion. The copies are already submitted (above), so the fence is
	// signalled (or imminently will be) and the read completes instead of deadlocking.
	FTextureReadQueue& Queue = const_cast<FTextureReadQueue&>(TextureReadQueue);
	ENQUEUE_RENDER_COMMAND(TempoBlockingTextureRead)(
		[&Queue, RenderTarget](FRHICommandListImmediate&)
		{
			Queue.ReadAllAvailable(RenderTarget, /*bBlock=*/true);
		});
	FlushRenderingCommands();
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

void UTempoTiledSceneCaptureComponent::MaybeMarkPendingCapture()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float TimerPeriod = 1.0f / FMath::Max(UE_KINDA_SMALL_NUMBER, RateHz);
	if (!FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRate(TimerHandle), TimerPeriod))
	{
		RestartCaptureTimer();
	}

	if (!HasPendingRequests())
	{
		return;
	}

	// Don't capture while a property change is pending. RenderCapture would run at the old shared
	// RT / staging sizes, then the proxy CaptureScene's UpdateSceneCaptureContents would notice
	// TextureTarget != SizeXY and reinit mid-capture — leaving the in-flight FTextureRead with an
	// old-size StagingTexture but new-size ImageSize, which crashes ReadAllAvailable's memcpy.
	// TryApplyPendingReconfigure will resync the internal mirrors once reads have drained.
	if (HasDetectedParameterChange())
	{
		return;
	}

	if (!SharedTextureTarget)
	{
		return;
	}

	if (GetNumActiveTiles() == 0)
	{
		return;
	}

	const int32 MaxQueueSize = GetMaxTextureQueueSize();
	if (MaxQueueSize > 0 && TextureReadQueue.Num() > MaxQueueSize)
	{
		UE_LOG(LogTempoSensors, Warning, TEXT("Fell behind while reading frames from sensor %s. Skipping capture."), *GetName());
		return;
	}

	if (!SharedTextureTarget->GameThread_GetRenderTargetResource())
	{
		return;
	}

	if (!World->Scene)
	{
		return;
	}

	bNeedsCapture = true;
}

void UTempoTiledSceneCaptureComponent::ExecutePendingCapture()
{
	if (!bNeedsCapture)
	{
		return;
	}
	// Final guard: a property may have changed between MaybeMarkPendingCapture (which set
	// bNeedsCapture) and this call. Keep bNeedsCapture set so the next frame's
	// ExecutePendingCapture picks it up once ReconfigureTilesNow has resynced.
	if (HasDetectedParameterChange())
	{
		return;
	}
	bNeedsCapture = false;
	RenderCapture();
}

void UTempoTiledSceneCaptureComponent::RestartCaptureTimer()
{
	if (UWorld* World = GetWorld())
	{
		const float TimerPeriod = 1.0f / FMath::Max(UE_KINDA_SMALL_NUMBER, RateHz);
		World->GetTimerManager().SetTimer(TimerHandle, this, &UTempoTiledSceneCaptureComponent::MaybeMarkPendingCapture, TimerPeriod, true);
	}
}

int32 UTempoTiledSceneCaptureComponent::GetMaxTextureQueueSize() const
{
	return GetDefault<UTempoSensorsSettings>()->GetMaxRenderBufferSize();
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
		const int32 MaxSize = GetDefault<UTempoSensorsSettings>()->GetMaxRenderBufferSize();
		while (RetainedPPMs.Num() > MaxSize)
		{
			RetainedPPMs.RemoveAt(0);
		}
	}
}
