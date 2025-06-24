// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassRepresentationProcessor.h"
#include "MassVisualizationLODProcessor.h"

#include "MassTrafficFragments.h"

#include "MassTrafficParkedVehicleVisualizationProcessor.generated.h"

class UMassTrafficSubsystem;

/**
 * Overridden visualization processor to make it tied to the ParkedVehicle via the requirements
 */
UCLASS()
class MASSTRAFFIC_API UMassTrafficParkedVehicleVisualizationProcessor : public UMassVisualizationProcessor
{
	GENERATED_BODY()

public:	
	UMassTrafficParkedVehicleVisualizationProcessor();

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
};

/**
 * Custom visualization updates for ParkedVehicle 
 */
UCLASS()
class MASSTRAFFIC_API UMassTrafficParkedVehicleUpdateCustomVisualizationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassTrafficParkedVehicleUpdateCustomVisualizationProcessor();

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
