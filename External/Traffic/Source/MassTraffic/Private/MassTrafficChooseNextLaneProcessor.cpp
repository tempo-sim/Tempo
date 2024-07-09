// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficChooseNextLaneProcessor.h"
#include "MassTrafficDebugHelpers.h"
#include "MassTrafficFragments.h"
#include "MassTrafficLaneChange.h"
#include "MassTrafficLaneChangingProcessor.h"
#include "MassTrafficMovement.h"
#include "MassTrafficVehicleControlProcessor.h"

#include "DrawDebugHelpers.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassZoneGraphNavigationFragments.h"
#include "ZoneGraphSubsystem.h"
#include "MassGameplayExternalTraits.h"
#include "VisualLogger/VisualLogger.h"

namespace
{			
	enum EDensityToUseForChoosingLane
	{
		ChooseLaneByDownstreamFlowDensity = 0,
		ChooseLaneByFunctionalDensity = 1
	};
}


UMassTrafficChooseNextLaneProcessor::UMassTrafficChooseNextLaneProcessor()
	: EntityQuery_Conditional(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleBehavior;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PreVehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleSimulationLOD);
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficVehicleControlProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficLaneChangingProcessor::StaticClass()->GetFName());
}

void UMassTrafficChooseNextLaneProcessor::ConfigureQueries()
{
	EntityQuery_Conditional.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	EntityQuery_Conditional.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FMassTrafficVehicleLightsFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FMassTrafficNextVehicleFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FMassTrafficDebugFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.SetChunkFilter(&FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);
	EntityQuery_Conditional.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
#if WITH_MASSTRAFFIC_DEBUG
	EntityQuery_Conditional.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
#endif // WITH_MASSTRAFFIC_DEBUG
}

void UMassTrafficChooseNextLaneProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Use the Max of the Speed & Steering look ahead times & distances as our distance from lane exit to choose
	// next lane. This ensures a next lane is chosen in time for the chase targets to move along.
	const float ChooseNextLaneTime = FMath::Max(MassTrafficSettings->SpeedControlLaneLookAheadTime, MassTrafficSettings->SteeringControlLaneLookAheadTime);
	const float ChooseNextLaneMinDistance = FMath::Max(MassTrafficSettings->SpeedControlMinLookAheadDistance, MassTrafficSettings->SteeringControlMinLookAheadDistance);

	// Advance agents
	EntityQuery_Conditional.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& QueryContext)
	{	
		UMassTrafficSubsystem& MassTrafficSubsystem = QueryContext.GetMutableSubsystemChecked<UMassTrafficSubsystem>();
#if WITH_MASSTRAFFIC_DEBUG
		const UZoneGraphSubsystem& ZoneGraphSubsystem = QueryContext.GetSubsystemChecked<UZoneGraphSubsystem>();
#endif // WITH_MASSTRAFFIC_DEBUG
		const int32 NumEntities = QueryContext.GetNumEntities();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = QueryContext.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TConstArrayView<FAgentRadiusFragment> AgentRadiusFragments = QueryContext.GetFragmentView<FAgentRadiusFragment>();
		const TConstArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = QueryContext.GetFragmentView<FMassTrafficRandomFractionFragment>();
		const TArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = QueryContext.GetMutableFragmentView<FMassTrafficVehicleControlFragment>();
		const TArrayView<FMassTrafficVehicleLightsFragment> VehicleLightsFragments = QueryContext.GetMutableFragmentView<FMassTrafficVehicleLightsFragment>();
		const TArrayView<FMassTrafficNextVehicleFragment> NextVehicleFragments = QueryContext.GetMutableFragmentView<FMassTrafficNextVehicleFragment>();
	
		// VisLog
		#if WITH_MASSTRAFFIC_DEBUG
		const TConstArrayView<FMassTrafficDebugFragment> OptionalDebugFragments = QueryContext.GetFragmentView<FMassTrafficDebugFragment>();
		const TConstArrayView<FTransformFragment> TransformFragments = QueryContext.GetFragmentView<FTransformFragment>();
		#endif

	
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = LaneLocationFragments[Index];
			const FAgentRadiusFragment& AgentRadiusFragment = AgentRadiusFragments[Index];
			const FMassTrafficRandomFractionFragment& RandomFractionFragment = RandomFractionFragments[Index];
			FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[Index];
			FMassTrafficVehicleLightsFragment& VehicleLightsFragment = VehicleLightsFragments[Index];
			FMassTrafficNextVehicleFragment& NextVehicleFragment = NextVehicleFragments[Index];
	
		
			// If the vehicle can't stop, it's already reserved itself on its next lane. If we choose a different lane
			// now, we'll permanently upset that counter.
			if (VehicleControlFragment.bCantStopAtLaneExit)
			{
				continue;
			}

			
			// Only ever bother choosing a next lane when we get near the end of our current lane.
			{
				// Get distance threshold to choose next lane based on current speed.
				float ChooseNextLaneDistanceFromLaneEnd = FMath::Max(VehicleControlFragment.Speed * ChooseNextLaneTime, ChooseNextLaneMinDistance);

				// Also make sure we choose a lane with enough time to come to a stop if it's closed.
				const float AssumedVehicleSpeed = FMath::Max(VehicleControlFragment.Speed, VehicleControlFragment.CurrentLaneConstData.SpeedLimit * 0.25);
				ChooseNextLaneDistanceFromLaneEnd = FMath::Max(
					MassTrafficSettings->StopSignBrakingTime * AssumedVehicleSpeed,
					ChooseNextLaneDistanceFromLaneEnd);

				// Make sure we have chosen a lane if we're very close to the end of the lane.
				const float VehicleLength = 2.0f * AgentRadiusFragment.Radius;
				if (ChooseNextLaneDistanceFromLaneEnd < VehicleLength)
				{
					ChooseNextLaneDistanceFromLaneEnd = VehicleLength;
				}
		
				// We only need to choose a lane when we get near the end.
				if (LaneLocationFragment.DistanceAlongLane < (LaneLocationFragment.LaneLength - ChooseNextLaneDistanceFromLaneEnd))
				{
					continue;
				}
			}


			#if ENABLE_DRAW_DEBUG && WITH_MASSTRAFFIC_DEBUG
			if (GMassTrafficDebugChooseNextLane)
			{
				const FMassEntityHandle Entity = Context.GetEntity(Index);
				const FTransformFragment& TransformFragment = EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity);
				const FVector Location = TransformFragment.GetTransform().GetLocation();
										
				if (VehicleControlFragment.NextLane)
				{
					// Show if vehicle has a next lane fragment.
					{
						const FVector Z(0.0f, 0.0f, 600.0f);
						constexpr float Thick = 30.0f;
						constexpr float Time = 0.0f;
						const FColor Color = FColor::Blue;
						DrawDebugLine(GetWorld(), Location + Z, Location, Color, false, Time, 0, Thick);
					}
				
					// Show if next lane is closed ahead.
					{
						const FVector Z(0.0f, 0.0f, 300.0f);
						constexpr float Thick = 60.0f;
						constexpr float Time = 0.0f;
						const FColor Color = (VehicleControlFragment.NextLane->bIsOpen ? FColor::Green : FColor::Red);
						DrawDebugLine(GetWorld(), Location + Z, Location, Color, false, Time, 0, Thick);
					}
				}
			}
			#endif
			
				
			if (VehicleControlFragment.NextLane)
			{
				// If we have chosen a next lane already, should we keep our choice? (See all CHOOSENEWLANEOPEN.)
				if (VehicleControlFragment.NextLane->ConstData.bIsIntersectionLane) // ..next lane is in an intersection
				{
					if (VehicleControlFragment.NextLane->bIsOpen) // ..means upcoming intersection is open for us
					{
						// If we're happy with the lane we chose, then keep it. Otherwise, we'll end up choosing another one.
						if (VehicleControlFragment.ChooseNextLanePreference == EMassTrafficChooseNextLanePreference::KeepCurrentNextLane)
						{
							continue;
						}
					}
					else // ..means upcoming intersection is closed for us
					{
						// Choose a new next lane once the intersection is open again - but only if we're not near the
						// front of the lane now, since cars that change there minds at the front of the lane, that were
						// waiting on a lane and then suddenly aren't once the traffic light changes, look like they're
						// not really correctly paying attention to the traffic lights.
						// -and-
						// If we decide to choose a new lane at this point, we run the risk of not being able to choose
						// one in time, and will drive straight through the intersection instead.
						// (See all CHOOSENEWLANEOPEN.)
						if (LaneLocationFragment.DistanceAlongLane > (LaneLocationFragment.LaneLength - 3.0f * AgentRadiusFragment.Radius))
						{
							VehicleControlFragment.ChooseNextLanePreference = EMassTrafficChooseNextLanePreference::KeepCurrentNextLane;
						}
						else
						{
							VehicleControlFragment.ChooseNextLanePreference = EMassTrafficChooseNextLanePreference::ChooseAnyNextLane;
						}
					
						// Don't choose new lane right now, or we'll re-choosing one over and over, which is slow. (See all CHOOSENEWLANEOPEN.)
						continue;
					}
				}
				else // ..next lane is not in an intersection
				{
					// If we're happy with the lane we chose, then keep it. Otherwise, we'll end up choosing another one.
					if (VehicleControlFragment.ChooseNextLanePreference == EMassTrafficChooseNextLanePreference::KeepCurrentNextLane)
					{
						continue;
					}
				}
			}
			else // ..null next lane
			{
				// Just to make sure.
				VehicleControlFragment.ChooseNextLanePreference = EMassTrafficChooseNextLanePreference::ChooseAnyNextLane;
			}

			// When choosing a lane - we almost always want to make choices using 'downstream density'. But rarely, we'll use
			// 'functional density' instead. 
			// Why?
			// For chains of lanes on a road with no other traffic merging in, the 'downstream density' values can get stuck at
			// a high value. This is because downstream density only gets updated when a car tries to choose a next lane. But that 
			// will never happen if the lane is holding on to a high value so no cars end up attracted to that lane and end up
			// going down it in the first place.
			const EDensityToUseForChoosingLane DensityToUseForChoosingLane =
				RandomStream.FRand() < MassTrafficSettings->DownstreamFlowDensityQueryFraction ?
				ChooseLaneByFunctionalDensity /*rare*/ : 
				ChooseLaneByDownstreamFlowDensity /*common*/;

		
			// If we have a next lane, remove ourselves from it. Then if we choose it again, we'll be added
			// back further on.
			if (VehicleControlFragment.NextLane)
			{
				--VehicleControlFragment.NextLane->NumVehiclesApproachingLane;
			}

			// Dead end check
			FZoneGraphTrafficLaneData& CurrentLane = MassTrafficSubsystem.GetMutableTrafficLaneDataChecked(LaneLocationFragment.LaneHandle);
			if (CurrentLane.NextLanes.IsEmpty())
			{
				// Should never happen. 
				VehicleControlFragment.NextLane = nullptr;
				VehicleControlFragment.ChooseNextLanePreference = EMassTrafficChooseNextLanePreference::ChooseAnyNextLane;
			
				#if WITH_MASSTRAFFIC_DEBUG
					UE_VLOG_LOCATION(&MassTrafficSubsystem, TEXT("MassTraffic Validation"), Error,
						TransformFragments[Index].GetTransform().GetLocation() + FVector(0,0,400), 10.0f, FColor::Red,
						TEXT("Vehicle is on a lane with no NextLane links (a dead end)"));
				#endif
			
				continue;
			}


			// If we only have one next lane or we're an intersection, we can avoid any lane choosing logic
			// entirely.
			if (CurrentLane.NextLanes.Num() == 1)
			{			
				// No choice, must choose this
				VehicleControlFragment.NextLane = CurrentLane.NextLanes[0];
				VehicleControlFragment.ChooseNextLanePreference = EMassTrafficChooseNextLanePreference::KeepCurrentNextLane;
			
				++VehicleControlFragment.NextLane->NumVehiclesApproachingLane;

				VehicleLightsFragment.bLeftTurnSignalLights = VehicleControlFragment.NextLane->bTurnsLeft;
				VehicleLightsFragment.bRightTurnSignalLights = VehicleControlFragment.NextLane->bTurnsRight;

				// While we're here, update downstream traffic density.
				CurrentLane.UpdateDownstreamFlowDensity(MassTrafficSettings->DownstreamFlowDensityMixtureFraction);

				// Check trunk lane restrictions on next lane
				if (!UE::MassTraffic::TrunkVehicleLaneCheck(VehicleControlFragment.NextLane, VehicleControlFragment))
				{
					UE_LOG(LogMassTraffic, Error, TEXT("%s - Trunk-lane-only vehicle %d, on lane %d, can only access a single non-trunk next lane %d."),
						ANSI_TO_TCHAR(__FUNCTION__), QueryContext.GetEntity(Index).Index, CurrentLane.LaneHandle.Index, VehicleControlFragment.NextLane->LaneHandle.Index);
				}
			
				continue;
			}
		

			const float SpaceTakenByVehicleOnLane = UE::MassTraffic::GetSpaceTakenByVehicleOnLane(
				AgentRadiusFragment.Radius, RandomFractionFragment.RandomFraction,
				MassTrafficSettings->MinimumDistanceToNextVehicleRange);


			FZoneGraphTrafficLaneData* BestNextTrafficLaneData = nullptr;
			float BestNextLaneDensity = TNumericLimits<float>::Max();

		
			// This lane might be have intersection lanes as next lanes, so lets run through just those and asses the
			// lane they are connected to.
			for (FZoneGraphTrafficLaneData* NextLane : CurrentLane.NextLanes)
			{
				// Check trunk lane restrictions 
				if (!UE::MassTraffic::TrunkVehicleLaneCheck(NextLane, VehicleControlFragment))
				{
					continue;
				}

				if (!NextLane->ConstData.bIsIntersectionLane)
				{
					// This is not an intersection lane.
				
					// We want a different lane than this one.
					if (VehicleControlFragment.ChooseNextLanePreference == EMassTrafficChooseNextLanePreference::ChooseDifferentNextLane &&
						VehicleControlFragment.NextLane == NextLane)
					{
						continue;
					}
				
					// Consider this lane if it has enough space -or- if it's too short (because if they're all too
					// short, we still have to pick one.)
					const bool bLaneHasEnoughSpaceForVehicle = (NextLane->SpaceAvailable >= SpaceTakenByVehicleOnLane);
					const bool bLaneIsTooShortForVehicle = NextLane->Length < SpaceTakenByVehicleOnLane;
					if (!bLaneHasEnoughSpaceForVehicle && !bLaneIsTooShortForVehicle)
					{
						continue;
					}
				
					// Does this lane have more space than the others? If so, remember it.
					const float NextLaneDensity =
						DensityToUseForChoosingLane == ChooseLaneByDownstreamFlowDensity ?
						NextLane->GetDownstreamFlowDensity() :
						NextLane->FunctionalDensity();
					if (NextLaneDensity <= BestNextLaneDensity)
					{
						BestNextLaneDensity = NextLaneDensity;
						BestNextTrafficLaneData = NextLane;
					}
				}
				else // This is an intersection lane.
				{
					// Intersection lanes must have exactly one next lane - at the intersection exit.
					if (NextLane->NextLanes.Num() != 1)
					{
						UE_LOG(LogMassTraffic, Warning, TEXT("%s - Lane %s is an intersection lane, that should have only one next lane, but it has %d."),
							ANSI_TO_TCHAR(__FUNCTION__), *NextLane->LaneHandle.ToString(), NextLane->NextLanes.Num());

						continue;
					}
				
					// So this is the lane *after* the intersection lane and are actually what we are interested in.
					const FZoneGraphTrafficLaneData* PostIntersectionTrafficLaneData = NextLane->NextLanes[0];

					// We want a different lane than this one.
					if (VehicleControlFragment.ChooseNextLanePreference == EMassTrafficChooseNextLanePreference::ChooseDifferentNextLane &&
						VehicleControlFragment.NextLane == NextLane)
					{
						continue;
					}
				
					// Consider this lane if it has enough space -or- if it's too short (because if they're all too
					// short, we still have to pick one.)
					const bool bLaneHasEnoughSpaceForVehicle = (PostIntersectionTrafficLaneData->SpaceAvailable >= SpaceTakenByVehicleOnLane);
					const bool bLaneIsTooShortForVehicle = PostIntersectionTrafficLaneData->Length < SpaceTakenByVehicleOnLane;
					if (!bLaneHasEnoughSpaceForVehicle && !bLaneIsTooShortForVehicle)
					{
						continue;
					}
				
					// Does this lane have more space than the others? If so, remember it.
					const float PostIntersectionLaneDensity =	
						DensityToUseForChoosingLane == ChooseLaneByDownstreamFlowDensity ?
						PostIntersectionTrafficLaneData->GetDownstreamFlowDensity() :
						PostIntersectionTrafficLaneData->FunctionalDensity();					
					if (PostIntersectionLaneDensity <= BestNextLaneDensity)
					{
						// We are searching the lanes after the intersection so we know which
						// intersection lane to take. That's why NextLane is being set to
						// IntersectionTrafficLaneData and not PostIntersectionTrafficLaneData.
						BestNextLaneDensity = PostIntersectionLaneDensity;
						BestNextTrafficLaneData = NextLane;
					}
				}
			}


			// IMPORTANT - This is one crucial place where we update downstream lane density of a lane.
			// NOTE - The above code should have brought all the current lane's next lanes into the cache, so this
			// should not be expensive.
			CurrentLane.UpdateDownstreamFlowDensity(MassTrafficSettings->DownstreamFlowDensityMixtureFraction);


			if (BestNextTrafficLaneData)
			{
				VehicleControlFragment.NextLane = BestNextTrafficLaneData;
				VehicleControlFragment.ChooseNextLanePreference = EMassTrafficChooseNextLanePreference::KeepCurrentNextLane;
			}
			else
			{
				// Should never happen. Will fail in the check below.
				VehicleControlFragment.NextLane = nullptr;
				VehicleControlFragment.ChooseNextLanePreference = EMassTrafficChooseNextLanePreference::ChooseAnyNextLane;
			}

		
			// Did we pick a new lane?
			if (VehicleControlFragment.NextLane)
			{				
				check(VehicleControlFragment.ChooseNextLanePreference == EMassTrafficChooseNextLanePreference::KeepCurrentNextLane);
			
				// Add ourselves to the number of cars waiting to get onto that lane.
				++VehicleControlFragment.NextLane->NumVehiclesApproachingLane;

				// Update turn signals to reflect our next chosen lane
				VehicleLightsFragment.bLeftTurnSignalLights = VehicleControlFragment.NextLane->bTurnsLeft;
				VehicleLightsFragment.bRightTurnSignalLights = VehicleControlFragment.NextLane->bTurnsRight;

				// If we don't have a current Next vehicle, set the new lane's Tail as our Next
				if (!NextVehicleFragment.HasNextVehicle() && VehicleControlFragment.NextLane->TailVehicle.IsSet())
				{
					NextVehicleFragment.SetNextVehicle(QueryContext.GetEntity(Index), VehicleControlFragment.NextLane->TailVehicle);

					// Sanity check (you can't be your own obstacle)
					checkSlow(NextVehicleFragment.GetNextVehicle() != QueryContext.GetEntity(Index));
				}
			}
			else
			{
				// Disable turn signals
				VehicleLightsFragment.bLeftTurnSignalLights = false;
				VehicleLightsFragment.bRightTurnSignalLights = false;
			}

		
			// VisLog
			#if WITH_MASSTRAFFIC_DEBUG
				#if ENABLE_VISUAL_LOG
				if (OptionalDebugFragments[Index].bVisLog)
				{
					if (VehicleControlFragment.NextLane)
					{
						const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(VehicleControlFragment.NextLane->LaneHandle.DataHandle);
						check(ZoneGraphStorage);
						
						UE_VLOG_ARROW(&MassTrafficSubsystem, TEXT("MassTraffic NextLane"), Display,
							TransformFragments[Index].GetTransform().GetLocation() + FVector(0,0,200),
							UE::MassTraffic::GetLaneMidPoint(VehicleControlFragment.NextLane->LaneHandle.Index, *ZoneGraphStorage) + FVector(0,0,100),
							FColor::Blue, TEXT("Next Lane"));
					}
					else
					{
						UE_VLOG_LOCATION(&MassTrafficSubsystem, TEXT("MassTraffic NextLane"), Error,
							TransformFragments[Index].GetTransform().GetLocation() + FVector(0,0,400), 10.0f, FColor::Red, TEXT("Couldn't Choose Next Lane"));
					}
				}
				#endif

				#if ENABLE_DRAW_DEBUG
				// Debug the chosen next lane.
				if (GMassTrafficDebugChooseNextLane)
				{
					if (VehicleControlFragment.NextLane)
					{
						const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(VehicleControlFragment.NextLane->LaneHandle.DataHandle);
						check(ZoneGraphStorage);
						
						const FMassEntityHandle Entity = Context.GetEntity(Index);
						const FTransformFragment& TransformFragment = EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity);
						const FVector Location = TransformFragment.GetTransform().GetLocation();
						const FVector LaneLocation = UE::MassTraffic::GetLaneEndPoint(VehicleControlFragment.NextLane->LaneHandle.Index, *ZoneGraphStorage);
						const FVector Z(0.0f, 0.0f, 500.0f);
						constexpr float Thick = 10.0f;
						constexpr float Time = 2.0f;
						const FColor Color = FColor::Yellow;
						DrawDebugLine(GetWorld(), Location + Z, Location, Color, false, Time, 0, Thick);
						DrawDebugLine(GetWorld(), Location + Z, LaneLocation, Color, false, Time, 0, Thick);
					}
				}
				#endif
			#endif
		}
	});
}
