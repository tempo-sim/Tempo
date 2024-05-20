// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorServiceSubsystem.h"

#include "TempoSensors.h"
#include "TempoSensorInterface.h"
#include "TempoSensorsSettings.h"
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

void UTempoSensorServiceSubsystem::GetAvailableSensors(const TempoSensors::AvailableSensorsRequest& Request, const TResponseDelegate<TempoSensors::AvailableSensorsResponse>& ResponseContinuation) const
{
	AvailableSensorsResponse Response;
	
	for (TObjectIterator<UObject> ObjectIt; ObjectIt; ++ObjectIt)
	{
		UObject* Object = *ObjectIt;
		if (const ITempoSensorInterface* Sensor = Cast<ITempoSensorInterface>(Object))
		{
			if (IsValid(Object) && Object->GetWorld() == GetWorld())
			{
				auto* AvailableSensor = Response.add_available_sensors();
				AvailableSensor->set_id(Sensor->GetSensorId());
				AvailableSensor->set_name(TCHAR_TO_UTF8(*Sensor->GetSensorName()));
				AvailableSensor->set_rate(Sensor->GetRate());
				for (const EMeasurementType MeasurementType : Sensor->GetMeasurementTypes())
				{
					AvailableSensor->add_measurement_types(ToProtoMeasurementType(MeasurementType));
				}
			}
		}
	}

	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoSensorServiceSubsystem::StreamColorImages(const TempoCamera::ColorImageRequest& Request, const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuation)
{
	const int32 Quality = UTempoColorCameraBase::QualityFromCompressionLevel(Request.compression_level());
	RequestedSensors.FindOrAdd(Request.sensor_id()).PendingColorImageRequests.Add(FColorImageRequest(ResponseContinuation, Quality));
}

void UTempoSensorServiceSubsystem::StreamDepthImages(const TempoCamera::DepthImageRequest& Request, const TResponseDelegate<TempoCamera::DepthImage>& ResponseContinuation)
{
	RequestedSensors.FindOrAdd(Request.sensor_id()).PendingDepthImageRequests.Add(FDepthImageRequest(ResponseContinuation));
}

void UTempoSensorServiceSubsystem::StreamLabelImages(const TempoCamera::LabelImageRequest& Request, const TResponseDelegate<TempoCamera::LabelImage>& ResponseContinuation)
{
	RequestedSensors.FindOrAdd(Request.sensor_id()).PendingLabelImageRequests.Add(FLabelImageRequest(ResponseContinuation));
}

void UTempoSensorServiceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// In fixed step mode we block the game thread on any pending texture reads.
	// This guarantees they will be sent out in the same frame when they were captured.
	if (GetDefault<UTempoCoreSettings>()->GetTimeMode() == ETimeMode::FixedStep)
	{
		for (const auto& PendingImageReqeust : RequestedSensors)
		{
			if (PendingImageReqeust.Value.ColorTextureReadQueue.HasOutstandingTextureReads() ||
				PendingImageReqeust.Value.LinearColorTextureReadQueue.HasOutstandingTextureReads())
			{
				FlushRenderingCommands();
				break;
			}
		}
	}
	
	for (auto& Elem : RequestedSensors)
	{
		FRequestedSensor& RequestedSensor = Elem.Value;
		
		if (const TUniquePtr<TTextureRead<FLinearColor>> TextureRead = RequestedSensor.LinearColorTextureReadQueue.DequeuePendingTextureRead())
		{
			for (auto ImageRequest = RequestedSensor.PendingDepthImageRequests.CreateIterator(); ImageRequest; ++ImageRequest)
			{
				ImageRequest->ExtractAndRespond(TextureRead.Get(), GetWorld()->GetTimeSeconds());
				ImageRequest.RemoveCurrent();
			}
		}
		
		if (const TUniquePtr<TTextureRead<FColor>> TextureRead = RequestedSensor.ColorTextureReadQueue.DequeuePendingTextureRead())
		{
			for (auto ImageRequest = RequestedSensor.PendingColorImageRequests.CreateIterator(); ImageRequest; ++ImageRequest)
			{
				ImageRequest->ExtractAndRespond(TextureRead.Get(), GetWorld()->GetTimeSeconds());
				ImageRequest.RemoveCurrent();
			}
			for (auto ImageRequest = RequestedSensor.PendingLabelImageRequests.CreateIterator(); ImageRequest; ++ImageRequest)
			{
				ImageRequest->ExtractAndRespond(TextureRead.Get(), GetWorld()->GetTimeSeconds());
				ImageRequest.RemoveCurrent();
			}
		}
	}
}

TStatId UTempoSensorServiceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTempoSensorServiceSubsystem, STATGROUP_Tickables);
}

bool UTempoSensorServiceSubsystem::HasPendingRequestForSensor(int32 SensorId) const
{
	return RequestedSensors.Contains(SensorId) &&
		(!RequestedSensors[SensorId].PendingColorImageRequests.IsEmpty() ||
		 !RequestedSensors[SensorId].PendingDepthImageRequests.IsEmpty() ||
		 !RequestedSensors[SensorId].PendingLabelImageRequests.IsEmpty());
}

void UTempoSensorServiceSubsystem::RequestedMeasurementReady_Color(int32 SensorId, int32 SequenceId, FTextureRenderTargetResource* TextureResource)
{
	FRequestedSensor* RequestedSensor = RequestedSensors.Find(SensorId);
	if (!RequestedSensor)
	{
		checkf(false, TEXT("Camera %d sent image but was not requested"), SensorId);
	}

	EnqueueTextureRead<FColor>(SensorId, SequenceId, TextureResource, RequestedSensor->ColorTextureReadQueue);
}

void UTempoSensorServiceSubsystem::RequestedMeasurementReady_LinearColor(int32 SensorId, int32 SequenceId, FTextureRenderTargetResource* TextureResource)
{
	FRequestedSensor* RequestedSensor = RequestedSensors.Find(SensorId);
	if (!RequestedSensor)
	{
		checkf(false, TEXT("Camera %d sent image but was not requested"), SensorId);
	}

	EnqueueTextureRead<FLinearColor>(SensorId, SequenceId, TextureResource, RequestedSensor->LinearColorTextureReadQueue);
}

template <typename ColorType>
void UTempoSensorServiceSubsystem::EnqueueTextureRead(int32 SensorId, int32 SequenceId, FTextureRenderTargetResource* TextureResource, TTextureReadQueue<ColorType>& TextureReadQueue)
{
	if (TextureReadQueue.GetNumPendingTextureReads() > GetDefault<UTempoSensorsSettings>()->GetMaxCameraRenderBufferSize())
	{
		UE_LOG(LogTempoSensors, Warning, TEXT("Fell behind while rendering images from camera %d. Dropping image."), SensorId);
		return;
	}

	TTextureRead<ColorType>* TextureRead = new TTextureRead<ColorType>(
		TextureResource->GetSizeXY(),
		SequenceId,
		GetWorld()->GetTimeSeconds());
	
	struct FReadSurfaceContext{
		FRenderTarget* SrcRenderTarget;
		TArray<ColorType>* OutData;
		FIntRect Rect;
		FReadSurfaceDataFlags Flags;
	};

	FReadSurfaceContext Context = {
		TextureResource,
		&TextureRead->Image,
		FIntRect(0,0,TextureResource->GetSizeX(), TextureResource->GetSizeY()),
		FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX)
	};

	ENQUEUE_RENDER_COMMAND(ReadSurfaceCommand)(
		[Context](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.ReadSurfaceData(
			Context.SrcRenderTarget->GetRenderTargetTexture(),
			Context.Rect,
			*Context.OutData,
			Context.Flags
		);
	});
	
	TextureRead->RenderFence.BeginFence();
	TextureReadQueue.EnqueuePendingTextureRead(TextureRead);
}
