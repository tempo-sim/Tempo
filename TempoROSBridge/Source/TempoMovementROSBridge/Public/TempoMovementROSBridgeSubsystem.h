// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoMovementControlServiceSubsystem.h"
#include "TempoMovementROSBridgeSubsystem.generated.h"

UCLASS()
class TEMPOMOVEMENTROSBRIDGE_API UTempoMovementROSBridgeSubsystem : public UTempoMovementControlServiceSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:
	UPROPERTY()
	class UTempoROSNode* ROSNode;
};
