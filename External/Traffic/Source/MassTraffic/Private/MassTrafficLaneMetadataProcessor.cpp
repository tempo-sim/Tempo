// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficLaneMetadataProcessor.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassMovementFragments.h"
#include "MassEntityView.h"
#include "ZoneGraphSubsystem.h"
#include "MassGameplayExternalTraits.h"
#include "MassTrafficMovement.h"
#include "MassTrafficSubsystem.h"
#include "MassZoneGraphNavigationFragments.h"

//----------------------------------------------------------------------//
// UMassTrafficLaneMetadataProcessor
//----------------------------------------------------------------------//

// Important:  UMassTrafficLaneMetadataProcessor is meant to run before
// UMassTrafficVehicleControlProcessor and UMassTrafficCrowdYieldProcessor.
// So, this must be setup in DefaultMass.ini.
UMassTrafficLaneMetadataProcessor::UMassTrafficLaneMetadataProcessor()
	: EntityQuery(*this)
{
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleLaneMetadata;
	
	ExecutionOrder.ExecuteBefore.Add(UE::MassTraffic::ProcessorGroupNames::VehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PreVehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleSimulationLOD);
}

void UMassTrafficLaneMetadataProcessor::ConfigureQueries()
{
	EntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadOnly);
	
	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficLaneMetadataProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Gather all Vehicle Entities.
	TArray<FMassEntityHandle> AllVehicleEntities;
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](const FMassExecutionContext& QueryContext)
	{
		const int32 NumEntities = QueryContext.GetNumEntities();
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			AllVehicleEntities.Add(QueryContext.GetEntity(Index));
		}
	});
	
	if (AllVehicleEntities.IsEmpty())
	{
		return;
	}
	
	TArray<FMassEntityHandle> DescendingVehicleEntities(AllVehicleEntities);
	
	// Sort by ZoneGraphStorage Index, then Lane Index, then *descending* DistanceAlongLane.
	DescendingVehicleEntities.Sort(
		[&EntityManager](const FMassEntityHandle& EntityA, const FMassEntityHandle& EntityB)
		{
			const FMassZoneGraphLaneLocationFragment& A = EntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(EntityA);
			const FMassZoneGraphLaneLocationFragment& B = EntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(EntityB);

			if (A.LaneHandle == B.LaneHandle)
			{
				return A.DistanceAlongLane > B.DistanceAlongLane;
			}
			else if (A.LaneHandle.DataHandle == B.LaneHandle.DataHandle)
			{
				return A.LaneHandle.Index < B.LaneHandle.Index;
			}
			else
			{
				return A.LaneHandle.DataHandle.Index < B.LaneHandle.DataHandle.Index;
			}
		}
	);

	UMassTrafficSubsystem& MassTrafficSubsystem = Context.GetMutableSubsystemChecked<UMassTrafficSubsystem>();

	// First, we need to reset the "distance along lane" fields for the "lead" vehicle Entity.
	TArrayView<FMassTrafficZoneGraphData*> MassTrafficZoneGraphDatas = MassTrafficSubsystem.GetMutableTrafficZoneGraphData();
	for (FMassTrafficZoneGraphData* MassTrafficZoneGraphData : MassTrafficZoneGraphDatas)
	{
		if (!ensureMsgf(MassTrafficZoneGraphData != nullptr, TEXT("Expected valid MassTrafficZoneGraphData in UMassTrafficLaneMetadataProcessor::Execute.  Continuing.")))
		{
			continue;
		}
		
		for (FZoneGraphTrafficLaneData& TrafficLaneData : MassTrafficZoneGraphData->TrafficLaneDataArray)
		{
			TrafficLaneData.LeadVehicleEntityHandle.Reset();
			TrafficLaneData.LeadVehicleDistanceAlongLane.Reset();
			TrafficLaneData.LeadVehicleAccelerationEstimate.Reset();
			TrafficLaneData.LeadVehicleSpeed.Reset();
			TrafficLaneData.bLeadVehicleIsYielding.Reset();
			TrafficLaneData.LeadVehicleRadius.Reset();
			TrafficLaneData.LeadVehicleNextLane.Reset();
			TrafficLaneData.LeadVehicleStopSignIntersectionLane.Reset();
			TrafficLaneData.LeadVehicleRandomFraction.Reset();
			TrafficLaneData.LeadVehicleIsNearStopLineAtIntersection.Reset();
			TrafficLaneData.LeadVehicleRemainingStopSignRestTime.Reset();
		}
	}

	// Then, update the "distance along lane" fields for the "lead" vehicle Entity.
	for (int32 SortedVehicleEntityIndex = 0; SortedVehicleEntityIndex < DescendingVehicleEntities.Num(); ++SortedVehicleEntityIndex)
	{
		const FMassEntityHandle LeadVehicleEntityHandle = DescendingVehicleEntities[SortedVehicleEntityIndex];
			
		const FMassEntityView LeadVehicleEntityView(EntityManager, LeadVehicleEntityHandle);
		const FMassZoneGraphLaneLocationFragment& LeadVehicleLaneLocation = LeadVehicleEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		const FMassTrafficVehicleControlFragment& LeadVehicleVehicleControlFragment = LeadVehicleEntityView.GetFragmentData<FMassTrafficVehicleControlFragment>();
		const FAgentRadiusFragment& LeadVehicleRadiusFragment = LeadVehicleEntityView.GetFragmentData<FAgentRadiusFragment>();
		const FMassTrafficRandomFractionFragment& LeadVehicleRandomFractionFragment = LeadVehicleEntityView.GetFragmentData<FMassTrafficRandomFractionFragment>();

		if (LeadVehicleLaneLocation.LaneLength > 0.0f)
		{
			FMassTrafficZoneGraphData* LeadVehicleTrafficZoneGraphData = MassTrafficSubsystem.GetMutableTrafficZoneGraphData(LeadVehicleLaneLocation.LaneHandle.DataHandle);
			if (LeadVehicleTrafficZoneGraphData == nullptr)
			{
				continue;
			}

			FZoneGraphTrafficLaneData* LeadVehicleLaneData = LeadVehicleTrafficZoneGraphData->GetMutableTrafficLaneData(LeadVehicleLaneLocation.LaneHandle);
			if (LeadVehicleLaneData == nullptr)
			{
				continue;
			}

			// We only set the LeadVehicleDistanceAlongLane value for a given lane, if it hasn't already been set.
			// And, since we're indexing into DescendingVehicleEntities, which has been sorted by descending DistanceAlongLane,
			// the first Entity to be indexed for a given lane will in fact be the "Lead Entity" for that lane,
			// and it will have the first (and only) opportunity to set the LeadVehicleDistanceAlongLane field.
			if (!LeadVehicleLaneData->LeadVehicleDistanceAlongLane.IsSet())
			{
				LeadVehicleLaneData->LeadVehicleEntityHandle = LeadVehicleVehicleControlFragment.VehicleEntityHandle;
				LeadVehicleLaneData->LeadVehicleDistanceAlongLane = LeadVehicleLaneLocation.DistanceAlongLane;
				LeadVehicleLaneData->LeadVehicleAccelerationEstimate = LeadVehicleVehicleControlFragment.AccelerationEstimate;
				LeadVehicleLaneData->LeadVehicleSpeed = LeadVehicleVehicleControlFragment.IsYieldingAtIntersection() ? 0.0f : LeadVehicleVehicleControlFragment.Speed;
				LeadVehicleLaneData->bLeadVehicleIsYielding = LeadVehicleVehicleControlFragment.IsYieldingAtIntersection();
				LeadVehicleLaneData->LeadVehicleRadius = LeadVehicleRadiusFragment.Radius;
				LeadVehicleLaneData->LeadVehicleNextLane = LeadVehicleVehicleControlFragment.NextLane;
				LeadVehicleLaneData->LeadVehicleStopSignIntersectionLane = LeadVehicleVehicleControlFragment.StopSignIntersectionLane;
				LeadVehicleLaneData->LeadVehicleRandomFraction = LeadVehicleRandomFractionFragment.RandomFraction.GetFloat();

				LeadVehicleLaneData->LeadVehicleIsNearStopLineAtIntersection = UE::MassTraffic::IsVehicleNearStopLineAtIntersection(
					LeadVehicleVehicleControlFragment.NextLane,
					LeadVehicleLaneLocation.DistanceAlongLane,
					LeadVehicleLaneLocation.LaneLength,
					LeadVehicleRadiusFragment.Radius,
					LeadVehicleRandomFractionFragment.RandomFraction,
					MassTrafficSettings->StoppingDistanceRange);

				if (LeadVehicleVehicleControlFragment.IsVehicleCurrentlyStopped())
				{
					const UWorld* World = MassTrafficSubsystem.GetWorld();
					
					const float CurrentTimeSeconds = World != nullptr ? World->GetTimeSeconds() : TNumericLimits<float>::Lowest();
					const float VehicleStoppedElapsedTime = CurrentTimeSeconds - LeadVehicleVehicleControlFragment.TimeVehicleStopped;
					
					LeadVehicleLaneData->LeadVehicleRemainingStopSignRestTime = LeadVehicleVehicleControlFragment.MinVehicleStopSignRestTime - VehicleStoppedElapsedTime;
				}
			}
		}
	}
}
