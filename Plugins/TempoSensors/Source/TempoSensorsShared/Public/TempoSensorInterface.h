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
	virtual int32 GetSensorId() const = 0;

	virtual FString GetSensorName() const = 0;

	virtual float GetRate() const = 0;

	virtual const TArray<TEnumAsByte<EMeasurementType>>& GetMeasurementTypes() const = 0;
	
	static int32 AllocateSensorId() { return SensorIdAllocator++; }
	
private:
	static int32 SensorIdAllocator;
};
