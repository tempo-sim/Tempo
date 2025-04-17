// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsSettings.generated.h"

/**
 * TempoAgents Plugin Settings.
 */
UCLASS(Config=Plugins, DefaultConfig)
class TEMPOAGENTSSHARED_API UTempoAgentsSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public:
	UTempoAgentsSettings();

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif

	float GetMaxThroughRoadAngleDegrees() const { return MaxThroughRoadAngleDegrees; }
	
protected:

	// Maximum angle (in degrees) between roads entering an intersection
	// before they are rejected as potential "through" roads.
	UPROPERTY(EditAnywhere, Config, Category = "Road Configuration")
	float MaxThroughRoadAngleDegrees = 15.0f;
};
