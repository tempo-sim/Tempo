// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoWorldSubsystem.h"
#include "TempoScriptable.h"
#include "TempoScriptingServer.h"

#include "CoreMinimal.h"

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

UCLASS()
class TEMPOSENSORS_API UTempoSensorServiceSubsystem : public UTempoWorldSubsystem, public ITempoWorldScriptable
{
	GENERATED_BODY()
	
public:
	virtual void RegisterWorldServices(UTempoScriptingServer* ScriptingServer) override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void OnWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaSeconds);

protected:
	void GetAvailableSensors(const TempoSensors::AvailableSensorsRequest& Request, const TResponseDelegate<TempoSensors::AvailableSensorsResponse>& ResponseContinuation) const;

	void StreamColorImages(const TempoCamera::ColorImageRequest& Request, const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuation) const;

	void StreamDepthImages(const TempoCamera::DepthImageRequest& Request, const TResponseDelegate<TempoCamera::DepthImage>& ResponseContinuation) const;

	void StreamLabelImages(const TempoCamera::LabelImageRequest& Request, const TResponseDelegate<TempoCamera::LabelImage>& ResponseContinuation) const;

	void ForEachSensor(const TFunction<void(class ITempoSensorInterface*)>& Callback) const;

	template <typename RequestType, typename ResponseType>
	void RequestImages(const RequestType& Request, const TResponseDelegate<ResponseType>& ResponseContinuation) const;
};
