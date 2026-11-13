// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSensorsTypes.h"

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "TempoSensorInterface.generated.h"

UINTERFACE()
class TEMPOSENSORS_API UTempoSensorInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOSENSORS_API ITempoSensorInterface
{
	GENERATED_BODY()

public:
	// Get the name of the Actor who owns this sensor.
	virtual FString GetOwnerName() const = 0;

	// Get this sensor's name.
	virtual FString GetSensorName() const = 0;

	// Get the rate (in Hz) this sensor updates at.
	virtual float GetRate() const = 0;

	// What measurement types does this sensor support?
	virtual const TArray<TEnumAsByte<EMeasurementType>>& GetMeasurementTypes() const = 0;

	// Is this sensor waiting on a render? Called on the render thread.
	virtual bool IsAwaitingRender() = 0;

	// A render frame has completed. Requested render data may be ready to be read and sent. Called on the render thread.
	virtual void OnRenderCompleted() = 0;

	// Block the game thread until responses for all pending measurement requests are ready.
	virtual void BlockUntilMeasurementsReady() const = 0;

	// Respond to pending measurement requests. Returns a future, so these can be parallelized.
	virtual TOptional<TFuture<void>> SendMeasurements() = 0;

	// Game thread. Wait for the decodes this sensor launched itself (from the render thread, as a read
	// completed) to have responded.
	virtual void WaitForDecodes() {}

	// Whether any request is waiting for a measurement. Between frames, a sensor with none isn't sent:
	// a readback that completes then is kept until a request arrives, or until the next game frame
	// sends it. A sensor that can't say returns true and is always sent.
	virtual bool HasPendingMeasurementRequests() const { return true; }

	// Whether ExecutePendingCapture will render this frame.
	virtual bool HasPendingCapture() const { return false; }

	// Execute any pending capture scheduled by MaybeMarkPendingCapture. Called from the subsystem
	// after OnWorldTickEnd, when all actor transforms are final and SendAllEndOfFrameUpdates has run.
	virtual void ExecutePendingCapture() {}
};
