// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoGeographicServiceSubsystem.h"
#include "TempoGeographicROSBridgeSubsystem.generated.h"

UCLASS()
class TEMPOGEOGRAPHICROSBRIDGE_API UTempoGeographicROSBridgeSubsystem : public UTempoGeographicServiceSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	UPROPERTY()
	class UTempoROSNode* ROSNode;
};
