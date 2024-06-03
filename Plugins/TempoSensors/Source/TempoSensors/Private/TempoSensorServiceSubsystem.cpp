// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorServiceSubsystem.h"

#include "TempoSensorInterface.h"
#include "TempoSensors/Sensors.grpc.pb.h"

#include "TempoCamera.h"
#include "TempoCamera/Camera.pb.h"

#include "TempoCoreSettings.h"

using SensorService = TempoSensors::SensorService::AsyncService;
using SensorDescriptor = TempoSensors::SensorDescriptor;
using AvailableSensorsRequest = TempoSensors::AvailableSensorsRequest;
using AvailableSensorsResponse = TempoSensors::AvailableSensorsResponse;
using ColorImageRequest = TempoCamera::ColorImageRequest;
using DepthImageRequest = TempoCamera::DepthImageRequest;
using LabelImageRequest = TempoCamera::LabelImageRequest;
using ColorImage = TempoCamera::ColorImage;
using DepthImage = TempoCamera::DepthImage;
using LabelImage = TempoCamera::LabelImage;

void UTempoSensorServiceSubsystem::RegisterWorldServices(UTempoScriptingServer* ScriptingServer)
{
	ScriptingServer->RegisterService<SensorService>(
		TSimpleRequestHandler<SensorService, AvailableSensorsRequest, AvailableSensorsResponse>(&SensorService::RequestGetAvailableSensors).BindUObject(this, &UTempoSensorServiceSubsystem::GetAvailableSensors),
		TStreamingRequestHandler<SensorService, ColorImageRequest, ColorImage>(&SensorService::RequestStreamColorImages).BindUObject(this, &UTempoSensorServiceSubsystem::StreamColorImages),
		TStreamingRequestHandler<SensorService, DepthImageRequest, DepthImage>(&SensorService::RequestStreamDepthImages).BindUObject(this, &UTempoSensorServiceSubsystem::StreamDepthImages),
		TStreamingRequestHandler<SensorService, LabelImageRequest, LabelImage>(&SensorService::RequestStreamLabelImages).BindUObject(this, &UTempoSensorServiceSubsystem::StreamLabelImages)
		);
}

void UTempoSensorServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// OnWorldTickStart is fired before Tick has actually begun, while the world time is still the tick
	// of the last frame. We use this last opportunity, having waited as long as possible, to collect
	// and send all the sensor measurements from the previous frame.
	FWorldDelegates::OnWorldTickStart.AddUObject(this, &UTempoSensorServiceSubsystem::OnWorldTickStart);
}

void UTempoSensorServiceSubsystem::OnWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World == GetWorld() && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE))
	{	
		// In fixed step mode we block the game thread on any pending texture reads.
		// This guarantees they will be sent out in the same frame when they were captured.
		if (GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep)
		{
			ForEachSensor([](const ITempoSensorInterface* Sensor)
			{
				Sensor->FlushPendingRenderingCommands();
			});
		}

		TArray<TFuture<void>> Futures;
		ForEachSensor([&Futures](ITempoSensorInterface* Sensor)
		{
			if (TOptional<TFuture<void>> Future = Sensor->FlushMeasurementResponses())
			{
				Futures.Add(MoveTemp(Future.GetValue()));
			}
		});

		for (const TFuture<void>& Future : Futures)
		{
			Future.Wait();
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
	default:
		{
			checkf(false, TEXT("Unhandled image type"));
			return TempoSensors::COLOR_IMAGE;
		}
	}
}

void UTempoSensorServiceSubsystem::ForEachSensor(const TFunction<void(ITempoSensorInterface*)>& Callback) const
{
	check(GetWorld());
	
	for (TObjectIterator<UActorComponent> ComponentIt; ComponentIt; ++ComponentIt)
	{
		UActorComponent* Component = *ComponentIt;
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

	ForEachSensor([&Response](const ITempoSensorInterface* Sensor)
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

void UTempoSensorServiceSubsystem::StreamColorImages(const TempoCamera::ColorImageRequest& Request, const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuation) const
{
	RequestImages<TempoCamera::ColorImageRequest, TempoCamera::ColorImage>(Request, ResponseContinuation);
}

void UTempoSensorServiceSubsystem::StreamDepthImages(const TempoCamera::DepthImageRequest& Request, const TResponseDelegate<TempoCamera::DepthImage>& ResponseContinuation) const
{
	RequestImages<TempoCamera::DepthImageRequest, TempoCamera::DepthImage>(Request, ResponseContinuation);
}

void UTempoSensorServiceSubsystem::StreamLabelImages(const TempoCamera::LabelImageRequest& Request, const TResponseDelegate<TempoCamera::LabelImage>& ResponseContinuation) const
{
	RequestImages<TempoCamera::LabelImageRequest, TempoCamera::LabelImage>(Request, ResponseContinuation);
}
