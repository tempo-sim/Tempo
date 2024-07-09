// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassRepresentationProcessor.h"
#include "MassVisualizationLODProcessor.h"

#include "MassTrafficFragments.h"

#include "MassTrafficTrailerVisualizationProcessor.generated.h"

class UMassTrafficSubsystem;

/**
 * Overridden visualization processor to make it tied to the TrafficVehicleTrailer via the requirements
 */
UCLASS()
class MASSTRAFFIC_API UMassTrafficTrailerVisualizationProcessor : public UMassVisualizationProcessor
{
	GENERATED_BODY()

public:	
	UMassTrafficTrailerVisualizationProcessor();

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	virtual void ConfigureQueries() override;
};

/**
 * Custom visualization updates for TrafficVehicleTrailer
 */
UCLASS()
class MASSTRAFFIC_API UMassTrafficTrailerUpdateCustomVisualizationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassTrafficTrailerUpdateCustomVisualizationProcessor();

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:

	UPROPERTY(Transient)
	UWorld* World;

	FMassEntityQuery EntityQuery;
};
