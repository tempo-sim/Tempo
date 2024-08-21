// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "ActorClassificationInterface.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "DefaultActorClassifier.generated.h"

/**
 *  A default implementation for the ActorClassificationInterface.
 */
UCLASS()
class TEMPOCORE_API UDefaultActorClassifier : public UWorldSubsystem, public IActorClassificationInterface
{
	GENERATED_BODY()

public:
	static FName GetDefaultActorClassification(const AActor* Actor);
	
	virtual FName GetActorClassification(const AActor* Actor) const override;
};
