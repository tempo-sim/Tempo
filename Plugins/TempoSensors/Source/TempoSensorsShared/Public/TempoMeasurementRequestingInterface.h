// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptingServer.h"

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "TempoMeasurementRequestingInterface.generated.h"

template <typename ColorType>
struct TTextureRead
{
	TTextureRead(const FIntPoint& ImageSizeIn, int32 SequenceIdIn, double CaptureTimeIn)
		   : ImageSize(ImageSizeIn), SequenceId(SequenceIdIn), CaptureTime(CaptureTimeIn), Image(TArray(&FColor::Green, ImageSize.X * ImageSize.Y)) {}

	FIntPoint ImageSize;
	int32 SequenceId;
	double CaptureTime;
	TArray<ColorType> Image;
	FRenderCommandFence RenderFence;
};

template <typename MeasurementType>
struct FMeasurementRequest
{
       FMeasurementRequest(const TResponseDelegate<MeasurementType>& ResponseContinuationIn)
               : ResponseContinuation(ResponseContinuationIn) {}

       TResponseDelegate<MeasurementType> ResponseContinuation;
};

UINTERFACE()
class TEMPOSENSORSSHARED_API UTempoMeasurementRequestingInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOSENSORSSHARED_API ITempoMeasurementRequestingInterface
{
	GENERATED_BODY()

public:
	virtual bool HasPendingRequestForSensor(int32 SensorId) const = 0;

	virtual void RequestedMeasurementReady_Color(int32 SensorId, int32 SequenceId, FTextureRenderTargetResource* TextureResource) = 0;

	virtual void RequestedMeasurementReady_LinearColor(int32 SensorId, int32 SequenceId, FTextureRenderTargetResource* TextureResource) = 0;
};
