// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficLaneChangingProcessor.h"
#include "MassTraffic.h"
#include "MassTrafficDebugHelpers.h"
#include "MassTrafficFragments.h"
#include "MassTrafficInterpolation.h"
#include "MassTrafficLaneChange.h"
#include "MassTrafficMovement.h"
#include "MassTrafficOverseerProcessor.h"
#include "MassExecutionContext.h"
#include "MassEntityView.h"
#include "MassLODUtils.h"
#include "MassZoneGraphNavigationFragments.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphTypes.h"
#include "MassGameplayExternalTraits.h"

#define DEBUG_LANE_CHANGE_LEVEL 0


using namespace UE::MassTraffic;

static bool CanAnyEntitiesLaneChangeInChunk(const FMassExecutionContext& Context, const UMassTrafficSettings& MassTrafficSettings)
{
	const EMassLOD::Type ChunkLODLevel = UE::MassLOD::GetLODFromArchetype(Context);
	
	// Check lane change modes.
	if (GMassTrafficLaneChange == -1 /*lane changing controlled by coordinator*/)
	{
		if (MassTrafficSettings.LaneChangeMode == EMassTrafficLaneChangeMode::Off)
		{
			return false;
		}
		else if (GMassTrafficLaneChange == 2 /*lane changing allowed only for off-LOD vehicles*/ && ChunkLODLevel != EMassLOD::Off)
		{
			return false;	
		}
	}		
	else
	{
		if (GMassTrafficLaneChange == 0 /*lane changing is off (no lane changing allowed at all)*/)
		{
			return false;
		}
		else if (GMassTrafficLaneChange == 2 /*lane changing allowed only for off-LOD vehicles*/ && ChunkLODLevel != EMassLOD::Off)
		{
			return false;
		}
	}

	return true;
}


static void TryStartingNewLaneChange(
	FMassEntityHandle Entity_Current,
	FMassExecutionContext& Context,
	const FAgentRadiusFragment& AgentRadiusFragment_Current,
	const FMassTrafficRandomFractionFragment& RandomFractionFragment_Current,
	FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
	FTransformFragment& TransformFragment_Current,
	FMassTrafficInterpolationFragment& InterpolationFragment_Current,
	FMassTrafficVehicleControlFragment& VehicleControlFragment_Current,
	FMassTrafficVehicleLightsFragment& VehicleLightsFragment_Current,
	FMassZoneGraphLaneLocationFragment& ZoneGraphLaneLocationFragment_Current,
	FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment_Current,
	FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment_Current,
	bool bVisLog,
	//
	UMassTrafficSubsystem& MassTrafficSubsystem,
	const UMassTrafficSettings& MassTrafficSettings,
	FRandomStream& RandomStream,
	FMassEntityManager& EntityManager, const FZoneGraphStorage& ZoneGraphStorage)
{
	// Don't consider starting a new lane change for this vehicle if -
	//		(1) It has a lane change already in progress.
	//		(2) It's on a lane that it's not allowed to change lanes on.
	//		(3) If lanes are splitting or merging (no lane changes allowed on these.)
	//		(4) All lane changes are blocked until we get on the next lane. 
	//		(5) The lane change sleep timer is not zero. This helps with performance.
	//		(6) Vehicle says it can't stop, and has registered itself with a next lane it must go onto next.
	//          If the vehicle can't stop, it's already reserved itself on its next lane. If we choose a different lane
	//          now, we'll permanently upset that counter.
	//		(7) It's not time to lane change AND the lanes are not transverse. If they're transverse, we consider a lane
	//			change anyway, because these are regions where lanes used to merge and split at the same time, and means
	//			the car should consider changing lanes.


	// See (1) (2) (4) (5) (6) above.
	if (LaneChangeFragment_Current.IsLaneChangeInProgress() ||
		LaneChangeFragment_Current.bBlockAllLaneChangesUntilNextLane ||
		LaneChangeFragment_Current.StaggeredSleepCounterForStartNewLaneChanges ||
		!VehicleControlFragment_Current.CurrentLaneConstData.bIsLaneChangingLane ||
		VehicleControlFragment_Current.bCantStopAtLaneExit)
	{
		return;
	}
	
	const int32 LaneIndex_Current = ZoneGraphLaneLocationFragment_Current.LaneHandle.Index;
	FZoneGraphTrafficLaneData* Lane_Current = MassTrafficSubsystem.GetMutableTrafficLaneData(ZoneGraphLaneLocationFragment_Current.LaneHandle);
	check(Lane_Current->LaneHandle.DataHandle == ZoneGraphStorage.DataHandle);

	// See (3) above.
	if (!Lane_Current->SplittingLanes.IsEmpty() ||
		!Lane_Current->MergingLanes.IsEmpty())
	{
		return;		
	}

	// See (7) above.
	if (!LaneChangeFragment_Current.IsTimeToAttemptLaneChange() &&
		!Lane_Current->bHasTransverseLaneAdjacency)
	{
		return;
	}

	
	// Choose which lane to change to (if any.)

	const float DistanceAlongLane_Current = ZoneGraphLaneLocationFragment_Current.DistanceAlongLane;
	const float LaneLength_Current = ZoneGraphLaneLocationFragment_Current.LaneLength;
	
	FMassTrafficLaneChangeRecommendation LaneChangeRecommendation; 
	ChooseLaneForLaneChange(
		DistanceAlongLane_Current,
		Lane_Current, AgentRadiusFragment_Current, RandomFractionFragment_Current, VehicleControlFragment_Current,
		RandomStream,
		MassTrafficSettings,
		/*out*/LaneChangeRecommendation);

	
	if (LaneChangeRecommendation.Level == StayOnCurrentLane_RetryNormal)
	{
		#if DEBUG_LANE_CHANGE_LEVEL
		DrawDebugZLine(Coordinator.GetWorld(), TransformFragment_Current.GetTransform().GetLocation(), FColor::Green, false, 0.5f, 50.0f);
		#endif

		LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings, RandomStream);

		// We're not going to try to lane change yet. Should we wait until the next lane before we check again?
		LaneChangeFragment_Current.bBlockAllLaneChangesUntilNextLane = LaneChangeRecommendation.bNoLaneChangesUntilNextLane;

		//INC_DWORD_STAT(STAT_Traffic_LaneChangeAttemptsNone);

		return;
	}
	else if (LaneChangeRecommendation.Level == StayOnCurrentLane_RetrySoon)
	{
		#if DEBUG_LANE_CHANGE_LEVEL
		DrawDebugZLine(Coordinator.GetWorld(), TransformFragment_Current.GetTransform().GetLocation(), FColor::Cyan, false, 0.5f, 50.0f);
		#endif

		LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryOneHalfSecond, RandomStream);

		// We're not going to try to lane change yet. Should we wait until the next lane before we check again?
		LaneChangeFragment_Current.bBlockAllLaneChangesUntilNextLane = LaneChangeRecommendation.bNoLaneChangesUntilNextLane;

		//INC_DWORD_STAT(STAT_Traffic_LaneChangeAttemptsNone);

		return;
	}
	else if (LaneChangeRecommendation.Level == NormalLaneChange)
	{
		#if DEBUG_LANE_CHANGE_LEVEL
		DrawDebugZLine(Coordinator.GetWorld(), TransformFragment_Current.GetTransform().GetLocation(), FColor::Yellow, false, 0.5f, 50.0f);
		#endif
		
		//INC_DWORD_STAT(STAT_Traffic_LaneChangeAttemptsOptional);
		//INC_DWORD_STAT(STAT_Traffic_LaneChangeAttemptsTotal);
	}
	else if (LaneChangeRecommendation.Level == TransversingLaneChange)
	{
		#if DEBUG_LANE_CHANGE_LEVEL
		DrawDebugZLine(Coordinator.GetWorld(), TransformFragment_Current.GetTransform().GetLocation(), FColor::Orange, false, 0.5f, 50.0f);
		#endif
		
		//INC_DWORD_STAT(STAT_Traffic_LaneChangeAttemptsTransversing);
		//INC_DWORD_STAT(STAT_Traffic_LaneChangeAttemptsTotal);
	}


	// OPTIONAL?
	// Skip lane change if the current vehicle has a full list of lane change next vehicles. Very rare, but probably
	// good to check for now.
	
	if (NextVehicleFragment_Current.NextVehicles_LaneChange.IsFull())
	{
		UE_LOG(LogMassTraffic, Warning, TEXT("%s - Current vehicle has full list of lane change next vehicles. Skipping lane change."), ANSI_TO_TCHAR(__FUNCTION__));
		LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings, RandomStream);
		return; 
	}

	
	/** 
	 * LANE CHANGE IS BASICALLY ALLOWED
	 */ 
	FZoneGraphTrafficLaneData* Lane_Chosen = LaneChangeRecommendation.Lane_Chosen; // ..only valid if not staying on current lane 
	check(Lane_Chosen->LaneHandle.DataHandle == ZoneGraphStorage.DataHandle);
	const uint32 LaneIndex_Chosen = Lane_Chosen->LaneHandle.Index;
	const float LaneLength_Chosen = Lane_Chosen->Length;
	
	
	float DistanceAlongLane_Chosen = 0.0f;
	float DistanceBetweenLanes = 0.0f;
	FVector Position_Current = FVector::ZeroVector;
	FVector Position_Chosen = FVector::ZeroVector;
	{
		FZoneGraphLaneLocation ZoneGraphLocationOnLane_Current;
		UE::ZoneGraph::Query::CalculateLocationAlongLane(ZoneGraphStorage, Lane_Current->LaneHandle, DistanceAlongLane_Current, ZoneGraphLocationOnLane_Current);
		if (!ZoneGraphLocationOnLane_Current.IsValid())
		{
			// Should never happen.
			UE_LOG(LogMassTraffic, Error, TEXT("%s - Could not get locaton on current lane %d given distance along lane %f. Lane length is %f."), ANSI_TO_TCHAR(__FUNCTION__),
				LaneIndex_Current, DistanceAlongLane_Current, LaneLength_Current);
			return;
		}

		Position_Current = ZoneGraphLocationOnLane_Current.Position;

		const float ZoneGraphLaneSearchDistance = MassTrafficSettings.LaneChangeSearchDistanceScale * GetMaxDistanceBetweenLanes(Lane_Current->LaneHandle.Index, Lane_Chosen->LaneHandle.Index, ZoneGraphStorage); 
		float DistanceSquared;
		const FZoneGraphLaneLocation ZoneGraphLocationOnLane_Chosen = GetClosestLocationOnLane(Position_Current, Lane_Chosen->LaneHandle.Index, ZoneGraphLaneSearchDistance, ZoneGraphStorage, &DistanceSquared);
		if (!ZoneGraphLocationOnLane_Chosen.IsValid())
		{
			UE_LOG(LogMassTraffic, Error, TEXT("%s - Could not get closest location on chosen lane %d. Search location is %s."), ANSI_TO_TCHAR(__FUNCTION__),
				LaneIndex_Chosen, *Position_Current.ToString());
			return;
		}

		Position_Chosen = ZoneGraphLocationOnLane_Chosen.Position;
		DistanceAlongLane_Chosen = ZoneGraphLocationOnLane_Chosen.DistanceAlongLane;
		DistanceBetweenLanes = FMath::Sqrt(DistanceSquared);
	}

	
	// Lane change begin and end distances along lane.

	const float BeginDistanceAlongLaneForLaneChange_Chosen = DistanceAlongLane_Chosen;
	float EndDistanceAlongLaneForLaneChange_Chosen = 0.0f;
	float DeltaDistanceAlongLaneForLaneChange_Chosen = 0.0f;
	{
		// Optional lane changes shouldn't go ahead if there's not enough room to complete the lane change.
		
		const float MaxDistanceAlongLane_Chosen = LaneLength_Chosen - AgentRadiusFragment_Current.Radius;
		if (MaxDistanceAlongLane_Chosen <= 0.0f)
		{
			//DrawDebugZLine(Coordinator.GetWorld(), TransformFragment_Current.GetTransform().GetLocation(), FColor::Red, false, 1.0f, 60.0f, 10000.f);
			UE_LOG(LogMassTraffic, Error, TEXT("%s - Lane is too short for vehicle! -- lane len %.2f < vehicle radius %.2f"),
				ANSI_TO_TCHAR(__FUNCTION__), LaneLength_Chosen, AgentRadiusFragment_Current.Radius);
			LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings, RandomStream);
			return;	
		}
		if (DistanceAlongLane_Chosen >= MaxDistanceAlongLane_Chosen)
		{
			//DrawDebugZLine(Coordinator.GetWorld(), TransformFragment_Current.GetTransform().GetLocation(), FColor::Red, false, 0.5f); 
			LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings, RandomStream);
			return;
		}

		const float LaneChangeDuration = // ..may be revised below
			MassTrafficSettings.BaseSecondsToExecuteLaneChange +
			MassTrafficSettings.AdditionalSecondsToExecuteLaneChangePerUnitOfVehicleLength * (2.0f * AgentRadiusFragment_Current.Radius);

		DeltaDistanceAlongLaneForLaneChange_Chosen = FMath::Max(
			VehicleControlFragment_Current.Speed * LaneChangeDuration,
			(2.0f * AgentRadiusFragment_Current.Radius) * MassTrafficSettings.MinLaneChangeDistanceVehicleLengthScale);

		EndDistanceAlongLaneForLaneChange_Chosen = BeginDistanceAlongLaneForLaneChange_Chosen + DeltaDistanceAlongLaneForLaneChange_Chosen;
		
		if (EndDistanceAlongLaneForLaneChange_Chosen > MaxDistanceAlongLane_Chosen)
		{
			LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings, RandomStream);
			return;
		}
	}

	
	// Find nearby vehicles on chosen lane.
	// NOTE - This is expensive, so save it for as late as possible.
	
	FMassEntityHandle Entity_Chosen_Behind;
	FMassEntityHandle Entity_Chosen_Ahead;
	if (!FindNearbyVehiclesOnLane_RelativeToDistanceAlongLane(Lane_Chosen, DistanceAlongLane_Chosen, /*out*/Entity_Chosen_Behind, /*out*/Entity_Chosen_Ahead, /*Coordinator,*/ EntityManager))
	{
		// Error condition. Try again next time.
		//DrawDebugZLine(Coordinator.GetWorld(), TransformFragment_Current.GetTransform().GetLocation(), FColor::Red, false, 0.5f);
		LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings, RandomStream);
		return;
	}

	
	bool bIsValid_Chosen_Behind = Entity_Chosen_Behind.IsSet();
	bool bIsValid_Chosen_Ahead = Entity_Chosen_Ahead.IsSet();

	
	FAgentRadiusFragment* RadiusFragment_Chosen_Behind = nullptr;
	FMassZoneGraphLaneLocationFragment* ZoneGraphLaneLocationFragment_Chosen_Behind = nullptr;
	FMassTrafficNextVehicleFragment* NextVehicleFragment_Chosen_Behind = nullptr;
	FMassTrafficVehicleLaneChangeFragment* LaneChangeFragment_Chosen_Behind = nullptr;
	FMassTrafficObstacleAvoidanceFragment* AvoidanceFragment_Chosen_Behind = nullptr;
	if (bIsValid_Chosen_Behind)
	{
		FMassEntityView EntityView(EntityManager, Entity_Chosen_Behind);
		RadiusFragment_Chosen_Behind = &EntityView.GetFragmentData<FAgentRadiusFragment>();
		ZoneGraphLaneLocationFragment_Chosen_Behind = &EntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		NextVehicleFragment_Chosen_Behind = &EntityView.GetFragmentData<FMassTrafficNextVehicleFragment>();
		LaneChangeFragment_Chosen_Behind = &EntityView.GetFragmentData<FMassTrafficVehicleLaneChangeFragment>();
		AvoidanceFragment_Chosen_Behind = &EntityView.GetFragmentData<FMassTrafficObstacleAvoidanceFragment>();
	}
	
	FMassTrafficVehicleControlFragment* VehicleControlFragment_Chosen_Ahead = nullptr;
	FAgentRadiusFragment* RadiusFragment_Chosen_Ahead = nullptr;
	FMassZoneGraphLaneLocationFragment* ZoneGraphLaneLocationFragment_Chosen_Ahead = nullptr;
	FMassTrafficVehicleLaneChangeFragment* LaneChangeFragment_Chosen_Ahead = nullptr;
	if (bIsValid_Chosen_Ahead)
	{
		FMassEntityView EntityView(EntityManager, Entity_Chosen_Ahead);
		VehicleControlFragment_Chosen_Ahead = &EntityView.GetFragmentData<FMassTrafficVehicleControlFragment>();
		RadiusFragment_Chosen_Ahead = &EntityView.GetFragmentData<FAgentRadiusFragment>();
		ZoneGraphLaneLocationFragment_Chosen_Ahead = &EntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		LaneChangeFragment_Chosen_Ahead = &EntityView.GetFragmentData<FMassTrafficVehicleLaneChangeFragment>();
	}

	
	// If one of the other vehicles in the chosen lane is involved in a lane change, let's avoid lane changing ourselves.
	// Vehicles might run the risk of becoming 'entangled' with each other (with interlocked next vehicle
	// pointers) with both unable to move forward - although this is very rare, it does happen.
	
	if ((LaneChangeFragment_Chosen_Ahead && LaneChangeFragment_Chosen_Ahead->IsLaneChangeInProgress()) ||
		(LaneChangeFragment_Chosen_Behind && LaneChangeFragment_Chosen_Behind->IsLaneChangeInProgress()))
	{
		LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings, RandomStream);
		return;
	}

	
	// See if the current vehicle can fit on the chose lane.
	// NOTE - This is expensive, so save it for as late as possible.

	FMassTrafficLaneChangeFitReport LaneChangeFitReport;	
	CanVehicleLaneChangeToFitOnChosenLane(
		DistanceAlongLane_Chosen, LaneLength_Chosen, DeltaDistanceAlongLaneForLaneChange_Chosen,
		VehicleControlFragment_Current, AgentRadiusFragment_Current, RandomFractionFragment_Current,
		bIsValid_Chosen_Behind,
		RadiusFragment_Chosen_Behind, ZoneGraphLaneLocationFragment_Chosen_Behind,
		bIsValid_Chosen_Ahead,
		VehicleControlFragment_Chosen_Ahead, RadiusFragment_Chosen_Ahead, ZoneGraphLaneLocationFragment_Chosen_Ahead,
		MassTrafficSettings.MinimumDistanceToNextVehicleRange,
		/*Out*/LaneChangeFitReport);

	if (!LaneChangeFitReport.IsClear())
	{
		LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings, RandomStream);
		return;
	}

	// Find nearby vehicles on current lane.
	// 
	// NOTE - This is expensive, so save it for as late as possible.
	// 
	// NOTE - This will only find the Entity_Current_Behind if it's on the same lane as Entity_Current. However, it's
	//		  often that the previous vehicle / vehicle who's NextVehicle is us, is on the previous lane.
	//		  
	//			e.g
	//			
	//			|   Previous Lane    ( Previous Vehicle ) --------- | -- Current Lane -------> ( Entity_Current ) 
	//
	//		  In this case, this previous vehicle won't be found here by FindNearbyVehiclesOnLane_RelativeToVehicleEntity
	//		  as our Entity_Current_Behind and as such it's NextVehicle will be left pointing to us, after we've lane
	//		  changed over to our chosen lane.
	//		  
	//			e.g
	//			
	//			|   Previous Lane    ( Previous Vehicle ) --------- | -- Current Lane --\
	//			|___________________________________________________|____________________\______________________________
	//			|                                                   |                     \
	//			|                                                   |    Chosen Lane       ----> ( Entity_Current )
	//
	//		  This should be ok though as we generally keep 'lazy' NextVehicle pointers and the PreviousVehicle
	//		  should pick up a new NextVehicle when it moves onto another lane. If we kept explicit PreviousVehicle
	//		  references this wouldn't be an issue though.
		
	FMassEntityHandle Entity_Current_Behind;
	FMassEntityHandle Entity_Current_Ahead;
	if (!FindNearbyVehiclesOnLane_RelativeToVehicleEntity(Lane_Current, Entity_Current, NextVehicleFragment_Current, /*out*/Entity_Current_Behind, /*out*/Entity_Current_Ahead, EntityManager, /*VisLogOwner*/&MassTrafficSubsystem))
	{
		// Error condition. Try again next time.
		//DrawDebugZLine(Coordinator.GetWorld(), TransformFragment_Current.GetTransform().GetLocation(), FColor::Red, false, 1.0f, 20.0f, 2000.f);
		LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings, RandomStream);
		return;
	}
	

	const bool bIsValid_Current_Behind = Entity_Current_Behind.IsSet();
	const bool bIsValid_Current_Ahead = Entity_Current_Ahead.IsSet();

	FMassTrafficNextVehicleFragment* NextVehicleFragment_Current_Behind = nullptr;
	FMassTrafficVehicleLaneChangeFragment* LaneChangeFragment_Current_Behind = nullptr;
	if (bIsValid_Current_Behind)
	{
		FMassEntityView VehicleEntityView(EntityManager, Entity_Current_Behind);
		NextVehicleFragment_Current_Behind = &VehicleEntityView.GetFragmentData<FMassTrafficNextVehicleFragment>();
		LaneChangeFragment_Current_Behind = &VehicleEntityView.GetFragmentData<FMassTrafficVehicleLaneChangeFragment>();
	}

	FMassTrafficVehicleLaneChangeFragment* LaneChangeFragment_Current_Ahead = nullptr;
	if (bIsValid_Current_Ahead)
	{
		FMassEntityView VehicleEntityView(EntityManager, Entity_Current_Ahead);
		LaneChangeFragment_Current_Ahead = &VehicleEntityView.GetFragmentData<FMassTrafficVehicleLaneChangeFragment>();
	}

	
	// OPTIONAL?
	// Skip lane change if the current behind vehicle has a full list of lane change next vehicles. Very rare, but probably
	// good to check for now.
	
	if (bIsValid_Current_Behind &&
		NextVehicleFragment_Current_Behind->NextVehicles_LaneChange.IsFull())
	{
		UE_LOG(LogMassTraffic, Warning, TEXT("%s - Current behind vehicle has full list of lane change next vehicles. Skipping lane change."), ANSI_TO_TCHAR(__FUNCTION__));
		LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings, RandomStream);
		return;
	}


	// If one of the other vehicles in the current lane is involved in a lane change, let's avoid lane changing ourselves.
	// Vehicles might run the risk of becoming entangled with each other (with interlocked next vehicle
	// pointers) with both unable to move forward - although this is very rare, it does happen.
		
	if ((LaneChangeFragment_Current_Ahead && LaneChangeFragment_Current_Ahead->IsLaneChangeInProgress()) ||
		(LaneChangeFragment_Current_Behind && LaneChangeFragment_Current_Behind->IsLaneChangeInProgress()))
	{
		LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings, RandomStream);
		return;
	}

	
	/**
	 * LANE CHANGE IS HAPPENING
	 */ 

	// Start by teleporting vehicle to the chosen lane.

	if (!TeleportVehicleToAnotherLane(
		// Current..
		Entity_Current,
		*Lane_Current, VehicleControlFragment_Current, AgentRadiusFragment_Current, RandomFractionFragment_Current, ZoneGraphLaneLocationFragment_Current, NextVehicleFragment_Current, AvoidanceFragment_Current,
		// Chosen..
		*Lane_Chosen, DistanceAlongLane_Chosen,
		// Current Behind..
		Entity_Current_Behind, NextVehicleFragment_Current_Behind,
		// Current Ahead..
		Entity_Current_Ahead,
		// Chosen Behind..
		Entity_Chosen_Behind, NextVehicleFragment_Chosen_Behind, RadiusFragment_Chosen_Behind, ZoneGraphLaneLocationFragment_Chosen_Behind, AvoidanceFragment_Chosen_Behind,
		// Chosen Ahead..
		Entity_Chosen_Ahead,
		RadiusFragment_Chosen_Ahead,
		ZoneGraphLaneLocationFragment_Chosen_Ahead,
		// Other..
		MassTrafficSettings,
		EntityManager))
	{
		LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsRetryUsingSettings, RandomStream);
		return;		
	}

	
	// Teleport will only find next vehicle on the chosen lane, it won't look beyond it.
	// If after teleport, the current vehicle doesn't have a next vehicle, we'd like to know if there is a vehicle ahead
	// of us on a next lane we should avoid. This prevents this lane changing vehicle from being surprised by a
	// vehicle on the next lane it later proceeds to.

	if (!NextVehicleFragment_Current.HasNextVehicle())
	{
		FMassEntityHandle Entity_NewNextVehicle = FindNearestTailVehicleOnNextLanes(*Lane_Chosen, Position_Chosen, EntityManager, EMassTrafficFindNextLaneVehicleType::Tail);
		NextVehicleFragment_Current.SetNextVehicle(Entity_Current, Entity_NewNextVehicle);
	}

	
	// Debug
	DrawDebugLaneChange(MassTrafficSubsystem.GetWorld(), TransformFragment_Current.GetMutableTransform(), LaneChangeRecommendation.bChoseLaneOnLeft, bVisLog, /*VisLogOwner*/&MassTrafficSubsystem);


	if (UE::MassLOD::GetLODFromArchetype(Context) == EMassLOD::Off)
	{
		// Lane change is instant, vehicle is already on the other lane, and we're pretty much done.

		// IMPORTANT - We should only try another lane change after the same amount of time a lane change would have taken.
		LaneChangeFragment_Current.SetLaneChangeCountdownSecondsToBeAtLeast(MassTrafficSettings, EMassTrafficLaneChangeCountdownSeconds::AsNewTryUsingSettings, RandomStream);

		// For instant lane changes, we need to update the transform to the new lane position so that
		// later processors like LOD calculation have the right transform to work with.
		InterpolatePositionAndOrientationAlongLane(ZoneGraphStorage, ZoneGraphLaneLocationFragment_Current.LaneHandle.Index, ZoneGraphLaneLocationFragment_Current.DistanceAlongLane, ETrafficVehicleMovementInterpolationMethod::Linear, InterpolationFragment_Current.LaneLocationLaneSegment, TransformFragment_Current.GetMutableTransform());

		//INC_DWORD_STAT(STAT_Traffic_LaneChangeInstant);
	}
	else
	{
		// Set up lane change.

		EMassTrafficLaneChangeSide LaneChangeSide = EMassTrafficLaneChangeSide::IsNotLaneChanging;
		if (LaneChangeRecommendation.bChoseLaneOnLeft && !LaneChangeRecommendation.bChoseLaneOnRight)
		{
			LaneChangeSide = EMassTrafficLaneChangeSide::IsLaneChangingToTheLeft;
		}
		else if (!LaneChangeRecommendation.bChoseLaneOnLeft && LaneChangeRecommendation.bChoseLaneOnRight)
		{
			LaneChangeSide = EMassTrafficLaneChangeSide::IsLaneChangingToTheRight;
		}
		else
		{
			UE_LOG(LogMassTraffic, Error, TEXT("%s - LaneChangeRecommendation says go left:%d right:%d"), ANSI_TO_TCHAR(__FUNCTION__),
				LaneChangeRecommendation.bChoseLaneOnLeft, LaneChangeRecommendation.bChoseLaneOnRight);
		}


		const bool bDidStartLaneChangeProgression = LaneChangeFragment_Current.BeginLaneChangeProgression(
			//DebugLabel,
			LaneChangeSide,
			BeginDistanceAlongLaneForLaneChange_Chosen, EndDistanceAlongLaneForLaneChange_Chosen,
			DistanceBetweenLanes,
			// Fragments..
			TransformFragment_Current,
			VehicleLightsFragment_Current,
			NextVehicleFragment_Current,
			ZoneGraphLaneLocationFragment_Current,
			Lane_Current/*initial*/, Lane_Chosen,
			// Other vehicles involved in lane change..
			Entity_Current,
			Entity_Current_Behind, Entity_Current_Ahead,
			Entity_Chosen_Behind, Entity_Chosen_Ahead,
			// Other..
			EntityManager);

		if (!bDidStartLaneChangeProgression)
		{
			UE_LOG(LogTemp, Error, TEXT("%s - FIXME. Lane change progression failed, vehicle has changed lanes instantly."), ANSI_TO_TCHAR(__FUNCTION__));
		}

		//INC_DWORD_STAT(STAT_Traffic_LaneChangeInterpolated);
	}


	// Block all lane changes until next lane (this lane change should be the only one on these lanes) -
	//		(1) If lane is transversing. We only want to make once choice on these lanes.
	//		(2) If lane change recommendation said so for some other reason.
	
	if (LaneChangeRecommendation.Level == TransversingLaneChange ||
		LaneChangeRecommendation.bNoLaneChangesUntilNextLane)
	{
		LaneChangeFragment_Current.bBlockAllLaneChangesUntilNextLane = true;
	}

	
	if (LaneChangeRecommendation.Level == NormalLaneChange)
	{
		//INC_DWORD_STAT(STAT_Traffic_LaneChangeStartsOptional);
		//INC_DWORD_STAT(STAT_Traffic_LaneChangeStartsTotal);
	}
	else if (LaneChangeRecommendation.Level == TransversingLaneChange)
	{
		//INC_DWORD_STAT(STAT_Traffic_LaneChangeStartsTransversing);
		//INC_DWORD_STAT(STAT_Traffic_LaneChangeStartsTotal);
	}
}


static void UpdateLaneChange(
	FMassTrafficVehicleLightsFragment& VehicleLightsFragment_Current,
	const FMassZoneGraphLaneLocationFragment& ZoneGraphLaneLocationFragment_Current,
	FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment_Current,
	FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
	//
	const float DeltaTimeSeconds,
	const FMassEntityManager& EntityManager,
	const UMassTrafficSettings& MassTrafficSettings,
	const FRandomStream& RandomStream)
{
	// Update lane change fragment.
	// Only count down the if we're in a lane changing lane. This prevents many cars changing lanes in the same place
	// when they they re-enter a zone where they are allowed to change lanes.
	
	LaneChangeFragment_Current.UpdateLaneChange(
		DeltaTimeSeconds, 
		VehicleLightsFragment_Current, NextVehicleFragment_Current, ZoneGraphLaneLocationFragment_Current,
		EntityManager, MassTrafficSettings, RandomStream);
}


UMassTrafficLaneChangingProcessor::UMassTrafficLaneChangingProcessor()
	: StartNewLaneChangesEntityQuery_Conditional(*this)
	, UpdateLaneChangesEntityQuery_Conditional(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::FrameStart;
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficOverseerProcessor::StaticClass()->GetFName());
}

void UMassTrafficLaneChangingProcessor::ConfigureQueries() 
{
	StartNewLaneChangesEntityQuery_Conditional.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::None);
	StartNewLaneChangesEntityQuery_Conditional.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	StartNewLaneChangesEntityQuery_Conditional.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadOnly);
	StartNewLaneChangesEntityQuery_Conditional.AddRequirement<FMassTrafficNextVehicleFragment>(EMassFragmentAccess::ReadWrite);
	StartNewLaneChangesEntityQuery_Conditional.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	StartNewLaneChangesEntityQuery_Conditional.AddRequirement<FMassTrafficInterpolationFragment>(EMassFragmentAccess::ReadWrite);
	StartNewLaneChangesEntityQuery_Conditional.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadWrite);
	StartNewLaneChangesEntityQuery_Conditional.AddRequirement<FMassTrafficVehicleLightsFragment>(EMassFragmentAccess::ReadWrite);
	StartNewLaneChangesEntityQuery_Conditional.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadWrite);
	StartNewLaneChangesEntityQuery_Conditional.AddRequirement<FMassTrafficVehicleLaneChangeFragment>(EMassFragmentAccess::ReadWrite);
	StartNewLaneChangesEntityQuery_Conditional.AddRequirement<FMassTrafficDebugFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	StartNewLaneChangesEntityQuery_Conditional.AddRequirement<FMassTrafficObstacleAvoidanceFragment>(EMassFragmentAccess::ReadWrite);
	StartNewLaneChangesEntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
	StartNewLaneChangesEntityQuery_Conditional.SetChunkFilter([&](const FMassExecutionContext& Context)
	{
		return FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame(Context) && CanAnyEntitiesLaneChangeInChunk(Context, *MassTrafficSettings);
	});
	StartNewLaneChangesEntityQuery_Conditional.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
	StartNewLaneChangesEntityQuery_Conditional.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);

	UpdateLaneChangesEntityQuery_Conditional.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::None);
	UpdateLaneChangesEntityQuery_Conditional.AddRequirement<FMassTrafficVehicleLightsFragment>(EMassFragmentAccess::ReadWrite);
	UpdateLaneChangesEntityQuery_Conditional.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	UpdateLaneChangesEntityQuery_Conditional.AddRequirement<FMassSimulationVariableTickFragment>(EMassFragmentAccess::ReadOnly);
	UpdateLaneChangesEntityQuery_Conditional.AddRequirement<FMassTrafficVehicleLaneChangeFragment>(EMassFragmentAccess::ReadWrite);
	UpdateLaneChangesEntityQuery_Conditional.AddRequirement<FMassTrafficNextVehicleFragment>(EMassFragmentAccess::ReadWrite);
	UpdateLaneChangesEntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
	UpdateLaneChangesEntityQuery_Conditional.SetChunkFilter(FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);
}


void UMassTrafficLaneChangingProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Quick checks to see if we should bother being here.
	if (GMassTrafficLaneChange == 0 /*lane changing forced off (no lane changing allowed at all)*/)
	{
		return;
	}
	else if (GMassTrafficLaneChange == -1 /*lane changing controlled by coordinator*/ && MassTrafficSettings->LaneChangeMode == EMassTrafficLaneChangeMode::Off)
	{
		return;
	}


	// Start some lane changes. 

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("StartNewLaneChanges"));

		StartNewLaneChangesEntityQuery_Conditional.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& QueryContext)
			{
				const UZoneGraphSubsystem& ZoneGraphSubsystem = QueryContext.GetSubsystemChecked<UZoneGraphSubsystem>();
				UMassTrafficSubsystem& MassTrafficSubsystem = QueryContext.GetMutableSubsystemChecked<UMassTrafficSubsystem>();
				

				const TConstArrayView<FAgentRadiusFragment> AgentRadiusFragments = Context.GetFragmentView<FAgentRadiusFragment>();
				const TConstArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = Context.GetFragmentView<FMassTrafficRandomFractionFragment>();
				const TArrayView<FMassTrafficNextVehicleFragment> NextVehicleFragments = Context.GetMutableFragmentView<FMassTrafficNextVehicleFragment>();
				const TArrayView<FTransformFragment> TransformFragments = Context.GetMutableFragmentView<FTransformFragment>();
				const TArrayView<FMassTrafficInterpolationFragment> InterpolationFragments = Context.GetMutableFragmentView<FMassTrafficInterpolationFragment>();
				const TArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = Context.GetMutableFragmentView<FMassTrafficVehicleControlFragment>();
				const TArrayView<FMassTrafficVehicleLightsFragment> VehicleLightsFragments = Context.GetMutableFragmentView<FMassTrafficVehicleLightsFragment>();
				const TArrayView<FMassZoneGraphLaneLocationFragment> ZoneGraphLaneLocationFragments = Context.GetMutableFragmentView<FMassZoneGraphLaneLocationFragment>(); 
				const TArrayView<FMassTrafficVehicleLaneChangeFragment> LaneChangeFragments = Context.GetMutableFragmentView<FMassTrafficVehicleLaneChangeFragment>();
				const TConstArrayView<FMassTrafficDebugFragment> DebugFragments = Context.GetFragmentView<FMassTrafficDebugFragment>();
				const TArrayView<FMassTrafficObstacleAvoidanceFragment> AvoidanceFragments = Context.GetMutableFragmentView<FMassTrafficObstacleAvoidanceFragment>();

				for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); EntityIndex++)
				{
					const FMassEntityHandle Entity = Context.GetEntity(EntityIndex);
				
					const FAgentRadiusFragment& AgentRadiusFragment = AgentRadiusFragments[EntityIndex]; 
					const FMassTrafficRandomFractionFragment& RandomFractionFragment = RandomFractionFragments[EntityIndex]; 
					FMassTrafficNextVehicleFragment& NextVehicleFragment = NextVehicleFragments[EntityIndex]; 
					FTransformFragment& TransformFragment = TransformFragments[EntityIndex];
					FMassTrafficInterpolationFragment& InterpolationFragment = InterpolationFragments[EntityIndex];
					FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[EntityIndex]; 
					FMassTrafficVehicleLightsFragment& VehicleLightsFragment = VehicleLightsFragments[EntityIndex]; 
					FMassZoneGraphLaneLocationFragment& ZoneGraphLaneLocationFragment = ZoneGraphLaneLocationFragments[EntityIndex]; 
					FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment = LaneChangeFragments[EntityIndex];
					FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment = AvoidanceFragments[EntityIndex];
				
					const bool bVisLog = DebugFragments.IsEmpty() ? false : DebugFragments[EntityIndex].bVisLog > 0;

					const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(ZoneGraphLaneLocationFragment.LaneHandle.DataHandle);
					check(ZoneGraphStorage);

					TryStartingNewLaneChange(Entity, Context,
						AgentRadiusFragment,
						RandomFractionFragment,
						NextVehicleFragment,
						TransformFragment,
						InterpolationFragment,
						VehicleControlFragment,
						VehicleLightsFragment,
						ZoneGraphLaneLocationFragment,
						LaneChangeFragment,
						AvoidanceFragment,
						bVisLog, MassTrafficSubsystem, *MassTrafficSettings, RandomStream, EntityManager, *ZoneGraphStorage);
				}
			});
	}
	

	// Update all lane changes? 
	// Get current fragments.

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("UpdateLaneChanges"));
		
		UpdateLaneChangesEntityQuery_Conditional.ForEachEntityChunk(
				EntityManager, Context, [&](FMassExecutionContext& ComponentSystemExecutionContext)
			{
				// NOTE - Don't check if we should skip this due to LOD. All lane changes, once started, should always be
				// updated until finished. 

				const TArrayView<FMassTrafficVehicleLightsFragment> VehicleLightsFragments = Context.GetMutableFragmentView<FMassTrafficVehicleLightsFragment>();
				const TConstArrayView<FMassZoneGraphLaneLocationFragment> ZoneGraphLaneLocationFragments = Context.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
				const TConstArrayView<FMassSimulationVariableTickFragment> SimulationVariableTickFragments = Context.GetFragmentView<FMassSimulationVariableTickFragment>();
				const TArrayView<FMassTrafficVehicleLaneChangeFragment> LaneChangeFragments = Context.GetMutableFragmentView<FMassTrafficVehicleLaneChangeFragment>();
				const TArrayView<FMassTrafficNextVehicleFragment> NextVehicleFragments = Context.GetMutableFragmentView<FMassTrafficNextVehicleFragment>();

				for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); EntityIndex++)
				{
					FMassTrafficVehicleLightsFragment& VehicleLightsFragment = VehicleLightsFragments[EntityIndex]; 
					const FMassZoneGraphLaneLocationFragment& ZoneGraphLaneLocationFragment = ZoneGraphLaneLocationFragments[EntityIndex]; 
					const FMassSimulationVariableTickFragment& SimulationVariableTickFragment = SimulationVariableTickFragments[EntityIndex]; 
					FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment = LaneChangeFragments[EntityIndex]; 
					FMassTrafficNextVehicleFragment& NextVehicleFragment = NextVehicleFragments[EntityIndex]; 

					UpdateLaneChange(
						VehicleLightsFragment,
						ZoneGraphLaneLocationFragment,
						LaneChangeFragment,
						NextVehicleFragment,
						SimulationVariableTickFragment.DeltaTime, EntityManager, *MassTrafficSettings, RandomStream);
				}
			});
	}
}
