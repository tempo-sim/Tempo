// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, DisplayName = "Tempo Agents")
class TEMPOAGENTSSHARED_API UTempoAgentsSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public:

	float GetMaxThroughRoadAngleDegrees() const { return MaxThroughRoadAngleDegrees; }
	int32 GetDefaultNumRoadLanes()  const { return DefaultNumRoadLanes; }
	float GetDefaultRoadLaneWidth() const { return DefaultRoadLaneWidth; }
	float GetDefaultRoadShoulderWidth() const { return DefaultRoadShoulderWidth; }
	float GetDefaultRoadSampleDistanceStepSize() const { return DefaultRoadSampleDistanceStepSize; }
	const TArray<FName>& GetDefaultRoadLaneTags() const { return DefaultRoadLaneTags; }

protected:

	// Maximum angle (in degrees) between roads entering an intersection
	// before they are rejected as potential "through" roads.
	UPROPERTY(EditAnywhere, Config, Category = "Tempo Agents|Road Configuration")
	float MaxThroughRoadAngleDegrees = 15.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Tempo Agents|Road Configuration")
	int32 DefaultNumRoadLanes = 4;

	UPROPERTY(EditAnywhere, Config, Category = "Tempo Agents|Road Configuration")
	float DefaultRoadLaneWidth = 300.0;

	UPROPERTY(EditAnywhere, Config, Category = "Tempo Agents|Road Configuration")
	float DefaultRoadShoulderWidth = 300.0;

	UPROPERTY(EditAnywhere, Config, Category = "Tempo Agents|Road Configuration")
	float DefaultRoadSampleDistanceStepSize = 1000.0;

	UPROPERTY(EditAnywhere, Config, Category = "Tempo Agents|Road Configuration")
	TArray<FName> DefaultRoadLaneTags = { TEXT("DrivingLane") };
};
