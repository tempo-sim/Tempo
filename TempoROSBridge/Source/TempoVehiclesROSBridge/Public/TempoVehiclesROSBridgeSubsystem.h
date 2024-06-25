// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoDrivingServiceSubsystem.h"
#include "TempoVehiclesROSBridgeSubsystem.generated.h"

UCLASS()
class TEMPOVEHICLESROSBRIDGE_API UTempoVehiclesROSBridgeSubsystem : public UTempoDrivingServiceSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:
	UPROPERTY()
	class UTempoROSNode* ROSNode;
};
