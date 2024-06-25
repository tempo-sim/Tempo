// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoTimeServiceSubsystem.h"
#include "TempoTimeROSBridgeSubsystem.generated.h"

UCLASS()
class TEMPOTIMEROSBRIDGE_API UTempoTimeROSBridgeSubsystem : public UTempoTimeServiceSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:
	UPROPERTY()
	class UTempoROSNode* ROSNode;
};
