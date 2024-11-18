// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "TempoDroneControlInterface.generated.h"

USTRUCT(BlueprintType)
struct FNormalizedFlyingInput {
	GENERATED_BODY();

	// Normalized pitch in [-1.0 <-> 1.0]
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Pitch = 0.0;

	// Normalized roll in [-1.0 <-> 1.0]
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Roll = 0.0;
	
	// Normalized yaw in [-1.0 <-> 1.0]
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Yaw = 0.0;
	
	// Normalized throttle in [-1.0 <-> 1.0]
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Throttle = 0.0;
};

UINTERFACE()
class TEMPOMOVEMENTSHARED_API UTempoDroneControlInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOMOVEMENTSHARED_API ITempoDroneControlInterface
{
	GENERATED_BODY()

public:
	virtual FString GetDroneName() = 0;
	
	virtual void HandleFlyingInput(const FNormalizedFlyingInput& Input) = 0;
};
