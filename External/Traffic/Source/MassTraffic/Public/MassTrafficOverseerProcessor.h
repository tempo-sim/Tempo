// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficVehicleSimulationTrait.h"
#include "MassTrafficFragments.h"

#include "MassEntityView.h"

#include "MassTrafficOverseerProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficOverseerProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

protected:
	UMassTrafficOverseerProcessor();
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	bool MoveVehicleToFreeSpaceOnRandomLane(
		const FMassEntityManager& EntityManager,
		const FZoneGraphStorage& ZoneGraphStorage,
		const FMassEntityHandle VehicleEntity,
		const struct FAgentRadiusFragment& Vehicle_RadiusFragment,
		const FMassTrafficRandomFractionFragment& Vehicle_RandomFractionFragment,
		FMassTrafficNextVehicleFragment& Vehicle_NextVehicleFragment,
		FMassTrafficVehicleControlFragment& Vehicle_VehicleControlFragment,
		FMassTrafficInterpolationFragment& Vehicle_InterpolationFragment,
		FTransformFragment& Vehicle_TransformFragment,
		FMassZoneGraphLaneLocationFragment& Vehicle_LaneLocationFragment,
		const FMassTrafficLaneOffsetFragment& Vehicle_LaneOffsetFragment,
		FMassTrafficObstacleAvoidanceFragment& Vehicle_AvoidanceFragment,
		struct FMassRepresentationFragment& Vehicle_RepresentationFragment,
		FZoneGraphTrafficLaneData& Vehicle_CurrentLane,
		FMassEntityHandle PreviousVehicleOnLane,
		FMassTrafficNextVehicleFragment* PreviousVehicleOnLane_NextVehicleFragment,
		FMassEntityHandle NextVehicleOnLane,
		const TArray<struct FZoneGraphTrafficLaneData*>& CandidateLanes,
		const TArray<FVector>& CandidateLaneLocations, bool bVisLog = false
	) const;
	
	FMassEntityQuery RecyclableTrafficVehicleEntityQuery;

	bool bTrunkLanesPhase = false;
	int32 PartitionIndex = 0;

	// Scratch buffers
	TArray<FMassEntityView> BusiestLaneVehiclesToTransfer;
	TArray<struct FZoneGraphTrafficLaneData*> BusiestLanes;
	TArray<float> BusiestLaneDensityExcesses;
	TArray<struct FZoneGraphTrafficLaneData*> LeastBusiestLanes;
	TArray<float> LeastBusiestLaneDensities;
	TArray<FVector> LeastBusiestLaneLocations;
};
