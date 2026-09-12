// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoTiledSceneCaptureComponent.h"

#include "TempoSensorRenderGroup.h"
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
	TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsOnRenderCompleted);

	if (!TextureReadQueue.IsAnyAwaitingRender())
	{
		return;
	}

	// Always non-blocking on the render thread. This callback runs inside OnEndFrameRT, which is
	// upstream of the end-of-frame GPU queue submit (RHIEndFrame). A GPU fence only signals after
	// its command buffer is submitted, so spinning on the fence here waits for a submit that cannot
	// happen until this callback returns: deadlock. (Platform-independent — true on Vulkan, Metal,
	// and D3D, with or without a separate RHI thread.) In fixed-step/non-pipelined mode the game
	// thread guarantees same-frame completion in BlockUntilMeasurementsReady (via
	// FlushRenderingCommands); in pipelined mode these opportunistic reads drain the queue across
	// frames.
	TextureReadQueue.ReadAllAvailable(/*bBlock=*/false);
}

void UTempoTiledSceneCaptureComponent::BlockUntilMeasurementsReady() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsBlockUntilMeasurementsReady);

	// Do the synchronous readback on the render thread (Read() asserts IsInRenderingThread), then
	// block the game thread on it via FlushRenderingCommands. The producer's RenderFence must not
	// be polled on the render thread: OnRenderCompleted runs inside OnEndFrameRT, upstream of the
	// end-of-frame GPU queue submit, so on some RHIs (Vulkan) that fence is never submitted in time
	// to poll mid-tick and the poll deadlocks. ReadAllAwaitingBlocking skips the poll and hands the
	// fence to RHIMapStagingSurface, which forces submission and blocks until the GPU completes.
	// The producer commands enqueued by RenderCapture run first in the render FIFO, so the staging
	// copy has been issued by the time this command maps it.
	FTextureReadQueue& Queue = const_cast<FTextureReadQueue&>(TextureReadQueue);
	ENQUEUE_RENDER_COMMAND(TempoBlockingTextureRead)(
		[&Queue](FRHICommandListImmediate&)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsBlockingTextureRead);
			Queue.ReadAllAwaitingBlocking();
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
		JoinRenderGroup();
		if (!RenderGroup)
		{
			RestartCaptureTimer();
		}
	}
}

void UTempoTiledSceneCaptureComponent::Deactivate()
{
	Super::Deactivate();

	if (UTempoCoreUtils::IsGameWorld(this))
	{
		LeaveRenderGroup();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(TimerHandle);
		}
		TextureReadQueue.Empty();
	}
}

void UTempoTiledSceneCaptureComponent::JoinRenderGroup()
{
	if (RenderGroup)
	{
		return;
	}
	if (IsRunningCommandlet() || IsTemplate())
	{
		return;
	}
	if (!GetDefault<UTempoSensorsSettings>()->GetSensorRenderGroupingEnabled())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || !UTempoCoreUtils::IsGameWorld(this))
	{
		return;
	}
	if (UTempoSensorRenderGroupSubsystem* Subsystem = World->GetSubsystem<UTempoSensorRenderGroupSubsystem>())
	{
		RenderGroup = Subsystem->JoinGroup(this);
	}
}

void UTempoTiledSceneCaptureComponent::LeaveRenderGroup()
{
	if (!RenderGroup)
	{
		return;
	}
	UTempoSensorRenderGroup* Group = RenderGroup;
	RenderGroup = nullptr;
	if (UWorld* World = GetWorld())
	{
		if (UTempoSensorRenderGroupSubsystem* Subsystem = World->GetSubsystem<UTempoSensorRenderGroupSubsystem>())
		{
			Subsystem->LeaveGroup(this, Group);
			return;
		}
	}
	Group->RemoveMember(this);
}

void UTempoTiledSceneCaptureComponent::EnforceGroupRate(float GroupRateHz)
{
	if (FMath::IsNearlyEqual(RateHz, GroupRateHz))
	{
		return;
	}
	UE_LOG(LogTempoSensors, Error,
		TEXT("Sensor %s changed RateHz from %g to %g while in a render group. Rates cannot change while the simulation is running when sensor render grouping is enabled; reverting to %g. Set the rate before the sensor activates, or disable grouping in Tempo Sensors settings."),
		*GetName(), GroupRateHz, RateHz, GroupRateHz);
	RateHz = GroupRateHz;
}

void UTempoTiledSceneCaptureComponent::OnRegister()
{
	Super::OnRegister();

	// Paired with the unbind in OnUnregister. Binding in BeginPlay instead would leave the sensor
	// deaf to later override changes: FComponentReregisterContext (any Details-panel edit during
	// PIE) and ReregisterComponent() run OnUnregister/OnRegister without EndPlay/BeginPlay, so the
	// unbind would happen with nothing to re-bind it.
	if (!IsTemplate())
	{
		GetMutableDefault<UTempoSensorsSettings>()->TempoSensorsLabelOverridesChangedEvent.AddUObject(
			this, &UTempoTiledSceneCaptureComponent::ApplyLabelOverridesToTiles);
	}
}

void UTempoTiledSceneCaptureComponent::OnUnregister()
{
	GetMutableDefault<UTempoSensorsSettings>()->TempoSensorsLabelOverridesChangedEvent.RemoveAll(this);

	LeaveRenderGroup();

	// Destroy tile view states while the scene is still valid, before Super unregisters us from it
	// (matches USceneCaptureComponent::OnUnregister, which destroys the inherited ViewStates here).
	DeactivateAllTiles();

	Super::OnUnregister();
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

	// A grouped sensor is ticked by its group's timer, at the group's (fixed) rate.
	if (!RenderGroup)
	{
		const float TimerPeriod = 1.0f / FMath::Max(UE_KINDA_SMALL_NUMBER, RateHz);
		if (!FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRate(TimerHandle), TimerPeriod))
		{
			RestartCaptureTimer();
		}
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
	if (RenderGroup)
	{
		RenderGroup->ExecutePendingCaptures();
		return;
	}
	if (ConsumePendingCapture())
	{
		RenderCapture();
	}
}

bool UTempoTiledSceneCaptureComponent::ConsumePendingCapture()
{
	if (!bNeedsCapture)
	{
		return false;
	}
	// Final guard: a property may have changed between MaybeMarkPendingCapture (which set
	// bNeedsCapture) and this call. Keep bNeedsCapture set so the next frame's
	// ExecutePendingCapture picks it up once ReconfigureTilesNow has resynced.
	if (HasDetectedParameterChange())
	{
		return false;
	}
	bNeedsCapture = false;
	return true;
}

void UTempoTiledSceneCaptureComponent::RenderCapture()
{
	const int32 NumStages = GetNumRenderStages();
	for (int32 Stage = 0; Stage < NumStages; ++Stage)
	{
		if (!RenderStageStandalone(Stage))
		{
			return;
		}
	}
}

bool UTempoTiledSceneCaptureComponent::RenderStageStandalone(int32 Stage)
{
	UWorld* World = GetWorld();
	FSceneInterface* Scene = World ? World->Scene : nullptr;
	if (!Scene)
	{
		return false;
	}

	FTempoSensorGroupRenderDesc Desc;
	if (!GetRenderStageDesc(Stage, Desc) || !Desc.BlockRT || !Desc.BlockRT->GameThread_GetRenderTargetResource())
	{
		return false;
	}

	TArray<TempoMultiViewCapture::FViewSetup> Views;
	if (!PrepareRenderStage(Stage, Views) || Views.IsEmpty())
	{
		return false;
	}

	// The multi-view path renders via its own FSceneRenderer and never calls
	// UpdateSceneCaptureContents, so it must expand FRayTracingScene's readback rings itself, and
	// pin the scene's readback buffers for a render that may not use ray tracing. Both must be
	// enqueued before RenderTiles enqueues the scene render.
	EnsureRayTracingReadbackBuffersExpanded(Scene);
	PinRayTracingSceneUsedThisFrame(Scene);

	TempoMultiViewCapture::RenderTiles(Scene, this, Desc.BlockRT, Views, Desc.CaptureSource, Desc.ResolutionFraction, &Desc.ShowFlags,
		FString::Printf(TEXT("Tempo %s stage %d"), *GetName(), Stage));

	FinishRenderStage(Stage);
	return true;
}

void UTempoTiledSceneCaptureComponent::RestartCaptureTimer()
{
	if (RenderGroup)
	{
		return;
	}
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
