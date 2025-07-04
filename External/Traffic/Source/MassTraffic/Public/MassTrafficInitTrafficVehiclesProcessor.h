// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"

#include "MassRepresentationSubsystem.h"
#include "MassReplicationTypes.h"

#include "MassTrafficInitTrafficVehiclesProcessor.generated.h"


USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficVehiclesSpawnData
{
	GENERATED_BODY()
	
	TArray<FZoneGraphLaneLocation> LaneLocations;
};

/**
 * Assigns FDataFragment_TrafficVehicleType.AgentType, FDataFragment_TrafficVehicleType.AgentIndex and 
 * FDataFragment_LaneLocation.LaneLocation according to the distribution probabilities in 
 * TrafficVehicleConfiguration.
 * 
 * Initial lane locations are ensured to be non-overlapping on a given lane. 
 */
UCLASS(meta=(DisplayName="Init Traffic Vehicles"))
class MASSTRAFFIC_API UMassTrafficInitTrafficVehiclesProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficInitTrafficVehiclesProcessor();

protected:
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void Initialize(UObject& InOwner) override;
#else
	virtual void InitializeInternal(UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	void InitNetIds(FMassEntityManager& EntityManager, FMassExecutionContext& Context);

	TWeakObjectPtr<UMassRepresentationSubsystem> MassRepresentationSubsystem;

	FMassEntityQuery EntityQuery;
};
