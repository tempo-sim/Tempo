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
	virtual FString GetOwnerName() const = 0;

	virtual FString GetSensorName() const = 0;

	virtual float GetRate() const = 0;

	virtual const TArray<TEnumAsByte<EMeasurementType>>& GetMeasurementTypes() const = 0;

	virtual void FlushMeasurementResponses() = 0;

	virtual bool HasPendingRenderingCommands() = 0;
};
