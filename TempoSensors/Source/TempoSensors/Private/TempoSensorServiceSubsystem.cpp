// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorServiceSubsystem.h"

#include "TempoSensorInterface.h"
#include "TempoSensors/Sensors.grpc.pb.h"

#include "TempoCamera.h"
#include "TempoLidar.h"
#include "TempoReadbackWatcher.h"
#include "TempoSensors/Camera.pb.h"
#include "TempoSensors/Lidar.pb.h"

#include "TempoCoreSettings.h"
#include "TempoSensorsSettings.h"
#include "TempoWorldSettings.h"
#include "TempoServer.h"

#include "Components/SceneCaptureComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "SceneInterface.h"
#include "SceneRenderBuilderInterface.h"
#include "HAL/IConsoleManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TempoMultiViewCapture.h"

using SensorService = TempoSensors::SensorService;
using SensorAsyncService = TempoSensors::SensorService::AsyncService;
using SensorDescriptor = TempoSensors::SensorDescriptor;
using AvailableSensorsResponse = TempoSensors::AvailableSensorsResponse;
using ColorImageRequest = TempoSensors::ColorImageRequest;
using DepthImageRequest = TempoSensors::DepthImageRequest;
using LabelImageRequest = TempoSensors::LabelImageRequest;
using LidarScanRequest = TempoSensors::LidarScanRequest;
using ColorImage = TempoSensors::ColorImage;
using DepthImage = TempoSensors::DepthImage;
using LabelImage = TempoSensors::LabelImage;
using LidarScanSegment = TempoSensors::LidarScanSegment;
using VideoRequest = TempoSensors::VideoRequest;
using VideoFrame = TempoSensors::VideoFrame;
using SetPipelinedRenderingEnabledRequest = TempoSensors::SetPipelinedRenderingEnabledRequest;

void UTempoSensorServiceSubsystem::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<SensorService>(
		SimpleRequestHandler(&SensorAsyncService::RequestGetAvailableSensors, &UTempoSensorServiceSubsystem::GetAvailableSensors),
		StreamingRequestHandler(&SensorAsyncService::RequestStreamColorImages, &UTempoSensorServiceSubsystem::StreamColorImages),
		StreamingRequestHandler(&SensorAsyncService::RequestStreamDepthImages, &UTempoSensorServiceSubsystem::StreamDepthImages),
		StreamingRequestHandler(&SensorAsyncService::RequestStreamLabelImages, &UTempoSensorServiceSubsystem::StreamLabelImages),
		StreamingRequestHandler(&SensorAsyncService::RequestStreamBoundingBoxes, &UTempoSensorServiceSubsystem::StreamBoundingBoxes),
		StreamingRequestHandler(&SensorAsyncService::RequestStreamLidarScans, &UTempoSensorServiceSubsystem::StreamLidarScans),
		StreamingRequestHandler(&SensorAsyncService::RequestStreamVideo, &UTempoSensorServiceSubsystem::StreamVideo),
		SimpleRequestHandler(&SensorAsyncService::RequestSetPipelinedRenderingEnabled, &UTempoSensorServiceSubsystem::SetPipelinedRenderingEnabled)
		);
}

void UTempoSensorServiceSubsystem::SetPipelinedRenderingEnabled(const SetPipelinedRenderingEnabledRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const
{
	// Read fresh at every readback barrier, so this takes effect on the next frame with no
	// sensor teardown. Measurements already in flight keep whichever policy they were captured under.
	GetMutableDefault<UTempoSensorsSettings>()->SetPipelinedRendering(Request.enabled());

	ResponseContinuation.ExecuteIfBound(TempoCore::Empty(), grpc::Status_OK);
}

void UTempoSensorServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoServer::Get().ActivateService<SensorService>(this);

	// OnWorldTickStart is fired before Tick has actually begun, while the world time is still the tick
	// of the last frame. We use this last opportunity, having waited as long as possible, to collect
	// and send all the sensor measurements from the previous frame.
	FWorldDelegates::OnWorldTickStart.AddUObject(this, &UTempoSensorServiceSubsystem::OnWorldTickStart);
	FWorldDelegates::OnWorldTickEnd.AddUObject(this, &UTempoSensorServiceSubsystem::OnWorldTickEnd);
	FCoreDelegates::OnEndFrameRT.AddUObject(this, &UTempoSensorServiceSubsystem::OnRenderFrameCompleted);

	BetweenFramesPollHandle = ATempoWorldSettings::OnBetweenFramesPoll.AddUObject(this, &UTempoSensorServiceSubsystem::OnBetweenFramesPoll);
}

void UTempoSensorServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	ATempoWorldSettings::OnBetweenFramesPoll.Remove(BetweenFramesPollHandle);

	WaitForDecodesInFlight();
	FTempoServer::Get().DeactivateService<SensorService>();
}

void UTempoSensorServiceSubsystem::OnRenderFrameCompleted() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsOnRenderFrameCompleted);

	ForEachActiveSensor([](ITempoSensorInterface* Sensor)
	{
		Sensor->OnRenderCompleted();
	});
}

void UTempoSensorServiceSubsystem::OnWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld() || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
	{
		return;
	}

	// Flush all pending component transform updates to the render thread before rendering any sensor.
	// This is the last point in UWorld::Tick where all actor ticks (including TG_LastDemotable and
	// networking) have completed, so transforms are fully settled and velocity vectors will be correct.
	World->SendAllEndOfFrameUpdates();

	bool bWorldRenderingDisabled = false;
	if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
	{
		if (const ULocalPlayer* ClientPlayer = PlayerController->GetLocalPlayer())
		{
			if (const UGameViewportClient* ViewportClient = ClientPlayer->ViewportClient)
			{
				bWorldRenderingDisabled = ViewportClient->bDisableWorldRendering;
			}
		}
	}

	// Niagara runs GPU particle simulation ticks only in a render that isn't a scene capture. With the
	// main viewport off nothing renders but our captures, so the ticks queue until Niagara runs them on
	// its own every fx.Niagara.Batcher.TickFlush.MaxQueuedFrames frames (3 by default), and GPU
	// particles would hold still in the captures in between. Run them before this frame's captures.
	// This frame's ticks are already queued: Niagara submits them in OnWorldPostActorTick.
	if (bWorldRenderingDisabled)
	{
		bool bAnyCapturePending = false;
		ForEachActiveSensor([&bAnyCapturePending](ITempoSensorInterface* Sensor)
		{
			bAnyCapturePending |= Sensor->HasPendingCapture();
		});
		if (bAnyCapturePending)
		{
			if (FNiagaraGpuComputeDispatchInterface* NiagaraDispatch = FNiagaraGpuComputeDispatchInterface::Get(World))
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsFlushNiagaraGpuTicks);
				NiagaraDispatch->FlushPendingTicks_GameThread();
			}
		}
	}

	TArray<ITempoSensorInterface*> Sensors;
	ForEachActiveSensor([&Sensors](ITempoSensorInterface* Sensor)
	{
		Sensors.Add(Sensor);
	});

	// Poll the readbacks ahead of this frame's captures and behind each one. A fence that signals while
	// the render thread works through the captures is then read back between them. Otherwise a client
	// stepping between frames waits for a between-frames poll, whose command joins the render FIFO
	// behind every capture of this frame, as long as the whole frame takes to encode.
	EnqueueReadbackPoll(Sensors);
	const UTempoSensorsSettings* SensorsSettings = GetDefault<UTempoSensorsSettings>();
	const int32 MaxViewsPerFamily = SensorsSettings->GetMaxCamerasPerViewFamily();
	const int32 MaxTiledCamerasPerFamily = SensorsSettings->GetMaxTiledCamerasPerViewFamily();
	if ((MaxViewsPerFamily >= 2 || MaxTiledCamerasPerFamily >= 2) && World->Scene)
	{
		RenderCapturesGrouped(World->Scene, Sensors, MaxViewsPerFamily, MaxTiledCamerasPerFamily);
	}
	else
	{
		if (!LumenSceneOwners.IsEmpty())
		{
			ResetLumenSceneOwners();
		}
		for (ITempoSensorInterface* Sensor : Sensors)
		{
			if (!Sensor->HasPendingCapture())
			{
				continue;
			}
			Sensor->ExecutePendingCapture();
			EnqueueReadbackPoll(Sensors);
		}
	}

	// When the main viewport renders, the engine drains SceneCapturesToUpdateMap itself as part of
	// the main render path, and it does so with a valid MainViewFamily that our components can
	// sync jitter/Lumen state against. Only drain here when world rendering is disabled — otherwise
	// we'd preempt the engine and lose that sync.
	if (!bWorldRenderingDisabled)
	{
		return;
	}

	if (FSceneInterface* Scene = World->Scene)
	{
		USceneCaptureComponent::UpdateDeferredCaptures(Scene);
	}
}

namespace
{
	// Cameras farther apart than this are not grouped, even on one actor. A family shares one Lumen
	// scene, positioned and updated for its first view (one view origin for families of more than
	// two views), and other per-family state placed from the first view; that is only equivalent to
	// each camera rendering alone when the cameras sit close together, as on one vehicle or rig.
	constexpr double MaxGroupedCameraSeparation = 1000.0;

	// Slots in a family's atlas are this far apart in both directions (beyond the view size plus a
	// guard band). Several engine shaders seed blue noise, which repeats every 128 pixels, with the
	// absolute position in the scene textures; with strides that are multiples of 128 every view of
	// a family gets the same blue noise as when rendered alone.
	constexpr int32 GroupSlotAlignment = 128;
	constexpr int32 GroupSlotGuard = 64;
	constexpr int32 GroupAtlasColumns = 2;

	// Multi-tile cameras' tile atlases are blocks of their family's atlas, packed left to right in rows
	// no wider than this (a wider block gets a row of its own), at offsets aligned like the slots.
	constexpr int32 TiledGroupAtlasMaxRowWidth = 4096;

	// Smallest family to form. With exactly two views Lumen keeps one view origin per view, and in
	// 5.8 hardware-ray-traced radiosity then traces the second view's surface cache tiles in that
	// view's ray tracing scene with the first view's translation, offsetting every radiosity ray by
	// the distance between the two cameras, which darkens the second camera's bounce lighting. Families
	// of more than two views take Lumen's single-origin path and are not affected, nor is a pair when
	// radiosity is not hardware ray traced.
	int32 GetMinViewsPerFamily()
	{
		const IConsoleVariable* HardwareRayTracing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.HardwareRayTracing"));
		const IConsoleVariable* HardwareRadiosity = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LumenScene.Radiosity.HardwareRayTracing"));
		const bool bHardwareRadiosity = (!HardwareRayTracing || HardwareRayTracing->GetInt() != 0) && (!HardwareRadiosity || HardwareRadiosity->GetInt() != 0);
		return bHardwareRadiosity ? 3 : 2;
	}
}

bool UTempoSensorServiceSubsystem::CanShareFamily(UTempoCamera* Primary, UTempoCamera* Camera, const FTempoCameraPreparedCapture& PrimaryPrepared, const FTempoCameraPreparedCapture& Prepared)
{
	// Family-wide settings must agree; per-view ones (pose, projection, post-process, view state,
	// view owner, hidden lists, LOD and view distance) may differ.
	const UTextureRenderTarget2D* A = Primary->GetFinalTextureTarget();
	const UTextureRenderTarget2D* B = Camera->GetFinalTextureTarget();
	return A && B
		&& A->SizeX == B->SizeX && A->SizeY == B->SizeY
		&& A->GetFormat() == B->GetFormat()
		&& A->TargetGamma == B->TargetGamma
		&& A->IsSRGB() == B->IsSRGB()
		&& FMath::IsNearlyEqual(PrimaryPrepared.ResolutionFraction, Prepared.ResolutionFraction)
		&& Primary->ShowFlags.ToString() == Camera->ShowFlags.ToString()
		&& Primary->CompositeMode == Camera->CompositeMode
		&& Primary->bCaptureEveryFrame == Camera->bCaptureEveryFrame
		&& Primary->bAlwaysPersistRenderingState == Camera->bAlwaysPersistRenderingState
		&& Primary->bUseRayTracingIfEnabled == Camera->bUseRayTracingIfEnabled
		&& Primary->SceneViewExtensions.IsEmpty() && Camera->SceneViewExtensions.IsEmpty()
		&& !Primary->IsUnlit() && !Camera->IsUnlit()
		&& FVector::Dist(Primary->GetComponentLocation(), Camera->GetComponentLocation()) <= MaxGroupedCameraSeparation;
}

UTextureRenderTarget2D* UTempoSensorServiceSubsystem::GetGroupAtlas(int32 GroupIndex, const UTextureRenderTarget2D* Like, const FIntPoint& Size)
{
	TObjectPtr<UTextureRenderTarget2D>& Atlas = GroupAtlases.FindOrAdd(GroupIndex);
	if (!Atlas || Atlas->SizeX != Size.X || Atlas->SizeY != Size.Y || Atlas->RenderTargetFormat != Like->RenderTargetFormat || Atlas->TargetGamma != Like->TargetGamma)
	{
		// Same format and gamma as the cameras' own targets: the tonemapper writes the same values
		// into it, and the copy back moves them unchanged.
		Atlas = NewObject<UTextureRenderTarget2D>(this);
		Atlas->TargetGamma = Like->TargetGamma;
		Atlas->bGPUSharedFlag = Like->bGPUSharedFlag;
		Atlas->RenderTargetFormat = Like->RenderTargetFormat;
		Atlas->ClearColor = Like->ClearColor;
		Atlas->InitAutoFormat(Size.X, Size.Y);
		Atlas->UpdateResourceImmediate(true);
	}
	return Atlas;
}

void UTempoSensorServiceSubsystem::SetLumenSceneOwner(UTempoCamera* Camera, FSceneViewStateInterface* Owner)
{
	// Owner is only compared, never dereferenced here: the redirect itself is by the owner's id.
	FSceneViewStateInterface*& Current = LumenSceneOwners.FindOrAdd(Camera);
	if (Current != Owner)
	{
		Camera->ForEachViewState([Owner](FSceneViewStateInterface* ViewState)
		{
			TempoMultiViewCapture::SetLumenSceneOwner(ViewState, ViewState == Owner ? nullptr : Owner);
		});
		Current = Owner;
	}
}

void UTempoSensorServiceSubsystem::ResetLumenSceneOwners()
{
	for (const TPair<TWeakObjectPtr<UTempoCamera>, FSceneViewStateInterface*>& Pair : LumenSceneOwners)
	{
		if (UTempoCamera* Camera = Pair.Key.Get(); Camera && Pair.Value)
		{
			Camera->ForEachViewState([](FSceneViewStateInterface* ViewState)
			{
				TempoMultiViewCapture::SetLumenSceneOwner(ViewState, nullptr);
			});
		}
	}
	LumenSceneOwners.Reset();
}

void UTempoSensorServiceSubsystem::RenderCapturesGrouped(FSceneInterface* Scene, TConstArrayView<ITempoSensorInterface*> Sensors, int32 MaxViews, int32 MaxTiledCameras)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsRenderCapturesGrouped);
	if (MaxViews != LastMaxViews || MaxTiledCameras != LastMaxTiledCameras)
	{
		// Slots, and with them atlas sizes, depend on the family size.
		LastMaxViews = MaxViews;
		LastMaxTiledCameras = MaxTiledCameras;
		GroupSlots.Reset();
		Groups.Reset();
	}
	// A camera that switched between the single-tile and the multi-tile path since it was grouped
	// would land in a group of the other kind; start the groups over.
	for (ITempoSensorInterface* Sensor : Sensors)
	{
		UTempoCamera* Camera = Cast<UTempoCamera>(Sensor);
		const FGroupSlot* Slot = Camera ? GroupSlots.Find(Camera) : nullptr;
		if (Slot && Groups[Slot->Group].bTiled == Camera->IsSingleTileFastPath())
		{
			GroupSlots.Reset();
			Groups.Reset();
			break;
		}
	}

	// A camera keeps its group and slot from frame to frame, so its view keeps its place in the
	// scene textures (its temporal history depends on it). A camera joins the newest group of its
	// actor that has room, if that group's first camera is close enough, or starts a new one.
	// Single-tile cameras and multi-tile cameras group separately: a single-tile camera is one view
	// rendered straight into its final target, a multi-tile camera all of its tiles rendered into its
	// HDR tile atlas.
	TArray<UTempoCamera*> Candidates;
	for (ITempoSensorInterface* Sensor : Sensors)
	{
		UTempoCamera* Camera = Cast<UTempoCamera>(Sensor);
		if (!Camera || !Sensor->HasPendingCapture() || !Camera->GetOwner())
		{
			continue;
		}
		const bool bTiled = !Camera->IsSingleTileFastPath();
		const int32 MaxSlots = bTiled ? MaxTiledCameras : MaxViews;
		if (MaxSlots < 2 || (bTiled && !Camera->GetTilesTextureTarget()))
		{
			continue;
		}
		if (!GroupSlots.Contains(Camera))
		{
			int32 GroupIndex = INDEX_NONE;
			for (int32 Index = Groups.Num() - 1; Index >= 0; --Index)
			{
				const FCameraGroup& Group = Groups[Index];
				if (Group.Owner.Get() == Camera->GetOwner() && Group.bTiled == bTiled)
				{
					const UTempoCamera* First = Group.First.Get();
					if (Group.NumSlots < MaxSlots && First
						&& FVector::Dist(First->GetComponentLocation(), Camera->GetComponentLocation()) <= MaxGroupedCameraSeparation)
					{
						GroupIndex = Index;
					}
					break;
				}
			}
			if (GroupIndex == INDEX_NONE)
			{
				GroupIndex = Groups.Num();
				FCameraGroup& NewGroup = Groups.AddDefaulted_GetRef();
				NewGroup.Owner = Camera->GetOwner();
				NewGroup.First = Camera;
				NewGroup.bTiled = bTiled;
			}
			FCameraGroup& Group = Groups[GroupIndex];
			GroupSlots.Add(Camera, { GroupIndex, Group.NumSlots++ });
			Group.Members.Add(Camera);
		}
		Candidates.Add(Camera);
	}

	// The cameras that render in a family this frame: at least GetMinViewsPerFamily of a single-tile
	// group, at least two of a multi-tile group (which RenderTiledFamily checks for enough views).
	const int32 MinViews = GetMinViewsPerFamily();
	TMap<int32, TArray<UTempoCamera*>> Families;
	for (UTempoCamera* Camera : Candidates)
	{
		Families.FindOrAdd(GroupSlots[Camera].Group).Add(Camera);
	}
	for (auto It = Families.CreateIterator(); It; ++It)
	{
		if (It.Value().Num() < (Groups[It.Key()].bTiled ? 2 : MinViews))
		{
			It.RemoveCurrent();
		}
	}

	// Sensors render in their order; a family renders where its first member comes, through a scene
	// render builder of its own, as every other capture does.
	TSet<int32> Rendered;
	for (ITempoSensorInterface* Sensor : Sensors)
	{
		if (!Sensor->HasPendingCapture())
		{
			continue;
		}
		UTempoCamera* Camera = Cast<UTempoCamera>(Sensor);
		const FGroupSlot* Slot = Camera ? GroupSlots.Find(Camera) : nullptr;
		TArray<UTempoCamera*>* Family = Slot ? Families.Find(Slot->Group) : nullptr;
		if (!Family)
		{
			if (Camera && (Camera->IsSingleTileFastPath() || LumenSceneOwners.Contains(Camera)))
			{
				// Its view states may still trace another camera's Lumen scene from an earlier family render.
				SetLumenSceneOwner(Camera, nullptr);
			}
			Sensor->ExecutePendingCapture();
			EnqueueReadbackPoll(Sensors);
			continue;
		}
		if (Rendered.Contains(Slot->Group))
		{
			continue;
		}
		Rendered.Add(Slot->Group);
		if (Groups[Slot->Group].bTiled)
		{
			RenderTiledFamily(Scene, Slot->Group, *Family);
		}
		else
		{
			RenderFamily(Scene, Slot->Group, *Family, MaxViews);
		}
		EnqueueReadbackPoll(Sensors);
	}
}

void UTempoSensorServiceSubsystem::RenderFamily(FSceneInterface* Scene, int32 GroupIndex, TConstArrayView<UTempoCamera*> Cameras, int32 MaxViews)
{
	TUniquePtr<ISceneRenderBuilder> Builder = ISceneRenderBuilder::Create(Scene);
	if (!Builder.IsValid())
	{
		return;
	}
	TempoMultiViewCapture::FCaptureSink Sink(Builder.Get());

	struct FMember
	{
		UTempoCamera* Camera = nullptr;
		TUniquePtr<FTempoCameraPreparedCapture> Prepared;
	};
	TArray<FMember> Members;
	for (UTempoCamera* Camera : Cameras)
	{
		if (!Camera->ConsumePendingCapture())
		{
			continue;
		}
		TUniquePtr<FTempoCameraPreparedCapture> Prepared = MakeUnique<FTempoCameraPreparedCapture>();
		if (!Camera->PrepareCapture(*Prepared) || !Prepared->bSingleTileFastPath || Prepared->ViewSetups.Num() != 1)
		{
			continue;
		}
		if (!Members.IsEmpty() && !CanShareFamily(Members[0].Camera, Camera, *Members[0].Prepared, *Prepared))
		{
			// Renders alone, as it would without grouping.
			SetLumenSceneOwner(Camera, nullptr);
			TempoMultiViewCapture::FCaptureSink Immediate;
			Camera->RenderPreparedCapture(*Prepared, Immediate);
			continue;
		}
		Members.Add({ Camera, MoveTemp(Prepared) });
	}
	if (Members.IsEmpty())
	{
		return;
	}

	const UTextureRenderTarget2D* PrimaryTarget = Members[0].Camera->GetFinalTextureTarget();
	const FIntPoint ViewSize(PrimaryTarget->SizeX, PrimaryTarget->SizeY);
	const FIntPoint Stride(
		Align(ViewSize.X + GroupSlotGuard, GroupSlotAlignment),
		Align(ViewSize.Y + GroupSlotGuard, GroupSlotAlignment));
	const int32 Columns = FMath::Min(GroupAtlasColumns, MaxViews);
	const int32 Rows = FMath::DivideAndRoundUp(MaxViews, Columns);
	const FIntPoint AtlasSize((Columns - 1) * Stride.X + ViewSize.X, (Rows - 1) * Stride.Y + ViewSize.Y);
	UTextureRenderTarget2D* Atlas = GetGroupAtlas(GroupIndex, PrimaryTarget, AtlasSize);

	TArray<TempoMultiViewCapture::FViewSetup> Views;
	TArray<TempoMultiViewCapture::FAtlasRectCopy> Copies;
	FSceneViewStateInterface* LumenOwner = Members[0].Prepared->ViewSetups[0].ViewState;
	float MaxExtrapolationFactor = 1.0f;
	for (FMember& Member : Members)
	{
		const int32 SlotIndex = GroupSlots[Member.Camera].Slot;
		const FIntPoint Offset((SlotIndex % Columns) * Stride.X, (SlotIndex / Columns) * Stride.Y);
		TempoMultiViewCapture::FViewSetup View = Member.Prepared->ViewSetups[0];
		View.ViewRect += Offset;
		// Each camera keeps its own exposure, as when it renders alone.
		View.ExposureViewState = View.ViewState;
		View.OwnerComponent = Member.Camera;
		Views.Add(View);
		Copies.Add({ View.ViewRect, Member.Camera->GetFinalTextureTarget() });
		// Lumen updates only the first view's scene in a family; every view traces that one.
		SetLumenSceneOwner(Member.Camera, Member.Camera == Members[0].Camera ? nullptr : LumenOwner);
		MaxExtrapolationFactor = FMath::Max(MaxExtrapolationFactor, Member.Prepared->MaxExtrapolationFactor);
		Member.Camera->SetCaptureExtensionsActive(*Member.Prepared, true, true);
	}
	{
		// The fog history weight is one setting for the family; members capture together, so their
		// factors only differ when one was just activated.
		TempoMultiViewCapture::FScopedVolumetricFogHistoryRescale ScopedFogHistory(MaxExtrapolationFactor, Builder.Get());
		TempoMultiViewCapture::AddTilesRenderer(*Builder, Scene, Members[0].Camera, Atlas, Views, ESceneCaptureSource::SCS_FinalColorLDR,
			Members[0].Prepared->ResolutionFraction, Copies);
	}
	for (FMember& Member : Members)
	{
		Member.Camera->SetCaptureExtensionsActive(*Member.Prepared, false, true);
		Member.Camera->FinishCapture(*Member.Prepared, Sink);
	}
	Builder->Execute();
}

bool UTempoSensorServiceSubsystem::CanShareTiledFamily(UTempoCamera* Primary, UTempoCamera* Camera, const FTempoCameraPreparedCapture& PrimaryPrepared, const FTempoCameraPreparedCapture& Prepared)
{
	// As CanShareFamily, for the cameras' tile atlases (their sizes may differ: each is its own block).
	const UTextureRenderTarget2D* A = Primary->GetTilesTextureTarget();
	const UTextureRenderTarget2D* B = Camera->GetTilesTextureTarget();
	return A && B
		&& !PrimaryPrepared.bSingleTileFastPath && !Prepared.bSingleTileFastPath
		&& A->GetFormat() == B->GetFormat()
		&& A->TargetGamma == B->TargetGamma
		&& A->IsSRGB() == B->IsSRGB()
		&& FMath::IsNearlyEqual(PrimaryPrepared.ResolutionFraction, Prepared.ResolutionFraction)
		&& Primary->ShowFlags.ToString() == Camera->ShowFlags.ToString()
		&& Primary->CompositeMode == Camera->CompositeMode
		&& Primary->bCaptureEveryFrame == Camera->bCaptureEveryFrame
		&& Primary->bAlwaysPersistRenderingState == Camera->bAlwaysPersistRenderingState
		&& Primary->bUseRayTracingIfEnabled == Camera->bUseRayTracingIfEnabled
		&& Primary->SceneViewExtensions.IsEmpty() && Camera->SceneViewExtensions.IsEmpty()
		&& !Primary->IsUnlit() && !Camera->IsUnlit()
		&& FVector::Dist(Primary->GetComponentLocation(), Camera->GetComponentLocation()) <= MaxGroupedCameraSeparation;
}

void UTempoSensorServiceSubsystem::RenderTiledFamily(FSceneInterface* Scene, int32 GroupIndex, TConstArrayView<UTempoCamera*> Cameras)
{
	TUniquePtr<ISceneRenderBuilder> Builder = ISceneRenderBuilder::Create(Scene);
	if (!Builder.IsValid())
	{
		return;
	}

	struct FMember
	{
		UTempoCamera* Camera = nullptr;
		TUniquePtr<FTempoCameraPreparedCapture> Prepared;
	};
	TArray<FMember> Members;
	TArray<FMember> Alone;
	int32 NumViews = 0;
	for (UTempoCamera* Camera : Cameras)
	{
		if (!Camera->ConsumePendingCapture())
		{
			continue;
		}
		TUniquePtr<FTempoCameraPreparedCapture> Prepared = MakeUnique<FTempoCameraPreparedCapture>();
		if (!Camera->PrepareCapture(*Prepared))
		{
			continue;
		}
		if (Prepared->bSingleTileFastPath || (!Members.IsEmpty() && !CanShareTiledFamily(Members[0].Camera, Camera, *Members[0].Prepared, *Prepared)))
		{
			Alone.Add({ Camera, MoveTemp(Prepared) });
			continue;
		}
		NumViews += Prepared->ViewSetups.Num();
		Members.Add({ Camera, MoveTemp(Prepared) });
	}
	if (Members.Num() < 2 || NumViews < GetMinViewsPerFamily())
	{
		Alone.Append(MoveTemp(Members));
		Members.Reset();
	}
	for (FMember& Member : Alone)
	{
		// Renders alone, as it would without grouping.
		SetLumenSceneOwner(Member.Camera, nullptr);
		TempoMultiViewCapture::FCaptureSink Immediate;
		Member.Camera->RenderPreparedCapture(*Member.Prepared, Immediate);
	}
	if (Members.IsEmpty())
	{
		return;
	}

	// The family's Lumen scene is its first view's: Lumen places it at that view and updates it for
	// that view alone (a family of more than two views takes its single-origin path), and every view
	// traces it. A camera whose view is not first can differ from its own render where it sees
	// surfaces lit from that scene, so the first camera is the one nearest to all the others: the
	// member whose farthest fellow member is closest (the earliest slot among equals).
	const TArray<TWeakObjectPtr<UTempoCamera>>& GroupMembers = Groups[GroupIndex].Members;
	const UTempoCamera* Central = nullptr;
	{
		double BestFarthest = TNumericLimits<double>::Max();
		for (const TWeakObjectPtr<UTempoCamera>& Weak : GroupMembers)
		{
			const UTempoCamera* Camera = Weak.Get();
			if (!Camera)
			{
				continue;
			}
			double Farthest = 0.0;
			for (const TWeakObjectPtr<UTempoCamera>& Other : GroupMembers)
			{
				if (const UTempoCamera* OtherCamera = Other.Get())
				{
					Farthest = FMath::Max(Farthest, FVector::Dist(Camera->GetComponentLocation(), OtherCamera->GetComponentLocation()));
				}
			}
			if (Farthest < BestFarthest)
			{
				BestFarthest = Farthest;
				Central = Camera;
			}
		}
	}
	if (const int32 CentralIndex = Members.IndexOfByPredicate([Central](const FMember& Member) { return Member.Camera == Central; }); CentralIndex > 0)
	{
		Members.Swap(0, CentralIndex);
	}

	// Each camera's tile atlas is one block of the family's atlas, placed by the group's members,
	// whether or not they capture this frame, so a camera's block stays where it is: the first camera's
	// at the origin, then the others in slot order.
	TMap<const UTempoCamera*, FIntPoint> Offsets;
	FIntPoint AtlasSize = FIntPoint::ZeroValue;
	{
		TArray<const UTempoCamera*> Order;
		if (Central)
		{
			Order.Add(Central);
		}
		for (const TWeakObjectPtr<UTempoCamera>& Weak : GroupMembers)
		{
			if (const UTempoCamera* Camera = Weak.Get(); Camera && Camera != Central)
			{
				Order.Add(Camera);
			}
		}
		FIntPoint Cursor = FIntPoint::ZeroValue;
		int32 RowHeight = 0;
		for (const UTempoCamera* Camera : Order)
		{
			const UTextureRenderTarget2D* Tiles = Camera->GetTilesTextureTarget();
			if (!Tiles)
			{
				continue;
			}
			const FIntPoint Size(Tiles->SizeX, Tiles->SizeY);
			if (Cursor.X > 0 && Cursor.X + Size.X > TiledGroupAtlasMaxRowWidth)
			{
				Cursor = FIntPoint(0, Cursor.Y + RowHeight);
				RowHeight = 0;
			}
			Offsets.Add(Camera, Cursor);
			AtlasSize = AtlasSize.ComponentMax(Cursor + Size);
			Cursor.X += Align(Size.X + GroupSlotGuard, GroupSlotAlignment);
			RowHeight = FMath::Max(RowHeight, Align(Size.Y + GroupSlotGuard, GroupSlotAlignment));
		}
	}
	UTextureRenderTarget2D* Atlas = GetGroupAtlas(GroupIndex, Members[0].Camera->GetTilesTextureTarget(), AtlasSize);

	TArray<TempoMultiViewCapture::FViewSetup> Views;
	TArray<TempoMultiViewCapture::FAtlasRectCopy> Copies;
	TArray<FEngineShowFlags> SavedShowFlags;
	FSceneViewStateInterface* LumenOwner = Members[0].Prepared->ViewSetups[0].ViewState;
	float MaxExtrapolationFactor = 1.0f;
	for (FMember& Member : Members)
	{
		const FIntPoint Offset = Offsets.FindRef(Member.Camera);
		// A camera's tiles share its first tile's exposure, as when it renders alone.
		FSceneViewStateInterface* ExposureViewState = Member.Prepared->ViewSetups[0].ViewState;
		for (const TempoMultiViewCapture::FViewSetup& Setup : Member.Prepared->ViewSetups)
		{
			TempoMultiViewCapture::FViewSetup& View = Views.Add_GetRef(Setup);
			View.ViewRect += Offset;
			View.ExposureViewState = ExposureViewState;
			View.OwnerComponent = Member.Camera;
		}
		UTextureRenderTarget2D* Tiles = Member.Camera->GetTilesTextureTarget();
		Copies.Add({ FIntRect(Offset, Offset + FIntPoint(Tiles->SizeX, Tiles->SizeY)), Tiles });
		// Lumen updates only the first view's scene in a family; every view traces that one.
		SetLumenSceneOwner(Member.Camera, LumenOwner);
		MaxExtrapolationFactor = FMath::Max(MaxExtrapolationFactor, Member.Prepared->MaxExtrapolationFactor);
		SavedShowFlags.Add(Member.Camera->ShowFlags);
		Member.Camera->ApplyTileShowFlags();
		Member.Camera->SetCaptureExtensionsActive(*Member.Prepared, true, true);
	}
	{
		TempoMultiViewCapture::FScopedVolumetricFogHistoryRescale ScopedFogHistory(MaxExtrapolationFactor, Builder.Get());
		TempoMultiViewCapture::AddTilesRenderer(*Builder, Scene, Members[0].Camera, Atlas, Views, ESceneCaptureSource::SCS_FinalColorHDR,
			Members[0].Prepared->ResolutionFraction, Copies);
	}
	for (int32 Index = 0; Index < Members.Num(); ++Index)
	{
		Members[Index].Camera->SetCaptureExtensionsActive(*Members[Index].Prepared, false, true);
		Members[Index].Camera->ShowFlags = SavedShowFlags[Index];
	}
	Builder->Execute();

	// Then each camera's stitch, exposure meter, proxy and read, in that order, as when it renders
	// alone. A multi-tile camera renders its proxy at once, not through a sink, so the stitch it
	// reads must already be queued behind the family's render.
	Members.Sort([&Cameras](const FMember& A, const FMember& B) { return Cameras.IndexOfByKey(A.Camera) < Cameras.IndexOfByKey(B.Camera); });
	for (FMember& Member : Members)
	{
		TempoMultiViewCapture::FCaptureSink Immediate;
		Member.Camera->AddStitch(Immediate);
		Member.Camera->FinishCapture(*Member.Prepared, Immediate);
	}
}

void UTempoSensorServiceSubsystem::OnWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World == GetWorld() && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE))
	{
		// In non-pipelined fixed step mode we block the game thread on any pending measurements.
		// This guarantees they will be sent out in the same frame when they were captured.
		// In pipelined mode we skip this block, allowing images to arrive 1-2 frames late.
		if (GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep
			&& !GetDefault<UTempoSensorsSettings>()->GetPipelinedRendering())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsWaitForMeasurements);
			ForEachActiveSensor([](const ITempoSensorInterface* Sensor)
			{
				Sensor->BlockUntilMeasurementsReady();
			});
		}

		// Finish the writes in flight first, once the between-frames decode tasks have issued theirs. A
		// stream asks for its next measurement only once its last response is written, and this frame's
		// server tick, which would finish them, runs after this send. A readback sent between frames whose write is still in flight, when the wait for the
		// client's next Step times out and this frame runs paused, would otherwise leave the next
		// readback with no request here, and it would be dropped. Only write completions are handled:
		// no new request runs inside the world tick, and a stream's re-invoked handler only registers
		// its next request (see FTempoServer::FinishWrites). It waits as long as the writes take, as the
		// server tick that follows would, up to Tempo.WriteStallTimeoutMs: a client that dropped a
		// stream without cancelling it never accepts its write, and the wait would hold its next Step.
		WaitForDecodesInFlight();
		if (GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep)
		{
			FTempoServer& Server = FTempoServer::Get();
			if (Server.HasUnflushedWork())
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsFinishWritesBeforeSend);
				Server.FinishWrites();
			}
		}

		SendCompletedMeasurements();
	}
}

namespace
{
	// Sends every measurement whose readback has completed and waits for the decode tasks, so that
	// the frame's measurements are all on their way when it proceeds.
	void SendMeasurementsAndWait(TConstArrayView<ITempoSensorInterface*> Sensors)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsSendMeasurements);
		TArray<TFuture<void>> SendMeasurementsTasks;
		for (ITempoSensorInterface* Sensor : Sensors)
		{
			if (TOptional<TFuture<void>> Future = Sensor->SendMeasurements())
			{
				SendMeasurementsTasks.Add(MoveTemp(Future.GetValue()));
			}
		}
		for (const TFuture<void>& SendMeasurementsTask : SendMeasurementsTasks)
		{
			SendMeasurementsTask.Wait();
		}
	}
}

void UTempoSensorServiceSubsystem::SendCompletedMeasurements()
{
	TArray<ITempoSensorInterface*> Sensors;
	ForEachActiveSensor([&Sensors](ITempoSensorInterface* Sensor)
	{
		Sensors.Add(Sensor);
	});
	SendMeasurementsAndWait(Sensors);
}

void UTempoSensorServiceSubsystem::WaitForDecodesInFlight()
{
	for (const TPair<TWeakObjectPtr<UActorComponent>, TFuture<void>>& Decode : DecodesInFlight)
	{
		Decode.Value.Wait();
	}
	DecodesInFlight.Empty();
	ForEachActiveSensor([](ITempoSensorInterface* Sensor)
	{
		Sensor->WaitForDecodes();
	});
}

void UTempoSensorServiceSubsystem::OnBetweenFramesPoll(UWorld* World)
{
	if (World != GetWorld())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsBetweenFramesPoll);

	if (BetweenFramesSensorsFrame != GFrameCounter)
	{
		BetweenFramesSensorsFrame = GFrameCounter;
		BetweenFramesSensors.Reset();
		ForEachActiveSensor([this](ITempoSensorInterface* Sensor)
		{
			BetweenFramesSensors.Add(Cast<UActorComponent>(Sensor));
		});
	}

	TArray<ITempoSensorInterface*> Sensors;
	for (const TWeakObjectPtr<UActorComponent>& Component : BetweenFramesSensors)
	{
		if (ITempoSensorInterface* Sensor = Cast<ITempoSensorInterface>(Component.Get()))
		{
			Sensors.Add(Sensor);
		}
	}

	// Readbacks are otherwise polled only at the render thread's end of frame, and a client advancing
	// steps holds the next frame off for many of them. Poll from a render command instead; it is
	// non-blocking, exactly like the end-of-frame poll. Where the readback watcher runs, it reads a
	// fence as it signals and no poll is needed between frames.
	if (!FTempoReadbackWatcher::IsActive() && ReadbackPollsQueued->load() == 0)
	{
		EnqueueReadbackPoll(Sensors);
	}

	// Only sensors with a request waiting. A capture can be taken on a request that an earlier capture's
	// readback then answers, and a stream asks again only once that response is written, so its own
	// readback can complete with no request yet. Sent then, it would be dropped. Kept, it goes to the
	// stream's next request, or to the next game frame's send, so nothing is held past a game frame.
	Sensors.RemoveAll([](const ITempoSensorInterface* Sensor) { return !Sensor->HasPendingMeasurementRequests(); });

	// Launch the decode tasks and go on: they respond from worker threads (the stream's request
	// manager serializes that against the game thread's write completions), and the client's next
	// Step is served while they run instead of after them. One per sensor at a time keeps a stream's
	// responses in read order; a sensor whose previous decode is still running is sent next poll.
	DecodesInFlight.RemoveAll([](const TPair<TWeakObjectPtr<UActorComponent>, TFuture<void>>& Decode) { return Decode.Value.IsReady(); });
	for (ITempoSensorInterface* Sensor : Sensors)
	{
		UActorComponent* Component = Cast<UActorComponent>(Sensor);
		if (DecodesInFlight.ContainsByPredicate([Component](const TPair<TWeakObjectPtr<UActorComponent>, TFuture<void>>& Decode) { return Decode.Key == Component; }))
		{
			continue;
		}
		if (TOptional<TFuture<void>> Future = Sensor->SendMeasurements())
		{
			DecodesInFlight.Emplace(Component, MoveTemp(Future.GetValue()));
		}
	}
}

void UTempoSensorServiceSubsystem::EnqueueReadbackPoll(TConstArrayView<ITempoSensorInterface*> Sensors)
{
	TArray<ITempoSensorInterface*> AwaitingRender;
	for (ITempoSensorInterface* Sensor : Sensors)
	{
		if (Sensor->IsAwaitingRender())
		{
			AwaitingRender.Add(Sensor);
		}
	}
	if (AwaitingRender.IsEmpty())
	{
		return;
	}

	// A sensor is only destroyed after the render thread has drained the commands queued before its
	// destruction began, so the raw pointers outlive this command.
	ReadbackPollsQueued->fetch_add(1);
	ENQUEUE_RENDER_COMMAND(TempoReadbackPoll)(
		[AwaitingRender = MoveTemp(AwaitingRender), Queued = ReadbackPollsQueued](FRHICommandListImmediate& RHICmdList)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsReadbackPoll);

			// A fence only reads as complete once the RHI has processed the GPU's completed work.
			// An RHI without an interrupt thread (Vulkan without timeline semaphores or with few
			// task threads) does that only when it submits, and between frames nothing submits.
			// A dispatch always ends in a submit, even with nothing recorded, which processes
			// whatever has completed, so the next poll sees it. It never waits on the GPU.
			RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);

			for (ITempoSensorInterface* Sensor : AwaitingRender)
			{
				Sensor->OnRenderCompleted();
			}
			Queued->fetch_sub(1);
		});
}

TempoSensors::MeasurementType ToProtoMeasurementType(EMeasurementType ImageType)
{
	switch (ImageType)
	{
	case EMeasurementType::COLOR_IMAGE:
		{
			return TempoSensors::MT_COLOR_IMAGE;
		}
	case EMeasurementType::DEPTH_IMAGE:
		{
			return TempoSensors::MT_DEPTH_IMAGE;
		}
	case EMeasurementType::LABEL_IMAGE:
		{
			return TempoSensors::MT_LABEL_IMAGE;
		}
	case EMeasurementType::LIDAR_SCAN:
		{
			return TempoSensors::MT_LIDAR_SCAN;
		}
	case EMeasurementType::BOUNDING_BOXES:
		{
			return TempoSensors::MT_BOUNDING_BOXES;
		}
	case EMeasurementType::VIDEO:
		{
			return TempoSensors::MT_VIDEO;
		}
	default:
		{
			checkf(false, TEXT("Unhandled measurement type"));
			return TempoSensors::MT_UNKNOWN;
		}
	}
}

void UTempoSensorServiceSubsystem::ForEachActiveSensor(const TFunction<void(ITempoSensorInterface*)>& Callback) const
{
	check(GetWorld());

	for (TObjectIterator<UActorComponent> ComponentIt; ComponentIt; ++ComponentIt)
	{
		UActorComponent* Component = *ComponentIt;
		if (!Component->IsActive())
		{
			continue;
		}
		if (ITempoSensorInterface* Sensor = Cast<ITempoSensorInterface>(Component))
		{
			if (IsValid(Component) && Component->GetWorld() == GetWorld())
			{
				Callback(Sensor);
			}
		}
	}
}

void UTempoSensorServiceSubsystem::GetAvailableSensors(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::AvailableSensorsResponse>& ResponseContinuation) const
{
	AvailableSensorsResponse Response;

	ForEachActiveSensor([&Response](const ITempoSensorInterface* Sensor)
	{
		auto* AvailableSensor = Response.add_available_sensors();
		AvailableSensor->set_owner(TCHAR_TO_UTF8(*Sensor->GetOwnerName()));
		AvailableSensor->set_name(TCHAR_TO_UTF8(*Sensor->GetSensorName()));
		AvailableSensor->set_rate_hz(Sensor->GetRate());
		for (const EMeasurementType MeasurementType : Sensor->GetMeasurementTypes())
		{
			AvailableSensor->add_measurement_types(ToProtoMeasurementType(MeasurementType));
		}
	});

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

template <typename RequestType, typename ResponseType>
void UTempoSensorServiceSubsystem::RequestImages(const RequestType& Request, const TResponseDelegate<ResponseType>& ResponseContinuation) const
{
	check(GetWorld());

	TMap<FString, TArray<UTempoCamera*>> OwnersToComponents;
	for (TObjectIterator<UTempoCamera> ComponentIt; ComponentIt; ++ComponentIt)
	{
		if (IsValid(*ComponentIt) && ComponentIt->GetWorld() == GetWorld())
		{
			OwnersToComponents.FindOrAdd(ComponentIt->GetOwnerName()).Add(*ComponentIt);
		}
	}

	const FString RequestedOwnerName(UTF8_TO_TCHAR(Request.owner().c_str()));
	const FString RequestedSensorName(UTF8_TO_TCHAR(Request.sensor().c_str()));

	// If owner name is not specified and only one owner has this sensor name, assume the client wants that owner
	if (RequestedOwnerName.IsEmpty())
	{
		if (OwnersToComponents.IsEmpty())
		{
			ResponseContinuation.ExecuteIfBound(ResponseType(), grpc::Status(grpc::StatusCode::NOT_FOUND, "No sensors found"));
			return;
		}

		UTempoCamera* FoundComponent = nullptr;
		for (const auto& OwnerComponents : OwnersToComponents)
		{
			const TArray<UTempoCamera*>& Components = OwnerComponents.Value;
			for (UTempoCamera* Component : Components)
			{
				if (Component->GetSensorName().Equals(RequestedSensorName, ESearchCase::IgnoreCase))
				{
					if (FoundComponent)
					{
						ResponseContinuation.ExecuteIfBound(ResponseType(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "More than one owner with specified sensor name found, owner name required."));
						return;
					}
					FoundComponent = Component;
				}
			}
		}

		if (!FoundComponent)
		{
			ResponseContinuation.ExecuteIfBound(ResponseType(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Did not find a sensor with the specified name"));
			return;
		}

		FoundComponent->RequestMeasurement(Request, ResponseContinuation);
		return;
	}

	for (const auto& OwnerComponents : OwnersToComponents)
	{
		const FString& OwnerName = OwnerComponents.Key;
		const TArray<UTempoCamera*>& Components = OwnerComponents.Value;
		if (OwnerName.Equals(RequestedOwnerName, ESearchCase::IgnoreCase))
		{
			for (UTempoCamera* Component : Components)
			{
				if (Component->GetSensorName().Equals(RequestedSensorName, ESearchCase::IgnoreCase))
				{
					Component->RequestMeasurement(Request, ResponseContinuation);
					return;
				}
			}
		}
	}

	ResponseContinuation.ExecuteIfBound(ResponseType(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Did not find a sensor with the specified owner and name"));
	return;
}

void UTempoSensorServiceSubsystem::StreamColorImages(const TempoSensors::ColorImageRequest& Request, const TResponseDelegate<TempoSensors::ColorImage>& ResponseContinuation) const
{
	RequestImages<TempoSensors::ColorImageRequest, TempoSensors::ColorImage>(Request, ResponseContinuation);
}

void UTempoSensorServiceSubsystem::StreamDepthImages(const TempoSensors::DepthImageRequest& Request, const TResponseDelegate<TempoSensors::DepthImage>& ResponseContinuation) const
{
	RequestImages<TempoSensors::DepthImageRequest, TempoSensors::DepthImage>(Request, ResponseContinuation);
}

void UTempoSensorServiceSubsystem::StreamLabelImages(const TempoSensors::LabelImageRequest& Request, const TResponseDelegate<TempoSensors::LabelImage>& ResponseContinuation) const
{
	RequestImages<TempoSensors::LabelImageRequest, TempoSensors::LabelImage>(Request, ResponseContinuation);
}

void UTempoSensorServiceSubsystem::StreamBoundingBoxes(const TempoSensors::BoundingBoxesRequest& Request, const TResponseDelegate<TempoSensors::BoundingBoxes>& ResponseContinuation) const
{
	RequestImages<TempoSensors::BoundingBoxesRequest, TempoSensors::BoundingBoxes>(Request, ResponseContinuation);
}

void UTempoSensorServiceSubsystem::StreamVideo(const TempoSensors::VideoRequest& Request, const TResponseDelegate<TempoSensors::VideoFrame>& ResponseContinuation) const
{
	RequestImages<TempoSensors::VideoRequest, TempoSensors::VideoFrame>(Request, ResponseContinuation);
}

void UTempoSensorServiceSubsystem::StreamLidarScans(const TempoSensors::LidarScanRequest& Request, const TResponseDelegate<TempoSensors::LidarScanSegment>& ResponseContinuation) const
{
	check(GetWorld());

	TMap<FString, TArray<UTempoLidar*>> OwnersToComponents;
	for (TObjectIterator<UTempoLidar> ComponentIt; ComponentIt; ++ComponentIt)
	{
		if (IsValid(*ComponentIt) && ComponentIt->GetWorld() == GetWorld())
		{
			OwnersToComponents.FindOrAdd(ComponentIt->GetOwnerName()).Add(*ComponentIt);
		}
	}

	const FString RequestedOwnerName(UTF8_TO_TCHAR(Request.owner().c_str()));
	const FString RequestedSensorName(UTF8_TO_TCHAR(Request.sensor().c_str()));

	// If owner name is not specified and only one owner has this sensor name, assume the client wants that owner
	if (RequestedOwnerName.IsEmpty())
	{
		if (OwnersToComponents.IsEmpty())
		{
			ResponseContinuation.ExecuteIfBound(LidarScanSegment(), grpc::Status(grpc::StatusCode::NOT_FOUND, "No sensors found"));
			return;
		}

		UTempoLidar* FoundComponent = nullptr;
		for (const auto& OwnerComponents : OwnersToComponents)
		{
			const TArray<UTempoLidar*>& Components = OwnerComponents.Value;
			for (UTempoLidar* Component : Components)
			{
				if (Component->GetSensorName().Equals(RequestedSensorName, ESearchCase::IgnoreCase))
				{
					if (FoundComponent)
					{
						ResponseContinuation.ExecuteIfBound(LidarScanSegment(), grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "More than one owner with specified sensor name found, owner name required."));
						return;
					}
					FoundComponent = Component;
				}
			}
		}

		if (!FoundComponent)
		{
			ResponseContinuation.ExecuteIfBound(LidarScanSegment(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Did not find a sensor with the specified name"));
			return;
		}

		FoundComponent->RequestMeasurement(Request, ResponseContinuation);
		return;
	}

	for (const auto& OwnerComponents : OwnersToComponents)
	{
		const FString& OwnerName = OwnerComponents.Key;
		const TArray<UTempoLidar*>& Components = OwnerComponents.Value;
		if (OwnerName.Equals(RequestedOwnerName, ESearchCase::IgnoreCase))
		{
			for (UTempoLidar* Component : Components)
			{
				if (Component->GetSensorName().Equals(RequestedSensorName, ESearchCase::IgnoreCase))
				{
					Component->RequestMeasurement(Request, ResponseContinuation);
					return;
				}
			}
		}
	}

	ResponseContinuation.ExecuteIfBound(LidarScanSegment(), grpc::Status(grpc::StatusCode::NOT_FOUND, "Did not find a sensor with the specified owner and name"));
}
