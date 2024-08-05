// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsSettings.generated.h"

UCLASS(Config = Plugins, DefaultConfig, DisplayName = "Tempo Agents")
class TEMPOAGENTSSHARED_API UTempoAgentsSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public:

	float GetMaxThroughRoadAngleDegrees() const { return MaxThroughRoadAngleDegrees; }
	
protected:
	
	UPROPERTY(EditAnywhere, Config, Category = "Tempo Agents|Road Configuration")
	float MaxThroughRoadAngleDegrees = 15.0f;
};
