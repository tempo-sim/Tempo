// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoColorCamera.h"
#include "TempoDepthCamera.h"
#include "TempoMeasurementRequestingInterface.h"

#include "TempoScriptable.h"
#include "TempoScriptingServer.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "TempoSensorServiceSubsystem.generated.h"

namespace TempoScripting
{
	class Empty;
}

namespace TempoSensors
{
	class AvailableSensorsRequest;
	class AvailableSensorsResponse;
}

namespace TempoCamera
{
	class ColorImageRequest;
	class DepthImageRequest;
	class LabelImageRequest;
	class ColorImage;
	class DepthImage;
	class LabelImage;
}

template <typename ColorType>
struct TTextureReadQueue
{
	int32 GetNumPendingTextureReads() const { return PendingTextureReads.Num(); }
	
	bool HasOutstandingTextureReads() const { return !PendingTextureReads.IsEmpty() && !PendingTextureReads[0]->RenderFence.IsFenceComplete(); }
	
	void EnqueuePendingTextureRead(TTextureRead<ColorType>* TextureRead)
	{
		PendingTextureReads.Emplace(TextureRead);
	}

	TUniquePtr<TTextureRead<ColorType>> DequeuePendingTextureRead()
	{
		if (!PendingTextureReads.IsEmpty() && PendingTextureReads[0]->RenderFence.IsFenceComplete())
		{
			TUniquePtr<TTextureRead<ColorType>> TextureRead(PendingTextureReads[0].Release());
			PendingTextureReads.RemoveAt(0);
			return MoveTemp(TextureRead);
		}
		return nullptr;
	}

private:
	TArray<TUniquePtr<TTextureRead<ColorType>>> PendingTextureReads;
};

struct FRequestedSensor
{
	TArray<FColorImageRequest> PendingColorImageRequests;
	TArray<FDepthImageRequest> PendingDepthImageRequests;
	TArray<FLabelImageRequest> PendingLabelImageRequests;

	TTextureReadQueue<FColor> ColorTextureReadQueue;
	TTextureReadQueue<FLinearColor> LinearColorTextureReadQueue;
};

UCLASS()
class TEMPOSENSORS_API UTempoSensorServiceSubsystem : public UTickableWorldSubsystem, public ITempoMeasurementRequestingInterface, public ITempoWorldScriptable
{
	GENERATED_BODY()
	
public:
	virtual void RegisterWorldServices(UTempoScriptingServer* ScriptingServer) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	virtual bool HasPendingRequestForSensor(int32 SensorId) const override;
	virtual void RequestedMeasurementReady_Color(int32 SensorId, int32 SequenceId, FTextureRenderTargetResource* TextureResource) override;
	virtual void RequestedMeasurementReady_LinearColor(int32 SensorId, int32 SequenceId, FTextureRenderTargetResource* TextureResource) override;

private:
	void GetAvailableSensors(const TempoSensors::AvailableSensorsRequest& Request, const TResponseDelegate<TempoSensors::AvailableSensorsResponse>& ResponseContinuation) const;

	void StreamColorImages(const TempoCamera::ColorImageRequest& Request, const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuation);

	void StreamDepthImages(const TempoCamera::DepthImageRequest& Request, const TResponseDelegate<TempoCamera::DepthImage>& ResponseContinuation);

	void StreamLabelImages(const TempoCamera::LabelImageRequest& Request, const TResponseDelegate<TempoCamera::LabelImage>& ResponseContinuation);
	
	template <typename ColorType>
	void EnqueueTextureRead(int32 CameraId, int32 SequenceId, FTextureRenderTargetResource* TextureResource, TTextureReadQueue<ColorType>& TextureReadQueue);
	
	TMap<int32, FRequestedSensor> RequestedSensors;
};
