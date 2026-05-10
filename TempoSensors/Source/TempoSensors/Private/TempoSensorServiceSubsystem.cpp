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

#include "Components/SceneCaptureComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

using SensorService = TempoSensors::SensorService;
using SensorAsyncService = TempoSensors::SensorService::AsyncService;
using SensorDescriptor = TempoSensors::SensorDescriptor;
using AvailableSensorsRequest = TempoSensors::AvailableSensorsRequest;
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

void UTempoSensorServiceSubsystem::RegisterServices(FTempoServer& Server)
{
	Server.RegisterService<SensorService>(
		SimpleRequestHandler(&SensorAsyncService::RequestGetAvailableSensors, &UTempoSensorServiceSubsystem::GetAvailableSensors),
		StreamingRequestHandler(&SensorAsyncService::RequestStreamColorImages, &UTempoSensorServiceSubsystem::StreamColorImages),
		StreamingRequestHandler(&SensorAsyncService::RequestStreamDepthImages, &UTempoSensorServiceSubsystem::StreamDepthImages),
		StreamingRequestHandler(&SensorAsyncService::RequestStreamLabelImages, &UTempoSensorServiceSubsystem::StreamLabelImages),
		StreamingRequestHandler(&SensorAsyncService::RequestStreamBoundingBoxes, &UTempoSensorServiceSubsystem::StreamBoundingBoxes),
		StreamingRequestHandler(&SensorAsyncService::RequestStreamLidarScans, &UTempoSensorServiceSubsystem::StreamLidarScans),
		StreamingRequestHandler(&SensorAsyncService::RequestStreamVideo, &UTempoSensorServiceSubsystem::StreamVideo)
		);
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
}

void UTempoSensorServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoServer::Get().DeactivateService<SensorService>();
}

void UTempoSensorServiceSubsystem::OnRenderFrameCompleted() const
{
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

	ForEachActiveSensor([](ITempoSensorInterface* Sensor)
	{
		Sensor->ExecutePendingCapture();
	});

	// When the main viewport renders, the engine drains SceneCapturesToUpdateMap itself as part of
	// the main render path, and it does so with a valid MainViewFamily that our components can
	// sync jitter/Lumen state against. Only drain here when world rendering is disabled — otherwise
	// we'd preempt the engine and lose that sync.
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

		TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorsSendMeasurements);
		TArray<TFuture<void>> SendMeasurementsTasks;
		ForEachActiveSensor([&SendMeasurementsTasks](ITempoSensorInterface* Sensor)
		{
			if (TOptional<TFuture<void>> Future = Sensor->SendMeasurements())
			{
				SendMeasurementsTasks.Add(MoveTemp(Future.GetValue()));
			}
		});

		for (const TFuture<void>& SendMeasurementsTask : SendMeasurementsTasks)
		{
			SendMeasurementsTask.Wait();
		}
	}
}

TempoSensors::MeasurementType ToProtoMeasurementType(EMeasurementType ImageType)
{
	switch (ImageType)
	{
	case EMeasurementType::COLOR_IMAGE:
		{
			return TempoSensors::COLOR_IMAGE;
		}
	case EMeasurementType::DEPTH_IMAGE:
		{
			return TempoSensors::DEPTH_IMAGE;
		}
	case EMeasurementType::LABEL_IMAGE:
		{
			return TempoSensors::LABEL_IMAGE;
		}
	case EMeasurementType::LIDAR_SCAN:
		{
			return TempoSensors::LIDAR_SCAN;
		}
	case EMeasurementType::BOUNDING_BOXES:
		{
			return TempoSensors::BOUNDING_BOXES;
		}
	case EMeasurementType::VIDEO:
		{
			return TempoSensors::VIDEO;
		}
	default:
		{
			checkf(false, TEXT("Unhandled measurement type"));
			return TempoSensors::COLOR_IMAGE;
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

void UTempoSensorServiceSubsystem::GetAvailableSensors(const TempoSensors::AvailableSensorsRequest& Request, const TResponseDelegate<TempoSensors::AvailableSensorsResponse>& ResponseContinuation) const
{
	AvailableSensorsResponse Response;

	ForEachActiveSensor([&Response](const ITempoSensorInterface* Sensor)
	{
		auto* AvailableSensor = Response.add_available_sensors();
		AvailableSensor->set_owner(TCHAR_TO_UTF8(*Sensor->GetOwnerName()));
		AvailableSensor->set_name(TCHAR_TO_UTF8(*Sensor->GetSensorName()));
		AvailableSensor->set_rate(Sensor->GetRate());
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
	
	const FString RequestedOwnerName(UTF8_TO_TCHAR(Request.owner_name().c_str()));
	const FString RequestedSensorName(UTF8_TO_TCHAR(Request.sensor_name().c_str()));

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

	const FString RequestedOwnerName(UTF8_TO_TCHAR(Request.owner_name().c_str()));
	const FString RequestedSensorName(UTF8_TO_TCHAR(Request.sensor_name().c_str()));

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
