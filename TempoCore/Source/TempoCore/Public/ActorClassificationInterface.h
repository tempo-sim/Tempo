// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ActorClassificationInterface.generated.h"

UINTERFACE()
class UActorClassificationInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implement this interface in a UWorldSubsystem to control how Actors will be classified by various parts of the simulator.
 */
class TEMPOCORE_API IActorClassificationInterface
{
	GENERATED_BODY()

public:
	virtual FName GetActorClassification(const AActor* Actor) const = 0;
};
