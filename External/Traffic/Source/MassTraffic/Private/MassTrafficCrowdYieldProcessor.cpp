// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficCrowdYieldProcessor.h"

#include "MassTraffic.h"
#include "MassTrafficFragments.h"
#include "MassCrowdFragments.h"
#include "MassExecutionContext.h"
#include "MassZoneGraphNavigationFragments.h"
#include "ZoneGraphTypes.h"
#include "MassNavigationFragments.h"


UMassTrafficCrowdYieldProcessor::UMassTrafficCrowdYieldProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = int32(EProcessorExecutionFlags::AllNetModes);
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

	EntityQuery.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
}


void UMassTrafficCrowdYieldProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& QueryContext)
		{
			UMassTrafficSubsystem& MassTrafficSubsystem = QueryContext.GetMutableSubsystemChecked<UMassTrafficSubsystem>();

			const TArrayView<FMassMoveTargetFragment> MoveTargetFragments = QueryContext.GetMutableFragmentView<FMassMoveTargetFragment>();
			const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = QueryContext.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
	
			const int32 NumEntities = Context.GetNumEntities();
		
			for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
			{
				const FMassEntityHandle EntityHandle = QueryContext.GetEntity(EntityIndex);
				
				FMassMoveTargetFragment& MoveTargetFragment = MoveTargetFragments[EntityIndex];
				const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = LaneLocationFragments[EntityIndex];

				if (!ensureMsgf(LaneLocationFragment.LaneLength > 0.0f, TEXT("Must have LaneLocationFragment.LaneLength > 0.0f in UMassTrafficCrowdYieldProcessor::Execute.  LaneLocationFragment.LaneHandle.Index: %d."), LaneLocationFragment.LaneHandle.Index))
				{
					return;
				}
				
				FMassTrafficCrosswalkLaneInfo* CrosswalkLaneInfo = MassTrafficSubsystem.GetMutableCrosswalkLaneInfo(LaneLocationFragment.LaneHandle);

				// If this Entity is not on a crosswalk, there's nothing to do here.
				if (CrosswalkLaneInfo == nullptr)
				{
					return;
				}

				const auto& ShouldYieldToIncomingVehicleLane = [&MassTrafficSubsystem, &LaneLocationFragment, this](const FZoneGraphLaneHandle& IncomingVehicleLane)
				{
					const FZoneGraphTrafficLaneData* IncomingTrafficLaneData = MassTrafficSubsystem.GetTrafficLaneData(IncomingVehicleLane);
					
					if (!ensureMsgf(IncomingTrafficLaneData != nullptr, TEXT("Must get valid IncomingTrafficLaneData from IncomingVehicleLane in UMassTrafficCrowdYieldProcessor::Execute.  IncomingVehicleLane.Index: %d."), IncomingVehicleLane.Index))
					{
						return false;
					}
					
					// If there are no vehicles on this lane, we don't need to worry about yielding to them.
					// Crowd Entities are only meant to *reactively* yield to vehicles.
					if (IncomingTrafficLaneData->NumVehiclesOnLane <= 0)
					{
						return false;
					}

					// If this lane already has yielding vehicles, we don't need to yield to them.
					if (IncomingTrafficLaneData->HasYieldingVehicles())
					{
						return false;
					}
					
					const float NormalizedDistanceAlongLane = LaneLocationFragment.DistanceAlongLane / LaneLocationFragment.LaneLength;

					// If we're "too far along the lane", we'll keep going through the intersection.
					// The expectation is that vehicles should yield to us under these conditions.
					if (NormalizedDistanceAlongLane > MassTrafficSettings->NormalizedYieldResumeLaneDistance_Crosswalk_AwayFromIntersectionExit)
					{
						return false;
					}

					// If no vehicle on this lane is yielding, but there are vehicles on this lane,
					// and we're not "too far along the lane", we need to yield to the incoming vehicles.
					return true;
				};

				const bool bWasAlreadyYieldingOnCrosswalk = CrosswalkLaneInfo->IsEntityOnCrosswalkYieldingToAnyLane(EntityHandle);

				bool bShouldYieldOnCrosswalk = false;
				for (const FZoneGraphLaneHandle& IncomingVehicleLane : CrosswalkLaneInfo->IncomingVehicleLanes)
				{
					
					if (ShouldYieldToIncomingVehicleLane(IncomingVehicleLane))
					{
						// We should yield to this incoming vehicle lane.
						// So, start tracking yield state data for the lane.
						CrosswalkLaneInfo->TrackEntityOnCrosswalkYieldingToLane(EntityHandle, IncomingVehicleLane);
						bShouldYieldOnCrosswalk = true;
					}
					else
					{
						// If we're not tracking yield state data for this lane, ignore it.
						if (!CrosswalkLaneInfo->IsEntityOnCrosswalkYieldingToLane(EntityHandle, IncomingVehicleLane))
						{
							continue;
						}

						// Stop tracking yield state data for this lane.
						CrosswalkLaneInfo->UnTrackEntityOnCrosswalkYieldingToLane(EntityHandle, IncomingVehicleLane);
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
