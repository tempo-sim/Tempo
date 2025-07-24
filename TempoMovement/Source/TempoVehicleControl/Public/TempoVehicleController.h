// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoVehicleControlInterface.h"

#include "AIController.h"
#include "CoreMinimal.h"
#include "GameFramework/Controller.h"
#include "TempoVehicleController.generated.h"

UCLASS()
class TEMPOVEHICLECONTROL_API ATempoVehicleController : public AAIController, public ITempoVehicleControlInterface
{
	GENERATED_BODY()

public:
	virtual FString GetVehicleName() override;
	
	virtual void HandleDrivingInput(const FNormalizedDrivingInput& Input) override;
};
