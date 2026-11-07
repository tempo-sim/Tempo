// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoServiceProvider.h"
#include "TempoServer.h"
#include "TempoSubsystems.h"

#include "CoreMinimal.h"

#include <atomic>

#include "TempoSensorServiceSubsystem.generated.h"

namespace TempoCore
{
	class Empty;
}

namespace TempoSensors
{
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
	class SetPipelinedRenderingEnabledRequest;
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

	void GetAvailableSensors(const TempoCore::Empty& Request, const TResponseDelegate<TempoSensors::AvailableSensorsResponse>& ResponseContinuation) const;

	void StreamColorImages(const TempoSensors::ColorImageRequest& Request, const TResponseDelegate<TempoSensors::ColorImage>& ResponseContinuation) const;

	void StreamDepthImages(const TempoSensors::DepthImageRequest& Request, const TResponseDelegate<TempoSensors::DepthImage>& ResponseContinuation) const;

	void StreamLabelImages(const TempoSensors::LabelImageRequest& Request, const TResponseDelegate<TempoSensors::LabelImage>& ResponseContinuation) const;

	void StreamBoundingBoxes(const TempoSensors::BoundingBoxesRequest& Request, const TResponseDelegate<TempoSensors::BoundingBoxes>& ResponseContinuation) const;

	void StreamLidarScans(const TempoSensors::LidarScanRequest& Request, const TResponseDelegate<TempoSensors::LidarScanSegment>& ResponseContinuation) const;

	void StreamVideo(const TempoSensors::VideoRequest& Request, const TResponseDelegate<TempoSensors::VideoFrame>& ResponseContinuation) const;

	void SetPipelinedRenderingEnabled(const TempoSensors::SetPipelinedRenderingEnabledRequest& Request, const TResponseDelegate<TempoCore::Empty>& ResponseContinuation) const;

protected:
	void OnRenderFrameCompleted() const;

	// Sends every measurement whose readback has completed, waiting for the decode tasks.
	void SendCompletedMeasurements();

	// Polled between the steps a client advances between frames; see ATempoWorldSettings::OnBetweenFramesPoll.
	void OnBetweenFramesPoll(UWorld* World);

	// Queues a render command that reads back every sensor whose render fence has signalled. Non-blocking.
	void EnqueueReadbackPoll(TConstArrayView<class ITempoSensorInterface*> Sensors);

	// Readback polls queued on the render thread and not yet run, so OnBetweenFramesPoll (which runs far
	// more often than the render thread drains) never queues one behind another.
	TSharedRef<std::atomic<int32>> ReadbackPollsQueued = MakeShared<std::atomic<int32>>(0);

	FDelegateHandle BetweenFramesPollHandle;

	// The active sensors, gathered once per engine frame: OnBetweenFramesPoll runs every millisecond or
	// so and ForEachActiveSensor walks every actor component. The set can change between steps (spawn
	// and destroy RPCs run there), but a sensor spawned there has nothing to read back until a frame
	// ticks, and the weak pointers drop one that is destroyed.
	TArray<TWeakObjectPtr<UActorComponent>> BetweenFramesSensors;
	uint64 BetweenFramesSensorsFrame = MAX_uint64;

	template <typename RequestType, typename ResponseType>
	void RequestImages(const RequestType& Request, const TResponseDelegate<ResponseType>& ResponseContinuation) const;
};
