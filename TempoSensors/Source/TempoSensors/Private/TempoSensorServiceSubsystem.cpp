// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorServiceSubsystem.h"

#include "TempoSensorInterface.h"
#include "TempoSensors/Sensors.grpc.pb.h"

#include "TempoCamera.h"
#include "TempoLidar.h"
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
	for (ITempoSensorInterface* Sensor : Sensors)
	{
		if (!Sensor->HasPendingCapture())
		{
			continue;
		}
		Sensor->ExecutePendingCapture();
		EnqueueReadbackPoll(Sensors);
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

		// Finish the writes in flight first. A stream asks for its next measurement only once its last
		// response is written, and this frame's server tick, which would finish them, runs after this
		// send. A readback sent between frames whose write is still in flight, when the wait for the
		// client's next Step times out and this frame runs paused, would otherwise leave the next
		// readback with no request here, and it would be dropped. Only write completions are handled:
		// no new request runs inside the world tick, and a stream's re-invoked handler only registers
		// its next request (see FTempoServer::FinishWrites). No timeout: the server tick that follows
		// would wait for the same writes as long as they take.
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
	// Sends every measurement whose readback has completed. The decode tasks respond to their gRPC
	// requests from worker threads, so they must finish before the game thread handles gRPC events
	// again.
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
	// non-blocking, exactly like the end-of-frame poll.
	if (ReadbackPollsQueued->load() == 0)
	{
		EnqueueReadbackPoll(Sensors);
	}

	// Only sensors with a request waiting. A capture can be taken on a request that an earlier capture's
	// readback then answers, and a stream asks again only once that response is written, so its own
	// readback can complete with no request yet. Sent then, it would be dropped. Kept, it goes to the
	// stream's next request, or to the next game frame's send, so nothing is held past a game frame.
	Sensors.RemoveAll([](const ITempoSensorInterface* Sensor) { return !Sensor->HasPendingMeasurementRequests(); });
	SendMeasurementsAndWait(Sensors);
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
