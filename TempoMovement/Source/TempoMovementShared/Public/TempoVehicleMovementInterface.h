// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoMovementTypes.h"

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "TempoVehicleMovementInterface.generated.h"

UINTERFACE()
class TEMPOMOVEMENTSHARED_API UTempoVehicleMovementInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOMOVEMENTSHARED_API ITempoVehicleMovementInterface
{
	GENERATED_BODY()

public:
	virtual void HandleDrivingInput(const FNormalizedDrivingInput& Command) = 0;
};
