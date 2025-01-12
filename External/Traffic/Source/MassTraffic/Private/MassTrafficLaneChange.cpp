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
	const FZoneGraphStorage& ZoneGraphStorage,
	FMassTrafficLaneChangeRecommendation& InOutRecommendation
)
{
	const bool bAlreadyChoseLane = InOutRecommendation.Lane_Chosen != nullptr;
	if (!bAlreadyChoseLane)
	{
		InOutRecommendation = FMassTrafficLaneChangeRecommendation();
	}

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

	FZoneGraphTrafficLaneData* CandidateTrafficLaneData_Left = !bAlreadyChoseLane || InOutRecommendation.bChoseLaneOnLeft ? TrafficLaneData_Initial->LeftLane : nullptr;
	FZoneGraphTrafficLaneData* CandidateTrafficLaneData_Right = !bAlreadyChoseLane || InOutRecommendation.bChoseLaneOnRight ? TrafficLaneData_Initial->RightLane : nullptr;

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
	
	if (!bAlreadyChoseLane)
	{
		// Get lane change priority.
		const int32 LeftLaneChangePriority = GetLanePriority(CandidateTrafficLaneData_Left, VehicleControlFragment.LaneChangePriorityFilters, ZoneGraphStorage);
		const int32 RightLaneChangePriority = GetLanePriority(CandidateTrafficLaneData_Right, VehicleControlFragment.LaneChangePriorityFilters, ZoneGraphStorage);

		const bool bHasLeftLaneChangePriority = LeftLaneChangePriority >= 0;
		const bool bHasRightLaneChangePriority = RightLaneChangePriority >= 0;

		// Filter lanes based on lane change priority.
		if (bHasLeftLaneChangePriority && bHasRightLaneChangePriority)
		{
			// If both candidate lanes have a valid lane change priority value, keep the one with the higher priority.
			// If they have equal priority, then keep both.  In this case, it will fall to the density logic
			// to decide between them, then finally a random choice.
			CandidateTrafficLaneData_Left = LeftLaneChangePriority >= RightLaneChangePriority ? CandidateTrafficLaneData_Left : nullptr;
			CandidateTrafficLaneData_Right = RightLaneChangePriority >= LeftLaneChangePriority ? CandidateTrafficLaneData_Right : nullptr;
		}
		else
		{
			// Remove candidate lanes as an option, if they don't have a valid lane change priority value.
			// If this is the case, it means they are not compatible with any of the LaneChangePriorityFilters
			// on the VehicleControlFragment.
			CandidateTrafficLaneData_Left = bHasLeftLaneChangePriority ? CandidateTrafficLaneData_Left : nullptr;
			CandidateTrafficLaneData_Right = bHasRightLaneChangePriority ? CandidateTrafficLaneData_Right : nullptr;
		}
	}
	
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
			InOutRecommendation.Lane_Chosen = (bTestLeftFirst ? CandidateTrafficLaneData_Left : CandidateTrafficLaneData_Right);
			InOutRecommendation.bChoseLaneOnRight = !bTestLeftFirst;
			InOutRecommendation.bChoseLaneOnLeft = bTestLeftFirst;
			InOutRecommendation.Level = TransversingLaneChange;
			return;
		}
		else if (TestTransverseCandidateTrafficLaneData(
			(!bTestLeftFirst ? CandidateTrafficLaneData_Left : CandidateTrafficLaneData_Right), !bTestLeftFirst))
		{
			InOutRecommendation.Lane_Chosen = (!bTestLeftFirst ? CandidateTrafficLaneData_Left : CandidateTrafficLaneData_Right);
			InOutRecommendation.bChoseLaneOnRight = bTestLeftFirst;
			InOutRecommendation.bChoseLaneOnLeft = !bTestLeftFirst;
			InOutRecommendation.Level = TransversingLaneChange;
			return;
		}
		else
		{
			InOutRecommendation.Level = StayOnCurrentLane_RetrySoon; // ..make lane change on transverse lanes more likely than on normal lanes
			return;
		}
	}
	
	
	if (!CandidateTrafficLaneData_Left && !CandidateTrafficLaneData_Right)
	{
		return;
	}
	else if (CandidateTrafficLaneData_Left && !CandidateTrafficLaneData_Right)
	{
		InOutRecommendation.Lane_Chosen = CandidateTrafficLaneData_Left;
		InOutRecommendation.bChoseLaneOnLeft = true;
		InOutRecommendation.Level = bAlreadyChoseLane ? TurningLaneChange : NormalLaneChange;			
		return;
	}
	else if (!CandidateTrafficLaneData_Left && CandidateTrafficLaneData_Right)
	{
		InOutRecommendation.Lane_Chosen = CandidateTrafficLaneData_Right;
		InOutRecommendation.bChoseLaneOnRight = true;
		InOutRecommendation.Level = bAlreadyChoseLane ? TurningLaneChange : NormalLaneChange;			
		return;
	}
	else if (CandidateTrafficLaneData_Left && CandidateTrafficLaneData_Right)
	{
		// Choose the one with less density, or random if equal.
		
		if (DownstreamFlowDensity_Candidate_Left < DownstreamFlowDensity_Candidate_Right)
		{
			InOutRecommendation.Lane_Chosen = CandidateTrafficLaneData_Left;
			InOutRecommendation.bChoseLaneOnLeft = true;
			InOutRecommendation.Level = bAlreadyChoseLane ? TurningLaneChange : NormalLaneChange;
			return;				
		}
		else if (DownstreamFlowDensity_Candidate_Right < DownstreamFlowDensity_Candidate_Left) 
		{
			InOutRecommendation.Lane_Chosen = CandidateTrafficLaneData_Right;
			InOutRecommendation.bChoseLaneOnRight = true;
			InOutRecommendation.Level = bAlreadyChoseLane ? TurningLaneChange : NormalLaneChange;
			return;
		}
		else // ..not as rare as you'd guess - happens (1) with float-16 density values (2) when density is zero
		{
			if (RandomStream.FRand() < 0.5f)
			{
				InOutRecommendation.Lane_Chosen = CandidateTrafficLaneData_Left;
				InOutRecommendation.bChoseLaneOnLeft = true;
				InOutRecommendation.Level = bAlreadyChoseLane ? TurningLaneChange : NormalLaneChange;
				return;
			}
			else
			{
				InOutRecommendation.Lane_Chosen = CandidateTrafficLaneData_Right;
				InOutRecommendation.bChoseLaneOnRight = true;
				InOutRecommendation.Level = bAlreadyChoseLane ? TurningLaneChange : NormalLaneChange;
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

bool ShouldYieldToCrosswalks_Internal(
	const FZoneGraphTrafficLaneData& CurrentLaneData,
	const TFunction<bool(const FZoneGraphLaneHandle&)>& IsDownstreamCrosswalkLaneClearFunc,
	const TFunction<bool(const FZoneGraphLaneHandle&)>& DownstreamCrosswalkLaneHasYieldingEntitiesFunc)
{
	// Check all the downstream crosswalk lanes for our current lane.
	for (const FZoneGraphLaneHandle& DownstreamCrosswalkLane : CurrentLaneData.DownstreamCrosswalkLanes)
	{
		// And, we skip over any crosswalk lanes, which already have yielding Entities.
		if (DownstreamCrosswalkLaneHasYieldingEntitiesFunc(DownstreamCrosswalkLane))
		{
			continue;
		}
		
		// We should yield, if any of our downstream crosswalk lanes are not clear.
		if (!IsDownstreamCrosswalkLaneClearFunc(DownstreamCrosswalkLane))
		{
			return true;
		}
	}

	return false;
}

bool ShouldYieldAtIntersection_Internal(
	const FZoneGraphTrafficLaneData& CurrentLaneData,
	const FZoneGraphTrafficLaneData& PrevLaneData,
	const TFunction<bool(const FZoneGraphTrafficLaneData&)>& IsTestLaneClearFunc,
	const bool bConsiderYieldForGoingStraight = true,
	const bool bConsiderYieldForLeftTurns = true,
	const bool bConsiderYieldForRightTurns = true)
{
	// Only consider whether to yield at intersection lanes.
	if (!CurrentLaneData.ConstData.bIsIntersectionLane)
	{
		return false;
	}

	// If we're turning left at an intersection, ...
	if (CurrentLaneData.bTurnsLeft && bConsiderYieldForLeftTurns)
	{
		// We need to iterate left from our *previous* lane and look at the successors of those lanes.
		// Note:  Iterating left or right from the current lane (which is in the intersection) will only yield lanes
		// of the same type as our current lane (ie. only left turns, only right turns, or only straight lanes).
		for (FZoneGraphTrafficLaneData* LeftLane = PrevLaneData.LeftLane; LeftLane != nullptr; LeftLane = LeftLane->LeftLane)
		{
			ensureMsgf(!LeftLane->ConstData.bIsIntersectionLane, TEXT("Lanes left of the previous lane should *not* be intersection lanes."));
			
			for (FZoneGraphTrafficLaneData* NextLeftLane : LeftLane->NextLanes)
			{
				ensureMsgf(NextLeftLane->ConstData.bIsIntersectionLane, TEXT("Successors of lanes left of the previous lane *should* be intersection lanes."));
				
				// Then, we need to check all the intersection lanes on the left of us that are not also turning left.
				if (!NextLeftLane->bTurnsLeft)
				{
					// And, we skip over any lanes, which already have yielding vehicles. 
					if (NextLeftLane->HasYieldingVehicles())
					{
						continue;
					}
					
					// But, for the remaining lanes, we should yield, if they are not clear.
					if (!IsTestLaneClearFunc(*NextLeftLane))
					{
						return true;
					}
				}
			}
		}
	}
	// If we're turning right at an intersection, ...
	else if (CurrentLaneData.bTurnsRight && bConsiderYieldForRightTurns)
	{
		// We need to iterate right from our *previous* lane and look at the successors of those lanes.
		// Note:  Iterating left or right from the current lane (which is in the intersection) will only yield lanes
		// of the same type as our current lane (ie. only left turns, only right turns, or only straight lanes).
		for (FZoneGraphTrafficLaneData* RightLane = PrevLaneData.RightLane; RightLane != nullptr; RightLane = RightLane->RightLane)
		{
			ensureMsgf(!RightLane->ConstData.bIsIntersectionLane, TEXT("Lanes right of the previous lane should *not* be intersection lanes."));
			
			for (FZoneGraphTrafficLaneData* NextRightLane : RightLane->NextLanes)
			{
				ensureMsgf(NextRightLane->ConstData.bIsIntersectionLane, TEXT("Successors of lanes right of the previous lane *should* be intersection lanes."));
				
				// Then, we need to check all the intersection lanes on the right of us that are not also turning right.
				if (!NextRightLane->bTurnsRight)
				{
					// And, we skip over any lanes, which already have yielding vehicles. 
					if (NextRightLane->HasYieldingVehicles())
					{
						continue;
					}
					
					// But, for the remaining lanes, we should yield, if they are not clear.
					if (!IsTestLaneClearFunc(*NextRightLane))
					{
						return true;
					}
				}
			}
		}
	}
	// If we're going straight at an intersection, ...
	else if (bConsiderYieldForGoingStraight)
	{
		// We need to iterate left from our *previous* lane and look at the successors of those lanes.
		// Note:  Iterating left or right from the current lane (which is in the intersection) will only yield lanes
		// of the same type as our current lane (ie. only left turns, only right turns, or only straight lanes).
		for (FZoneGraphTrafficLaneData* LeftLane = PrevLaneData.LeftLane; LeftLane != nullptr; LeftLane = LeftLane->LeftLane)
		{
			ensureMsgf(!LeftLane->ConstData.bIsIntersectionLane, TEXT("Lanes left of the previous lane should *not* be intersection lanes."));
			
			for (FZoneGraphTrafficLaneData* NextLeftLane : LeftLane->NextLanes)
			{
				ensureMsgf(NextLeftLane->ConstData.bIsIntersectionLane, TEXT("Successors of lanes left of the previous lane *should* be intersection lanes."));
				
				// We need to make sure that no one is crossing in front of us by turning right from our left side.
				if (NextLeftLane->bTurnsRight)
				{
					// And, we skip over any lanes, which already have yielding vehicles. 
					if (NextLeftLane->HasYieldingVehicles())
					{
						continue;
					}
					
					// But, for the remaining lanes, we should yield, if they are not clear.
					if (!IsTestLaneClearFunc(*NextLeftLane))
					{
						return true;
					}
				}
			}
		}
		
		// We need to iterate right from our *previous* lane and look at the successors of those lanes.
		// Note:  Iterating left or right from the current lane (which is in the intersection) will only yield lanes
		// of the same type as our current lane (ie. only left turns, only right turns, or only straight lanes).
		for (FZoneGraphTrafficLaneData* RightLane = PrevLaneData.RightLane; RightLane != nullptr; RightLane = RightLane->RightLane)
		{
			ensureMsgf(!RightLane->ConstData.bIsIntersectionLane, TEXT("Lanes right of the previous lane should *not* be intersection lanes."));
			
			for (FZoneGraphTrafficLaneData* NextRightLane : RightLane->NextLanes)
			{
				ensureMsgf(NextRightLane->ConstData.bIsIntersectionLane, TEXT("Successors of lanes right of the previous lane *should* be intersection lanes."));
				
				// We need to make sure that no one is crossing in front of us by turning left from our right side.
				if (NextRightLane->bTurnsLeft)
				{
					// And, we skip over any lanes, which already have yielding vehicles. 
					if (NextRightLane->HasYieldingVehicles())
					{
						continue;
					}
					
					// But, for the remaining lanes, we should yield, if they are not clear.
					if (!IsTestLaneClearFunc(*NextRightLane))
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

// Note:  As the "reactive yield" logic now needs to look ahead for pedestrians and merge considerations,
// this has really become a "roll-out" yield, rather than having the sole distinction that it anticipates
// a yield ahead of entering the intersection.  Will "rebrand" it as such in a future PR.
bool ShouldPerformPreemptiveYieldAtIntersection(
	const UMassTrafficSubsystem& MassTrafficSubsystem,
	const UMassCrowdSubsystem& MassCrowdSubsystem,
	const FMassEntityManager& EntityManager,
	const FMassTrafficVehicleControlFragment& VehicleControlFragment,
	const FMassZoneGraphLaneLocationFragment& LaneLocationFragment,
	const FAgentRadiusFragment& RadiusFragment,
	const FZoneGraphStorage& ZoneGraphStorage,
	bool& OutHasAnotherVehicleEnteredRelevantLaneAfterPreemptiveYieldRollOut)
{
	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>();
	if (!ensureMsgf(MassTrafficSettings != nullptr, TEXT("Can't access MassTrafficSettings in ShouldPerformPreemptiveYieldAtIntersection.  Yield behavior disabled.")))
	{
		return false;
	}
	
	const FZoneGraphTrafficLaneData* CurrentLaneData = MassTrafficSubsystem.GetTrafficLaneData(LaneLocationFragment.LaneHandle);
	if (!ensureMsgf(CurrentLaneData != nullptr, TEXT("Can't access CurrentLaneData in ShouldPerformPreemptiveYieldAtIntersection.  Returning false.")))
	{
		return false;
	}

	// We need to find our intersection lane.
	// Pre-emptive yields start by looking ahead to the next lane, but they need to switch to the "current" lane,
	// once the vehicle rolls-out into the intersection.
	const auto& GetIntersectionLaneData = [&VehicleControlFragment, &CurrentLaneData]() -> const FZoneGraphTrafficLaneData*
	{
		if (VehicleControlFragment.NextLane != nullptr && VehicleControlFragment.NextLane->ConstData.bIsIntersectionLane)
		{
			return VehicleControlFragment.NextLane;
		}
		
		if (CurrentLaneData->ConstData.bIsIntersectionLane)
		{
			return CurrentLaneData;
		}

		return nullptr;
	};

	// If neither our next lane, nor our current lane is an intersection lane, we can't pre-emptively yield.
	const FZoneGraphTrafficLaneData* IntersectionLaneData = GetIntersectionLaneData();
	if (IntersectionLaneData == nullptr)
	{
		return false;
	}

	// We can't *start* a pre-emptive yield in the intersection, only before we're in the intersection.
	// Otherwise, we are only governed by the reactive yield logic.
	if (CurrentLaneData == IntersectionLaneData && !VehicleControlFragment.IsPreemptivelyYieldingAtIntersection())
	{
		return false;
	}

	// Then, we need to find the lane before the intersection.
	const auto& GetLaneDataBeforeIntersection = [&MassTrafficSubsystem, &VehicleControlFragment, &IntersectionLaneData, &CurrentLaneData]() -> const FZoneGraphTrafficLaneData*
	{
		if (IntersectionLaneData == VehicleControlFragment.NextLane)
		{
			return CurrentLaneData;
		}

		if (IntersectionLaneData == CurrentLaneData)
		{
			const FMassTrafficZoneGraphData* TrafficZoneGraphData = MassTrafficSubsystem.GetTrafficZoneGraphData(CurrentLaneData->LaneHandle.DataHandle);
	
			const FZoneGraphTrafficLaneData* PrevLaneData = TrafficZoneGraphData != nullptr && VehicleControlFragment.PreviousLaneIndex != INDEX_NONE
				? TrafficZoneGraphData->GetTrafficLaneData(VehicleControlFragment.PreviousLaneIndex)
				: nullptr;

			return PrevLaneData;
		}

		return nullptr;
	};

	// If we can't find the lane before the intersection,
	// we won't be able to traverse the lane graph in the internal "should yield" logic.
	const FZoneGraphTrafficLaneData* LaneDataBeforeIntersection = GetLaneDataBeforeIntersection();
	if (LaneDataBeforeIntersection == nullptr)
	{
		return false;
	}

	// If we haven't started preemptively yielding and the intersection is our next lane, ...
	if (!VehicleControlFragment.IsPreemptivelyYieldingAtIntersection() && IntersectionLaneData == VehicleControlFragment.NextLane)
	{
		const float DistanceFromEndOfLane = LaneLocationFragment.LaneLength - (LaneLocationFragment.DistanceAlongLane + RadiusFragment.Radius);

		// And, if we're not close enough to the end of the lane, we shouldn't pre-emptively yield.
		if (DistanceFromEndOfLane > MassTrafficSettings->MaxDistanceFromEndOfLaneForPreemptiveYield)
		{
			return false;
		}
	}

	const auto& IsTestLaneClear = [&EntityManager, &VehicleControlFragment, &CurrentLaneData, &LaneDataBeforeIntersection, MassTrafficSettings](const FZoneGraphTrafficLaneData& TestLane)
	{
		if (!VehicleControlFragment.IsPreemptivelyYieldingAtIntersection())
		{
			// A pre-emptive yield starts when a vehicle is ready to use the test lane before we are in the intersection.
			return !(CurrentLaneData == LaneDataBeforeIntersection && TestLane.HasVehiclesReadyToUseIntersectionLane());	// (See all READYLANE.)
		}
		
		// Once in a pre-emptive yield scenario,
		// we wait for the other vehicle(s) to enter the lane
		// and get far enough through the intersection before ending our yield.
		if (TestLane.NumVehiclesOnLane > 0)
		{
			const auto& TryGetNormalizedDistanceAlongTestLane = [&EntityManager, &TestLane](float& OutNormalizedDistanceAlongTestLane)
			{
				float DistanceAlongTestLane = 0.0f;
				if (!TestLane.TryGetDistanceFromStartOfLaneToTailVehicle(EntityManager, DistanceAlongTestLane))
				{
					ensureMsgf(TestLane.NumVehiclesOnLane == 0, TEXT("If we don't have a tail vehicle, then TestLane should have no vehicles on it."));
				
					// The lane doesn't have a tail vehicle.
					return false;
				}
				
				if (!ensureMsgf(TestLane.Length > 0.0f, TEXT("TestLane should have a length greater than zero.")))
				{
					return false;
				}
		
				const float NormalizedDistanceAlongTestLane = DistanceAlongTestLane / TestLane.Length;
		
				OutNormalizedDistanceAlongTestLane = NormalizedDistanceAlongTestLane;
				return true;
			};
		
			float NormalizedDistanceAlongTestLane = 0.0f;
			if (!TryGetNormalizedDistanceAlongTestLane(NormalizedDistanceAlongTestLane))
			{
				// If we can't determine the other vehicle's distance along the test lane,
				// just say it's clear to keep traffic flowing.
				return true;
			}

			const float NormalizedYieldResumeLaneDistance =
				TestLane.bTurnsLeft ? MassTrafficSettings->NormalizedYieldResumeLaneDistance_Left :
				TestLane.bTurnsRight ? MassTrafficSettings->NormalizedYieldResumeLaneDistance_Right :
				MassTrafficSettings->NormalizedYieldResumeLaneDistance_Straight;
		
			if (NormalizedDistanceAlongTestLane < NormalizedYieldResumeLaneDistance)
			{
				return false;
			}
		}
		
		return true;
	};

	// Note:  For pre-emptive yields, turning vehicles give the right of way to vehicles that are going straight.
	// So, we don't consider pre-emptively yielding when going straight.
	// However, vehicles going straight can reactively yield
	// after an opportunity is given for turning vehicles to reactively yield first.
	const bool bShouldYieldAtIntersection = ShouldYieldAtIntersection_Internal(*IntersectionLaneData, *LaneDataBeforeIntersection, IsTestLaneClear, false);

	// If we are pre-emptively yielding, ...
	if (VehicleControlFragment.IsPreemptivelyYieldingAtIntersection())
	{
		// Wait until we roll-out, then we'll start checking our wait time.
		// During this period of time, we remain in the pre-emptive yield.
		if (!VehicleControlFragment.HasStartedWaitingAfterRollOutForPreemptiveYieldAtIntersection())
		{
			return true;
		}
		
		// We're pre-emptively yielding,
		// but the vehicle(s) that we decided to pre-emptively yield for aren't on the relevant lane(s) yet.
		//
		// So, we give them some time to get on the relevant lane(s), while we remain in the pre-emptive yield.
		//
		// During this time, we also indicate to the caller whether other vehicles have entered
		// relevant lanes from a pre-emptive yield perspective.
		// If so, this allows the logic in UpdateYieldAtIntersectionState to end our wait time early.
		//
		// If our wait time fully elapses, then the reactive yield logic will take over.
		//
		// However, if other vehicles enter relevant lanes first,
		// the pre-emptive yield logic will remain in effect until the lanes are clear
		// from a pre-emptive yield perspective.
		if (!VehicleControlFragment.HasFinishedWaitingAfterRollOutForPreemptiveYieldAtIntersection())
		{
			OutHasAnotherVehicleEnteredRelevantLaneAfterPreemptiveYieldRollOut = bShouldYieldAtIntersection;
			return true;
		}
	}

	return bShouldYieldAtIntersection;
}

bool ShouldPerformReactiveYieldAtIntersection(
	const UMassTrafficSubsystem& MassTrafficSubsystem,
	const UMassCrowdSubsystem& MassCrowdSubsystem,
	const FMassEntityManager& EntityManager,
	const FMassTrafficVehicleControlFragment& VehicleControlFragment,
	const FMassZoneGraphLaneLocationFragment& LaneLocationFragment,
	const FAgentRadiusFragment& RadiusFragment,
	const FMassTrafficRandomFractionFragment& RandomFractionFragment,
	const FZoneGraphStorage& ZoneGraphStorage,
	bool& OutShouldGiveOpportunityForTurningVehiclesToReactivelyYieldAtIntersection,
	int32& OutMergeYieldCaseIndex)
{
	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>();
	if (!ensureMsgf(MassTrafficSettings != nullptr, TEXT("Can't access MassTrafficSettings in ShouldPerformReactiveYieldAtIntersection.  Yield behavior disabled.")))
	{
		return false;
	}
	
	const FZoneGraphTrafficLaneData* CurrentLaneData = MassTrafficSubsystem.GetTrafficLaneData(LaneLocationFragment.LaneHandle);
	if (CurrentLaneData == nullptr)
	{
		return false;
	}

	const FMassTrafficZoneGraphData* TrafficZoneGraphData = MassTrafficSubsystem.GetTrafficZoneGraphData(CurrentLaneData->LaneHandle.DataHandle);
	
	const FZoneGraphTrafficLaneData* PrevLaneData = TrafficZoneGraphData != nullptr && VehicleControlFragment.PreviousLaneIndex != INDEX_NONE
		? TrafficZoneGraphData->GetTrafficLaneData(VehicleControlFragment.PreviousLaneIndex)
		: nullptr;

	if (PrevLaneData == nullptr)
	{
		return false;
	}

	// We need to find our intersection lane.
	// We need to look out for crosswalk lanes just beyond the entrance to the intersection.
	const auto& GetIntersectionLaneData = [&VehicleControlFragment, &CurrentLaneData]() -> const FZoneGraphTrafficLaneData*
	{
		if (VehicleControlFragment.NextLane != nullptr && VehicleControlFragment.NextLane->ConstData.bIsIntersectionLane)
		{
			return VehicleControlFragment.NextLane;
		}
		
		if (CurrentLaneData->ConstData.bIsIntersectionLane)
		{
			return CurrentLaneData;
		}

		return nullptr;
	};

	// Currently, we only support yielding while entering or exiting intersections.
	// So, either our current lane or our next lane needs to be an intersection lane.
	const FZoneGraphTrafficLaneData* IntersectionLaneData = GetIntersectionLaneData();
	if (IntersectionLaneData == nullptr)
	{
		return false;
	}

	// Then, we need to find the lane before the intersection.
	const auto& GetLaneDataBeforeIntersection = [&MassTrafficSubsystem, &VehicleControlFragment, &IntersectionLaneData, &CurrentLaneData]() -> const FZoneGraphTrafficLaneData*
	{
		if (IntersectionLaneData == VehicleControlFragment.NextLane)
		{
			return CurrentLaneData;
		}

		if (IntersectionLaneData == CurrentLaneData)
		{
			const FMassTrafficZoneGraphData* TrafficZoneGraphData = MassTrafficSubsystem.GetTrafficZoneGraphData(CurrentLaneData->LaneHandle.DataHandle);
	
			const FZoneGraphTrafficLaneData* PrevLaneData = TrafficZoneGraphData != nullptr && VehicleControlFragment.PreviousLaneIndex != INDEX_NONE
				? TrafficZoneGraphData->GetTrafficLaneData(VehicleControlFragment.PreviousLaneIndex)
				: nullptr;

			return PrevLaneData;
		}

		return nullptr;
	};

	// If we can't find the lane before the intersection,
	// we won't be able to traverse the lane graph in the internal "should yield" logic.
	const FZoneGraphTrafficLaneData* LaneDataBeforeIntersection = GetLaneDataBeforeIntersection();
	if (LaneDataBeforeIntersection == nullptr)
	{
		return false;
	}
	
	const auto& DownstreamCrosswalkLaneHasYieldingEntities = [&MassTrafficSubsystem, &CurrentLaneData](const FZoneGraphLaneHandle& TestDownstreamCrosswalkLane)
	{
		const FMassTrafficCrosswalkLaneInfo* CrosswalkLaneInfo = MassTrafficSubsystem.GetCrosswalkLaneInfo(TestDownstreamCrosswalkLane);

		if (!ensureMsgf(CrosswalkLaneInfo != nullptr, TEXT("Must get valid CrosswalkLaneInfo in DownstreamCrosswalkLaneHasYieldingEntities.  TestDownstreamCrosswalkLane.Index: %d."), TestDownstreamCrosswalkLane.Index))
		{
			return false;
		}

		// Are there any Entities on the crosswalk lane that are yielding to our current lane?
		return CrosswalkLaneInfo->IsAnyEntityOnCrosswalkYieldingToLane(CurrentLaneData->LaneHandle);
	};
	
	const auto& IsDownstreamCrosswalkLaneClear = [&MassTrafficSubsystem, &MassCrowdSubsystem, &CurrentLaneData, &IntersectionLaneData, &VehicleControlFragment, &LaneLocationFragment, &RadiusFragment, &ZoneGraphStorage, MassTrafficSettings](const FZoneGraphLaneHandle& TestDownstreamCrosswalkLane)
	{
		if (!ensureMsgf(CurrentLaneData->Length > 0.0f && LaneLocationFragment.LaneLength > 0.0f, TEXT("CurrentLane should have a length greater than zero.")))
		{
			// If we have errant lane data, just say it's clear to attempt to keep traffic flowing.
			return true;
		}

		if (!ensureMsgf(IntersectionLaneData->Length > 0.0f, TEXT("IntersectionLaneData should have a length greater than zero.")))
		{
			// If we have errant lane data, just say it's clear to attempt to keep traffic flowing.
			return true;
		}
		
		const FCrowdTrackingLaneData* CrowdTrackingLaneData = MassCrowdSubsystem.GetCrowdTrackingLaneData(TestDownstreamCrosswalkLane);
		
		if (!ensureMsgf(CrowdTrackingLaneData != nullptr, TEXT("Must get valid CrowdTrackingLaneData in IsDownstreamCrosswalkLaneClear.  TestDownstreamCrosswalkLane.Index: %d."), TestDownstreamCrosswalkLane.Index))
		{
			// Since we're in an error state, assume it's clear to keep traffic flowing.
			return true;
		}
		
		if (CrowdTrackingLaneData->NumEntitiesOnLane <= 0)
		{
			return true;
		}

		if (!ensureMsgf(
			CrowdTrackingLaneData->LeadEntityHandle.IsSet()
			&& CrowdTrackingLaneData->LeadEntityDistanceAlongLane.IsSet()
			&& CrowdTrackingLaneData->LeadEntitySpeedAlongLane.IsSet()
			&& CrowdTrackingLaneData->LeadEntityAccelerationAlongLane.IsSet()
			&& CrowdTrackingLaneData->LeadEntityRadius.IsSet(),
			TEXT("Since CrowdTrackingLaneData has Entities on the lane, required LeadEntity properties must be set in IsDownstreamCrosswalkLaneClear.  TestDownstreamCrosswalkLane.Index: %d."), TestDownstreamCrosswalkLane.Index))
		{
			// Since there are Entities on the test lane, but we can't determine the necessary properties
			// about the lead Entity, we just wait until all Entities fully clear the test lane
			// before allowing vehicles to resume from yielding.
			return false;
		}

		if (!ensureMsgf(
			CrowdTrackingLaneData->TailEntityHandle.IsSet()
			&& CrowdTrackingLaneData->TailEntityDistanceAlongLane.IsSet()
			&& CrowdTrackingLaneData->TailEntitySpeedAlongLane.IsSet()
			&& CrowdTrackingLaneData->TailEntityAccelerationAlongLane.IsSet()
			&& CrowdTrackingLaneData->TailEntityRadius.IsSet(),
			TEXT("Since CrowdTrackingLaneData has Entities on the lane, required TailEntity properties must be set in IsDownstreamCrosswalkLaneClear.  TestDownstreamCrosswalkLane.Index: %d."), TestDownstreamCrosswalkLane.Index))
		{
			// Since there are Entities on the test lane, but we can't determine the necessary properties
			// about the tail Entity, we just wait until all Entities fully clear the test lane
			// before allowing vehicles to resume from yielding.
			return false;
		}
		
		const float EffectiveVehicleSpeed = VehicleControlFragment.IsYieldingAtIntersection() ? 0.0f : VehicleControlFragment.Speed;

		float VehicleEnterTime;
		float VehicleExitTime;

		float VehicleEnterDistance;
		float VehicleExitDistance;

		if (!TryGetVehicleEnterAndExitTimesForCrossingLane(
				MassTrafficSubsystem,
				*MassTrafficSettings,
				ZoneGraphStorage,
				*CurrentLaneData,
				*IntersectionLaneData,
				TestDownstreamCrosswalkLane,
				LaneLocationFragment.DistanceAlongLane,
				EffectiveVehicleSpeed,
				RadiusFragment.Radius,
				VehicleEnterTime,
				VehicleExitTime,
				&VehicleEnterDistance,
				&VehicleExitDistance))
		{
			// Since there are Entities on the test lane,
			// we just wait until all Entities fully clear the test lane before allowing vehicles to resume
			// from yielding.
			return false;
		}

		if (VehicleControlFragment.IsYieldingAtIntersection())
		{
			GetEnterAndExitTimeForYieldingEntity(
				VehicleEnterDistance,
				VehicleExitDistance,
				VehicleControlFragment.AccelerationEstimate,
				VehicleEnterTime,
				VehicleExitTime);
		}

		// Once we've entered the crosswalk lane, or we've gone past it,
		// we don't consider yielding to it anymore.
		if (VehicleEnterDistance < 0.0f)
		{
			return true;
		}

		// We just say the crosswalk lane is clear from our perspective
		// if we won't enter the lane until after some specified horizon time.
		// Note:  This time should be set, such that it would be sufficient to fully brake to a stop.
		if (VehicleEnterTime > MassTrafficSettings->VehicleCrosswalkYieldLookAheadTime)
		{
			return true;
		}

		// Don't consider yielding at stop sign controlled intersections
		// until after the vehicle has completed its stop sign rest behavior.
		if (IntersectionLaneData->HasTrafficSignThatRequiresStop())
		{
			if (CurrentLaneData != IntersectionLaneData && VehicleControlFragment.StopSignIntersectionLane != IntersectionLaneData)
			{
				return true;
			}
		}

		const auto& TryGetPedestrianEnterAndExitInfo = [&ZoneGraphStorage, &TestDownstreamCrosswalkLane, &IntersectionLaneData, &CrowdTrackingLaneData, &MassTrafficSubsystem, &MassTrafficSettings](float& OutPedestrianEnterTime, float& OutPedestrianExitTime, float& OutPedestrianEnterDistance, float& OutPedestrianExitDistance)
		{
			if (CrowdTrackingLaneData->NumEntitiesOnLane > 0)
			{
				float LeadPedestrianEnterTime;
				float LeadPedestrianExitTime;

				float LeadPedestrianEnterDistance;
				float LeadPedestrianExitDistance;
				
				if (!TryGetEntityEnterAndExitTimesForCrossingLane(
					MassTrafficSubsystem,
					*MassTrafficSettings,
					ZoneGraphStorage,
					TestDownstreamCrosswalkLane,
					IntersectionLaneData->LaneHandle,
					CrowdTrackingLaneData->LeadEntityDistanceAlongLane.GetValue(),
					CrowdTrackingLaneData->LeadEntitySpeedAlongLane.GetValue(),
					CrowdTrackingLaneData->LeadEntityRadius.GetValue(),
					LeadPedestrianEnterTime,
					LeadPedestrianExitTime,
					&LeadPedestrianEnterDistance,
					&LeadPedestrianExitDistance))
				{
					return false;
				}
				
				OutPedestrianEnterTime = LeadPedestrianEnterTime;
				OutPedestrianExitTime = LeadPedestrianExitTime;

				OutPedestrianEnterDistance = LeadPedestrianEnterDistance;
				OutPedestrianExitDistance = LeadPedestrianExitDistance;
			}

			if (CrowdTrackingLaneData->NumEntitiesOnLane > 1)
			{
				float TailPedestrianEnterTime;
				float TailPedestrianExitTime;

				float TailPedestrianEnterDistance;
				float TailPedestrianExitDistance;
				
				if (!TryGetEntityEnterAndExitTimesForCrossingLane(
					MassTrafficSubsystem,
					*MassTrafficSettings,
					ZoneGraphStorage,
					TestDownstreamCrosswalkLane,
					IntersectionLaneData->LaneHandle,
					CrowdTrackingLaneData->TailEntityDistanceAlongLane.GetValue(),
					CrowdTrackingLaneData->TailEntitySpeedAlongLane.GetValue(),
					CrowdTrackingLaneData->TailEntityRadius.GetValue(),
					TailPedestrianEnterTime,
					TailPedestrianExitTime,
					&TailPedestrianEnterDistance,
					&TailPedestrianExitDistance))
				{
					return false;
				}

				OutPedestrianExitTime = TailPedestrianExitTime;
				
				OutPedestrianExitDistance = TailPedestrianExitDistance;
			}

			return true;
		};

		float PedestrianEnterTime;
		float PedestrianExitTime;

		float PedestrianEnterDistance;
		float PedestrianExitDistance;

		// If we can't get enter and exit info for the lead and/or tail pedestrian,
		// we need to wait until they completely clear the crosswalk lane before proceeding
		// since we're missing the necessary information to safely navigate past them before that.
		if (!TryGetPedestrianEnterAndExitInfo(PedestrianEnterTime, PedestrianExitTime, PedestrianEnterDistance, PedestrianExitDistance))
		{
			return false;
		}

		const bool bPedestrianIsInVehicleLane = PedestrianEnterDistance <= 0.0f && PedestrianExitDistance > 0.0f;

		const bool bInTimeConflictWithPedestrian = (VehicleExitTime >= 0.0f && PedestrianExitTime >= 0.0f)
			&& (VehicleEnterTime < TNumericLimits<float>::Max() && PedestrianEnterTime < TNumericLimits<float>::Max())
			&& VehicleEnterTime < PedestrianExitTime + MassTrafficSettings->VehicleCrosswalkYieldTimeBuffer && PedestrianEnterTime < VehicleExitTime + MassTrafficSettings->VehicleCrosswalkYieldTimeBuffer;
		
		const bool bInDistanceConflictWithPedestrian = bPedestrianIsInVehicleLane && VehicleEnterDistance < MassTrafficSettings->VehiclePedestrianBufferDistanceOnCrosswalk && VehicleEnterDistance > 0.0f;
		
		if (bInTimeConflictWithPedestrian || bInDistanceConflictWithPedestrian)
		{
			return false;
		}
		
		return true;
	};

	const auto& IsTestLaneClear = [&EntityManager, &CurrentLaneData, &LaneLocationFragment, &RadiusFragment, MassTrafficSettings](const FZoneGraphTrafficLaneData& TestLane)
	{
		if (!ensureMsgf(CurrentLaneData->Length > 0.0f && LaneLocationFragment.LaneLength > 0.0f, TEXT("CurrentLane should have a length greater than zero.")))
		{
			// If we have errant lane data, just say it's clear to attempt to keep traffic flowing.
			return true;
		}
		
		if (TestLane.NumVehiclesOnLane > 0)
		{
			const auto& TryGetNormalizedDistanceAlongTestLane = [&EntityManager, &TestLane](float& OutNormalizedDistanceAlongTestLane)
			{
				float DistanceAlongTestLane = 0.0f;
				if (!TestLane.TryGetDistanceFromStartOfLaneToTailVehicle(EntityManager, DistanceAlongTestLane))
				{
					ensureMsgf(TestLane.NumVehiclesOnLane == 0, TEXT("If we don't have a tail vehicle, then TestLane should have no vehicles on it."));
				
					// The lane doesn't have a tail vehicle.
					return false;
				}
				
				if (!ensureMsgf(TestLane.Length > 0.0f, TEXT("TestLane should have a length greater than zero.")))
				{
					return false;
				}

				const float NormalizedDistanceAlongTestLane = DistanceAlongTestLane / TestLane.Length;

				OutNormalizedDistanceAlongTestLane = NormalizedDistanceAlongTestLane;
				return true;
			};

			const float NormalizedDistanceAlongCurrentLane = (LaneLocationFragment.DistanceAlongLane - RadiusFragment.Radius) / LaneLocationFragment.LaneLength;

			float NormalizedDistanceAlongTestLane = 0.0f;
			if (!TryGetNormalizedDistanceAlongTestLane(NormalizedDistanceAlongTestLane))
			{
				// If we can't determine the other vehicle's distance along the test lane,
				// just say it's clear to keep traffic flowing.
				return true;
			}

			const float NormalizedYieldCutoffLaneDistance =
				TestLane.bTurnsLeft ? MassTrafficSettings->NormalizedYieldCutoffLaneDistance_Left :
				TestLane.bTurnsRight ? MassTrafficSettings->NormalizedYieldCutoffLaneDistance_Right :
				MassTrafficSettings->NormalizedYieldCutoffLaneDistance_Straight;

			const float NormalizedYieldResumeLaneDistance =
				TestLane.bTurnsLeft ? MassTrafficSettings->NormalizedYieldResumeLaneDistance_Left :
				TestLane.bTurnsRight ? MassTrafficSettings->NormalizedYieldResumeLaneDistance_Right :
				MassTrafficSettings->NormalizedYieldResumeLaneDistance_Straight;

			if (NormalizedDistanceAlongCurrentLane < NormalizedYieldCutoffLaneDistance && NormalizedDistanceAlongTestLane < NormalizedYieldResumeLaneDistance)
			{
				return false;
			}
		}

		return true;
	};

	// Only yield in response to merge conditions, if we are eligible to attempt a merge in the first place.
	if (IsVehicleEligibleToMergeOntoLane(
		MassTrafficSubsystem,
		VehicleControlFragment,
		CurrentLaneData->LaneHandle,
		IntersectionLaneData->LaneHandle,
		LaneLocationFragment.DistanceAlongLane,
		RadiusFragment.Radius,
		RandomFractionFragment.RandomFraction.GetFloat(),
		MassTrafficSettings->StoppingDistanceRange))
	{
		const bool bShouldYieldToMergeConflict = !ShouldVehicleMergeOntoLane(
			MassTrafficSubsystem,
			*MassTrafficSettings,
			VehicleControlFragment,
			CurrentLaneData->LaneHandle,
			IntersectionLaneData->LaneHandle,
			LaneLocationFragment.DistanceAlongLane,
			RadiusFragment.Radius,
			RandomFractionFragment.RandomFraction.GetFloat(),
			MassTrafficSettings->StoppingDistanceRange,
			ZoneGraphStorage,
			OutMergeYieldCaseIndex
			);

		if (bShouldYieldToMergeConflict)
		{
			return true;
		}
	}

	const bool bShouldYieldToCrosswalks = ShouldYieldToCrosswalks_Internal(*IntersectionLaneData, IsDownstreamCrosswalkLaneClear, DownstreamCrosswalkLaneHasYieldingEntities);
	
	if (bShouldYieldToCrosswalks)
	{
		return true;
	}
	
	const bool bShouldReactivelyYieldAtIntersection = ShouldYieldAtIntersection_Internal(*CurrentLaneData, *PrevLaneData, IsTestLaneClear);

	// If we haven't started reactively yielding, ...
	if (!VehicleControlFragment.IsReactivelyYieldingAtIntersection())
	{
		// And, if we're going straight, and we should reactively yield, but we haven't given the opportunity
		// for a turning vehicle to reactively yield to us, then we indicate that we should do so.
		// We pass this state back to the caller to forward to UpdateYieldAtIntersectionState.
		// If the turning vehicles don't take this opportunity to reactively yield to us,
		// then we will reactively yield to them next update, if the reactive yield logic still applies at that time.
		if (!CurrentLaneData->bTurnsLeft && !CurrentLaneData->bTurnsRight && bShouldReactivelyYieldAtIntersection && !VehicleControlFragment.HasGivenOpportunityForTurningVehiclesToReactivelyYieldAtIntersection())
		{
			OutShouldGiveOpportunityForTurningVehiclesToReactivelyYieldAtIntersection = true;
			return false;
		}
	}

	return bShouldReactivelyYieldAtIntersection;
}

}
