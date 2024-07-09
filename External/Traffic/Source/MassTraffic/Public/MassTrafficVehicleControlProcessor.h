// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#include "MassActorSubsystem.h"
#include "MassTrafficVehicleControlProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficVehicleControlProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficVehicleControlProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;
	
	void SimpleVehicleControl(
		FMassEntityManager& EntityManager,
		UMassTrafficSubsystem& MassTrafficSubsystem,
		FMassExecutionContext& Context,
		const int32 EntityIndex,
		const FAgentRadiusFragment& AgentRadiusFragment,
		const FMassTrafficRandomFractionFragment& RandomFractionFragment,
		const FTransformFragment& TransformFragment,
		const struct FMassSimulationVariableTickFragment& VariableTickFragment,
		FMassTrafficVehicleControlFragment& VehicleControlFragment,
		FMassTrafficVehicleLightsFragment& VehicleLightsFragment,
		FMassZoneGraphLaneLocationFragment& LaneLocationFragment,
		FMassTrafficLaneOffsetFragment& LaneOffsetFragment,
		FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment,
		FMassTrafficVehicleLaneChangeFragment* LaneChangeFragment,
		const FMassTrafficNextVehicleFragment& NextVehicleFragment, const bool bVisLog = false) const;

	void PIDVehicleControl(
		const FMassEntityManager& EntityManager,
		const FMassExecutionContext& Context,
		const int32 EntityIndex,
		const FZoneGraphStorage& ZoneGraphStorage,
		const FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment,
		const FAgentRadiusFragment& AgentRadiusFragment,
		const FMassTrafficRandomFractionFragment& RandomFractionFragment,
		const struct FMassSimulationVariableTickFragment& VariableTickFragment,
		const FTransformFragment& TransformFragment,
		FMassTrafficVehicleLaneChangeFragment* LaneChangeFragment,
		FMassTrafficVehicleControlFragment& VehicleControlFragment,
		FMassTrafficVehicleLightsFragment& VehicleLightsFragment,
		const FMassZoneGraphLaneLocationFragment& LaneLocationFragment,
		FMassTrafficPIDVehicleControlFragment& PIDVehicleControlFragment,
		FMassTrafficPIDControlInterpolationFragment& VehiclePIDMovementInterpolationFragment,
		const FMassTrafficNextVehicleFragment& NextVehicleFragment,
		const bool bVisLog = false) const;

	FMassEntityQuery SimpleVehicleControlEntityQuery_Conditional;
	FMassEntityQuery PIDVehicleControlEntityQuery_Conditional;
};
