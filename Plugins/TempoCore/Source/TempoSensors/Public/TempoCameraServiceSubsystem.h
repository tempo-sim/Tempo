// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoScriptable.h"

#include "TempoScriptingServer.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "TempoCameraServiceSubsystem.generated.h"

namespace TempoScripting
{
	class Empty;
}

namespace TempoSensors
{
	class AvailableCamerasRequest;
	class AvailableCamerasResponse;
	class StreamImagesRequest;
	class Image;
}

UCLASS()
class TEMPOSENSORS_API UTempoCameraServiceSubsystem : public UWorldSubsystem, public ITempoWorldScriptable
{
	GENERATED_BODY()
	
public:
	virtual void RegisterWorldServices(UTempoScriptingServer* ScriptingServer) override;

private:
	void GetAvailableCameras(const TempoSensors::AvailableCamerasRequest& Request, const TResponseDelegate<TempoSensors::AvailableCamerasResponse>& ResponseContinuation) const;

	void StreamImages(const TempoSensors::StreamImagesRequest& Request, const TResponseDelegate<TempoSensors::Image>& ResponseContinuation);

	TMap<int32, TResponseDelegate<TempoSensors::Image>> PendingImageResponseContinuations;
};
