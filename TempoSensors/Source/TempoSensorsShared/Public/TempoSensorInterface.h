// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSensorsTypes.h"

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "TempoSensorInterface.generated.h"

UINTERFACE()
class TEMPOSENSORSSHARED_API UTempoSensorInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOSENSORSSHARED_API ITempoSensorInterface
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

	// Respond to pending measurement requests. May return one or more futures, so these can be parallelized.
	virtual TArray<TFuture<void>> SendMeasurements() = 0;
};
