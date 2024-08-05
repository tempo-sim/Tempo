// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsWorldSubsystem.generated.h"

UCLASS()
class TEMPOAGENTSSHARED_API UTempoAgentsWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Agents|World Subsystem|Traffic Controllers")
	void SetupTrafficControllers();

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	
protected:
	
};
