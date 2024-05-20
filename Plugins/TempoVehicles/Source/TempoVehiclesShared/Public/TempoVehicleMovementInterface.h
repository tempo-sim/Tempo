// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "TempoVehicleMovementInterface.generated.h"

USTRUCT(BlueprintType)
struct FDrivingCommand {
	GENERATED_BODY();

	// Acceleration in cm/s
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Acceleration = 0.0;

	// Steering in degrees from center
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SteeringAngle = 0.0; 
};

UINTERFACE()
class TEMPOVEHICLESSHARED_API UTempoVehicleMovementInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOVEHICLESSHARED_API ITempoVehicleMovementInterface
{
	GENERATED_BODY()

public:
	virtual float GetLinearVelocity() = 0;
	
	virtual void HandleDrivingCommand(const FDrivingCommand& Command) = 0;
};
