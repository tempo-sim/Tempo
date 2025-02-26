// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficCrowdYieldProcessor.h"

#include "MassTraffic.h"
#include "MassTrafficFragments.h"
#include "MassCrowdFragments.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassZoneGraphNavigationFragments.h"
#include "ZoneGraphTypes.h"
#include "MassNavigationFragments.h"
#include "MassTrafficLaneChange.h"


UMassTrafficCrowdYieldProcessor::UMassTrafficCrowdYieldProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::CrowdYieldBehavior;

	// IMPORTANT:  We really want to run *just before* UMassSteerToMoveTargetProcessor,
	// but it doesn't define itself to be in a "Steering" group, in order for us to add it to our ExecuteBefore list.
	// And, it's in the MassAI Plugin, which we haven't started to modify yet.
	// So, we'll just set our ExecuteBefore and ExecuteAfter lists to be the same as UMassSteerToMoveTargetProcessor,
	// which will put us effectively in the same "group" as UMassSteerToMoveTargetProcessor.
	// And then, in **the project's Mass Entity Settings**, we'll add UMassSteerToMoveTargetProcessor
	// to a "Steering" group, and add that "Steering" group to our ExecuteBefore list to ensure
	// UMassTrafficCrowdYieldProcessor runs "just before" UMassSteerToMoveTargetProcessor.
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Tasks);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Avoidance);
}

void UMassTrafficCrowdYieldProcessor::ConfigureQueries()
{
	EntityQuery.AddTagRequirement<FMassCrowdTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassForceFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	
	EntityQuery.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
}


void UMassTrafficCrowdYieldProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& QueryContext)
	{
		UMassTrafficSubsystem& MassTrafficSubsystem = QueryContext.GetMutableSubsystemChecked<UMassTrafficSubsystem>();
		const UZoneGraphSubsystem& ZoneGraphSubsystem = QueryContext.GetSubsystemChecked<UZoneGraphSubsystem>();

		const TArrayView<FMassMoveTargetFragment> MoveTargetFragments = QueryContext.GetMutableFragmentView<FMassMoveTargetFragment>();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = QueryContext.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocityFragments = QueryContext.GetFragmentView<FMassVelocityFragment>();
		const TConstArrayView<FMassForceFragment> ForceFragments = QueryContext.GetFragmentView<FMassForceFragment>();
		const TConstArrayView<FAgentRadiusFragment> RadiusFragments = QueryContext.GetFragmentView<FAgentRadiusFragment>();

		const int32 NumEntities = QueryContext.GetNumEntities();
	
		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassEntityHandle EntityHandle = QueryContext.GetEntity(EntityIndex);
			
			FMassMoveTargetFragment& MoveTargetFragment = MoveTargetFragments[EntityIndex];
			const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = LaneLocationFragments[EntityIndex];
			const FMassVelocityFragment& VelocityFragment = VelocityFragments[EntityIndex];
			const FMassForceFragment& ForceFragment = ForceFragments[EntityIndex];
			const FAgentRadiusFragment& RadiusFragment = RadiusFragments[EntityIndex];

			if (!ensureMsgf(LaneLocationFragment.LaneLength > 0.0f, TEXT("Must have LaneLocationFragment.LaneLength > 0.0f in UMassTrafficCrowdYieldProcessor::Execute.  LaneLocationFragment.LaneHandle.Index: %d."), LaneLocationFragment.LaneHandle.Index))
			{
				continue;
			}

			const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(LaneLocationFragment.LaneHandle.DataHandle);
			if (!ensureMsgf(ZoneGraphStorage != nullptr, TEXT("Must get valid ZoneGraphStorage in UMassTrafficCrowdYieldProcessor::Execute.  LaneLocationFragment.LaneHandle.DataHandle.Index: %d LaneLocationFragment.LaneHandle.DataHandle.Generation: %d."), LaneLocationFragment.LaneHandle.DataHandle.Index, LaneLocationFragment.LaneHandle.DataHandle.Generation))
			{
				continue;
			}
			
			FMassTrafficCrosswalkLaneInfo* CrosswalkLaneInfo = MassTrafficSubsystem.GetMutableCrosswalkLaneInfo(LaneLocationFragment.LaneHandle);

			// If this Entity is not on a crosswalk, there's nothing to do here.
			if (CrosswalkLaneInfo == nullptr)
			{
				continue;
			}

			const bool bWasAlreadyYieldingOnCrosswalk = CrosswalkLaneInfo->IsEntityOnCrosswalkYieldingToAnyLane(EntityHandle);
			const float DesiredSpeed = bWasAlreadyYieldingOnCrosswalk ? CrosswalkLaneInfo->GetEntityYieldResumeSpeed(EntityHandle).Get() : MoveTargetFragment.DesiredSpeed.Get();
			
			const auto& ShouldYieldToIncomingVehicleLane = [&MassTrafficSubsystem, &LaneLocationFragment, &VelocityFragment, &ForceFragment, &RadiusFragment, &ZoneGraphStorage, bWasAlreadyYieldingOnCrosswalk, DesiredSpeed, this](const FZoneGraphTrafficLaneData& CurrentTrafficLaneData, const FZoneGraphTrafficLaneData& CrossingTrafficLaneData)
			{
				// If there are no vehicles on this lane, we don't need to worry about yielding to them.
				if (CurrentTrafficLaneData.NumVehiclesOnLane <= 0)
				{
					return false;
				}
				
				if (!ensureMsgf(CurrentTrafficLaneData.LeadVehicleDistanceAlongLane.IsSet()
					&& CurrentTrafficLaneData.LeadVehicleSpeed.IsSet()
					&& CurrentTrafficLaneData.LeadVehicleRadius.IsSet(),
					TEXT("Since CurrentTrafficLaneData has vehicles, LeadVehicleDistanceAlongLane must be set in UMassTrafficCrowdYieldProcessor::Execute.  CurrentTrafficLaneData.LaneHandle.Index: %d."), CurrentTrafficLaneData.LaneHandle.Index))
				{
					// Since there are vehicles on the incoming vehicle lane,
					// we would like to determine in detail if we should yield to them.
					// However, we hit an ensure, so we will just yield until the vehicles clear,
					// if they are not yielding.

					// TODO:  There are vehicles on the current vehicle lane, but if we yield to them, we might cause a deadlock.  So, return false for now.  Once we have "deadlock protection", we can attempt to return true, here.
					return false;
				}

				FZoneGraphLaneLocation VehicleZoneGraphLaneLocation;
				UE::ZoneGraph::Query::CalculateLocationAlongLane(*ZoneGraphStorage, CurrentTrafficLaneData.LaneHandle, CurrentTrafficLaneData.LeadVehicleDistanceAlongLane.GetValue(), VehicleZoneGraphLaneLocation);
				
				float VehicleEnterTime;
				float VehicleExitTime;

				float VehicleEnterDistance;
				float VehicleExitDistance;

				if (!UE::MassTraffic::TryGetVehicleEnterAndExitTimesForCrossingLane(
					MassTrafficSubsystem,
					*MassTrafficSettings,
					*ZoneGraphStorage,
					CurrentTrafficLaneData,
					CrossingTrafficLaneData,
					LaneLocationFragment.LaneHandle,
					CurrentTrafficLaneData.LeadVehicleDistanceAlongLane.GetValue(),
					CurrentTrafficLaneData.LeadVehicleSpeed.GetValue(),
					CurrentTrafficLaneData.LeadVehicleRadius.GetValue(),
					VehicleEnterTime,
					VehicleExitTime,
					&VehicleEnterDistance,
					&VehicleExitDistance))
				{
					// Since there are vehicles on the incoming vehicle lane,
					// we would like to determine in detail if we should yield to them.
					// However, something went wrong, so we will just yield until the vehicles clear,
					// if they are not yielding.
					
					// TODO:  There are vehicles on the current vehicle lane, but if we yield to them, we might cause a deadlock.  So, return false for now.  Once we have "deadlock protection", we can attempt to return true, here.
					return false;
				}
				
				const bool bVehicleIsInCrosswalkLane = VehicleEnterDistance <= 0.0f && VehicleExitDistance > 0.0f;
				
				// If this lane already has yielding vehicles,
				// we don't need to yield to them, if they're not in the way.
				if (CurrentTrafficLaneData.HasYieldingVehicles() && !bVehicleIsInCrosswalkLane)
				{
					return false;
				}

				FZoneGraphLaneLocation PedestrianZoneGraphLaneLocation;
				UE::ZoneGraph::Query::CalculateLocationAlongLane(*ZoneGraphStorage, LaneLocationFragment.LaneHandle, LaneLocationFragment.DistanceAlongLane, PedestrianZoneGraphLaneLocation);
				const float PedestrianSpeedAlongLane = FVector::DotProduct(VelocityFragment.Value, PedestrianZoneGraphLaneLocation.Tangent);

				const float PedestrianEffectiveSpeedAlongLane = bWasAlreadyYieldingOnCrosswalk ? DesiredSpeed : PedestrianSpeedAlongLane;
				
				float PedestrianEnterTime;
				float PedestrianExitTime;

				float PedestrianEnterDistance;
				float PedestrianExitDistance;

				if (!UE::MassTraffic::TryGetEntityEnterAndExitTimesForCrossingLane(
						MassTrafficSubsystem,
						*MassTrafficSettings,
						*ZoneGraphStorage,
						LaneLocationFragment.LaneHandle,
						CrossingTrafficLaneData.LaneHandle,
						LaneLocationFragment.DistanceAlongLane,
						PedestrianEffectiveSpeedAlongLane,
						RadiusFragment.Radius,
						PedestrianEnterTime,
						PedestrianExitTime,
						&PedestrianEnterDistance,
						&PedestrianExitDistance))
				{
					// If there are vehicles on the incoming vehicle lane, and they are not yielding,
					// we would like to determine in detail if we should yield to them.
					// However, something went wrong, so we will just yield until the vehicles clear.
					
					// TODO:  There are vehicles on the current vehicle lane, but if we yield to them, we might cause a deadlock.  So, return false for now.  Once we have "deadlock protection", we can attempt to return true, here.
					return false;
				}
				
				const bool bInDistanceConflictWithVehicle = bVehicleIsInCrosswalkLane && PedestrianEnterDistance < MassTrafficSettings->PedestrianVehicleBufferDistanceOnCrosswalk && PedestrianEnterDistance > 0.0f;

				// Pedestrians only yield to vehicles when they are in the way.
				// Whereas, vehicles check for both "time conflicts" and "distance conflicts" with pedestrians.
				// This effectively means that pedestrians always have the "right of way" over vehicles.
				if (bInDistanceConflictWithVehicle)
				{
					DrawDebugDirectionalArrow(MassTrafficSubsystem.GetWorld(), PedestrianZoneGraphLaneLocation.Position + FVector(0.0f, 0.0f, 240.0f), VehicleZoneGraphLaneLocation.Position + FVector(0.0f, 0.0f, 240.0f), 1000.0f, FColor::White, false, 0.1f, 0, 10.0f);
					return true;
				}

				// No reason to yield to the lead vehicle on the current lane.
				return false;
			};

			const auto& UpdateYieldTrackingStateForVehicleLane = [&EntityHandle, &CrosswalkLaneInfo](const FZoneGraphLaneHandle& VehicleLane, const bool bShouldYieldOnCrosswalk)
			{
				if (bShouldYieldOnCrosswalk)
				{
					// We should yield to this incoming vehicle lane.
					// So, start tracking yield state data for the lane.
					CrosswalkLaneInfo->TrackEntityOnCrosswalkYieldingToLane(EntityHandle, VehicleLane);
				}
				else
				{
					// If we're currently tracking yield state data for this lane, ...
					if (CrosswalkLaneInfo->IsEntityOnCrosswalkYieldingToLane(EntityHandle, VehicleLane))
					{
						// Stop tracking yield state data for this lane.
						CrosswalkLaneInfo->UnTrackEntityOnCrosswalkYieldingToLane(EntityHandle, VehicleLane);
					}
				}
			};

			bool bShouldYieldOnCrosswalk = false;
			for (const FZoneGraphLaneHandle& IncomingVehicleLane : CrosswalkLaneInfo->IncomingVehicleLanes)
			{
				const FZoneGraphTrafficLaneData* IncomingTrafficLaneData = MassTrafficSubsystem.GetTrafficLaneData(IncomingVehicleLane);
                    				
				if (!ensureMsgf(IncomingTrafficLaneData != nullptr, TEXT("Must get valid IncomingTrafficLaneData from IncomingVehicleLane in UMassTrafficCrowdYieldProcessor::Execute.  IncomingVehicleLane.Index: %d IncomingVehicleLane.DataHandle.Index: %d, IncomingVehicleLane.DataHandle.Generation: %d."), IncomingVehicleLane.Index, IncomingVehicleLane.DataHandle.Index, IncomingVehicleLane.DataHandle.Generation))
				{
					continue;
				}
				
				const bool bShouldYieldOnCrosswalkToIncomingVehicleLane = ShouldYieldToIncomingVehicleLane(*IncomingTrafficLaneData, *IncomingTrafficLaneData);
				UpdateYieldTrackingStateForVehicleLane(IncomingTrafficLaneData->LaneHandle, bShouldYieldOnCrosswalkToIncomingVehicleLane);

				bShouldYieldOnCrosswalk |= bShouldYieldOnCrosswalkToIncomingVehicleLane;

				for (const FZoneGraphTrafficLaneData* PredecessorTrafficLaneData : IncomingTrafficLaneData->PrevLanes)
				{
					if (PredecessorTrafficLaneData == nullptr)
					{
						continue;
					}

					if (PredecessorTrafficLaneData->NumVehiclesOnLane <= 0)
					{
						continue;
					}

					if (!ensureMsgf(PredecessorTrafficLaneData->LeadVehicleNextLane.IsSet(),
						TEXT("Since PredecessorTrafficLaneData has vehicles, required LeadVehicle properties must be set in UMassTrafficCrowdYieldProcessor::Execute.  PredecessorTrafficLaneData->LaneHandle.Index: %d PredecessorTrafficLaneData->LaneHandle.DataHandle.Index: %d PredecessorTrafficLaneData->LaneHandle.DataHandle.Generation: %d."), PredecessorTrafficLaneData->LaneHandle.Index, PredecessorTrafficLaneData->LaneHandle.DataHandle.Index, PredecessorTrafficLaneData->LaneHandle.DataHandle.Generation))
					{
						continue;
					}

					// We're only concerned if the lead vehicle on the Predecessor Lane
					// will be heading into our Incoming Traffic Lane.
					if (PredecessorTrafficLaneData->LeadVehicleNextLane.GetValue() != IncomingTrafficLaneData)
					{
						continue;
					}

					const bool bShouldYieldOnCrosswalkToPredecessorVehicleLane = ShouldYieldToIncomingVehicleLane(*PredecessorTrafficLaneData, *IncomingTrafficLaneData);
					UpdateYieldTrackingStateForVehicleLane(PredecessorTrafficLaneData->LaneHandle, bShouldYieldOnCrosswalkToPredecessorVehicleLane);

					bShouldYieldOnCrosswalk |= bShouldYieldOnCrosswalkToPredecessorVehicleLane;
				}
			}

			// If we should *start* yielding on the crosswalk, ...
			if (!bWasAlreadyYieldingOnCrosswalk && bShouldYieldOnCrosswalk)
			{
				// Let's keep track of our MoveTargetFragment's DesiredSpeed
				// so that we can resume this DesiredSpeed after we're done yielding.
				CrosswalkLaneInfo->TrackEntityOnCrosswalkYieldResumeSpeed(EntityHandle, MoveTargetFragment.DesiredSpeed);
				
				// Set our MoveTargetFragment's DesiredSpeed to zero.
				// This is the mechanism we use to yield.
				MoveTargetFragment.DesiredSpeed = FMassInt16Real(0.0f);
			}
			// If we should *stop* yielding on the crosswalk, ...
			else if (bWasAlreadyYieldingOnCrosswalk && !bShouldYieldOnCrosswalk)
			{
				// Restore our MoveTargetFragment's DesiredSpeed to the value before we started yielding.
				// This will set us in motion again.
				MoveTargetFragment.DesiredSpeed = CrosswalkLaneInfo->GetEntityYieldResumeSpeed(EntityHandle);

				// Finally, we need to completely untrack all yield state data associated with this Entity.
				CrosswalkLaneInfo->UnTrackEntityYieldingOnCrosswalk(EntityHandle);
			}
		}
	});
}
