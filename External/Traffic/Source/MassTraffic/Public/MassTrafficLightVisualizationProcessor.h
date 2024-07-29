// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficFragments.h"

#include "MassRepresentationFragments.h"
#include "MassVisualizationLODProcessor.h"
#include "MassLODCollectorProcessor.h"
#include "MassRepresentationProcessor.h"

#include "MassTrafficLightVisualizationProcessor.generated.h"

class UMassTrafficSubsystem;

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficLightInstanceCustomData
{
	GENERATED_BODY()

	
	FMassTrafficLightInstanceCustomData()
	{
	}

	FMassTrafficLightInstanceCustomData(const bool VehicleGo, const bool VehiclePrepareToStop, const bool PedestrianGo_FrontSide, const bool PedestrianGo_LeftSide, const bool PedestrianGo_RightSide);
	
	FMassTrafficLightInstanceCustomData(const EMassTrafficLightStateFlags TrafficLightStateFlags); 

	
	/** Bit packed param with EMassTrafficLightStateFlags packed into the least significant 8 bits */ 
	UPROPERTY(BlueprintReadWrite)
	float PackedParam1 = 0;
};

/**
 * Overridden visualization processor to make it tied to the TrafficLight via the requirements
 */
UCLASS(meta = (DisplayName = "Traffic Intersection Visualization LOD"))
class MASSTRAFFIC_API UMassTrafficIntersectionVisualizationLODProcessor : public UMassVisualizationLODProcessor
{
	GENERATED_BODY()

public:
	UMassTrafficIntersectionVisualizationLODProcessor();

protected:
	virtual void ConfigureQueries() override;
};

/**
 * Overridden visualization processor to make it tied to the TrafficLight via the requirements
 */
UCLASS(meta = (DisplayName = "Traffic Intersection LOD Collector"))
class MASSTRAFFIC_API UMassTrafficIntersectionLODCollectorProcessor : public UMassLODCollectorProcessor
{
	GENERATED_BODY()

	UMassTrafficIntersectionLODCollectorProcessor();

protected:
	virtual void ConfigureQueries() override;
};

/**
 * Overridden visualization processor to make it tied to the TrafficLight via the requirements
 */
UCLASS()
class MASSTRAFFIC_API UMassTrafficLightVisualizationProcessor : public UMassVisualizationProcessor
{
	GENERATED_BODY()

public:
	UMassTrafficLightVisualizationProcessor();

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	virtual void ConfigureQueries() override;
};

/**
 * Custom visualization updates for TrafficLight
 */
UCLASS()
class MASSTRAFFIC_API UMassTrafficLightUpdateCustomVisualizationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassTrafficLightUpdateCustomVisualizationProcessor();

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:

	UPROPERTY(Transient)
	UWorld* World;

	FMassEntityQuery EntityQuery;
};
