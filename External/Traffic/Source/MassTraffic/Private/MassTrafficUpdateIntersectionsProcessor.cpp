// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficUpdateIntersectionsProcessor.h"
#include "MassTraffic.h"
#include "MassTrafficFragments.h"
#include "MassTrafficDebugHelpers.h"
#include "MassRepresentationFragments.h"
#include "MassExecutionContext.h"
#include "MassCrowdSubsystem.h"

#include "ZoneGraphTypes.h"
#include "DrawDebugHelpers.h"
#include "MassCommonFragments.h"
#include "MassLODUtils.h"
#include "ZoneGraphSubsystem.h"
#include "MassGameplayExternalTraits.h"


#define MAX_COUNTED_CROWD_WAIT_AREA_ARRAY 50

#define DEBUG_INTERSECTION_STALLS 0


namespace 
{
	FORCEINLINE void CloseLaneAndAllItsSplitLanes(FZoneGraphTrafficLaneData& TrafficLaneData)
	{
		TrafficLaneData.bIsOpen = false;
		for (FZoneGraphTrafficLaneData* SplitTrafficLaneData : TrafficLaneData.SplittingLanes)
		{
			SplitTrafficLaneData->bIsOpen = false;
		}
	}


	// See all CROSSWALKOVERLAP.
	FORCEINLINE bool IsStoppedVehicleBlockingCrosswalk(FMassTrafficIntersectionFragment& IntersectionFragment, const bool bClearValueOnLane)
	{
		bool bIsStoppedVehicleBlockingCrosswalk = false;
		
		for (FMassTrafficPeriod& Period : IntersectionFragment.Periods)
		{
			for (FZoneGraphTrafficLaneData* TrafficLaneData : Period.VehicleLanes)
			{
				if (TrafficLaneData->bIsStoppedVehicleInPreviousLaneOverlappingThisLane)
				{
					bIsStoppedVehicleBlockingCrosswalk = true;
					
					if (!bClearValueOnLane)
					{
						return true;
					}
					else
					{
						TrafficLaneData->bIsStoppedVehicleInPreviousLaneOverlappingThisLane = false;
					}
				}
			}
		}

		return bIsStoppedVehicleBlockingCrosswalk;
	}
		
	
	FORCEINLINE bool ArePedestriansClearOfIntersection(FMassTrafficIntersectionFragment& IntersectionFragment, const FZoneGraphStorage& ZoneGraphStorage, const UMassCrowdSubsystem* MassCrowdSubsystem)
	{
		if (!IsValid(MassCrowdSubsystem))
		{
			UE_LOG(LogMassTraffic, Error, TEXT("%s - Null crowd subsystem"), ANSI_TO_TCHAR(__FUNCTION__));
			return false;
		}
		
		const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();

		for (const int32 CrosswalkLaneIndex : CurrentPeriod.CrosswalkLanes)
		{
			const FZoneGraphLaneHandle LaneHandle(CrosswalkLaneIndex, ZoneGraphStorage.DataHandle);
			if (!LaneHandle.IsValid())
			{
				UE_LOG(LogMassTraffic, Error, TEXT("%s - Null Zone Graph lane handle for lane index %d"), ANSI_TO_TCHAR(__FUNCTION__), CrosswalkLaneIndex);
				continue;
			}

			const FCrowdTrackingLaneData* CrowdIntersectionData = MassCrowdSubsystem->GetCrowdTrackingLaneData(LaneHandle);
			if (!CrowdIntersectionData)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("%s - Null crowd tracking data ph lane handle for lane index %d"), ANSI_TO_TCHAR(__FUNCTION__), CrosswalkLaneIndex);
				continue;				
			}
			
			if (CrowdIntersectionData->NumEntitiesOnLane > 0)
			{
				return false;
			}
		}

		return true;
	}


	FORCEINLINE bool AreVehiclesClearOfIntersection(FMassTrafficIntersectionFragment& IntersectionFragment, const EMassTrafficIntersectionVehicleLaneType ClearTest, const bool bIncludeReservedVehicles = true)
	{
		const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();	

		for (int32 I = 0; I < CurrentPeriod.NumVehicleLanes(ClearTest); I++)
		{
			const FZoneGraphTrafficLaneData* VehicleLane = CurrentPeriod.GetVehicleLane(I, ClearTest);

			if (VehicleLane->NumVehiclesOnLane > 0)
			{
				return false;
			}
			if (bIncludeReservedVehicles && VehicleLane->NumReservedVehiclesOnLane > 0)
			{
				return false;
			}
		}

		return true;
	}

	
	FORCEINLINE void DebugDrawOccupiedVehicleLanes(const UWorld* World, const FZoneGraphStorage& ZoneGraphStorage, FMassTrafficIntersectionFragment& IntersectionFragment, const EMassTrafficIntersectionVehicleLaneType ClearTest)
	{
		const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();	
		const FVector Z(0.0f, 0.0f, 500.0f);

		for (int32 I = 0; I < CurrentPeriod.NumVehicleLanes(ClearTest); I++)
		{
			const FZoneGraphTrafficLaneData* VehicleLane = CurrentPeriod.GetVehicleLane(I, ClearTest);

			if (!VehicleLane->NumVehiclesOnLane && !VehicleLane->NumReservedVehiclesOnLane)
			{
				continue;
			}

			FColor Color = FColor::White;
			if (VehicleLane->NumVehiclesOnLane > 0 && VehicleLane->NumReservedVehiclesOnLane > 0)
			{
				Color = FColor::Orange;
			}
			else if (VehicleLane->NumVehiclesOnLane > 0)
			{
				Color = FColor::Silver;
			}
			else if (VehicleLane->NumReservedVehiclesOnLane > 0)
			{
				Color = FColor::Yellow;
			}

			const FVector Begin = UE::MassTraffic::GetLaneBeginPoint(VehicleLane->LaneHandle.Index, ZoneGraphStorage);
			const FVector End = UE::MassTraffic::GetLaneEndPoint(VehicleLane->LaneHandle.Index, ZoneGraphStorage);
			DrawDebugLine(World, Begin, Begin + Z, Color, false, 0.0f, 0, 10.0f);
			DrawDebugLine(World, Begin + Z, End, Color, false, 0.0f, 0, 10.0f);

			UE::MassTraffic::DrawDebugStringNearPlayerLocation(World, (0.75f * Begin + 0.25f * End) + Z, VehicleLane->LaneHandle.ToString(), nullptr, Color);
		}
	}

	
	FORCEINLINE int32 NumVehiclesInIntersection(FMassTrafficIntersectionFragment& IntersectionFragment, const EMassTrafficIntersectionVehicleLaneType ClearTest)
	{
		const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();	

		int Count = 0;
		for (int32 I = 0; I < CurrentPeriod.NumVehicleLanes(ClearTest); I++)
		{
			const FZoneGraphTrafficLaneData* VehicleLane = CurrentPeriod.GetVehicleLane(I, ClearTest);

			Count += VehicleLane->NumVehiclesOnLane;
			Count += VehicleLane->NumReservedVehiclesOnLane;
		}

		return Count;
	}

	
	FORCEINLINE bool AreVehiclesWaitingForIntersection(FMassTrafficIntersectionFragment& IntersectionFragment)
	{
		const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();

		for (const FZoneGraphTrafficLaneData* VehicleLane : CurrentPeriod.VehicleLanes)
		{
			if (VehicleLane->bIsVehicleReadyToUseLane) // (See all READYLANE.)
			{
				return true;
			}
		}

		return false;
	}


	FORCEINLINE int32 NumVehiclesWaitingForIntersection(FMassTrafficIntersectionFragment& IntersectionFragment)
	{
		const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();

		int32 Count = 0;
		for (const FZoneGraphTrafficLaneData* TrafficLaneData : CurrentPeriod.VehicleLanes)
		{
			if (TrafficLaneData->bIsVehicleReadyToUseLane) // (See all READYLANE.)
			{
				++Count;
			}
		}

		return Count;
	}

	
	FORCEINLINE bool IsIntersectionClear(FMassTrafficIntersectionFragment& IntersectionFragment, const EMassTrafficIntersectionVehicleLaneType ClearTest, const FZoneGraphStorage& ZoneGraphStorage, const UMassCrowdSubsystem* MassCrowdSubsystem, const bool bIncludeReservedVehicles = true)
	{
		if (AreVehiclesClearOfIntersection(IntersectionFragment, ClearTest, bIncludeReservedVehicles) &&
			ArePedestriansClearOfIntersection(IntersectionFragment, ZoneGraphStorage, MassCrowdSubsystem))
		{
			return true;
		}

		return false;
	}

	
	FORCEINLINE bool IsCurrentPeriodPedestrianOnly(FMassTrafficIntersectionFragment& IntersectionFragment)
	{
		const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();
		
		return CurrentPeriod.VehicleLanes.IsEmpty() && (!CurrentPeriod.CrosswalkLanes.IsEmpty() || !CurrentPeriod.CrosswalkWaitingLanes.IsEmpty()); 
	}

	FORCEINLINE int32 NumPedestriansWaitingForIntersection(FMassTrafficIntersectionFragment& IntersectionFragment, const FZoneGraphStorage& ZoneGraphStorage, const UMassCrowdSubsystem* MassCrowdSubsystem, const UWorld* Wold = nullptr/*..for debugging*/)
	{
		if (!IsValid(MassCrowdSubsystem))
		{
			UE_LOG(LogMassTraffic, Error, TEXT("%s - Null crowd subsystem"), ANSI_TO_TCHAR(__FUNCTION__));
			return 0;
		}
		
		const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();

		TStaticArray<const FCrowdWaitAreaData*, MAX_COUNTED_CROWD_WAIT_AREA_ARRAY> CountedCrowdWaitAreaDataArray(InPlace, nullptr);

		int32 NumPedestrians = 0;
		for (const int32 CrosswalkWaitingLaneIndex : CurrentPeriod.CrosswalkWaitingLanes)
		{
			const FZoneGraphLaneHandle LaneHandle(CrosswalkWaitingLaneIndex, ZoneGraphStorage.DataHandle);
			if (!LaneHandle.IsValid())
			{
				UE_LOG(LogMassTraffic, Error, TEXT("%s - Null Zone Graph lane handle for lane index %d"), ANSI_TO_TCHAR(__FUNCTION__), CrosswalkWaitingLaneIndex);
				continue;
			}

			const FCrowdWaitAreaData* CrowdWaitAreaData = MassCrowdSubsystem->GetCrowdWaitingAreaData(LaneHandle);
			
			if (!CrowdWaitAreaData)
			{
				continue;
			}

			
			int32 CountedCrowdWaitAreaDataArray_Index = 0;
			{
				// Don't worry. Won't scan entire list length. There should not be a lot of these. Didn't wanna use
				// a map, since it allocates on the heap.
				for (const FCrowdWaitAreaData* CountedCrowdWaitAreaData : CountedCrowdWaitAreaDataArray)
				{
					if (CountedCrowdWaitAreaData == CrowdWaitAreaData)
					{
						CountedCrowdWaitAreaDataArray_Index = -1;
						break;
					}
					if (CountedCrowdWaitAreaData == nullptr)
					{
						break;
					}
					
					++CountedCrowdWaitAreaDataArray_Index;
				}
				if (CountedCrowdWaitAreaDataArray_Index < 0)
				{
					continue;
				}
				if (CountedCrowdWaitAreaDataArray_Index >= MAX_COUNTED_CROWD_WAIT_AREA_ARRAY)
				{
					UE_LOG(LogMassTraffic, Error, TEXT("%s - Index:%d >= Max:%d"), ANSI_TO_TCHAR(__FUNCTION__), CountedCrowdWaitAreaDataArray_Index, MAX_COUNTED_CROWD_WAIT_AREA_ARRAY);
					return NumPedestrians;
				}
			}

			
			NumPedestrians += CrowdWaitAreaData->GetNumOccupiedSlots();
			CountedCrowdWaitAreaDataArray[CountedCrowdWaitAreaDataArray_Index] = CrowdWaitAreaData;
		}

		return NumPedestrians;
	}


	FORCEINLINE int32 NumPedestriansCrossing(FMassTrafficIntersectionFragment& IntersectionFragment, const FZoneGraphStorage& ZoneGraphStorage, const UMassCrowdSubsystem* MassCrowdSubsystem)
	{
		if (!IsValid(MassCrowdSubsystem)) return 0;
		
		const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();

		int32 NumPedestrians = 0;
		for (const int32 CrosswalkLaneIndex : CurrentPeriod.CrosswalkLanes)
		{
			const FZoneGraphLaneHandle LaneHandle(CrosswalkLaneIndex, ZoneGraphStorage.DataHandle);
			if (!LaneHandle.IsValid())
			{
				UE_LOG(LogMassTraffic, Error, TEXT("%s - Null Zone Graph lane handle for lane index %d"), ANSI_TO_TCHAR(__FUNCTION__), CrosswalkLaneIndex);
				continue;
			}

			const FCrowdTrackingLaneData* CrowdTrackingData = MassCrowdSubsystem->GetCrowdTrackingLaneData(LaneHandle);
			if (!CrowdTrackingData)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("%s - Null 'crowd tracking data' for lane index %d"), ANSI_TO_TCHAR(__FUNCTION__), CrosswalkLaneIndex);
				continue;
			}

			NumPedestrians += CrowdTrackingData->NumEntitiesOnLane;
		}

		return NumPedestrians;
	}

	
#if WITH_MASSTRAFFIC_DEBUG

	
	constexpr float DebugDrawArrowZOffset = 10.0f;
	constexpr float DebugDrawArrowZOffsetPhaseScale = 10.0f;


	FORCEINLINE FVector DrawDebugZOffset(const FMassTrafficIntersectionFragment& IntersectionFragment)
	{
		return FVector(0.0f, 0.0f, DebugDrawArrowZOffsetPhaseScale * static_cast<float>(IntersectionFragment.CurrentPeriodIndex) + DebugDrawArrowZOffset);
	}
	
	
	void DrawDebugVehicleLaneArrow(const UWorld* InWorld, const FZoneGraphStorage& ZoneGraphStorage, const int32 LaneIndex, const FMassTrafficIntersectionFragment& IntersectionFragment, const FColor& Color=FColor::White, const bool bPersistentLines = false, const float Lifetime = -1.0f, const uint8 DepthPriority = 0, const float Thickness = 5.0f, const float ArrowSize = 100.0f, const float ArrowLength = 500.0f)
	{
		const FVector PointA = ZoneGraphStorage.LanePoints[ZoneGraphStorage.Lanes[LaneIndex].PointsBegin];
		const FVector PointB = ZoneGraphStorage.LanePoints[ZoneGraphStorage.Lanes[LaneIndex].PointsEnd - 1];			
		const FVector ArrowStartPoint = PointA;
		const FVector ArrowEndPoint = PointA + ((PointB - PointA).GetSafeNormal() * ArrowLength);			

		const FVector ZOffset = DrawDebugZOffset(IntersectionFragment);

		DrawDebugDirectionalArrow(InWorld, ArrowStartPoint + DrawDebugZOffset(IntersectionFragment), ArrowEndPoint + ZOffset, ArrowSize, Color, bPersistentLines, Lifetime, DepthPriority, Thickness);
	}

	
	void DrawDebugVehicleLaneArrows(const UWorld* World, const FZoneGraphStorage& ZoneGraphStorage, FMassTrafficIntersectionFragment& IntersectionFragment, const float Lifetime)
	{
		const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();
		
		for (const FZoneGraphTrafficLaneData* VehicleLane : CurrentPeriod.VehicleLanes)
		{
			const float Thickness = (VehicleLane->bIsVehicleReadyToUseLane ? 20.0f : 5.0f); // (See all READYLANE.)
			
			FColor Color = FColor::White;
			if (VehicleLane->bIsOpen && !VehicleLane->bIsAboutToClose)
			{
				Color = FColor::Green;
			}
			else if (VehicleLane->bIsOpen && VehicleLane->bIsAboutToClose)
			{
				Color = FColor::Yellow;
			}
			else if (!VehicleLane->bIsOpen)
			{
				Color = FColor::Red;
			}
				
			DrawDebugVehicleLaneArrow(World, ZoneGraphStorage, VehicleLane->LaneHandle.Index, IntersectionFragment, Color, false, Lifetime, 0, Thickness);
		}
	}

	
	void DrawDebugPedestrianLaneArrow(const UWorld* World, const FZoneGraphStorage& ZoneGraphStorage, const int32 LaneIndex, const FMassTrafficIntersectionFragment& IntersectionFragment, const FColor& Color=FColor::White, const bool bPersistentLines = false, const float Lifetime = -1.0f, const uint8 DepthPriority = 0, const float Thickness = 5.0f, const float ArrowSize=100.0f)
	{
		const FVector PointA = ZoneGraphStorage.LanePoints[ZoneGraphStorage.Lanes[LaneIndex].PointsBegin];
		const FVector PointB = ZoneGraphStorage.LanePoints[ZoneGraphStorage.Lanes[LaneIndex].PointsEnd - 1];			
		const FVector ArrowStartPoint = PointA;
		const FVector ArrowEndPoint = PointB;			

		const FVector ZOffset = DrawDebugZOffset(IntersectionFragment);

		DrawDebugDirectionalArrow(World, ArrowStartPoint + ZOffset, ArrowEndPoint + ZOffset, ArrowSize, Color, bPersistentLines, Lifetime, DepthPriority, Thickness);
	}

	
	void DrawDebugPedestrianLaneArrows(const UWorld* World, const FZoneGraphStorage& ZoneGraphStorage, const UMassCrowdSubsystem* MassCrowdSubsystem, FMassTrafficIntersectionFragment& IntersectionFragment, const float DrawTime)
	{
		const FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();
		
		for (const int32 CrosswalkLaneIndex : CurrentPeriod.CrosswalkLanes)
		{
			const FZoneGraphLaneHandle LaneHandle(CrosswalkLaneIndex, ZoneGraphStorage.DataHandle);
			const FColor Color = MassCrowdSubsystem->GetLaneState(LaneHandle) == ECrowdLaneState::Opened ? FColor::Green : FColor::Red;					
			DrawDebugPedestrianLaneArrow(World, ZoneGraphStorage, CrosswalkLaneIndex, IntersectionFragment, Color, false, DrawTime);
		}		

		for (const int32 CrosswalkWaitingLaneIndex : CurrentPeriod.CrosswalkWaitingLanes)
		{
			const FZoneGraphLaneHandle LaneHandle(CrosswalkWaitingLaneIndex, ZoneGraphStorage.DataHandle);
			const FColor Color = MassCrowdSubsystem->GetLaneState(LaneHandle) == ECrowdLaneState::Opened ? FColor::Cyan : FColor::Orange;							
			DrawDebugPedestrianLaneArrow(World, ZoneGraphStorage, CrosswalkWaitingLaneIndex, IntersectionFragment, Color, false, DrawTime);
		}		
	}

	
	void DebugDrawAllOpenLaneArrowsAndTrafficLights(const UWorld* World, const FZoneGraphStorage& ZoneGraphStorage, const UMassCrowdSubsystem* MassCrowdSubsystem, FMassTrafficIntersectionFragment& IntersectionFragment, const FTransformFragment& TransformFragment/*for debugging*/, const EMassTrafficPeriodLanesAction PeriodAction, const float Lifetime=0.0f)
	{
		DrawDebugVehicleLaneArrows(World, ZoneGraphStorage, IntersectionFragment, Lifetime);
		DrawDebugPedestrianLaneArrows(World, ZoneGraphStorage, MassCrowdSubsystem, IntersectionFragment, Lifetime);
		for (const FMassTrafficLight& TrafficLight : IntersectionFragment.TrafficLights)
		{
			UE::MassTraffic::DrawDebugTrafficLight(World, TrafficLight.Position, TrafficLight.GetXDirection(), nullptr,
				TrafficLight.GetDebugColorForVehicles(),
				TrafficLight.GetDebugColorForPedestrians(EMassTrafficDebugTrafficLightSide::Front),
				TrafficLight.GetDebugColorForPedestrians(EMassTrafficDebugTrafficLightSide::Left),
				TrafficLight.GetDebugColorForPedestrians(EMassTrafficDebugTrafficLightSide::Right),
				false, Lifetime);
		}
	}

	void DrawDebugNumberOfPedestrians(const UWorld* World, FMassTrafficIntersectionFragment& IntersectionFragment, const FZoneGraphStorage& ZoneGraphStorage, const UMassCrowdSubsystem* MassCrowdSubsystem, const FVector& Location, const float Lifetime=0.0f)
	{
		const int32 NumWaiting = NumPedestriansWaitingForIntersection(IntersectionFragment, ZoneGraphStorage, MassCrowdSubsystem);
		if (NumWaiting)
		{
			DrawDebugBox(World, Location + FVector(0.0f,0.0f, 100.0f * NumWaiting),
				FVector(100.0f), FColor::Purple, false, Lifetime);
		}

		const int32 NumCrossing = NumPedestriansCrossing(IntersectionFragment, ZoneGraphStorage, MassCrowdSubsystem);
		if (NumCrossing)
		{
			DrawDebugSphere(World, Location + FVector(0.0f,0.0f, 100.0f * NumCrossing),
				100.0f, 12, FColor::Emerald, false, Lifetime);
		}
	}

	
#endif
}


UMassTrafficUpdateIntersectionsProcessor::UMassTrafficUpdateIntersectionsProcessor()
	: EntityQuery(*this)
{
	ProcessingPhase = EMassProcessingPhase::EndPhysics;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::EndPhysicsIntersectionBehavior;
}

void UMassTrafficUpdateIntersectionsProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassTrafficIntersectionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSubsystemRequirement<UMassCrowdSubsystem>(EMassFragmentAccess::ReadWrite);
#if WITH_MASSTRAFFIC_DEBUG
	EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
#endif

	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficUpdateIntersectionsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// This will skip intersection logic if there are no vehicles that would use the intersections.
	// This does mean they won't proceed through their cycles visually either, but that's okay
	// for this demo as the only time there are no cars is in the cinematic.

	UMassTrafficSubsystem& MassTrafficSubsystem = Context.GetMutableSubsystemChecked<UMassTrafficSubsystem>();
	if (!MassTrafficSubsystem.HasTrafficVehicleAgents())
	{
		return;
	}
	
	// Get world
	const UWorld* World = GetWorld();

	// Process chunks -
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&, World](FMassExecutionContext& QueryContext)
	{
		UMassCrowdSubsystem& MassCrowdSubsystem = QueryContext.GetMutableSubsystemChecked<UMassCrowdSubsystem>();
		const UZoneGraphSubsystem& ZoneGraphSubsystem = QueryContext.GetSubsystemChecked<UZoneGraphSubsystem>();

		const int32 NumEntities = QueryContext.GetNumEntities();
		const float DeltaTimeSeconds = QueryContext.GetDeltaTimeSeconds();
		const TArrayView<FMassTrafficIntersectionFragment> TrafficIntersectionFragments = QueryContext.GetMutableFragmentView<FMassTrafficIntersectionFragment>();
		#if WITH_MASSTRAFFIC_DEBUG
		const TConstArrayView<FMassRepresentationLODFragment> RepresentationLODFragments = QueryContext.GetFragmentView<FMassRepresentationLODFragment>();
		const TConstArrayView<FTransformFragment> TransformFragments = QueryContext.GetFragmentView<FTransformFragment>();
		#endif

		// Process all the intersections in this chunk -
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			FMassTrafficIntersectionFragment& IntersectionFragment = TrafficIntersectionFragments[Index];
			#if WITH_MASSTRAFFIC_DEBUG
			const FTransformFragment& TransformFragment = TransformFragments[Index];
			#endif

			const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(IntersectionFragment.ZoneGraphDataHandle);
			
			if (GMassTrafficDebugIntersections)
			{
#if WITH_MASSTRAFFIC_DEBUG
				const bool bIsStoppedVehicleBlockingCrosswalk = IsStoppedVehicleBlockingCrosswalk(IntersectionFragment, false);
				
				constexpr float Lifetime = 0.0f;
				const FVector Z(0.0f, 0.0f, 100.0f);
				const FString Str = FString::Printf(TEXT("%d - P:%d/%d - TL?%d - Vw:%d Pw:%d - V:%d Vx:%d - Pclr?%d - Cblock?%d - PTR:%.1f"),
					IntersectionFragment.ZoneIndex,
					IntersectionFragment.CurrentPeriodIndex, IntersectionFragment.Periods.Num(),
					IntersectionFragment.bHasTrafficLights,
					NumVehiclesWaitingForIntersection(IntersectionFragment),
					NumPedestriansWaitingForIntersection(IntersectionFragment, *ZoneGraphStorage, &MassCrowdSubsystem),
					NumVehiclesInIntersection(IntersectionFragment, EMassTrafficIntersectionVehicleLaneType::VehicleLane),
					NumVehiclesInIntersection(IntersectionFragment, EMassTrafficIntersectionVehicleLaneType::VehicleLane_ClosedInNextPeriod),
					ArePedestriansClearOfIntersection(IntersectionFragment, *ZoneGraphStorage, &MassCrowdSubsystem),
					bIsStoppedVehicleBlockingCrosswalk, // (See all CROSSWALKOVERLAP.)
					IntersectionFragment.PeriodTimeRemaining.GetFloat());

				DebugDrawOccupiedVehicleLanes(World, *ZoneGraphStorage, IntersectionFragment, EMassTrafficIntersectionVehicleLaneType::VehicleLane);
				
				UE::MassTraffic::DrawDebugStringNearPlayerLocation(World, TransformFragment.GetTransform().GetLocation() + Z
						, Str, nullptr, FColor::White, Lifetime, true, 1.0f);
				
				if (bIsStoppedVehicleBlockingCrosswalk)
				{
					UE::MassTraffic::DrawDebugZLine(World, TransformFragment.GetTransform().GetLocation(), FColor::Purple, false, 0.0f, 50.0f, 200000.0f);
				}
#endif // WITH_MASSTRAFFIC_DEBUG
			}

			// Skip empty intersections.
			if (IntersectionFragment.Periods.IsEmpty())
			{
				continue;
			}

			#if WITH_MASSTRAFFIC_DEBUG
				// Limit debug drawing to the High LOD of the intersections.
				const FMassRepresentationLODFragment& RepresentationLODFragment = RepresentationLODFragments[Index];
				const bool bDoDrawDebug = GMassTrafficDebugIntersections && (RepresentationLODFragment.LOD <= EMassLOD::High);
				if (bDoDrawDebug)
				{
					DrawDebugNumberOfPedestrians(World, IntersectionFragment, *ZoneGraphStorage, &MassCrowdSubsystem, TransformFragment.GetTransform().GetLocation());
				}
			#endif

			const float PeriodTimeRemaining_BeforeUpdate = IntersectionFragment.PeriodTimeRemaining;

			FMassTrafficPeriod& CurrentPeriod = IntersectionFragment.GetCurrentPeriod();

			// See if any of this period's vehicle lanes are actually open.
			bool bPeriodHasAnyOpenVehicleLanes = false;
			{
				for (int32 VehicleLaneLaneIndex = 0;
					VehicleLaneLaneIndex < CurrentPeriod.NumVehicleLanes(EMassTrafficIntersectionVehicleLaneType::VehicleLane);
					VehicleLaneLaneIndex++)
				{
					const FZoneGraphTrafficLaneData* VehicleLane = CurrentPeriod.GetVehicleLane(VehicleLaneLaneIndex, EMassTrafficIntersectionVehicleLaneType::VehicleLane);
					if (VehicleLane->bIsOpen)
					{
						bPeriodHasAnyOpenVehicleLanes = true;
						break;
					}
				}
			}

			// See if any of this period's pedestrian lanes are actually open. (Just need to check waiting lanes.)
			bool bPeriodHasAnyOpenCrosswalkLanes = false;
			{
				for (const int32 CrosswalkLaneIndex : CurrentPeriod.CrosswalkLanes)
				{
					const FZoneGraphLaneHandle LaneHandle(CrosswalkLaneIndex, IntersectionFragment.ZoneGraphDataHandle);
					if (MassCrowdSubsystem.GetLaneState(LaneHandle) == ECrowdLaneState::Opened)
					{
						bPeriodHasAnyOpenCrosswalkLanes = true;
						break;
					}
				}
			}

			
			// Count down time remaining for this period.
			
			if (IntersectionFragment.PeriodTimeRemaining > 0.0f)
			{
				const float CountDownSpeedSeconds = DeltaTimeSeconds;

				
				// Check if we can zoom by this period, or if we need to wait.
				if ((IsCurrentPeriodPedestrianOnly(IntersectionFragment) && !bPeriodHasAnyOpenCrosswalkLanes) ||
					(!IsCurrentPeriodPedestrianOnly(IntersectionFragment) && !bPeriodHasAnyOpenCrosswalkLanes && !bPeriodHasAnyOpenVehicleLanes))
				{
					IntersectionFragment.PeriodTimeRemaining = -DeltaTimeSeconds;
				}
				else if (IntersectionFragment.bHasTrafficLights)
				{

					// This intersection has traffic lights.

					// End this traffic light vehicle and/or pedestrian period if..
					if (// ..cars are no longer entering the intersection from this period..
						IsIntersectionClear(IntersectionFragment, EMassTrafficIntersectionVehicleLaneType::VehicleLane, *ZoneGraphStorage, &MassCrowdSubsystem) &&
						// ..AND intersection has no cars waiting to enter the it..
						!AreVehiclesWaitingForIntersection(IntersectionFragment) &&
						// ..AND intersection has no open pedestrian lanes..
						!bPeriodHasAnyOpenCrosswalkLanes &&
						// ..AND we're not showing a yellow light..
						IntersectionFragment.PeriodTimeRemaining > MassTrafficSettings->StandardTrafficPrepareToStopSeconds)
					{
						// Go to yellow light.
						IntersectionFragment.PeriodTimeRemaining = MassTrafficSettings->StandardTrafficPrepareToStopSeconds - DeltaTimeSeconds;
					}
				}
				else // ..no traffic lights, means stop-sign intersection
				{
					
					// This intersection does not have traffic lights. It functions as a stop-sign intersection.
					
					// A vehicle has entered the intersection. Close the lane it's on, and all the lanes from the
					// same intersection side (using it's splitting lanes.)
					for (FZoneGraphTrafficLaneData* TrafficLaneData : CurrentPeriod.VehicleLanes)
					{
						if (TrafficLaneData->NumVehiclesOnLane)
						{
							CloseLaneAndAllItsSplitLanes(*TrafficLaneData);
						}
					}

					bool bAreVehicleLanesInThisPeriodOpenAndReady = false;
					for (const FZoneGraphTrafficLaneData* IntersectionTrafficLaneData : CurrentPeriod.VehicleLanes)
					{
						if (IntersectionTrafficLaneData->bIsOpen && IntersectionTrafficLaneData->bIsVehicleReadyToUseLane) // (See all READYLANE.)
						{
							bAreVehicleLanesInThisPeriodOpenAndReady = true;
							break;
						}
					}
					
					if (!bAreVehicleLanesInThisPeriodOpenAndReady && !bPeriodHasAnyOpenCrosswalkLanes)
					{
						IntersectionFragment.PeriodTimeRemaining = -DeltaTimeSeconds;
					}
				}


				// Update traffic lights.
				// (Do this before we count down period time remaining, so lights don't flash red if yellow light is done.)
				IntersectionFragment.UpdateTrafficLightsForCurrentPeriod();

				
				IntersectionFragment.PeriodTimeRemaining = IntersectionFragment.PeriodTimeRemaining - CountDownSpeedSeconds;
			}
			

			
			// Lambda, so it has access to everything here.
			auto DrawDebugPeriod = [&]()
			{
				#if WITH_MASSTRAFFIC_DEBUG
				if (bDoDrawDebug) 
				{
					DebugDrawAllOpenLaneArrowsAndTrafficLights(World, *ZoneGraphStorage, &MassCrowdSubsystem, IntersectionFragment, TransformFragment, EMassTrafficPeriodLanesAction::Open);
				}
				#endif
			};

			

			// Tell all the lanes in this period they will close soon.
			if (IntersectionFragment.PeriodTimeRemaining <= MassTrafficSettings->StandardTrafficPrepareToStopSeconds &&
				IntersectionFragment.PeriodTimeRemaining > 0.0f/*optimization*/)
			{

				IntersectionFragment.ApplyLanesActionToCurrentPeriod(
					EMassTrafficPeriodLanesAction::SoftPrepareToClose, EMassTrafficPeriodLanesAction::None,
					&MassCrowdSubsystem, false);

				
				IntersectionFragment.UpdateTrafficLightsForCurrentPeriod();

				
				// Tell lanes how long they have until they close.
				for (int32 I = 0; I < CurrentPeriod.NumVehicleLanes(EMassTrafficIntersectionVehicleLaneType::VehicleLane_ClosedInNextPeriod); I++)
				{
					FZoneGraphTrafficLaneData* OpenVehicleLane = CurrentPeriod.GetVehicleLane(I, EMassTrafficIntersectionVehicleLaneType::VehicleLane_ClosedInNextPeriod);

					OpenVehicleLane->FractionUntilClosed =
						MassTrafficSettings->StandardTrafficPrepareToStopSeconds > 0.0f ?
						IntersectionFragment.PeriodTimeRemaining / MassTrafficSettings->StandardTrafficPrepareToStopSeconds :
						0.0f;
				}
			}

			
			if (IntersectionFragment.PeriodTimeRemaining <= 0.0f && PeriodTimeRemaining_BeforeUpdate > 0.0f)
			{

				// Close all lanes that close in next period.
				IntersectionFragment.ApplyLanesActionToCurrentPeriod(
					EMassTrafficPeriodLanesAction::SoftClose, EMassTrafficPeriodLanesAction::HardClose,
					&MassCrowdSubsystem, false);
				

				IntersectionFragment.UpdateTrafficLightsForCurrentPeriod();
				IntersectionFragment.PedestrianLightsShowStop();

				
				// IMPORTANT - We have just closed lanes. Some vehicles may be overlapping the crosswalks, and will
				// want to keep going, and will register their occupancy on one of the intersection lanes. We need to
				// not advance to the next period quite yet, to give them a chance to do this.
				DrawDebugPeriod();
				continue; // ..next intersection
			}

			
			if (IntersectionFragment.PeriodTimeRemaining <= 0.0f && PeriodTimeRemaining_BeforeUpdate <= 0.0f)
			{

				// Should we open another period yet? Or wait for this one to clear?

#if DEBUG_INTERSECTION_STALLS
				// See all INTERSTALL.
				// 1min at 30fps..
				const int32 StallCounterAlert = 1800;
#endif
				
				if (!IsIntersectionClear(IntersectionFragment, EMassTrafficIntersectionVehicleLaneType::VehicleLane_ClosedInNextPeriod, *ZoneGraphStorage, &MassCrowdSubsystem))
				{
#if DEBUG_INTERSECTION_STALLS
					// See all INTERSTALL.
					++IntersectionFragment.StallCounter;
					if (IntersectionFragment.StallCounter == StallCounterAlert)
					{
						UE_LOG(LogTemp, Warning, TEXT("INTERSECTION STALL %d"), IntersectionFragment.ZoneIndex);										
						const FString Str = FString::Printf(TEXT("STALL %d - LOD:%d - TL?%d"),
							IntersectionFragment.ZoneIndex,
							static_cast<int32>(RepresentationLODFragment.LOD),
							IntersectionFragment.bHasTrafficLights);
						UE::MassTraffic::LogBugItGo(TransformFragment.GetTransform().GetLocation(), Str);
					}
					if (IntersectionFragment.StallCounter >= StallCounterAlert)
					{
						UE::MassTraffic::DrawDebugZLine(World, TransformFragment.GetTransform().GetLocation(), FColor::Orange, false, 0.0f, 50.0f, 20000.0f);
					}
#endif

					DrawDebugPeriod();
					continue; // ..next intersection entity
				}

#if DEBUG_INTERSECTION_STALLS
				// See all INTERSTALL.
				if (IntersectionFragment.StallCounter >= StallCounterAlert)
				{
					UE_LOG(LogTemp, Warning, TEXT("INTERSECTION UNSTALL %d"), IntersectionFragment.ZoneIndex);										
				}
				IntersectionFragment.StallCounter = 0;
#endif
				
		
				// Move on to the next period.
				IntersectionFragment.AdvancePeriod();


				// Open the next period.
				// We only open the vehicle lanes if at least one vehicle has stated it's 'ready' to use one of them
				// We only open the crosswalk lanes if there are actually enough pedestrians waiting.
				// We do this, because we don't want them to start walking if the next period ends up getting ended early.
				// It takes them a while to get off the curb onto the crosswalk, and the intersection won't sense this in time.
				{
					const EMassTrafficPeriodLanesAction VehicleLanesAction =
						AreVehiclesWaitingForIntersection(IntersectionFragment) ?
						EMassTrafficPeriodLanesAction::Open :
						EMassTrafficPeriodLanesAction::SoftClose; 
	
					
					const int32 MinPedestrians =
						IntersectionFragment.bHasTrafficLights ?
						MassTrafficSettings->MinPedestriansForCrossingAtTrafficLights :
						MassTrafficSettings->MinPedestriansForCrossingAtStopSigns;

					// Stop-sign intersections get too blocked up too slowly if we let pedestrians cross too often.
					// But made option for traffic-light intersections too.
					const bool bCanOpenPedestrianLanesByProbability =
						IntersectionFragment.bHasTrafficLights ?
						RandomStream.FRand() <= MassTrafficSettings->TrafficLightPedestrianLaneOpenProbability :
						RandomStream.FRand() <= MassTrafficSettings->StopSignPedestrianLaneOpenProbability;
					
					const EMassTrafficPeriodLanesAction PedestrianLanesAction =
							bCanOpenPedestrianLanesByProbability &&
							NumPedestriansWaitingForIntersection(IntersectionFragment, *ZoneGraphStorage, &MassCrowdSubsystem) >= MinPedestrians &&
							// WARNING - If there are no pedestrians in the level, this will never end up being executed, so the value will never be cleared -
							!IsStoppedVehicleBlockingCrosswalk(IntersectionFragment, true) /*(See all CROSSWALKOVERLAP.)*/ ?
						EMassTrafficPeriodLanesAction::Open :
						EMassTrafficPeriodLanesAction::HardClose;

					
					IntersectionFragment.ApplyLanesActionToCurrentPeriod(
						VehicleLanesAction, PedestrianLanesAction,
						&MassCrowdSubsystem, false);


					IntersectionFragment.UpdateTrafficLightsForCurrentPeriod();

					
					IntersectionFragment.AddTimeRemainingToCurrentPeriod();
				}
			}

			
			// NOTE - This will run only if we have not skipped to the next intersection. (See the 'continue' above.)
			DrawDebugPeriod();
		}
	});
}
