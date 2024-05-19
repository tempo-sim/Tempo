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
	virtual int32 GetVehicleId() override { return VehicleId; }
	
	virtual void HandleDrivingInput(const FNormalizedDrivingInput& Input) override;

protected:
	UPROPERTY(EditAnywhere)
	int32 VehicleId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxAcceleration = 200.0; // CM/S/S (~0.2g)

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxDeceleration = 1000.0; // CM/S/S (~1.0g) 
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxSteerAngle = 10.0; // Degrees, symmetric
};
