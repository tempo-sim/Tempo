// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorServiceSubsystem.h"

#include "TempoSensorInterface.h"
#include "TempoSensors/Sensors.grpc.pb.h"

#include "TempoColorCamera.h"
#include "TempoDepthCamera.h"
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

void UTempoSensorServiceSubsystem::ForEachSensor(const TFunction<bool(ITempoSensorInterface*)>& Callback) const
{
	check(GetWorld());
	
	for (TObjectIterator<UActorComponent> ComponentIt; ComponentIt; ++ComponentIt)
	{
		UActorComponent* Component = *ComponentIt;
		if (ITempoSensorInterface* Sensor = Cast<ITempoSensorInterface>(Component))
		{
			if (IsValid(Component) && Component->GetWorld() == GetWorld())
			{
				if (Callback(Sensor))
				{
					break;
				}
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
		AvailableSensor->set_id(Sensor->GetSensorId());
		AvailableSensor->set_name(TCHAR_TO_UTF8(*Sensor->GetSensorName()));
		AvailableSensor->set_rate(Sensor->GetRate());
		for (const EMeasurementType MeasurementType : Sensor->GetMeasurementTypes())
		{
			AvailableSensor->add_measurement_types(ToProtoMeasurementType(MeasurementType));
		}
		return false; // Don't break out of foreach
	});

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

template <typename ComponentType, typename RequestType, typename ResponseType>
void UTempoSensorServiceSubsystem::RequestImages(const RequestType& Request, const TResponseDelegate<ResponseType>& ResponseContinuation) const
{
	check(GetWorld());
	
	for (TObjectIterator<ComponentType> ComponentIt; ComponentIt; ++ComponentIt)
	{
		if (ComponentIt->GetSensorId() == Request.sensor_id() && IsValid(*ComponentIt) && ComponentIt->GetWorld() == GetWorld())
		{
			ComponentIt->RequestMeasurement(Request, ResponseContinuation);
		}
	}
}

void UTempoSensorServiceSubsystem::StreamColorImages(const TempoCamera::ColorImageRequest& Request, const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuation) const
{
	RequestImages<UTempoColorCamera, TempoCamera::ColorImageRequest, TempoCamera::ColorImage>(Request, ResponseContinuation);
}

void UTempoSensorServiceSubsystem::StreamDepthImages(const TempoCamera::DepthImageRequest& Request, const TResponseDelegate<TempoCamera::DepthImage>& ResponseContinuation) const
{
	RequestImages<UTempoDepthCamera, TempoCamera::DepthImageRequest, TempoCamera::DepthImage>(Request, ResponseContinuation);
}

void UTempoSensorServiceSubsystem::StreamLabelImages(const TempoCamera::LabelImageRequest& Request, const TResponseDelegate<TempoCamera::LabelImage>& ResponseContinuation) const
{
	RequestImages<UTempoColorCamera, TempoCamera::LabelImageRequest, TempoCamera::LabelImage>(Request, ResponseContinuation);
}

void UTempoSensorServiceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// In fixed step mode we block the game thread on any pending texture reads.
	// This guarantees they will be sent out in the same frame when they were captured.
	if (GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep)
	{
		ForEachSensor([](ITempoSensorInterface* Sensor)
		{
			if (Sensor->HasPendingRenderingCommands())
			{
				FlushRenderingCommands();
				return true; // Break out of foreach
			}
			return false; // Don't break out of foreach
		});
	}

	ForEachSensor([](ITempoSensorInterface* Sensor)
	{
		Sensor->FlushMeasurementResponses();
		return false; // Don't break out of foreach
	});
}

TStatId UTempoSensorServiceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTempoSensorServiceSubsystem, STATGROUP_Tickables);
}
