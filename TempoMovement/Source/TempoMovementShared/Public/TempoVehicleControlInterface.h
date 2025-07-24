// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoMovementTypes.h"

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "TempoVehicleControlInterface.generated.h"

UINTERFACE()
class TEMPOMOVEMENTSHARED_API UTempoVehicleControlInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOMOVEMENTSHARED_API ITempoVehicleControlInterface
{
	GENERATED_BODY()
	
public:
	virtual FString GetVehicleName() = 0;
	
	virtual void HandleDrivingInput(const FNormalizedDrivingInput& Input) = 0;
};
