// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoServiceProvider.h"
#include "TempoServer.h"
#include "TempoSubsystems.h"

#include "CoreMinimal.h"

#include "TempoSensorServiceSubsystem.generated.h"

namespace TempoCore
{
	class Empty;
}

namespace TempoSensors
{
	class AvailableSensorsRequest;
	class AvailableSensorsResponse;
	class ColorImageRequest;
	class DepthImageRequest;
	class LabelImageRequest;
	class BoundingBoxesRequest;
	class ColorImage;
	class DepthImage;
	class LabelImage;
	class BoundingBoxes;
	class LidarScanRequest;
	class LidarScanSegment;
	class VideoRequest;
	class VideoFrame;
}

UCLASS()
class TEMPOSENSORS_API UTempoSensorServiceSubsystem : public UTempoGameWorldSubsystem, public ITempoServiceProvider
{
	GENERATED_BODY()
	
public:
	virtual void RegisterServices(FTempoServer& Server) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	virtual void OnWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	virtual void OnWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	void ForEachActiveSensor(const TFunction<void(class ITempoSensorInterface*)>& Callback) const;

	void GetAvailableSensors(const TempoSensors::AvailableSensorsRequest& Request, const TResponseDelegate<TempoSensors::AvailableSensorsResponse>& ResponseContinuation) const;

	void StreamColorImages(const TempoSensors::ColorImageRequest& Request, const TResponseDelegate<TempoSensors::ColorImage>& ResponseContinuation) const;

	void StreamDepthImages(const TempoSensors::DepthImageRequest& Request, const TResponseDelegate<TempoSensors::DepthImage>& ResponseContinuation) const;

	void StreamLabelImages(const TempoSensors::LabelImageRequest& Request, const TResponseDelegate<TempoSensors::LabelImage>& ResponseContinuation) const;

	void StreamBoundingBoxes(const TempoSensors::BoundingBoxesRequest& Request, const TResponseDelegate<TempoSensors::BoundingBoxes>& ResponseContinuation) const;

	void StreamLidarScans(const TempoSensors::LidarScanRequest& Request, const TResponseDelegate<TempoSensors::LidarScanSegment>& ResponseContinuation) const;

	void StreamVideo(const TempoSensors::VideoRequest& Request, const TResponseDelegate<TempoSensors::VideoFrame>& ResponseContinuation) const;

protected:
	void OnRenderFrameCompleted() const;

	template <typename RequestType, typename ResponseType>
	void RequestImages(const RequestType& Request, const TResponseDelegate<ResponseType>& ResponseContinuation) const;
};
