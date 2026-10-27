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
	// The owner's angular velocity vector in deg/s, in Unreal's left-handed world frame: the vector
	// a physics body reports, so v = ω × r holds, and a positive Z turns from +X toward +Y, which is
	// also a positive yaw rate. Not FRotator rates, which differ from it in the signs of X and Y.
	virtual FVector GetAngularVelocity() const = 0;
};
