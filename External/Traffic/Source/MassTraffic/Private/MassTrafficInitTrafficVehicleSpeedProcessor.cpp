// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficInitTrafficVehicleSpeedProcessor.h"
#include "MassTrafficFragments.h"
#include "MassTrafficMovement.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassZoneGraphNavigationFragments.h"

UMassTrafficInitTrafficVehicleSpeedProcessor::UMassTrafficInitTrafficVehicleSpeedProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = false;
}

void UMassTrafficInitTrafficVehicleSpeedProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassTrafficObstacleAvoidanceFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficInitTrafficVehicleSpeedProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Advance agents
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& QueryContext)
	{
		const int32 NumEntities = QueryContext.GetNumEntities();
		const TConstArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = QueryContext.GetFragmentView<FMassTrafficRandomFractionFragment>();
		const TConstArrayView<FMassTrafficObstacleAvoidanceFragment> AvoidanceFragments = QueryContext.GetFragmentView<FMassTrafficObstacleAvoidanceFragment>();
		const TConstArrayView<FAgentRadiusFragment> RadiusFragments = QueryContext.GetFragmentView<FAgentRadiusFragment>();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = QueryContext.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = QueryContext.GetMutableFragmentView<FMassTrafficVehicleControlFragment>();

		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			const FMassTrafficRandomFractionFragment& RandomFractionFragment = RandomFractionFragments[Index];
			const FAgentRadiusFragment& AgentRadiusFragment = RadiusFragments[Index];
			const FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment = AvoidanceFragments[Index];
			const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = LaneLocationFragments[Index];
			FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[Index];

			
			// Compute stable distance based noise
			const float NoiseValue = UE::MassTraffic::CalculateNoiseValue(VehicleControlFragment.NoiseInput, MassTrafficSettings->NoisePeriod);

			// Calculate varied speed limit
			const float SpeedLimit = UE::MassTraffic::GetSpeedLimitAlongLane(LaneLocationFragment.LaneLength,
				VehicleControlFragment.CurrentLaneConstData.SpeedLimit,
				VehicleControlFragment.CurrentLaneConstData.AverageNextLanesSpeedLimit,
				LaneLocationFragment.DistanceAlongLane, VehicleControlFragment.Speed, MassTrafficSettings->SpeedLimitBlendTime
			);
			const float VariedSpeedLimit = UE::MassTraffic::VarySpeedLimit(SpeedLimit, MassTrafficSettings->SpeedLimitVariancePct, MassTrafficSettings->SpeedVariancePct, RandomFractionFragment.RandomFraction, NoiseValue);
			
			// Should stop?
			bool bRequestDifferentNextLane = false;
			bool bVehicleCantStopAtLaneExit = false;
			bool bIsFrontOfVehicleBeyondEndOfLane = false;
			bool bNoNext = false;
			bool bNoRoom = false;
			const bool bMustStopAtLaneExit = UE::MassTraffic::ShouldStopAtLaneExit(
				LaneLocationFragment.DistanceAlongLane,
				VehicleControlFragment.Speed,
				AgentRadiusFragment.Radius,
				RandomFractionFragment.RandomFraction,
				LaneLocationFragment.LaneLength,
				VehicleControlFragment.NextLane,
				MassTrafficSettings->MinimumDistanceToNextVehicleRange,
				EntityManager,
				/*out*/bRequestDifferentNextLane,
				/*in/out*/bVehicleCantStopAtLaneExit,
				/*out*/bIsFrontOfVehicleBeyondEndOfLane,
				/*out*/bNoRoom,
				/*out*/bNoNext,
				MassTrafficSettings->StandardTrafficPrepareToStopSeconds
			);
			
			// CalculateTargetSpeed has time based variables that use the current speed to convert times
			// to distances. So these eval to non-zero we use VariedSpeedLimit as our stand in Current Speed when
			// computing initial speed.
			const float BaseSpeed = VariedSpeedLimit;

			// CalculateTargetSpeed relies on already having a current speed value set that it can
			//		 use to compute the distance to start braking for avoidance.
			const float TargetSpeed = UE::MassTraffic::CalculateTargetSpeed(
				LaneLocationFragment.DistanceAlongLane,
				BaseSpeed, 
				AvoidanceFragment.DistanceToNext,
				AvoidanceFragment.TimeToCollidingObstacle,
				AvoidanceFragment.DistanceToCollidingObstacle,
				AgentRadiusFragment.Radius,
				RandomFractionFragment.RandomFraction,
				LaneLocationFragment.LaneLength,
				VariedSpeedLimit,
				MassTrafficSettings->IdealTimeToNextVehicleRange,
				MassTrafficSettings->MinimumDistanceToNextVehicleRange,
				/*NextVehicleAvoidanceBrakingPower*/3.0f, // @todo Expose
				MassTrafficSettings->ObstacleAvoidanceBrakingTimeRange,
				MassTrafficSettings->MinimumDistanceToObstacleRange,
				/*ObstacleAvoidanceBrakingPower*/0.5f, // @todo Expose
				MassTrafficSettings->StopSignBrakingTime,
				MassTrafficSettings->StoppingDistanceRange,
				/*StopSignBrakingPower*/0.5f, // @todo Expose
				bMustStopAtLaneExit
			);

			// Init speed to pure target speed
			VehicleControlFragment.Speed = TargetSpeed;
		}
	});
}