// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsSettings.generated.h"

UCLASS(Config=Plugins, DefaultConfig, DisplayName = "Agents")
class TEMPOAGENTSSHARED_API UTempoAgentsSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public:
	UTempoAgentsSettings();

	float GetMaxThroughRoadAngleDegrees() const { return MaxThroughRoadAngleDegrees; }
	
protected:

	// Maximum angle (in degrees) between roads entering an intersection
	// before they are rejected as potential "through" roads.
	UPROPERTY(EditAnywhere, Config, Category = "Tempo Agents|Road Configuration")
	float MaxThroughRoadAngleDegrees = 15.0f;
};
