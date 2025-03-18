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


// Important:  UMassTrafficCrowdYieldProcessor is meant to run before UMassTrafficVehicleControlProcessor.
// So, this must be setup in DefaultMass.ini.
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

		const UWorld* World = QueryContext.GetWorld();

		if (!ensureMsgf(World != nullptr, TEXT("Must get valid World in UMassTrafficCrowdYieldProcessor::Execute.")))
		{
			return;
		}

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
			
			const auto& ShouldYieldToIncomingVehicleLane = [&MassTrafficSubsystem, &LaneLocationFragment, &VelocityFragment, &ForceFragment, &RadiusFragment, &ZoneGraphStorage, &EntityHandle, bWasAlreadyYieldingOnCrosswalk, DesiredSpeed, this](const FZoneGraphTrafficLaneData& CurrentTrafficLaneData, const FZoneGraphTrafficLaneData& CrossingTrafficLaneData, FMassEntityHandle& OutYieldTargetEntity)
			{
				TSet<FMassTrafficCoreVehicleInfo> CoreVehicleInfos;
				if (!MassTrafficSubsystem.TryGetCoreVehicleInfos(CurrentTrafficLaneData.LaneHandle, CoreVehicleInfos))
				{
					// We don't have any info about vehicles on this lane.
					// So, we don't need to yield.
					//
					// Note:  Due to where NumVehiclesOnLane gets updated in the frame,
					// relative to where CoreVehicleInfos gets updated in the frame,
					// NumVehiclesOnLane might be greater than 0 for this lane,
					// while CoreVehicleInfos is empty.
					// However, in this case, the CoreVehicleInfos data for the lane
					// will be populated by the next frame.
					return false;
				}

				// If there are no vehicles on the current lane, ...
				if (CoreVehicleInfos.IsEmpty())
				{
					// We don't need to yield to the current lane.
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
					// Due to UMassTrafficYieldDeadlockResolutionProcessor requirements,
					// if we're unable to determine the yield target Entity, we can't yield.
					// So, we proceed under this error condition.
					return false;
				}

				// Consider each vehicle on the lane.
				for (const FMassTrafficCoreVehicleInfo& CoreVehicleInfo : CoreVehicleInfos)
				{
					// Skip processing our yield logic if we have a yield override for this Lane-Entity Pair.
					// This mechanism is used to prevent yield cycle deadlocks.
					if (MassTrafficSubsystem.HasYieldOverride(
						LaneLocationFragment.LaneHandle, EntityHandle,
						CurrentTrafficLaneData.LaneHandle, CoreVehicleInfo.VehicleEntityHandle))
					{
						continue;
					}
					
					// For vehicles that are not currently on the crossing lane, ...
					if (CurrentTrafficLaneData.LaneHandle != CrossingTrafficLaneData.LaneHandle)
					{
						// Only consider the vehicle if it is heading into the crossing lane.
						if (CoreVehicleInfo.VehicleNextLane == nullptr || CoreVehicleInfo.VehicleNextLane->LaneHandle != CrossingTrafficLaneData.LaneHandle)
						{
							continue;
						}
					}

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
						CoreVehicleInfo.VehicleDistanceAlongLane,
						CoreVehicleInfo.VehicleSpeed,
						CoreVehicleInfo.VehicleRadius,
						VehicleEnterTime,
						VehicleExitTime,
						&VehicleEnterDistance,
						&VehicleExitDistance))
					{
						// Due to UMassTrafficYieldDeadlockResolutionProcessor requirements,
						// if we're unable to determine the yield target Entity, we can't yield.
						// So, we proceed under this error condition.
						return false;
					}
					
					const bool bVehicleIsInCrosswalkLane = VehicleEnterDistance <= 0.0f && VehicleExitDistance > 0.0f;
					
					const bool bInDistanceConflictWithVehicle = bVehicleIsInCrosswalkLane && PedestrianEnterDistance < MassTrafficSettings->PedestrianVehicleBufferDistanceOnCrosswalk && PedestrianEnterDistance > 0.0f;

					// Pedestrians only yield to vehicles when they are in the way.
					// Whereas, vehicles check for both "time conflicts" and "distance conflicts" with pedestrians.
					// This effectively means that pedestrians always have the "right of way" over vehicles.
					if (bInDistanceConflictWithVehicle)
					{
						OutYieldTargetEntity = CoreVehicleInfo.VehicleEntityHandle;
						return true;
					}
				}

				// No reason to yield to the current lane.
				return false;
			};

			const auto& UpdateYieldTrackingStateForVehicleLane = [&MassTrafficSubsystem, &EntityHandle, &CrosswalkLaneInfo, &LaneLocationFragment](const FZoneGraphLaneHandle& YieldTargetVehicleLane, const FMassEntityHandle& YieldTargetEntity, const bool bShouldYieldOnCrosswalk)
			{
				if (bShouldYieldOnCrosswalk)
				{
					// We should yield to this incoming vehicle lane.
					// So, start tracking yield state data for the lane.
					CrosswalkLaneInfo->TrackEntityOnCrosswalkYieldingToLane(EntityHandle, YieldTargetVehicleLane);

					if (ensureMsgf(YieldTargetVehicleLane.IsValid() && YieldTargetEntity.IsValid(), TEXT("Expected valid YieldTargetVehicleLane and YieldTargetEntity in UpdateYieldTrackingStateForVehicleLane in UMassTrafficCrowdYieldProcessor::Execute.  YieldTargetVehicleLane.Index: %d YieldTargetEntity.Index: %d."), YieldTargetVehicleLane.Index, YieldTargetEntity.Index))
					{
						// Add this to the global view of all yields for this frame.
						// The yield deadlock resolution processor will use this information to find and break any yield cycle deadlocks.
						MassTrafficSubsystem.AddYieldInfo(LaneLocationFragment.LaneHandle, EntityHandle, YieldTargetVehicleLane, YieldTargetEntity);
					}
				}
				else
				{
					// If we're currently tracking yield state data for this lane, ...
					if (CrosswalkLaneInfo->IsEntityOnCrosswalkYieldingToLane(EntityHandle, YieldTargetVehicleLane))
					{
						// Stop tracking yield state data for this lane.
						CrosswalkLaneInfo->UnTrackEntityOnCrosswalkYieldingToLane(EntityHandle, YieldTargetVehicleLane);
					}
				}
			};

			FMassEntityHandle YieldTargetEntity;
			bool bShouldYieldOnCrosswalk = false;
			
			for (const FZoneGraphLaneHandle& IncomingVehicleLane : CrosswalkLaneInfo->IncomingVehicleLanes)
			{
				const FZoneGraphTrafficLaneData* IncomingTrafficLaneData = MassTrafficSubsystem.GetTrafficLaneData(IncomingVehicleLane);
                    				
				if (!ensureMsgf(IncomingTrafficLaneData != nullptr, TEXT("Must get valid IncomingTrafficLaneData from IncomingVehicleLane in UMassTrafficCrowdYieldProcessor::Execute.  IncomingVehicleLane.Index: %d IncomingVehicleLane.DataHandle.Index: %d, IncomingVehicleLane.DataHandle.Generation: %d."), IncomingVehicleLane.Index, IncomingVehicleLane.DataHandle.Index, IncomingVehicleLane.DataHandle.Generation))
				{
					continue;
				}

				YieldTargetEntity.Reset();
				
				const bool bShouldYieldOnCrosswalkToIncomingVehicleLane = ShouldYieldToIncomingVehicleLane(*IncomingTrafficLaneData, *IncomingTrafficLaneData, YieldTargetEntity);
				UpdateYieldTrackingStateForVehicleLane(IncomingTrafficLaneData->LaneHandle, YieldTargetEntity, bShouldYieldOnCrosswalkToIncomingVehicleLane);

				bShouldYieldOnCrosswalk |= bShouldYieldOnCrosswalkToIncomingVehicleLane;

				for (const FZoneGraphTrafficLaneData* PredecessorTrafficLaneData : IncomingTrafficLaneData->PrevLanes)
				{
					if (PredecessorTrafficLaneData == nullptr)
					{
						continue;
					}

					YieldTargetEntity.Reset();

					const bool bShouldYieldOnCrosswalkToPredecessorVehicleLane = ShouldYieldToIncomingVehicleLane(*PredecessorTrafficLaneData, *IncomingTrafficLaneData, YieldTargetEntity);
					UpdateYieldTrackingStateForVehicleLane(PredecessorTrafficLaneData->LaneHandle, YieldTargetEntity, bShouldYieldOnCrosswalkToPredecessorVehicleLane);

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

				// Track when we started yielding.
				const float CurrentTimeSeconds = World->GetTimeSeconds();
				CrosswalkLaneInfo->TrackEntityOnCrosswalkYieldStartTime(EntityHandle, CurrentTimeSeconds);
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
			// If we weren't already yielding, and we shouldn't start now, ...
			else if (!bWasAlreadyYieldingOnCrosswalk && !bShouldYieldOnCrosswalk)
			{
				// And, we've got a "near zero" DesiredSpeed, ...
				if (FMath::IsNearlyZero(MoveTargetFragment.DesiredSpeed.Get(), 1.0f))
				{
					// Set a reasonable DesiredSpeed as a failsafe so that if pedestrians
					// get stuck on the crosswalk for any reason, they can resume motion
					// and clear the crosswalk to prevent deadlock issues.
					MoveTargetFragment.DesiredSpeed = FMassInt16Real(MassTrafficSettings->PedestrianFailsafeCrosswalkYieldResumeSpeed);
					UE_LOG(LogMassTraffic, Log, TEXT("[Pedestrian Yield DesiredSpeed Failsafe] Correcting DesiredSpeed to %f for Entity: %d."), MoveTargetFragment.DesiredSpeed.Get(), EntityHandle.Index);
				}
			}
			// If we were already yielding, and should continue yielding, ...
			else // if (bWasAlreadyYieldingOnCrosswalk && bShouldYieldOnCrosswalk)
			{
				const float CurrentTimeSeconds = World->GetTimeSeconds();
				const float EntityYieldStartTime = CrosswalkLaneInfo->GetEntityYieldStartTime(EntityHandle);

				// And, we've been yielding for "too long", potentially involved in an unresolved yield deadlock, ...
				if (CurrentTimeSeconds > EntityYieldStartTime + MassTrafficSettings->PedestrianMaxYieldOnCrosswalkTime)
				{
					// Get a "free pass" to ignore yielding to *any* Entities until we cross the crosswalk,
					// if we don't already have one, in order to prevent unforeseen yield deadlock issues, as a failsafe.
					if (!MassTrafficSubsystem.HasWildcardYieldOverride(LaneLocationFragment.LaneHandle, EntityHandle))
					{
						UE_LOG(LogMassTraffic, Log,
							TEXT("[Pedestrian Yield Override Failsafe] Granting yield override for all potential yield targets to Entity: %d on LaneHandle.Index: %d LaneHandle.DataHandle.Index: %d LaneHandle.DataHandle.Generation: %d."),
							EntityHandle.Index, LaneLocationFragment.LaneHandle.Index, LaneLocationFragment.LaneHandle.DataHandle.Index, LaneLocationFragment.LaneHandle.DataHandle.Generation);
						
						MassTrafficSubsystem.AddWildcardYieldOverride(LaneLocationFragment.LaneHandle, EntityHandle);
					}
				}
			}
		}
	});
}
