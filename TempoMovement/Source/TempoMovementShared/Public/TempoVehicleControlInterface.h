// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "TempoVehicleControlInterface.generated.h"

USTRUCT(BlueprintType)
struct FNormalizedDrivingInput {
	GENERATED_BODY();

	// Normalized acceleration in [-1.0 <-> 1.0]
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Acceleration = 0.0;

	// Normalized steering in [-1.0 <-> 1.0]
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Steering = 0.0;
};

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

	virtual float GetMaxAcceleration() const = 0;

	virtual float GetMaxDeceleration() const = 0;

	virtual float GetMaxSteerAngle() const = 0;
};
