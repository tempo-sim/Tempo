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

	FMassTrafficLightInstanceCustomData(const bool VehicleGo, const bool VehicleGoProtectedLeft, const bool VehiclePrepareToStop, const bool PedestrianGo_FrontSide, const bool PedestrianGo_LeftSide, const bool PedestrianGo_RightSide);
	
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
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
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
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
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
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
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
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:

	UPROPERTY(Transient)
	UWorld* World;

	FMassEntityQuery EntityQuery;
};
