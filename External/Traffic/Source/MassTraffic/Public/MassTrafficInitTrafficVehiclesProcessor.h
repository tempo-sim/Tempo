// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassTrafficFragments.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
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
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void InitializeInternal(UObject& InOwner, const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	void InitNetIds(FMassEntityManager& EntityManager, FMassExecutionContext& Context);

	TWeakObjectPtr<UMassRepresentationSubsystem> MassRepresentationSubsystem;

	FMassEntityQuery EntityQuery;
};
