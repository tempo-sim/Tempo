// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficSignUpdateIntersectionsProcessor.h"
#include "MassTraffic.h"
#include "MassTrafficFragments.h"
#include "MassRepresentationFragments.h"
#include "MassExecutionContext.h"
#include "MassCrowdSubsystem.h"

#include "ZoneGraphTypes.h"
#include "DrawDebugHelpers.h"
#include "ZoneGraphSubsystem.h"
#include "MassGameplayExternalTraits.h"
#include "MassZoneGraphNavigationFragments.h"


#define MAX_COUNTED_CROWD_WAIT_AREA_ARRAY 50

#if WITH_MASSTRAFFIC_DEBUG

static constexpr float DebugDrawArrowZOffset = 10.0f;
static constexpr float DebugDrawArrowZOffsetPhaseScale = 10.0f;

#endif

namespace
{
#if WITH_MASSTRAFFIC_DEBUG
	
	FORCEINLINE FVector DrawDebugZOffset(const FMassTrafficSignIntersectionSide& IntersectionSide)
	{
		const float ZOffsetIndex = IntersectionSide.TrafficControllerSignType == EMassTrafficControllerSignType::StopSign
			? static_cast<int32>(IntersectionSide.StopSignIntersectionState)
			: IntersectionSide.TrafficControllerSignType == EMassTrafficControllerSignType::YieldSign
			? static_cast<int32>(IntersectionSide.YieldSignIntersectionState)
			: 0;
		
		return FVector(0.0f, 0.0f, DebugDrawArrowZOffsetPhaseScale * static_cast<float>(ZOffsetIndex) + DebugDrawArrowZOffset);
	}
	
	
	void DrawDebugVehicleLaneArrow(const UWorld& InWorld, const FZoneGraphStorage& ZoneGraphStorage, const int32 LaneIndex, const FMassTrafficSignIntersectionSide& IntersectionSide, const FColor& Color=FColor::White, const bool bPersistentLines = false, const float Lifetime = -1.0f, const uint8 DepthPriority = 0, const float Thickness = 5.0f, const float ArrowSize = 100.0f, const float ArrowLength = 500.0f)
	{
		const FVector PointA = ZoneGraphStorage.LanePoints[ZoneGraphStorage.Lanes[LaneIndex].PointsBegin];
		const FVector PointB = ZoneGraphStorage.LanePoints[ZoneGraphStorage.Lanes[LaneIndex].PointsEnd - 1];			
		const FVector ArrowStartPoint = PointA;
		const FVector ArrowEndPoint = PointA + ((PointB - PointA).GetSafeNormal() * ArrowLength);			

		const FVector ZOffset = DrawDebugZOffset(IntersectionSide);

		DrawDebugDirectionalArrow(&InWorld, ArrowStartPoint + DrawDebugZOffset(IntersectionSide), ArrowEndPoint + ZOffset, ArrowSize, Color, bPersistentLines, Lifetime, DepthPriority, Thickness);
	}

	
	void DrawDebugVehicleLaneArrows(const UWorld& World, const FZoneGraphStorage& ZoneGraphStorage, FMassTrafficSignIntersectionSide& IntersectionSide, const float Lifetime)
	{
		for (const FZoneGraphTrafficLaneData* VehicleIntersectionLane : IntersectionSide.VehicleIntersectionLanes)
		{
			const float Thickness = (VehicleIntersectionLane->HasVehiclesReadyToUseIntersectionLane() ? 20.0f : 5.0f); // (See all READYLANE.)
			
			FColor Color = FColor::White;
			if (VehicleIntersectionLane->bIsOpen && !VehicleIntersectionLane->bIsAboutToClose)
			{
				Color = FColor::Green;
			}
			else if (VehicleIntersectionLane->bIsOpen && VehicleIntersectionLane->bIsAboutToClose)
			{
				Color = FColor::Yellow;
			}
			else if (!VehicleIntersectionLane->bIsOpen)
			{
				Color = FColor::Red;
			}
				
			DrawDebugVehicleLaneArrow(World, ZoneGraphStorage, VehicleIntersectionLane->LaneHandle.Index, IntersectionSide, Color, false, Lifetime, 0, Thickness);
		}
	}

	
	void DrawDebugPedestrianLaneArrow(const UWorld& World, const FZoneGraphStorage& ZoneGraphStorage, const int32 LaneIndex, const FMassTrafficSignIntersectionSide& IntersectionSide, const FColor& Color=FColor::White, const bool bPersistentLines = false, const float Lifetime = -1.0f, const uint8 DepthPriority = 0, const float Thickness = 5.0f, const float ArrowSize=100.0f)
	{
		const FVector PointA = ZoneGraphStorage.LanePoints[ZoneGraphStorage.Lanes[LaneIndex].PointsBegin];
		const FVector PointB = ZoneGraphStorage.LanePoints[ZoneGraphStorage.Lanes[LaneIndex].PointsEnd - 1];			
		const FVector ArrowStartPoint = PointA;
		const FVector ArrowEndPoint = PointB;			

		const FVector ZOffset = DrawDebugZOffset(IntersectionSide);

		DrawDebugDirectionalArrow(&World, ArrowStartPoint + ZOffset, ArrowEndPoint + ZOffset, ArrowSize, Color, bPersistentLines, Lifetime, DepthPriority, Thickness);
	}

	
	void DrawDebugPedestrianLaneArrows(const UWorld& World, const FZoneGraphStorage& ZoneGraphStorage, const UMassCrowdSubsystem& MassCrowdSubsystem, const FMassTrafficSignIntersectionSide& IntersectionSide, const float DrawTime)
	{
		for (const int32 CrosswalkLaneIndex : IntersectionSide.CrosswalkLanes)
		{
			const FZoneGraphLaneHandle LaneHandle(CrosswalkLaneIndex, ZoneGraphStorage.DataHandle);
			const FColor Color = MassCrowdSubsystem.GetLaneState(LaneHandle) == ECrowdLaneState::Opened ? FColor::Green : FColor::Red;					
			DrawDebugPedestrianLaneArrow(World, ZoneGraphStorage, CrosswalkLaneIndex, IntersectionSide, Color, false, DrawTime);
		}		

		for (const int32 CrosswalkWaitingLaneIndex : IntersectionSide.CrosswalkWaitingLanes)
		{
			const FZoneGraphLaneHandle LaneHandle(CrosswalkWaitingLaneIndex, ZoneGraphStorage.DataHandle);
			const FColor Color = MassCrowdSubsystem.GetLaneState(LaneHandle) == ECrowdLaneState::Opened ? FColor::Cyan : FColor::Orange;							
			DrawDebugPedestrianLaneArrow(World, ZoneGraphStorage, CrosswalkWaitingLaneIndex, IntersectionSide, Color, false, DrawTime);
		}		
	}

	
	void DebugDrawTrafficSignIntersectionState(const UWorld& World, const FZoneGraphStorage& ZoneGraphStorage, const UMassCrowdSubsystem& MassCrowdSubsystem, FMassTrafficSignIntersectionFragment& IntersectionFragment, const float Lifetime=0.0f)
	{
		for (FMassTrafficSignIntersectionSide& IntersectionSide : IntersectionFragment.IntersectionSides)
		{
			if (IntersectionSide.TrafficControllerSignType != EMassTrafficControllerSignType::None)
			{
				DrawDebugVehicleLaneArrows(World, ZoneGraphStorage, IntersectionSide, Lifetime);
				DrawDebugPedestrianLaneArrows(World, ZoneGraphStorage, MassCrowdSubsystem, IntersectionSide, Lifetime);
			}
		}
	}
#endif
}

UMassTrafficSignUpdateIntersectionsProcessor::UMassTrafficSignUpdateIntersectionsProcessor()
	: EntityQuery_TrafficSignIntersection(*this)
	, EntityQuery_Vehicle(*this)
{
	ProcessingPhase = EMassProcessingPhase::EndPhysics;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::EndPhysicsTrafficSignIntersectionBehavior;
	
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::EndPhysicsTrafficLightIntersectionBehavior);
}

void UMassTrafficSignUpdateIntersectionsProcessor::ConfigureQueries()
{
	EntityQuery_TrafficSignIntersection.AddRequirement<FMassTrafficSignIntersectionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_TrafficSignIntersection.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
	EntityQuery_TrafficSignIntersection.AddSubsystemRequirement<UMassCrowdSubsystem>(EMassFragmentAccess::ReadWrite);

	EntityQuery_Vehicle.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	EntityQuery_Vehicle.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Vehicle.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);

	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficSignUpdateIntersectionsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UMassTrafficSubsystem& MassTrafficSubsystem = Context.GetMutableSubsystemChecked<UMassTrafficSubsystem>();
	if (!MassTrafficSubsystem.HasTrafficVehicleAgents())
	{
		return;
	}
	
	const UWorld* World = GetWorld();

	if (!ensureMsgf(World != nullptr, TEXT("Must get valid World in UMassTrafficSignUpdateIntersectionsProcessor::Execute.")))
	{
		return;
	}

	TMap<FZoneGraphTrafficLaneData*, float> NearestVehicleMap;
	TMap<FZoneGraphTrafficLaneData*, bool> VehicleInStopQueueMap;
	TMap<FZoneGraphTrafficLaneData*, bool> VehicleCompletedStopSignRestMap;
	
	// Process vehicle chunks.
	EntityQuery_Vehicle.ForEachEntityChunk(EntityManager, Context, [&, World](FMassExecutionContext& QueryContext)
	{
		const TArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = Context.GetMutableFragmentView<FMassTrafficVehicleControlFragment>();
		const TArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = Context.GetMutableFragmentView<FMassZoneGraphLaneLocationFragment>();

		const int32 NumEntities = Context.GetNumEntities();
		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[EntityIndex];
			const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = LaneLocationFragments[EntityIndex];

			if (VehicleControlFragment.NextLane == nullptr || !VehicleControlFragment.NextLane->ConstData.bIsIntersectionLane)
			{
				continue;
			}
			
			const float DistanceToIntersectionForThisLane = FMath::Max(LaneLocationFragment.LaneLength - LaneLocationFragment.DistanceAlongLane, 0.0f);

			// Build NearestVehicleMap.
			if (float* MinDistanceToIntersectionForThisLane = NearestVehicleMap.Find(VehicleControlFragment.NextLane))
			{
				if (DistanceToIntersectionForThisLane < *MinDistanceToIntersectionForThisLane)
				{
					*MinDistanceToIntersectionForThisLane = DistanceToIntersectionForThisLane;
				}
			}
			else
			{
				NearestVehicleMap.Add(VehicleControlFragment.NextLane, DistanceToIntersectionForThisLane);
			}
			
			// Build VehicleInStopQueueMap.
			const bool bIsVehicleInStopQueue = MassTrafficSubsystem.IsVehicleEntityInIntersectionStopQueue(VehicleControlFragment.VehicleEntityHandle, VehicleControlFragment.NextLane->IntersectionEntityHandle);
			if (bool* IsAnyVehicleInStopQueueForLane = VehicleInStopQueueMap.Find(VehicleControlFragment.NextLane))
			{
				if (bIsVehicleInStopQueue)
				{
					*IsAnyVehicleInStopQueueForLane = true;
				}
			}
			else
			{
				VehicleInStopQueueMap.Add(VehicleControlFragment.NextLane, bIsVehicleInStopQueue);
			}
			
			// Build VehicleCompletedStopSignRestMap.
			if (bool* VehicleCompletedStopSignRestForThisLane = VehicleCompletedStopSignRestMap.Find(VehicleControlFragment.NextLane))
			{
				if (VehicleControlFragment.StopSignIntersectionLane == VehicleControlFragment.NextLane)
				{
					*VehicleCompletedStopSignRestForThisLane = true;
				}
			}
			else
			{
				VehicleCompletedStopSignRestMap.Add(VehicleControlFragment.NextLane, VehicleControlFragment.StopSignIntersectionLane == VehicleControlFragment.NextLane);
			}
		}
	});
	
	const auto& IsVehicleNearIntersectionSide = [&NearestVehicleMap](const FMassTrafficSignIntersectionSide& IntersectionSide, const float NearDistance)
	{
		for (FZoneGraphTrafficLaneData* VehicleIntersectionLane : IntersectionSide.VehicleIntersectionLanes)
		{
			if (float* VehicleDistanceToIntersectionSide = NearestVehicleMap.Find(VehicleIntersectionLane))
			{
				if (*VehicleDistanceToIntersectionSide < NearDistance)
				{
					return true;
				}
			}
		}

		return false;
	};

	const auto& AreAnyVehiclesInStopQueueForIntersectionSide = [&VehicleInStopQueueMap](const FMassTrafficSignIntersectionSide& IntersectionSide)
    {
		for (FZoneGraphTrafficLaneData* VehicleIntersectionLane : IntersectionSide.VehicleIntersectionLanes)
		{
			if (bool* IsAnyVehicleInStopQueueForLane = VehicleInStopQueueMap.Find(VehicleIntersectionLane))
			{
				if (*IsAnyVehicleInStopQueueForLane)
				{
					return true;
				}
			}
		}

		return false;
    };
	
	const auto& DidVehicleCompleteStopForIntersectionSide = [&VehicleCompletedStopSignRestMap](const FMassTrafficSignIntersectionSide& IntersectionSide)
	{
		for (FZoneGraphTrafficLaneData* VehicleIntersectionLane : IntersectionSide.VehicleIntersectionLanes)
		{
			if (bool* VehicleCompletedStopSignRestIntersectionSide = VehicleCompletedStopSignRestMap.Find(VehicleIntersectionLane))
			{
				if (*VehicleCompletedStopSignRestIntersectionSide)
				{
					return true;
				}
			}
		}

		return false;
	};

	// Process traffic sign intersection chunks.
	EntityQuery_TrafficSignIntersection.ForEachEntityChunk(EntityManager, Context, [&, World](FMassExecutionContext& QueryContext)
	{
		UMassCrowdSubsystem& MassCrowdSubsystem = QueryContext.GetMutableSubsystemChecked<UMassCrowdSubsystem>();
		const UZoneGraphSubsystem& ZoneGraphSubsystem = QueryContext.GetSubsystemChecked<UZoneGraphSubsystem>();
		
		const TArrayView<FMassTrafficSignIntersectionFragment> TrafficIntersectionFragments = QueryContext.GetMutableFragmentView<FMassTrafficSignIntersectionFragment>();

		const int32 NumEntities = QueryContext.GetNumEntities();

		// TODO:  Make this a property somewhere appropriate.
		constexpr float VehicleTooCloseForPedestriansToCrossAtYieldSignDistance = 5000.0f;

		// TODO:  Make this a property somewhere appropriate.
		constexpr float AllowedVehiclePriorityTime = 5.0f;
		
		// TODO:  Make this a property somewhere appropriate.
		constexpr float RequiredPedestrianWaitToCrossTime = 5.0f;

		// Process all the non-all-way stop sign intersections in this chunk.
		// Note:  Currently, all-way stop sign intersections are handled by
		// the period logic of UMassTrafficLightUpdateIntersectionsProcessor.
		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassTrafficSignIntersectionFragment& IntersectionFragment = TrafficIntersectionFragments[EntityIndex];

			const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(IntersectionFragment.ZoneGraphDataHandle);

			if (!ensureMsgf(ZoneGraphStorage != nullptr, TEXT("Must get valid ZoneGraphStorage in UMassTrafficSignUpdateIntersectionsProcessor::Execute.")))
			{
				continue;
			}

			for (FMassTrafficSignIntersectionSide& IntersectionSide : IntersectionFragment.IntersectionSides)
			{
				// On intersection sides with stop signs, ...
				if (IntersectionSide.TrafficControllerSignType == EMassTrafficControllerSignType::StopSign)
				{
					// The crosswalk lanes stay open until a vehicle comes to a stop and completes its stop sign rest behavior.
					if (IntersectionSide.StopSignIntersectionState == EMassTrafficStopSignIntersectionState::CrosswalkOpen)
					{
						if (DidVehicleCompleteStopForIntersectionSide(IntersectionSide))
						{
							IntersectionFragment.ApplyLanesActionToIntersectionSide(IntersectionSide, EMassTrafficControllerLanesAction::Open, EMassTrafficControllerLanesAction::HardClose, MassCrowdSubsystem, false);
							IntersectionSide.StopSignIntersectionState = EMassTrafficStopSignIntersectionState::VehiclesHaveHighPriority;
							IntersectionSide.TimeHighPriorityStateStarted = World->GetTimeSeconds();
						}
					}

					// Vehicles get a short "high priority" period,
					// where the vehicle lanes remain open and the crosswalk lanes remain closed.
					if (IntersectionSide.StopSignIntersectionState == EMassTrafficStopSignIntersectionState::VehiclesHaveHighPriority)
					{
						const float CurrentTimeSeconds = World->GetTimeSeconds();
						if (CurrentTimeSeconds > IntersectionSide.TimeHighPriorityStateStarted + AllowedVehiclePriorityTime)
						{
							// Once this period is over, the crosswalk lanes re-open.  And, the cycle repeats.
							IntersectionFragment.ApplyLanesActionToIntersectionSide(IntersectionSide, EMassTrafficControllerLanesAction::Open, EMassTrafficControllerLanesAction::Open, MassCrowdSubsystem, false);
							IntersectionSide.StopSignIntersectionState = EMassTrafficStopSignIntersectionState::CrosswalkOpen;
						}
					}
				}
				// On intersection sides with yield signs, ...
				else if (IntersectionSide.TrafficControllerSignType == EMassTrafficControllerSignType::YieldSign)
				{
					// Once pedestrians are waiting to cross this intersection side,
					// we start a short timer to allow multiple pedestrians to potentially queue up.
					if (IntersectionSide.YieldSignIntersectionState == EMassTrafficYieldSignIntersectionState::CrosswalkClosed)
					{
						if (!IntersectionSide.AreAllCrosswalkWaitingLanesClear(IntersectionFragment, MassCrowdSubsystem))
						{
							IntersectionSide.YieldSignIntersectionState = EMassTrafficYieldSignIntersectionState::PedestriansAreWaitingToCross;
							IntersectionSide.TimePedestriansStartedWaitingToCross = World->GetTimeSeconds();
						}
					}

					// Then, pedestrians are allowed to cross if the vehicles are far enough away,
					// or once a nearby vehicle comes to a stop.
					// We also make sure the vehicle lanes are clear before closing them.
					if (IntersectionSide.YieldSignIntersectionState == EMassTrafficYieldSignIntersectionState::PedestriansAreWaitingToCross)
					{
						const float CurrentTimeSeconds = World->GetTimeSeconds();
						if (CurrentTimeSeconds > IntersectionSide.TimePedestriansStartedWaitingToCross + RequiredPedestrianWaitToCrossTime)
						{
							if ((!IsVehicleNearIntersectionSide(IntersectionSide, VehicleTooCloseForPedestriansToCrossAtYieldSignDistance) || AreAnyVehiclesInStopQueueForIntersectionSide(IntersectionSide))
								&& IntersectionSide.AreAllVehicleIntersectionLanesClear())
							{
								IntersectionFragment.ApplyLanesActionToIntersectionSide(IntersectionSide, EMassTrafficControllerLanesAction::HardClose, EMassTrafficControllerLanesAction::Open, MassCrowdSubsystem, false);
								IntersectionSide.YieldSignIntersectionState = EMassTrafficYieldSignIntersectionState::PedestriansHaveHighPriority;
								IntersectionSide.TimeHighPriorityStateStarted = World->GetTimeSeconds();
							}
						}
					}

					// Pedestrians get a short "high priority" period,
					// where the crosswalk lanes remain open and the vehicle lanes remain closed.
					if (IntersectionSide.YieldSignIntersectionState == EMassTrafficYieldSignIntersectionState::PedestriansHaveHighPriority)
					{
						const float CurrentTimeSeconds = World->GetTimeSeconds();
						if (CurrentTimeSeconds > IntersectionSide.TimeHighPriorityStateStarted + AllowedVehiclePriorityTime)
						{
							IntersectionFragment.ApplyLanesActionToIntersectionSide(IntersectionSide, EMassTrafficControllerLanesAction::HardClose, EMassTrafficControllerLanesAction::HardClose, MassCrowdSubsystem, false);
							IntersectionSide.YieldSignIntersectionState = EMassTrafficYieldSignIntersectionState::WaitingForPedestriansToClear;
						}
					}

					// Then, we wait until pedestrians clear the crosswalks before re-opening the vehicle lanes.
					if (IntersectionSide.YieldSignIntersectionState == EMassTrafficYieldSignIntersectionState::WaitingForPedestriansToClear)
                    {
						if (IntersectionSide.AreAllCrosswalkLanesClear(IntersectionFragment, MassCrowdSubsystem))
                    	{
                    		// Once the crosswalks are clear, the vehicle lanes re-open.  And, the cycle repeats.
                    		IntersectionFragment.ApplyLanesActionToIntersectionSide(IntersectionSide, EMassTrafficControllerLanesAction::Open, EMassTrafficControllerLanesAction::HardClose, MassCrowdSubsystem, false);
                    		IntersectionSide.YieldSignIntersectionState = EMassTrafficYieldSignIntersectionState::CrosswalkClosed;
                    	}
                    }
				}

				// Push whether pedestrians are waiting to cross this intersection side to the lanes.
				for (FZoneGraphTrafficLaneData* VehicleIntersectionLane : IntersectionSide.VehicleIntersectionLanes)
				{
					if (VehicleIntersectionLane == nullptr)
					{
						continue;
					}

					VehicleIntersectionLane->bHasPedestriansWaitingToCross = !IntersectionSide.AreAllCrosswalkWaitingLanesClear(IntersectionFragment, MassCrowdSubsystem);
					VehicleIntersectionLane->bHasPedestriansInDownstreamCrosswalkLanes = !IntersectionSide.AreAllCrosswalkLanesClear(IntersectionFragment, MassCrowdSubsystem);
				}
			}

#if WITH_MASSTRAFFIC_DEBUG
			if (GMassTrafficDebugIntersections)
			{
				DebugDrawTrafficSignIntersectionState(*World, *ZoneGraphStorage, MassCrowdSubsystem, IntersectionFragment);
			}
#endif
		}
	});
}
