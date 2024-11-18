// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "TempoDroneMovementInterface.generated.h"

USTRUCT(BlueprintType)
struct FFlyingCommand {
	GENERATED_BODY();

	// Pitch in deg
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Pitch = 0.0;

	// Yaw in deg
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Yaw = 0.0;

	// Roll in deg
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Roll = 0.0;

	// Throttle in ...
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Throttle = 0.0; 
};

UINTERFACE()
class TEMPOMOVEMENTSHARED_API UTempoDroneMovementInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOMOVEMENTSHARED_API ITempoDroneMovementInterface
{
	GENERATED_BODY()

public:
	virtual float GetLinearVelocity() = 0;

	virtual void HandleFlyingCommand(const FFlyingCommand& Command) = 0;
};
