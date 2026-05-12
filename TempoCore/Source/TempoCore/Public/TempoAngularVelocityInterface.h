// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TempoAngularVelocityInterface.generated.h"

UINTERFACE()
class TEMPOCORE_API UTempoAngularVelocityInterface : public UInterface
{
	GENERATED_BODY()
};

// Implemented by movement components that can report their owner's angular velocity.
class TEMPOCORE_API ITempoAngularVelocityInterface
{
	GENERATED_BODY()

public:
	// Angular velocity in deg/s, left-handed (Unreal native).
	virtual FVector GetAngularVelocity() const = 0;
};
