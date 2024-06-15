// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficLaneChange.h"
#include "MassTrafficMovement.h"
#include "MassTrafficUtils.h"
#include "MassTrafficDebugHelpers.h"

#include "DrawDebugHelpers.h"
#include "MassEntityView.h"
#include "Math/UnrealMathUtility.h"
#include "MassZoneGraphNavigationFragments.h"

namespace UE::MassTraffic {


/**
 * Returns true if -
 *		(1) Lane is a trunk lane, and can therefore support any vehicle.
 *		(2) Or, if not a trunk lane, the vehicle is unrestricted (not restricted to trunk lanes).
 */
bool TrunkVehicleLaneCheck(
	const FZoneGraphTrafficLaneData* TrafficLaneData,
	const FMassTrafficVehicleControlFragment& VehicleControlFragment)
{
	return TrafficLaneData &&
		(TrafficLaneData->ConstData.bIsTrunkLane || !VehicleControlFragment.bRestrictedToTrunkLanesOnly);
}

	
void CanVehicleLaneChangeToFitOnChosenLane(
	const float DistanceAlongLane_Chosen, const float LaneLength_Chosen, const float DeltaDistanceAlongLaneForLaneChange_Chosen,
	//
	const FMassTrafficVehicleControlFragment& VehicleControlFragment_Current,
	const FAgentRadiusFragment& RadiusFragment_Current,
	const FMassTrafficRandomFractionFragment& RandomFractionFragment_Current,
	//
	const bool bIsValid_Behind, // ..means all these fragments are non-null..
	const FAgentRadiusFragment* RadiusFragment_Chosen_Behind,
	const FMassZoneGraphLaneLocationFragment* LaneLocationFragment_Chosen_Behind,
	//
	const bool bIsValid_Ahead, // ..means all these fragments are non-null..
	const FMassTrafficVehicleControlFragment* VehicleControlFragment_Chosen_Ahead,
	const FAgentRadiusFragment* RadiusFragment_Chosen_Ahead,
	const FMassZoneGraphLaneLocationFragment* LaneLocationFragment_Chosen_Ahead,
	//
	const FVector2D MinimumDistanceToNextVehicleRange,
	//
	FMassTrafficLaneChangeFitReport& OutLaneChangeFitReport)
{

	OutLaneChangeFitReport.ClearAll();

	// Speed can't be 0 for calculating lane change duration estimate.
	if (VehicleControlFragment_Current.Speed == 0.0f)
	{
		OutLaneChangeFitReport.BlockAll();
		return;
	}
	
	const float LaneChangeDurationAtCurrentSpeed = DeltaDistanceAlongLaneForLaneChange_Chosen / VehicleControlFragment_Current.Speed;
	
	
	// Test vehicle behind..
	if (bIsValid_Behind)
	{
		// If someone will be behind us, we change lanes whether or not there is safe space. The vehicle behind us will
		// slow down. 			

		const float DistanceAlongLane_Chosen_Behind = LaneLocationFragment_Chosen_Behind->DistanceAlongLane;
	
		const float SpaceAvailableNow = (DistanceAlongLane_Chosen - DistanceAlongLane_Chosen_Behind) -
			- RadiusFragment_Current.Radius /*accounts for back of our car*/
			- RadiusFragment_Chosen_Behind->Radius; /*accounts for front of their car*/
		if (SpaceAvailableNow < 0.0f)
		{
			OutLaneChangeFitReport.bIsClearOfVehicleBehind = false;
		}
	}
	
	
	// Test start of lane..
	{
		// If nobody is behind of us, we still need to check if we're too close to the beginning of the lane.
		// We don't want to cut anyone off that suddenly appears on the lane we'd move into, making them slam on the
		// brakes the moment they do. (This happens for cars coming out of intersections.)
		// Since there is no behind vehicle, so we make guesses using the current vehicle.

		const float DistanceAlongLane_Chosen_Begin = GetMinimumDistanceToObstacle(RandomFractionFragment_Current.RandomFraction, MinimumDistanceToNextVehicleRange);

		const float SpaceAvailableNow = (DistanceAlongLane_Chosen - 0.0f/*start of lane*/)
			- 2.0f * RadiusFragment_Current.Radius /*accounts for full length of car (whole care should be in lane) */
			- DistanceAlongLane_Chosen_Begin;
		if (SpaceAvailableNow < 0.0f)
		{
			OutLaneChangeFitReport.bIsClearOfLaneStart = false;
		}
	}


	// Test vehicle ahead..
	if (bIsValid_Ahead)
	{
		// There needs to be enough space to safely lane change behind the vehicle in front of us.
		// We also need to compare our speed with the speed of the vehicle in front of us, because -
		//		- if we're moving faster than the vehicle in front, then there will actually be less space to complete the lane change.
		//		- if we're moving slower than the vehicle in front, then there will actually be more space to complete the lane change.

		const float DistanceAlongLane_Chosen_Ahead = LaneLocationFragment_Chosen_Ahead->DistanceAlongLane;
		
		// If someone will be ahead of us, check if there's room behind them.
		// We don't want to end up right behind someone and have to slam on the brakes.
		const float SafeLaneChangeDistanceToVehicleAhead_FromChosen = GetMinimumDistanceToObstacle(RandomFractionFragment_Current.RandomFraction, MinimumDistanceToNextVehicleRange);

		const float SpaceAvailableNow = (DistanceAlongLane_Chosen_Ahead - DistanceAlongLane_Chosen)
			- RadiusFragment_Current.Radius /*accounts for front of our car*/
			- RadiusFragment_Chosen_Ahead->Radius /*accounts for back of their car*/
			- SafeLaneChangeDistanceToVehicleAhead_FromChosen;
		const float SpaceChangeByLaneChangeCompletion = (VehicleControlFragment_Chosen_Ahead->Speed - VehicleControlFragment_Current.Speed) * LaneChangeDurationAtCurrentSpeed;
		const float SpaceAvailableByLaneChangeCompletion = SpaceAvailableNow + SpaceChangeByLaneChangeCompletion;
		if (SpaceAvailableNow < 0.0f || SpaceAvailableByLaneChangeCompletion < 0.0f)
		{
			OutLaneChangeFitReport.bIsClearOfVehicleAhead = false;
		}
	}


	// Test end of lane..
	{
		// Whether or not someone is ahead of the chosen lane location, check if there's room before the end of the lane.
		// Ahead lane location is where the vehicle needs to stop. (This isn't right at the end of the lane.)
		// There needs to be enough space to safely lane change before the end of lane, by the time the lane change
		// would be complete.

		const float SpaceAvailableNow = (LaneLength_Chosen - DistanceAlongLane_Chosen)
			- RadiusFragment_Current.Radius; /*accounts for the front of our car*/
		const float SpaceAvailableByLaneChangeCompletion = SpaceAvailableNow - DeltaDistanceAlongLaneForLaneChange_Chosen;
		if (SpaceAvailableByLaneChangeCompletion < 0.0f)
		{
			OutLaneChangeFitReport.bIsClearOfLaneEnd = false;
		}
	}
}


bool FindNearbyVehiclesOnLane_RelativeToDistanceAlongLane(
	const FZoneGraphTrafficLaneData* TrafficLaneData,
	float DistanceAlongLane,
	FMassEntityHandle& OutEntity_Behind, FMassEntityHandle& OutEntity_Ahead,
	//const AMassTrafficCoordinator& Coordinator, // Only used for debug drawing.
	const FMassEntityManager& EntityManager)
{
	OutEntity_Behind.Reset();
	OutEntity_Ahead.Reset();
	
	check(TrafficLaneData->LaneHandle.IsValid());
	
	// Look for vehicles on the lane. Start at the last vehicle on the lane, and work our way up the lane,
	// comparing to our given distance.
	
	FMassEntityHandle Entity_Marching = TrafficLaneData->TailVehicle; // ..start here
	int32 MarchCount = 0;
	while (Entity_Marching.IsSet())
	{
		const FMassEntityView EntityView_Marching(EntityManager, Entity_Marching);
		const FMassZoneGraphLaneLocationFragment& ZoneGraphLaneLocationFragment_Marching = EntityView_Marching.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		const FMassTrafficNextVehicleFragment& NextVehicleFragment_Marching = EntityView_Marching.GetFragmentData<FMassTrafficNextVehicleFragment>();
		const float DistanceAlongLane_Marching = ZoneGraphLaneLocationFragment_Marching.DistanceAlongLane;

		if (ZoneGraphLaneLocationFragment_Marching.LaneHandle != TrafficLaneData->LaneHandle)
		{				
			// Marching vehicle has moved on to another lane. Given that, it should be ahead of us, but we are not
			// interested in it. (When the current vehicle gets to the end of its lane, it
			// will re-find a new next vehicle anyway.)
			return true;
		}
		else if (DistanceAlongLane_Marching <= DistanceAlongLane)
		{
			// Marching vehicle is (1) still on the lane (2) behind us (3) would be the closest one behind us that
			// we've seen so far, since we're marching up the lane from the back -
			OutEntity_Behind = Entity_Marching;
		}
		else if (DistanceAlongLane_Marching > DistanceAlongLane)
		{
			// Marching vehicle is ahead of us, and still on the lane.
			OutEntity_Ahead = Entity_Marching;
			return true; // ..done
		}


		// An OK optimization, but really prevents endless loops.
		if (++MarchCount >= 200)
		{
			UE_LOG(LogMassTraffic, Warning, TEXT("%s - March eject at %d"), ANSI_TO_TCHAR(__FUNCTION__), MarchCount); 
			return false;
		}

		// March to next vehicle.

		Entity_Marching = NextVehicleFragment_Marching.GetNextVehicle();
		
		if (Entity_Marching == TrafficLaneData->TailVehicle)
		{
			UE_LOG(LogMassTraffic, Warning, TEXT("%s - March eject at %d - rediscovered tail"), ANSI_TO_TCHAR(__FUNCTION__), MarchCount); 
			return false;
		}
	}

	
	return true;
}

	
bool FindNearbyVehiclesOnLane_RelativeToVehicleEntity(
	const FZoneGraphTrafficLaneData* TrafficLaneData, 
	const FMassEntityHandle Entity_Current,
	const FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
	FMassEntityHandle& OutEntity_Behind, FMassEntityHandle& OutEntity_Ahead,
	const FMassEntityManager& EntityManager,
	const UObject* VisLogOwner)
{
	OutEntity_Behind.Reset();
	OutEntity_Ahead.Reset();

	if (!Entity_Current.IsSet())
	{
		UE_LOG(LogMassTraffic, Error, TEXT("%s - Current entity not set."), ANSI_TO_TCHAR(__FUNCTION__));
		return false;
	}
	
	check(TrafficLaneData->LaneHandle.IsValid());
	
	// Get next vehicle on lane.

	{
		const FMassEntityHandle Entity_Current_Next = NextVehicleFragment_Current.GetNextVehicle();
		if (Entity_Current_Next.IsSet())
		{
			const FMassEntityView EntityView_Current_Next(EntityManager, Entity_Current_Next);
			const FMassZoneGraphLaneLocationFragment& LaneLocationFragment_Current_Next = EntityView_Current_Next.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
			if (LaneLocationFragment_Current_Next.LaneHandle == TrafficLaneData->LaneHandle)
			{
				OutEntity_Ahead = Entity_Current_Next;
			}
		}
	}

	// If we're the tail, we have no vehicle behind and have already got the next vehicle above
	
	if (Entity_Current == TrafficLaneData->TailVehicle)
	{
		return true;
	}

	
	// Look for previous vehicle on the lane. Start at the last vehicle on the lane, and work our way up the lane,
	// comparing to our given entity.
	
	FMassEntityHandle Entity_Marching = TrafficLaneData->TailVehicle; // ..start here
	int32 MarchCount = 0;
	while (Entity_Marching.IsSet())
	{
		const FMassEntityView EntityView_Marching(EntityManager, Entity_Marching);
		const FMassZoneGraphLaneLocationFragment& ZoneGraphLaneLocationFragment_Marching = EntityView_Marching.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		const FMassTrafficNextVehicleFragment& NextVehicleFragment_Marching = EntityView_Marching.GetFragmentData<FMassTrafficNextVehicleFragment>();
		
		// If we've hit a vehicle on a new lane before encountering Entity_Current which should be on this lane,
		// then the lane is malformed. Somehow this Entity_Marching vehicle is on the wrong lane, segmenting the
		// NextVehicle linkage to Entity_Current or Entity_Current shouldn't think it's on this lane.
		
		if (!ensureMsgf(ZoneGraphLaneLocationFragment_Marching.LaneHandle == TrafficLaneData->LaneHandle, TEXT("Lane %s's next vehicle links are malformed. Vehicle %d was encountered on lane %s before vehicle %d could be reached"), *TrafficLaneData->LaneHandle.ToString(), Entity_Marching.Index, *ZoneGraphLaneLocationFragment_Marching.LaneHandle.ToString(), Entity_Current.Index))
		{
			UE::MassTraffic::VisLogMalformedNextLaneLinks(EntityManager, TrafficLaneData->LaneHandle.Index, TrafficLaneData->TailVehicle, Entity_Current, /*MarchEjectAt*/1000, VisLogOwner);
			return false;
		}

		
		// Found the vehicle behind us!
		
		if (NextVehicleFragment_Marching.GetNextVehicle() == Entity_Current)
		{
			// Marching vehicle is (1) still on the lane (2) right behind us, since we're marching up the lane from the
			// back -
			OutEntity_Behind = Entity_Marching;
			return true;
		}


		// An OK optimization, but really just prevents endless loops.
		if (!ensure(++MarchCount < 200))
		{
			UE_LOG(LogMassTraffic, Warning, TEXT("%s - March eject at %d"), ANSI_TO_TCHAR(__FUNCTION__), MarchCount);
			VisLogMalformedNextLaneLinks(EntityManager, TrafficLaneData->LaneHandle.Index, TrafficLaneData->TailVehicle, Entity_Current, /*MarchEjectAt*/1000, VisLogOwner);
			return false;
		}


		// Infinite loop check
		if (!ensureMsgf(Entity_Marching != NextVehicleFragment_Marching.GetNextVehicle(), TEXT("%s - March eject along %s at %d - vehicle %d's NextVehicle is itself, creating an infinite loop'"), ANSI_TO_TCHAR(__FUNCTION__), *TrafficLaneData->LaneHandle.ToString(), MarchCount, Entity_Marching.Index))
		{
			return false;
		}

		// March to next vehicle.
		Entity_Marching = NextVehicleFragment_Marching.GetNextVehicle();
		
		if (Entity_Marching == TrafficLaneData->TailVehicle)
		{
			UE_LOG(LogMassTraffic, Warning, TEXT("%s - March eject along %s at %d - rediscovered tail"), ANSI_TO_TCHAR(__FUNCTION__), *TrafficLaneData->LaneHandle.ToString(), MarchCount); 
			return false;
		}
	}


	return true;
}


FMassEntityHandle FindNearestTailVehicleOnNextLanes(const FZoneGraphTrafficLaneData& CurrentTrafficLaneData, const FVector& VehiclePosition, const FMassEntityManager& EntityManager, const EMassTrafficFindNextLaneVehicleType Type)
{
	
	FMassEntityHandle NearestNextVehicleEntity = FMassEntityHandle();
	float NearestNextVehicleDistanceSquared = TNumericLimits<float>::Max();

	
	auto TestAndSetNextVehicleEntity = [&](const FMassEntityHandle NextVehicleEntity) -> void
	{
		if (!NextVehicleEntity.IsSet())
		{
			return;
		}

		const FMassEntityView NextVehicleEntityView(EntityManager, NextVehicleEntity);
		const FTransformFragment& NextVehicleTransformFragment = NextVehicleEntityView.GetFragmentData<FTransformFragment>();
		const FVector NextVehiclePosition = NextVehicleTransformFragment.GetTransform().GetLocation();
		const float DistanceSquared = FVector::DistSquared(VehiclePosition, NextVehiclePosition);
		if (DistanceSquared < NearestNextVehicleDistanceSquared)
		{
			NearestNextVehicleEntity = NextVehicleEntity;
			NearestNextVehicleDistanceSquared = DistanceSquared;
		}
	};
	
	
	for (FZoneGraphTrafficLaneData* NextTrafficLaneData : CurrentTrafficLaneData.NextLanes)
	{
		if (Type == EMassTrafficFindNextLaneVehicleType::Tail || Type == EMassTrafficFindNextLaneVehicleType::Any)
		{
			TestAndSetNextVehicleEntity(NextTrafficLaneData->TailVehicle);
		}
		else if (Type == EMassTrafficFindNextLaneVehicleType::LaneChangeGhostTail || Type == EMassTrafficFindNextLaneVehicleType::Any)
		{
			TestAndSetNextVehicleEntity(NextTrafficLaneData->GhostTailVehicle_FromLaneChangingVehicle);
		}
		else if (Type == EMassTrafficFindNextLaneVehicleType::SplittingLaneGhostTail || Type == EMassTrafficFindNextLaneVehicleType::Any)
		{
			TestAndSetNextVehicleEntity(NextTrafficLaneData->GhostTailVehicle_FromSplittingLaneVehicle);
		}
		else if (Type == EMassTrafficFindNextLaneVehicleType::MergingLaneGhostTail || Type == EMassTrafficFindNextLaneVehicleType::Any)
		{
			TestAndSetNextVehicleEntity(NextTrafficLaneData->GhostTailVehicle_FromMergingLaneVehicle);
		}
	}

	return NearestNextVehicleEntity;
}

	
void AdjustVehicleTransformDuringLaneChange(
	const FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment,
	const float InDistanceAlongLane,
	FTransform& Transform,
	UWorld* World, // ..for debug drawing only, nullptr for no debug draw
	const bool bVisLog,
	UObject* VisLogOwner)
{
	
	if (!LaneChangeFragment.IsLaneChangeInProgress())
	{
		return;
	}


	// This clamp is only necessary when physics vehicles are used. In that case, InDistanceAlongLane will have an
	// additional amount added to it, to make lane changing work better for physics. In the non-physics case, this clamp
	// won't actually do anything. (See all LANECHANGEPHYSICS1.)
	const float DistanceAlongLane = FMath::Clamp(InDistanceAlongLane, LaneChangeFragment.DistanceAlongLane_Final_Begin, LaneChangeFragment.DistanceAlongLane_Final_End);
	
	const float LaneChangeProgressionScale = LaneChangeFragment.GetLaneChangeProgressionScale(DistanceAlongLane);
	const float Alpha_Linear = FMath::Abs(LaneChangeProgressionScale);
	const float Sign = (LaneChangeProgressionScale >= 0.0f ? 1.0f : -1.0f);

	const float Alpha_Cubic = SimpleNormalizedCubicSpline(Alpha_Linear);
	const float Alpha_CubicDerivative = SimpleNormalizedCubicSplineDerivative(Alpha_Linear); 
	
	// Offset vector - from final lane location to initial lane location.
	// The transform is already on the lane change's final lane.
	// The distance between lanes was found using closest point on the final lane - which means that a line from the
	// point on the initial lane was 90 degrees to the final lane.
	// So we can use the (scaled) right vector of the transform, which is on the final lane now, to get back to where we
	// were on the initial lane.
	const FVector OffsetVector = (Sign * LaneChangeFragment.DistanceBetweenLanes_Begin * Alpha_Cubic) * Transform.GetUnitAxis(EAxis::Y);


	// Yaw rotation matrix.
	// This is a rotation that's meant to be local around the vehicle at the END of the offset vector.
	// Also, it's applied to the transform, which is now on the final lane, rotated according to where it is on that lane.
	// The amount of the rotation is a delta of that rotation.
	// Also, this rotation will be applied FIRST (later) before being translated.
	FQuat LocalRotationToApply;
	{
		const float InitialYaw = LaneChangeFragment.Yaw_Initial;
		float FinalYaw = Transform.GetRotation().Euler().Z;
		
		// Make sure yaw interpolation takes the shortest way around the circle. Examples -
		//		Something like  -173 ->  170  will become  -173 -> -190 (same as +170)
		//		Something like  +173 -> -170  will become  +173 -> +190 (same as -170)
		if (InitialYaw - FinalYaw < -180.0f)
		{
			FinalYaw -= 360.0f;
		}
		if (InitialYaw - FinalYaw > 180.0f)
		{
			FinalYaw += 360.0f;
		}

		const float DeltaLaneChangeDistance = LaneChangeFragment.DistanceAlongLane_Final_End - LaneChangeFragment.DistanceAlongLane_Final_Begin;
		const float MaxYawDelta = FMath::RadiansToDegrees(FMath::Atan2(LaneChangeFragment.DistanceBetweenLanes_Begin, DeltaLaneChangeDistance));

		const float Yaw = FMath::Lerp(0.0f, InitialYaw - FinalYaw, Alpha_Cubic)  +  (-Sign * Alpha_CubicDerivative * MaxYawDelta);

		LocalRotationToApply = FQuat::MakeFromEuler(FVector(0.0f, 0.0f, Yaw));
	}

	// Modify transform.
	Transform.ConcatenateRotation(LocalRotationToApply);
	Transform.AddToTranslation(OffsetVector);
	
	// Debug
	DrawDebugLaneChangeProgression(World, Transform.GetLocation(), OffsetVector, bVisLog, VisLogOwner);
}


FZoneGraphLaneLocation GetClosestLocationOnLane(
		const FVector& Location,
		const int32 LaneIndex,
		const float MaxSearchDistance,
		const FZoneGraphStorage& ZoneGraphStorage,
		float* OutDistanceSquared)
{
	const FZoneGraphLaneHandle LaneHandle(LaneIndex, ZoneGraphStorage.DataHandle);
	FZoneGraphLaneLocation ZoneGraphLaneLocation;
	float DistanceSquared = -1.0f;
	UE::ZoneGraph::Query::FindNearestLocationOnLane(ZoneGraphStorage, LaneHandle, Location, MaxSearchDistance, /*out*/ZoneGraphLaneLocation, /*out*/DistanceSquared);

	if (OutDistanceSquared)
	{
		*OutDistanceSquared = DistanceSquared;	
	}
	
	return ZoneGraphLaneLocation;
}


FORCEINLINE FZoneGraphTrafficLaneData* FilterLaneForLaneChangeSuitability(
	FZoneGraphTrafficLaneData* TrafficLaneData_Candidate,
	const FZoneGraphTrafficLaneData& TrafficLaneData_Current,
	const FMassTrafficVehicleControlFragment& VehicleControlFragment_Current,
	const float SpaceTakenByVehicleOnLane)
{
	if (TrafficLaneData_Candidate &&
		
		// Candidate lane is lower density than current lane.
		TrafficLaneData_Candidate->GetDownstreamFlowDensity() < TrafficLaneData_Current.GetDownstreamFlowDensity() &&

		// Candidate lane has enough space.
		TrafficLaneData_Candidate->SpaceAvailable > SpaceTakenByVehicleOnLane &&

		// Neither lane is an intersection lane.
		!TrafficLaneData_Candidate->ConstData.bIsIntersectionLane &&
		!TrafficLaneData_Current.ConstData.bIsIntersectionLane &&
		
		// Neither lane is part of a set of merging lanes.
		// (Don't lane change off of or onto these, space is being very carefully managed on them.)
		TrafficLaneData_Candidate->MergingLanes.Num() == 0 &&
		TrafficLaneData_Current.MergingLanes.Num() == 0 &&

		// Neither lane part of a set of splitting lanes.
		// (We don't allow cars to change lanes from a splitting lane. There are special next-vehicle fragments set up
		// for cars on these. To avoid possible accumulation on these lanes, also don't lane change onto them.)
		TrafficLaneData_Candidate->SplittingLanes.Num() == 0 && // ..may not be necessary to check this.
		TrafficLaneData_Current.SplittingLanes.Num() == 0 &&
	
		// Neither lane is downstream from an intersection that is currently feeding it vehicles.
		// We don't want lane changes to happen when this is the case, because lane space can change suddenly on this
		// downstream lane, which can end up stranding vehicles upstream in the intersection.
		// (See all INTERSTRAND1.)
		!AreVehiclesCurrentlyApproachingLaneFromIntersection(*TrafficLaneData_Candidate) &&
		!AreVehiclesCurrentlyApproachingLaneFromIntersection(TrafficLaneData_Current) &&

		// (See all LANECHANGEONOFF.)
		// Once a lane change begins, the vehicle ceases to officially be on it's initial lane. When several lane changes
		// happen FROM a lane, a lane change nearer to the start of the lane can complete before a lane change further
		// down the lane does. The lane changing vehicle further down the lane won't be seen by vehicles lane changing
		// ONTO on this lane from somewhere behind it - since there won't be any next vehicle references to it. This
		// prevents vehicles from colliding with it, but also makes a bit less lane changes happen. The candidate lane
		// fragment is what we will lane change TO, and the current lane fragment is what we will lane change FROM. We
		// need to test both lanes for the same problem. We don't want a vehicle to leave a lane, leaving unknown space
		// that a vehicle actually occupies during its lane change, and that another vehicle further behind us can end up
		// going through. This also prevents side-collisions, when two vehicles both lane change to the right or to the
		// left on adjacent lanes, but one is doing it faster than the other.
		TrafficLaneData_Candidate->NumVehiclesLaneChangingOffOfLane == 0 &&
		TrafficLaneData_Current.NumVehiclesLaneChangingOntoLane == 0 &&

		// Committed to next lane, cannot change lanes. (See all CANTSTOPLANEEXIT.)
		!VehicleControlFragment_Current.bCantStopAtLaneExit &&
		
		// If the vehicle is long, it needs to be on a trunk lane.
		TrunkVehicleLaneCheck(TrafficLaneData_Candidate, VehicleControlFragment_Current)
		)
	{
		return TrafficLaneData_Candidate;
	}

	return nullptr;
}

	
void ChooseLaneForLaneChange(
	const float DistanceAlongCurrentLane_Initial,
	const FZoneGraphTrafficLaneData* TrafficLaneData_Initial, const FAgentRadiusFragment& AgentRadiusFragment, const FMassTrafficRandomFractionFragment& RandomFractionFragment, const FMassTrafficVehicleControlFragment& VehicleControlFragment,
	const FRandomStream& RandomStream,
	const UMassTrafficSettings& MassTrafficSettings,
	FMassTrafficLaneChangeRecommendation& OutRecommendation
)
{
	OutRecommendation = FMassTrafficLaneChangeRecommendation();

	
	if (!TrafficLaneData_Initial->ConstData.bIsLaneChangingLane)
	{
		// Can't change lanes while in an intersection.
		
		return;		
	}
	else if (!TrafficLaneData_Initial->SplittingLanes.IsEmpty() || !TrafficLaneData_Initial->MergingLanes.IsEmpty())
	{
		// Don't change lanes on splitting or merging lanes.
		
		return;
	}

	// Need to choose a lane from the lanes to the left and/or right of us.

	
	// Get left and right lane candidates.

	FZoneGraphTrafficLaneData* CandidateTrafficLaneData_Left = TrafficLaneData_Initial->LeftLane;
	FZoneGraphTrafficLaneData* CandidateTrafficLaneData_Right = TrafficLaneData_Initial->RightLane;

	
	// Get candidate lane densities.

	const float DownstreamFlowDensity_Current = TrafficLaneData_Initial->GetDownstreamFlowDensity();

	const float DownstreamFlowDensity_Candidate_Left = CandidateTrafficLaneData_Left ?
		CandidateTrafficLaneData_Left->GetDownstreamFlowDensity() :
		TNumericLimits<float>::Max();

	const float DownstreamFlowDensity_Candidate_Right = CandidateTrafficLaneData_Right ?
		CandidateTrafficLaneData_Right->GetDownstreamFlowDensity() :
		TNumericLimits<float>::Max();


	// Filter lanes based on suitability.
	// IMPORTANT - Do this after getting their densities!
	const float SpaceTakenByVehicleOnLane = GetSpaceTakenByVehicleOnLane(
		AgentRadiusFragment.Radius, RandomFractionFragment.RandomFraction,
		MassTrafficSettings.MinimumDistanceToNextVehicleRange);

	CandidateTrafficLaneData_Left = FilterLaneForLaneChangeSuitability(
		CandidateTrafficLaneData_Left, *TrafficLaneData_Initial, VehicleControlFragment, SpaceTakenByVehicleOnLane);
	
	CandidateTrafficLaneData_Right = FilterLaneForLaneChangeSuitability(
		CandidateTrafficLaneData_Right, *TrafficLaneData_Initial, VehicleControlFragment, SpaceTakenByVehicleOnLane);

	
	// If the lane is transversing (has replaced merging and splitting) lanes, then this car should be more likely
	// to lane change. (We can choose it now.)
		 
	if (TrafficLaneData_Initial->bHasTransverseLaneAdjacency)
	{
		auto TestTransverseCandidateTrafficLaneData = [&](FZoneGraphTrafficLaneData* CandidateTrafficLaneData, const bool bTestingLeftLane) -> bool
		{
			if (!CandidateTrafficLaneData || !CandidateTrafficLaneData->bHasTransverseLaneAdjacency)
			{
				return false;
			}
				
			if ((bTestingLeftLane ? DownstreamFlowDensity_Candidate_Left : DownstreamFlowDensity_Candidate_Right) < DownstreamFlowDensity_Current)
			{
				// Prevent these lane changes from all happening in the same place (right at the beginning of the lane.)
				// Also, prevent them from happening if it seems too late to do them nicely - they are optional.
				// NOTE - We shouldn't have a situation where both a right and left lane replace transversing lanes.
				const float CurrentLaneLength = TrafficLaneData_Initial->Length;
				const float MinDistanceAlongCurrentLane = RandomFractionFragment.RandomFraction * (MassTrafficSettings.LaneChangeTransverseSpreadFromStartOfLaneFraction * CurrentLaneLength);
				return (DistanceAlongCurrentLane_Initial > MinDistanceAlongCurrentLane);
			}

			return false;
		};

		
		const bool bTestLeftFirst = (RandomStream.FRand() <= 0.5f);
		
		if (TestTransverseCandidateTrafficLaneData(
			(bTestLeftFirst ? CandidateTrafficLaneData_Left : CandidateTrafficLaneData_Right), bTestLeftFirst))
		{
			OutRecommendation.Lane_Chosen = (bTestLeftFirst ? CandidateTrafficLaneData_Left : CandidateTrafficLaneData_Right);
			OutRecommendation.bChoseLaneOnRight = !bTestLeftFirst;
			OutRecommendation.bChoseLaneOnLeft = bTestLeftFirst;
			OutRecommendation.Level = TransversingLaneChange;
			return;
		}
		else if (TestTransverseCandidateTrafficLaneData(
			(!bTestLeftFirst ? CandidateTrafficLaneData_Left : CandidateTrafficLaneData_Right), !bTestLeftFirst))
		{
			OutRecommendation.Lane_Chosen = (!bTestLeftFirst ? CandidateTrafficLaneData_Left : CandidateTrafficLaneData_Right);
			OutRecommendation.bChoseLaneOnRight = bTestLeftFirst;
			OutRecommendation.bChoseLaneOnLeft = !bTestLeftFirst;
			OutRecommendation.Level = TransversingLaneChange;
			return;
		}
		else
		{
			OutRecommendation.Level = StayOnCurrentLane_RetrySoon; // ..make lane change on transverse lanes more likely than on normal lanes
			return;
		}
	}
	
	
	if (!CandidateTrafficLaneData_Left && !CandidateTrafficLaneData_Right)
	{
		return;
	}
	else if (CandidateTrafficLaneData_Left && !CandidateTrafficLaneData_Right)
	{
		OutRecommendation.Lane_Chosen = CandidateTrafficLaneData_Left;
		OutRecommendation.bChoseLaneOnLeft = true;
		OutRecommendation.Level = NormalLaneChange;			
		return;
	}
	else if (!CandidateTrafficLaneData_Left && CandidateTrafficLaneData_Right)
	{
		OutRecommendation.Lane_Chosen = CandidateTrafficLaneData_Right;
		OutRecommendation.bChoseLaneOnRight = true;
		OutRecommendation.Level = NormalLaneChange;			
		return;
	}
	else if (CandidateTrafficLaneData_Left && CandidateTrafficLaneData_Right)
	{
		// Choose the one with less density, or random if equal.
		
		if (DownstreamFlowDensity_Candidate_Left < DownstreamFlowDensity_Candidate_Right)
		{
			OutRecommendation.Lane_Chosen = CandidateTrafficLaneData_Left;
			OutRecommendation.bChoseLaneOnLeft = true;
			OutRecommendation.Level = NormalLaneChange;
			return;				
		}
		else if (DownstreamFlowDensity_Candidate_Right < DownstreamFlowDensity_Candidate_Left) 
		{
			OutRecommendation.Lane_Chosen = CandidateTrafficLaneData_Right;
			OutRecommendation.bChoseLaneOnRight = true;
			OutRecommendation.Level = NormalLaneChange;
			return;
		}
		else // ..not as rare as you'd guess - happens (1) with float-16 density values (2) when density is zero
		{
			if (RandomStream.FRand() < 0.5f)
			{
				OutRecommendation.Lane_Chosen = CandidateTrafficLaneData_Left;
				OutRecommendation.bChoseLaneOnLeft = true;
				OutRecommendation.Level = NormalLaneChange;
				return;
			}
			else
			{
				OutRecommendation.Lane_Chosen = CandidateTrafficLaneData_Right;
				OutRecommendation.bChoseLaneOnRight = true;
				OutRecommendation.Level = NormalLaneChange;
				return;
			}
		}
	}

	UE_LOG(LogMassTraffic, Error, TEXT("%s - All tests failed."), ANSI_TO_TCHAR(__FUNCTION__));
}

bool CheckNextVehicle(const FMassEntityHandle Entity, const FMassEntityHandle NextEntity, const FMassEntityManager& EntityManager)
{
	static TArray<FVector> ReportedLocations;
	
	if (!Entity.IsSet() || !NextEntity.IsSet())
	{
		return true; // ..only check for valid entities	
	}
	
	const FMassEntityView EntityView(EntityManager, Entity);
	const FMassEntityView NextEntityView(EntityManager, NextEntity);

	const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = EntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();	
	const FMassZoneGraphLaneLocationFragment& NextLaneLocationFragment = NextEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
	if (LaneLocationFragment.LaneHandle != NextLaneLocationFragment.LaneHandle)
	{
		return true; // ..we're only checking vehicles that are on the same lane	
	}
	
	if (LaneLocationFragment.DistanceAlongLane < 0.01f && NextLaneLocationFragment.DistanceAlongLane < 0.01f)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("CheckNextVehicle - Next is coincident at lane start"));
	}
	else if (LaneLocationFragment.DistanceAlongLane >= NextLaneLocationFragment.DistanceAlongLane)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("CheckNextVehicle - Next is behind"));
	}
	else
	{
		return true;
	}
	
	return false;
}

	
}