// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficMovement.h"
#include "MassTrafficSubsystem.h"
#include "MassTrafficDebugHelpers.h"
#include "MassTrafficFragments.h"
#include "MassTrafficLaneChange.h"

#include "MassEntityView.h"
#include "MassZoneGraphNavigationFragments.h"


namespace UE::MassTraffic
{
	
float CalculateTargetSpeed(
	float DistanceAlongLane,
	float Speed,
	float DistanceToNext,
	float TimeToCollidingObstacle,
	float DistanceToCollidingObstacle,
	float Radius,
	float RandomFraction,
	float LaneLength,
	float SpeedLimit,
	const FVector2D& IdealTimeToNextVehicleRange,
	const FVector2D& MinimumDistanceToNextVehicleRange,
	float NextVehicleAvoidanceBrakingPower,  // 3.0f  @todo Better param name
	const FVector2D& ObstacleAvoidanceBrakingTimeRange,
	const FVector2D& MinimumDistanceToObstacleRange,
	float ObstacleAvoidanceBrakingPower,  // 0.5f  @todo Better param name
	float StopSignBrakingTime,
	FVector2D StoppingDistanceFromLaneEndRange,
	float StopSignBrakingPower, // 0.5f  @todo Better param name
	bool bStopAtLaneExit
#if WITH_MASSTRAFFIC_DEBUG
	, bool bVisLog
	, const UObject* VisLogOwner
	, const FTransform* VisLogTransform
#endif
)
{
	// Start with speed limit +/- random variance
	float TargetSpeed = SpeedLimit; 
	
	// Brake to maintain distance to next vehicle
	const float MinimumDistanceToNextVehicle = GetMinimumDistanceToObstacle(RandomFraction, MinimumDistanceToNextVehicleRange);
	const float IdealDistanceToNextVehicle = GetIdealDistanceToObstacle(Speed, RandomFraction, IdealTimeToNextVehicleRange, MinimumDistanceToNextVehicle);
	if (DistanceToNext < IdealDistanceToNextVehicle)
	{
		const float ObstacleAvoidanceBrakingSpeedFactor = GetObstacleAvoidanceBrakingSpeedFactor(DistanceToNext, MinimumDistanceToNextVehicle, IdealDistanceToNextVehicle, NextVehicleAvoidanceBrakingPower);
		const float MaxAvoidanceSpeed = SpeedLimit * ObstacleAvoidanceBrakingSpeedFactor; 
		TargetSpeed = FMath::Min(TargetSpeed, MaxAvoidanceSpeed);
	}

	// Brake to avoid collision
	const float ObstacleAvoidanceBrakingTime = GeObstacleAvoidanceBrakingTime(RandomFraction, ObstacleAvoidanceBrakingTimeRange);
	if (TimeToCollidingObstacle < ObstacleAvoidanceBrakingTime)
	{
		const float MinimumDistanceToObstacle = GetMinimumDistanceToObstacle(RandomFraction, MinimumDistanceToObstacleRange);
		const float ObstacleAvoidanceBrakingDistance = ObstacleAvoidanceBrakingTime * SpeedLimit; 
		const float ObstacleAvoidanceBrakingSpeedFactor = GetObstacleAvoidanceBrakingSpeedFactor(DistanceToCollidingObstacle, MinimumDistanceToObstacle, ObstacleAvoidanceBrakingDistance, ObstacleAvoidanceBrakingPower);
		const float MaxAvoidanceSpeed = SpeedLimit * ObstacleAvoidanceBrakingSpeedFactor; 
		TargetSpeed = FMath::Min(TargetSpeed, MaxAvoidanceSpeed);
	}
				
	// Stop at lane exit?
	if (bStopAtLaneExit)
	{
		const float DistanceAlongLaneToStopAt = GetDistanceAlongLaneToStopAt(Radius, LaneLength, RandomFraction, StoppingDistanceFromLaneEndRange);
		const float DistanceAlongLaneToBrakeFrom = GetDistanceAlongLaneToBrakeFrom(SpeedLimit, Radius, LaneLength, StopSignBrakingTime, DistanceAlongLaneToStopAt);
		if (DistanceAlongLane >= DistanceAlongLaneToBrakeFrom)
		{
			const float StoppingSpeedFactor = GetStopSignBrakingSpeedFactor(DistanceAlongLaneToStopAt, DistanceAlongLaneToBrakeFrom, DistanceAlongLane, StopSignBrakingPower);
			const float MaxStoppingSpeed = SpeedLimit * StoppingSpeedFactor;
			TargetSpeed = FMath::Min(TargetSpeed, MaxStoppingSpeed);
		}
	}

	// Target speed may be negative if we've overshot a stop mark and the controller wants to reverse.
	// Disallow this for right now as we don't have proper reversing logic.
	TargetSpeed = FMath::Max(TargetSpeed, 0.0f);

	return TargetSpeed;
}

bool ShouldStopAtLaneExit(
	float DistanceAlongLane,
	float Speed,
	float Radius,
	float RandomFraction,
	float LaneLength,
	FZoneGraphTrafficLaneData* NextTrafficLaneData,
	const FVector2D& MinimumDistanceToNextVehicleRange,
	const FMassEntityManager& EntityManager,
	bool& bOut_RequestDifferentNextLane,
	bool& bInOut_CantStopAtLaneExit,
	bool& bOut_IsFrontOfVehicleBeyondLaneExit,
	bool& bOut_VehicleHasNoNextLane,
	bool& bOut_VehicleHasNoRoom,
	const float StandardTrafficPrepareToStopSeconds
#if WITH_MASSTRAFFIC_DEBUG
	, bool bVisLog
	, const UObject* VisLogOwner
	, const FTransform* VisLogTransform
#endif
	, const UWorld *World // ..for debugging
	, const FVector* VehicleLocation // ..for debugging
)
{
	bOut_RequestDifferentNextLane = false;
	bOut_IsFrontOfVehicleBeyondLaneExit = false;
	bOut_VehicleHasNoNextLane = false;
	bOut_VehicleHasNoRoom = false;
	
			
	const float DistanceAlongLane_FrontOfVehicle = DistanceAlongLane + Radius;
	const float DistanceLeftToGo = LaneLength - DistanceAlongLane_FrontOfVehicle;
	bOut_IsFrontOfVehicleBeyondLaneExit = (DistanceLeftToGo < 0.0f);

	constexpr float DebugDotSize = 10.0f;

	// A next lane has not yet been chosen yet, stop at end of lane to prevent vehicle from driving on no lane off into
	// oblivion.
	if (!NextTrafficLaneData || NextTrafficLaneData->NextLanes.IsEmpty())
	{
		// Cannot drive onward no matter what. We have no next lane.
		IF_MASSTRAFFIC_ENABLE_DEBUG( DrawDebugShouldStop(DebugDotSize, FColor::Blue, "NONEXT", bVisLog, VisLogOwner, VisLogTransform) );
		bOut_VehicleHasNoNextLane = true;
		return true; // ..should never happen near end of lane
	}

	// Coming up to an intersection?
	// If we don't have space on the other side, we might have to stop, or possibly request a different lane. 
	if (NextTrafficLaneData->ConstData.bIsIntersectionLane)
	{
		// All the vehicles in the next lane will end up in the post-intersection lane (since they won't stop.)
		// Will there also be enough space on the post-intersection lane for this vehicle?
		const float SpaceAlreadyTakenOnIntersectionLane = FMath::Max(NextTrafficLaneData->Length - NextTrafficLaneData->SpaceAvailable, 0.0f);
		const float SpaceTakenByVehicleOnLane = GetSpaceTakenByVehicleOnLane(Radius, RandomFraction, MinimumDistanceToNextVehicleRange);

		const FZoneGraphTrafficLaneData* PostIntersectionTrafficLaneData = NextTrafficLaneData->NextLanes[0];
		const float PostIntersectionSpaceAvailable = PostIntersectionTrafficLaneData->SpaceAvailableFromStartOfLaneForVehicle(EntityManager, true, false); // (See all INTERSTRAND1.)
		const float FutureSpaceAvailableOnPostIntersectionLane = PostIntersectionSpaceAvailable - SpaceAlreadyTakenOnIntersectionLane;
		
		if (FutureSpaceAvailableOnPostIntersectionLane < SpaceTakenByVehicleOnLane)
		{
			// Don't cross onto the next lane (which is in an intersection) as there isn't enough space on the other
			// side. Try to choose different lane.
			
			// Don't request a new lane if we're getting close to the end. If vehicle gets to the end and still hasn't
			// requested a new lane, it will have to stop, and we won't want it to stop suddenly, half-way in a crosswalk,
			// because it couldn't choose lane.
			bOut_RequestDifferentNextLane = (DistanceAlongLane < LaneLength - 3.0f/*arbitrary*/ * Radius);

			// Cannot drive onward. There is no space, an we can't get stranded in the intersection, or we can freeze the
			// intersection.
			bOut_VehicleHasNoRoom = true;
			IF_MASSTRAFFIC_ENABLE_DEBUG( DrawDebugShouldStop(DebugDotSize, FColor::Purple, "NOROOM", bVisLog, VisLogOwner, VisLogTransform) );
			return true;
		}
	}


	// Is the lane we chose closed, or about to close? (See all CANTSTOPLANEEXIT.)
	if (!bInOut_CantStopAtLaneExit && (!NextTrafficLaneData->bIsOpen || NextTrafficLaneData->bIsAboutToClose))
	{
		if (!NextTrafficLaneData->bIsOpen)
		{
			// If the lane is closed, then we can't stop if we're already beyond the end of the lane.
			bInOut_CantStopAtLaneExit |= bOut_IsFrontOfVehicleBeyondLaneExit;
		}
		else if (NextTrafficLaneData->bIsAboutToClose)
		{
			// If the lane is about to close, then we can't stop if we won't be able to stop in time, or we're already
			// beyond the end of the lane.
			const float SecondsUntilClose = NextTrafficLaneData->FractionUntilClosed * StandardTrafficPrepareToStopSeconds;
			const float SpeedUntilClose = SecondsUntilClose > 0.0 ? DistanceLeftToGo / SecondsUntilClose : TNumericLimits<float>::Max();
			const bool bIsVehicleTooFast = (Speed > SpeedUntilClose);
			bInOut_CantStopAtLaneExit |= (bIsVehicleTooFast || bOut_IsFrontOfVehicleBeyondLaneExit);
		}

		// Leave this here (so we only return true if can't stop AND we know we want to stop in the first place.)
		if (!bInOut_CantStopAtLaneExit) // ..if we now have to stop
		{
			IF_MASSTRAFFIC_ENABLE_DEBUG( DrawDebugShouldStop(DebugDotSize, FColor::Red, "STOP", bVisLog, VisLogOwner, VisLogTransform) );
			return true;
		}
		else
		{
			IF_MASSTRAFFIC_ENABLE_DEBUG( DrawDebugShouldStop(DebugDotSize, FColor::Yellow, "RUN", bVisLog, VisLogOwner, VisLogTransform) );
			return false;			
		}
	}
	else
	{
		IF_MASSTRAFFIC_ENABLE_DEBUG( DrawDebugShouldStop(DebugDotSize, FColor::Green, "GO", bVisLog, VisLogOwner, VisLogTransform) );
		return false;		
	}
}

float TimeToCollision(
	const FVector& AgentLocation, const FVector& AgentVelocity, float AgentRadius,
	const FVector& ObstacleLocation, const FVector& ObstacleVelocity, float ObstacleRadius)
{
	const float RadiusSum = AgentRadius + ObstacleRadius;
	const FVector VecToObstacle = ObstacleLocation - AgentLocation;
	const float C = FVector::DotProduct(VecToObstacle, VecToObstacle) - RadiusSum * RadiusSum;
		
	if (C < 0.f) //agents are colliding
	{
		return 0.f;
	}
	const FVector VelocityDelta = AgentVelocity - ObstacleVelocity;
	const float A = FVector::DotProduct(VelocityDelta, VelocityDelta);
	const float B = FVector::DotProduct(VecToObstacle, VelocityDelta);
	const float Discriminator = B * B - A * C;
	if (Discriminator <= 0)
	{
		return TNumericLimits<float>::Max();
	}
	const float Tau = (B - FMath::Sqrt(Discriminator)) / A;
	return (Tau < 0) ? TNumericLimits<float>::Max() : Tau;
}

void MoveVehicleToNextLane(
	FMassEntityManager& EntityManager,
	UMassTrafficSubsystem& MassTrafficSubsystem,
	const FMassEntityHandle VehicleEntity,
	const FAgentRadiusFragment& AgentRadiusFragment,
	const FMassTrafficRandomFractionFragment& RandomFractionFragment,
	FMassTrafficVehicleControlFragment& VehicleControlFragment,
	FMassTrafficVehicleLightsFragment& VehicleLightsFragment,
	FMassZoneGraphLaneLocationFragment& LaneLocationFragment,
	FMassTrafficNextVehicleFragment& NextVehicleFragment,
	FMassTrafficVehicleLaneChangeFragment* LaneChangeFragment, bool& bIsVehicleStuck)
{
	bIsVehicleStuck = false;
	
	check(VehicleControlFragment.NextLane);
	check(VehicleControlFragment.NextLane->LaneHandle != LaneLocationFragment.LaneHandle);

	const UMassTrafficSettings* MassTrafficSettings = GetDefault<UMassTrafficSettings>();

	FZoneGraphTrafficLaneData& CurrentLane = MassTrafficSubsystem.GetMutableTrafficLaneDataChecked(LaneLocationFragment.LaneHandle);

	// Get space taken up by this vehicle to add back to current lane space available and consume from next lane    
	const float SpaceTakenByVehicleOnLane = GetSpaceTakenByVehicleOnLane(AgentRadiusFragment.Radius, RandomFractionFragment.RandomFraction, MassTrafficSettings->MinimumDistanceToNextVehicleRange);
	
	// Reset the tail vehicle if it was us.
	if (CurrentLane.TailVehicle == VehicleEntity)
	{
		CurrentLane.TailVehicle.Reset();

		// We were the last vehicle so set the length explicitly.
		// Mainly doing this because I'm suspicious of floating point error over long runtimes.
		CurrentLane.ClearVehicleOccupancy();
	}
	else
	{
		// Add back this vehicle's space, to the space available on the lane, so the ChooseNextLaneProcessor can direct
		// traffic to less congested areas.
		
		CurrentLane.RemoveVehicleOccupancy(SpaceTakenByVehicleOnLane);
	}
	
	// Subtract the current lane length from distance, leaving how much overshot, as the distance on the next lane
	LaneLocationFragment.DistanceAlongLane -= LaneLocationFragment.LaneLength;

	// Capture new lane fragment pointer before we clear it
	FZoneGraphTrafficLaneData& NewCurrentLane = *VehicleControlFragment.NextLane;


	// We are moving onto a new lane -
	// This vehicle MIGHT have set this flag on the new lane. Assume it did, and clear it. If a different vehicle
	// has also set this flag on this same lane, it will set it again right away. (See all READYLANE.)
	NewCurrentLane.bIsVehicleReadyToUseLane = false;

	
	// If a vehicle that couldn't stop at it's lane exit has reserved itself on this lane, clear the reservation,
	// since that vehicle is now actually on the lane. See all CANTSTOPLANEEXIT.
	if (VehicleControlFragment.bCantStopAtLaneExit)
	{
		--NewCurrentLane.NumReservedVehiclesOnLane;
		VehicleControlFragment.bCantStopAtLaneExit = false;
	}

	
	// Set our current lane as our previous lane
	VehicleControlFragment.PreviousLaneIndex = LaneLocationFragment.LaneHandle.Index;
	VehicleControlFragment.PreviousLaneLength = LaneLocationFragment.LaneLength;

	// Set lane data for new lane
	LaneLocationFragment.LaneHandle = NewCurrentLane.LaneHandle;
	LaneLocationFragment.LaneLength = NewCurrentLane.Length;
	VehicleControlFragment.CurrentLaneConstData = NewCurrentLane.ConstData;

	// We are moving to this lane so we aren't waiting any more, take ourselves off.
	// This is incremented in ChooseNextLane and used in FMassTrafficPeriod::ShouldSkipPeriod().
	--NewCurrentLane.NumVehiclesApproachingLane;
	
	// If the new lane is short enough, we could have overshot it entirely already.
	if (LaneLocationFragment.DistanceAlongLane > LaneLocationFragment.LaneLength)
	{
		LaneLocationFragment.DistanceAlongLane = LaneLocationFragment.LaneLength;
	}

	// While we've already de-referenced VehicleControlFragment.NextTrafficLaneData here, we do a quick check
	// to see if it only has one next lane. In this case we can pre-emptively set that as our new next lane
	if (NewCurrentLane.NextLanes.Num() == 1)
	{
		VehicleControlFragment.NextLane = NewCurrentLane.NextLanes[0];
		++VehicleControlFragment.NextLane->NumVehiclesApproachingLane;

		// While we're here, update downstream traffic densities - for all the lanes we have accessed. 
		// IMPORTANT - Order is important here. Most downstream first.
		NewCurrentLane.UpdateDownstreamFlowDensity(MassTrafficSettings->DownstreamFlowDensityMixtureFraction);
		CurrentLane.UpdateDownstreamFlowDensity(MassTrafficSettings->DownstreamFlowDensityMixtureFraction);
		
		// Check trunk lane restrictions on next lane
		if (!TrunkVehicleLaneCheck(VehicleControlFragment.NextLane, VehicleControlFragment))
		{
			UE_LOG(LogMassTraffic, Error, TEXT("%s - Trunk-lane-only vehicle %d, on lane %d, can only access a single non-trunk next lane %d."),
				ANSI_TO_TCHAR(__FUNCTION__), VehicleEntity.Index, NewCurrentLane.LaneHandle.Index, VehicleControlFragment.NextLane->LaneHandle.Index);
		}
	}
	else
	{
		VehicleControlFragment.NextLane = nullptr;
	}

	// Update turn signals
	VehicleLightsFragment.bLeftTurnSignalLights = NewCurrentLane.bTurnsLeft;
	VehicleLightsFragment.bRightTurnSignalLights = NewCurrentLane.bTurnsRight;

	// Set next to be the new lane's current tail.
	if (NewCurrentLane.TailVehicle.IsSet())
	{
		NextVehicleFragment.SetNextVehicle(VehicleEntity, NewCurrentLane.TailVehicle);

		const FMassEntityView NextVehicleView(EntityManager, NextVehicleFragment.GetNextVehicle());
		const FMassZoneGraphLaneLocationFragment& NextVehicleLaneLocationFragment = NextVehicleView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		const FAgentRadiusFragment& NextVehicleAgentRadiusFragment = NextVehicleView.GetFragmentData<FAgentRadiusFragment>();

		// Clamp distance to ensure we don't overshoot past our new next
		float MaxDistanceAlongNextLane = NextVehicleLaneLocationFragment.DistanceAlongLane - NextVehicleAgentRadiusFragment.Radius - AgentRadiusFragment.Radius;
		bIsVehicleStuck = (MaxDistanceAlongNextLane < 0.0f); // (See all RECYCLESTUCK.)
		MaxDistanceAlongNextLane = FMath::Max(MaxDistanceAlongNextLane, 0.0f);
		LaneLocationFragment.DistanceAlongLane = FMath::Clamp(LaneLocationFragment.DistanceAlongLane, 0.0f, MaxDistanceAlongNextLane);
	}
	else
	{
		const FTransformFragment& TransformFragment = EntityManager.GetFragmentDataChecked<FTransformFragment>(VehicleEntity);
		const FMassEntityHandle NearestNextVehicle = FindNearestTailVehicleOnNextLanes(NewCurrentLane, TransformFragment.GetTransform().GetLocation(), EntityManager, EMassTrafficFindNextLaneVehicleType::Tail);
		if (NearestNextVehicle.IsSet())
		{
			NextVehicleFragment.SetNextVehicle(VehicleEntity, NearestNextVehicle);			
		}
		else
		{
			NextVehicleFragment.UnsetNextVehicle();			
		}
	}
	
	// Take space away from this lane since we're joining it.
	// NOTE - Don't do this if the vehicle has already done it preemptively. This happens if the vehicle on the
	// previous lane decided it can't stop. (See all CANTSTOPLANEEXIT.)
	NewCurrentLane.AddVehicleOccupancy(SpaceTakenByVehicleOnLane);


	// Vehicle is on a new lane. Clear the can't stop flag. (See all CANTSTOPLANEEXIT.)
	VehicleControlFragment.bCantStopAtLaneExit = false;

	
	// Make this the new tail vehicle of the next lane
	NewCurrentLane.TailVehicle = VehicleEntity;

	// Lane changing should be pre-clamped to complete at the lane's end. However, for Off LOD vehicles with large
	// delta times, they can leapfrog the lane change end distance in a single frame & onto the next lane, never seeing
	// that they surpassed the end distance. So, just in case a lane change is still in progress, reset the lane change
	// fragment to forcibly end the lane change progression.
	if (LaneChangeFragment)
	{
		LaneChangeFragment->EndLaneChangeProgression(VehicleLightsFragment, NextVehicleFragment, EntityManager);
		
		// We are on a new lane. Clear block lane changes until next lane. (This is deliberately not cleared by reset.)
		LaneChangeFragment->bBlockAllLaneChangesUntilNextLane = false;
	}
	
	
	// 'Lane changing' next vehicles..
	{
		// Entering lane -
		// Does the new lane have a 'lane changing' ghost tail vehicle?
		// If so, the current vehicle needs to add a next vehicle fragment so that it can avoid it.
		// But the lane changing vehicle needs to control the eventual removal of this fragment from the current vehicle.
		// So tell that lane changing vehicle's lane change fragment to add this fragment to (and register) the current
		// vehicle - the lane changing vehicle's lane change fragment will clear it from the current vehicle when it's done.
		if (NewCurrentLane.GhostTailVehicle_FromLaneChangingVehicle.IsSet())
		{
			FMassTrafficVehicleLaneChangeFragment* LaneChangeFragment_GhostTailEntity =
				EntityManager.GetFragmentDataPtr<FMassTrafficVehicleLaneChangeFragment>(NewCurrentLane.GhostTailVehicle_FromLaneChangingVehicle);
			
			if (LaneChangeFragment_GhostTailEntity && LaneChangeFragment_GhostTailEntity->IsLaneChangeInProgress())
			{
				LaneChangeFragment_GhostTailEntity->AddOtherLaneChangeNextVehicle_ForVehicleBehind(VehicleEntity, EntityManager);
			}

			// Since the current vehicle is now the tail vehicle on this lane, we can clear this ghost tail vehicle off the
			// new lane.
			NewCurrentLane.GhostTailVehicle_FromLaneChangingVehicle = FMassEntityHandle();
		}
	}

	
	// 'Splitting' or 'merging' lane ghost next vehicles..
	{		
		// Leaving lane -
		// If the current vehicle has old 'splitting/merging lanes' next vehicle fragments (from being on the old lane),
		// clear them.
		NextVehicleFragment.NextVehicle_SplittingLaneGhost = FMassEntityHandle();
		NextVehicleFragment.NextVehicle_MergingLaneGhost = FMassEntityHandle();
			
		// Entering lane -
		// If the new lane has a 'splitting/merging lanes' ghost tail vehicles, make this the current vehicle's 
		// 'splitting/merging lane' next vehicle fragment.
		// Always do this one, for intersection lanes or not.
		{
			if (NewCurrentLane.GhostTailVehicle_FromSplittingLaneVehicle.IsSet())
			{
				NextVehicleFragment.NextVehicle_SplittingLaneGhost = NewCurrentLane.GhostTailVehicle_FromSplittingLaneVehicle;

				// Since we are now the tail vehicle on this lane, we can clear this 'splitting lanes' ghost tail vehicle from the
				// new lane.
				NewCurrentLane.GhostTailVehicle_FromSplittingLaneVehicle = FMassEntityHandle();
			}
		}
		// IMPORTANT - Shouldn't have to worry about merging traffic in intersections. If we do, don't do this check!
		// And don't pull merging lane fragments into cache if we don't need to.
		// (See all INTERMERGE) Comment out check for 'is intersection lane' to allow merging lanes inside of intersections.
		if (!NewCurrentLane.ConstData.bIsIntersectionLane)
		{
			if (NewCurrentLane.GhostTailVehicle_FromMergingLaneVehicle.IsSet())
			{
				NextVehicleFragment.NextVehicle_MergingLaneGhost = NewCurrentLane.GhostTailVehicle_FromMergingLaneVehicle;

				// Since we are now the tail vehicle on this lane, we can clear this 'merging lanes' ghost tail vehicle from the
				// new lane.
				NewCurrentLane.GhostTailVehicle_FromMergingLaneVehicle = FMassEntityHandle();
			}
		}
		
		// Entering lane -
		// If we see we are on splitting/merging lanes, we need to set ourselves as a 'split/merge lanes' ghost vehicle
		// on all the other splitting/merging lanes on the new lane.
		// IMPORTANT - Do this AFTER the above section.
		// NOTE - Works on intersection lanes too, since they often split.
		// Always do this one, for intersection lanes or not -
		if (!NewCurrentLane.SplittingLanes.IsEmpty())
		{
			for (FZoneGraphTrafficLaneData* NewSplittingTrafficLaneData : NewCurrentLane.SplittingLanes) // ..if any
			{
				NewSplittingTrafficLaneData->GhostTailVehicle_FromSplittingLaneVehicle = VehicleEntity;
			}
		}
		// IMPORTANT - Shouldn't have to worry about merging traffic in intersections. If we do, don't do this check!
		// Not setting these entities saves time in calculating distances to next obstacle, in that processor.
		// And don't pull merging lane fragments into cache if we don't need to.
		// (See all INTERMERGE) Comment out check for 'is intersection lane' to allow merging lanes inside of intersections.
		if (!NewCurrentLane.MergingLanes.IsEmpty() && !NewCurrentLane.ConstData.bIsIntersectionLane)
		{
			for (FZoneGraphTrafficLaneData* NewMergingTrafficLaneData : NewCurrentLane.MergingLanes) // ..if any
			{
				NewMergingTrafficLaneData->GhostTailVehicle_FromMergingLaneVehicle = VehicleEntity;
			}
		}
		
		// Leaving lane -
		// On the old lanes, if we see were on splitting/merging lanes, we should remove ourselves as 'split/merge lanes'
		// ghost vehicle on all the other splitting/merging lanes we might have been set on.
		// IMPORTANT - Do this AFTER the above section.
		// NOTE - Lane changing is forbidden on splitting/merging lanes, so we will still be on the same
		// splitting/merging lane we started on.
		// Always do this one, for intersection lanes or not.
		if (!CurrentLane.SplittingLanes.IsEmpty())
		{
			for (FZoneGraphTrafficLaneData* CurrentSplittingTrafficLaneData : CurrentLane.SplittingLanes) // ..if any
			{
				if (CurrentSplittingTrafficLaneData->GhostTailVehicle_FromSplittingLaneVehicle == VehicleEntity)
				{
					CurrentSplittingTrafficLaneData->GhostTailVehicle_FromSplittingLaneVehicle = FMassEntityHandle();
				}
			}
		}
		// IMPORTANT - Shouldn't have to worry about merging traffic in intersections. If we do, don't do this check!
		// And don't pull merging lane fragments into cache if we don't need to. 
		// (See all INTERMERGE) Comment out check for 'is intersection lane' to allow merging lanes inside of intersections.
		if (!CurrentLane.MergingLanes.IsEmpty() && !CurrentLane.ConstData.bIsIntersectionLane)
		{
			for (FZoneGraphTrafficLaneData* CurrentMergingTrafficLaneData : CurrentLane.MergingLanes) // ..if any
			{
				if (CurrentMergingTrafficLaneData->GhostTailVehicle_FromMergingLaneVehicle == VehicleEntity)
				{
					CurrentMergingTrafficLaneData->GhostTailVehicle_FromMergingLaneVehicle = FMassEntityHandle();
				}
			}
		}
	}

	// Resolve end-of-lane vehicle's next pointing to start-of-lane vehicle.

	// See all BADMARCH.
	// Entering lane -
	// The current vehicle has just come on to a new lane. It's possible that a single vehicle right at the end of that
	// lane sees this current vehicle as it's next vehicle, and that will cause that vehicle to freeze, holding up traffic
	// forever. We need to find this vehicle (there will be only one, at the end of the lane), and clear it's next vehicle
	// if this is the case.
	// How does this happen? Vehicle (A) was ahead of vehicle (B) on a lane (L). Vehicle (A) made it through an intersection
	// at the end of the lane, and went on to other places. But vehicle (B) got stopped at that intersection. Vehicle (B)
	// still sees vehicle (A) as it's next vehicle. Vehicle (A) took a quick series of roads, and ended up arriving as
	// the tail vehicle on lane (L) - the same lane that vehicle (B) is still on. Vehicle (A) is now both the next vehicle
	// for vehicle (B) and behind it. We can simply clear vehicle (B)'s next vehicle, since it should be right at the end
	// of it's lane. It will get a new next vehicle once it begins to go through the intersection.
	{
		NewCurrentLane.ForEachVehicleOnLane(EntityManager, [VehicleEntity](const FMassEntityView& VehicleMassEntityView, struct FMassTrafficNextVehicleFragment& NextVehicleFragment, struct FMassZoneGraphLaneLocationFragment& LaneLocationFragment)
		{
			if (NextVehicleFragment.GetNextVehicle() == VehicleEntity)
			{
				NextVehicleFragment.UnsetNextVehicle();
				return false;
			}
			
			return true;
		});
	}
}

bool TeleportVehicleToAnotherLane(
	const FMassEntityHandle Entity_Current,
	FZoneGraphTrafficLaneData& TrafficLaneData_Current,
	FMassTrafficVehicleControlFragment& VehicleControlFragment_Current,
	const FAgentRadiusFragment& RadiusFragment_Current,
	const FMassTrafficRandomFractionFragment& RandomFractionFragment_Current,
	FMassZoneGraphLaneLocationFragment& LaneLocationFragment_Current,
	FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
	FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment_Current,
	//
	FZoneGraphTrafficLaneData& Lane_Chosen,
	const float DistanceAlongLane_Chosen,
	//
	FMassEntityHandle Entity_Current_Behind,
	FMassTrafficNextVehicleFragment* NextVehicleFragment_Current_Behind,
	//
	FMassEntityHandle Entity_Current_Ahead,
	//
	FMassEntityHandle Entity_Chosen_Behind,
	FMassTrafficNextVehicleFragment* NextVehicleFragment_Chosen_Behind,
	const FAgentRadiusFragment* RadiusFragment_Chosen_Behind,
	const FMassZoneGraphLaneLocationFragment* LaneLocationFragment_Chosen_Behind,
	FMassTrafficObstacleAvoidanceFragment* AvoidanceFragment_Chosen_Behind,
	//
	FMassEntityHandle Entity_Chosen_Ahead,
	const FAgentRadiusFragment* AgentRadiusFragment_Chosen_Ahead,
	const FMassZoneGraphLaneLocationFragment* ZoneGraphLaneLocationFragment_Chosen_Ahead,
	//
	const UMassTrafficSettings& MassTrafficSettings,
	const FMassEntityManager& EntityManager
)
{
	// If vehicle can't stop, it's committed itself and registered with the next lane. Do not teleport.
	
	if (VehicleControlFragment_Current.bCantStopAtLaneExit)
	{
		return false;	
	}

	// Run safety checks first. If any of them fail, abort. We do all the safety checks ahead of time, because the lane
	// surgery later on can't be aborted part way through the procedure without causing bigger problems.

	bool bAllGood = true;
	
	// Safety checks for - Remove current vehicle from it's current lane.	
	{
		if (Entity_Current_Behind.IsSet() && Entity_Current_Ahead.IsSet())
		{
			if (TrafficLaneData_Current.TailVehicle == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - Valid current behind vehicle - Valid current ahead vehicle - But current vehicle is also current lane tail vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
			
			if (Entity_Current_Behind == Entity_Current_Ahead)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - Valid current behind vehicle - Valid current ahead vehicle - But both the same vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
			if (Entity_Current_Behind == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - Valid current behind vehicle - Valid current ahead vehicle - But current vehicle is also current behind vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
			if (Entity_Current_Ahead == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - Valid current behind vehicle - Valid current ahead vehicle - But current vehicle is also current ahead vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
		}
		else if (Entity_Current_Behind.IsSet() && !Entity_Current_Ahead.IsSet())
		{
			if (TrafficLaneData_Current.TailVehicle == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - Valid current behind vehicle - No valid current ahead vehicle - But current vehicle is also current lane tail vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
			
			if (Entity_Current_Behind == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - Valid current behind vehicle - No valid current ahead vehicle - But current vehicle is also current behind vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
		}
		else if (!Entity_Current_Behind.IsSet() && Entity_Current_Ahead.IsSet())
		{
			if (TrafficLaneData_Current.TailVehicle != Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - No valid current behind vehicle - Valid current ahead vehicle - But current vehicle is not current lane tail vehicle - Is current lane tail vehicle valid? %d."), *TrafficLaneData_Current.LaneHandle.ToString(), TrafficLaneData_Current.TailVehicle.IsSet());
				bAllGood = false;
			}

			if (Entity_Current_Ahead == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - No valid current behind vehicle - Valid current ahead vehicle - But current vehicle is also current ahead vehicle."), *TrafficLaneData_Current.LaneHandle.ToString());
				bAllGood = false;
			}
		}
		else if (!Entity_Current_Behind.IsSet() && !Entity_Current_Ahead.IsSet())
		{
			if (TrafficLaneData_Current.TailVehicle != Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Current lane %s - No valid current behind vehicle - No valid current ahead vehicle - But current vehicle is not current lane tail vehicle - Is current lane tail vehicle valid? %d."), *TrafficLaneData_Current.LaneHandle.ToString(), TrafficLaneData_Current.TailVehicle.IsSet());
				bAllGood = false;
			}
		}
	}
	
	// Safety checks for - Insert current vehicle into the chosen lane.
	// @TODO If 1 lane ahead of us, next vehicle should be it's tail. (If >1 lanes ahead, do nothing for now.)
	{
		if (Entity_Chosen_Behind.IsSet() && Entity_Chosen_Ahead.IsSet())
		{			
			if (Entity_Chosen_Behind == Entity_Chosen_Ahead)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - Valid chosen behind vehicle - Valid chosen ahead vehicle - But both the same vehicle."), *Lane_Chosen.LaneHandle.ToString());
				bAllGood = false;
			}
			if (Entity_Chosen_Behind == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - Valid chosen behind vehicle - Valid chosen ahead vehicle - But current vehicle is also chosen behind vehicle."), *Lane_Chosen.LaneHandle.ToString());
				bAllGood = false;
			}
			if (Entity_Chosen_Ahead == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - Valid chosen behind vehicle - Valid chosen ahead vehicle - But current vehicle is also chosen ahead vehicle."), *Lane_Chosen.LaneHandle.ToString());
				bAllGood = false;
			}
		}
		else if (Entity_Chosen_Behind.IsSet() && !Entity_Chosen_Ahead.IsSet())
		{
			if (Entity_Chosen_Behind == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - Valid chosen behind vehicle - No valid chosen ahead vehicle - But current vehicle is also chosen behind vehicle."), *Lane_Chosen.LaneHandle.ToString());
				bAllGood = false;
			}
		}
		else if (!Entity_Chosen_Behind.IsSet() && Entity_Chosen_Ahead.IsSet())
		{
			if (Lane_Chosen.TailVehicle != Entity_Chosen_Ahead)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - No valid chosen behind vehicle - Valid chosen ahead vehicle - But chosen ahead vehicle is not also chosen lane tail vehicle - Chosen lane tail vehicle valid? %d."), *Lane_Chosen.LaneHandle.ToString(), Lane_Chosen.TailVehicle.IsSet());
				bAllGood = false;
			}

			if (Entity_Chosen_Ahead == Entity_Current)
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - No valid chosen behind vehicle - Valid chosen ahead vehicle - But current vehicle is also chosen ahead vehicle."), *Lane_Chosen.LaneHandle.ToString());
				bAllGood = false;
			}
		}
		else if (!Entity_Chosen_Behind.IsSet() && !Entity_Chosen_Ahead.IsSet())
		{
			if (Lane_Chosen.TailVehicle.IsSet())
			{
				UE_LOG(LogMassTraffic, Error, TEXT("Chosen lane %s - No valid chosen behind vehicle - No valid chosen ahead vehicle - But chosen lane has a tail vehicle."), *Lane_Chosen.LaneHandle.ToString());
				bAllGood = false;
			}
		}
	}

	if (!bAllGood)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("Failed in pre-safety-check, teleport from lane %s to lane %s abborted. See previous warning(s)."), *TrafficLaneData_Current.LaneHandle.ToString(), *Lane_Chosen.LaneHandle.ToString());
		return false;	
	}

	
	// Execute..

	// Remove current vehicle from it's current lane.
	{
		if (Entity_Current_Behind.IsSet() && Entity_Current_Ahead.IsSet())
		{
			NextVehicleFragment_Current_Behind->SetNextVehicle(Entity_Current_Behind, Entity_Current_Ahead);
		}
		else if (Entity_Current_Behind.IsSet() && !Entity_Current_Ahead.IsSet())
		{
			NextVehicleFragment_Current_Behind->UnsetNextVehicle();
		}
		else if (!Entity_Current_Behind.IsSet() && Entity_Current_Ahead.IsSet())
		{
			TrafficLaneData_Current.TailVehicle = Entity_Current_Ahead;
		}
		else if (!Entity_Current_Behind.IsSet() && !Entity_Current_Ahead.IsSet())
		{
			TrafficLaneData_Current.TailVehicle = FMassEntityHandle();
		}
	}

	// Before inserting Entity_Current into Lane_Chosen, first we need to break any NextVehicle references to
	// Entity_Current from vehicles already on the lane. Otherwise an infinite following loop can be formed. 
	//
	// It's extremely rare but possible that a single vehicle right at the end of the new lane lane sees this
	// current vehicle as it's next vehicle, and that will cause that vehicle to freeze, holding up traffic forever.
	// We need to find this vehicle (there will be only one, at the end of the lane), and clear it's next vehicle
	// if this is the case.
	// How does this happen? Vehicle (A) was ahead of vehicle (B) on a lane (L). Vehicle (A) made it through an intersection
	// at the end of the lane, and went on to other places. But vehicle (B) got stopped at that intersection. Vehicle (B)
	// still sees vehicle (A) as it's next vehicle. Vehicle (A) took a quick series of roads, and ended up arriving on a
	// lane (L) parallel to the same lane that vehicle (B) is still on. If vehicle (A) lane changes back onto the original
	// lane, behind vehicle (B), it is now both the next vehicle for vehicle (B) and behind it.
	// We can simply clear vehicle (B)'s next vehicle, since it should be right at the end
	// of it's lane. It will get a new next vehicle once it begins to go through the intersection.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("TeleportBreakLoop"))
		
		Lane_Chosen.ForEachVehicleOnLane(EntityManager, [Entity_Current](const FMassEntityView& VehicleMassEntityView, struct FMassTrafficNextVehicleFragment& NextVehicleFragment, struct FMassZoneGraphLaneLocationFragment& LaneLocationFragment)
		{
			if (NextVehicleFragment.GetNextVehicle() == Entity_Current)
			{
				NextVehicleFragment.UnsetNextVehicle();
				return false;
			}
			
			return true;
		});
	}

	// Insert current vehicle into the chosen lane.
	// @TODO If 1 lane ahead of us, next vehicle should be it's tail. (If >1 lanes ahead, do nothing for now.)
	{
		if (Entity_Chosen_Behind.IsSet() && Entity_Chosen_Ahead.IsSet())
		{
			NextVehicleFragment_Current.SetNextVehicle(Entity_Current, Entity_Chosen_Ahead);

			NextVehicleFragment_Chosen_Behind->SetNextVehicle(Entity_Chosen_Behind, Entity_Current);				
		}
		else if (Entity_Chosen_Behind.IsSet() && !Entity_Chosen_Ahead.IsSet())
		{
			NextVehicleFragment_Current.UnsetNextVehicle();
			
			NextVehicleFragment_Chosen_Behind->SetNextVehicle(Entity_Chosen_Behind, Entity_Current);				
		}
		else if (!Entity_Chosen_Behind.IsSet() && Entity_Chosen_Ahead.IsSet())
		{
			// Note: If TrafficLaneData_Chosen is empty, Entity_Chosen_Ahead might be on the lane ahead

			NextVehicleFragment_Current.SetNextVehicle(Entity_Current, Entity_Chosen_Ahead);

			Lane_Chosen.TailVehicle = Entity_Current;
		}
		else if (!Entity_Chosen_Behind.IsSet() && !Entity_Chosen_Ahead.IsSet())
		{
			NextVehicleFragment_Current.UnsetNextVehicle();

			Lane_Chosen.TailVehicle = Entity_Current;
		}
	}		

	// NOTE - VehicleControlFragment_Current.NextTrafficLaneData->AddVehicleApproachingLane() can't be set here, since
	// we don't yet know what the next lane will be. This will be done in choose-next-lane.

	// Adjust available space on lanes.
	{
		const float SpaceTakenByVehicle_Current = GetSpaceTakenByVehicleOnLane(RadiusFragment_Current.Radius, RandomFractionFragment_Current.RandomFraction, MassTrafficSettings.MinimumDistanceToNextVehicleRange);

		TrafficLaneData_Current.RemoveVehicleOccupancy(SpaceTakenByVehicle_Current);

		Lane_Chosen.AddVehicleOccupancy(SpaceTakenByVehicle_Current);
	}


	// Set additional current fragment parameters.
	VehicleControlFragment_Current.CurrentLaneConstData = Lane_Chosen.ConstData;
	VehicleControlFragment_Current.PreviousLaneIndex = INDEX_NONE; 
	
	LaneLocationFragment_Current.LaneHandle = Lane_Chosen.LaneHandle;
	LaneLocationFragment_Current.DistanceAlongLane = DistanceAlongLane_Chosen;
	LaneLocationFragment_Current.LaneLength = Lane_Chosen.Length;

	// CarsApproachingLane is incremented in ChooseNextLane and used in FMassTrafficPeriod::ShouldSkipPeriod().
	if (VehicleControlFragment_Current.NextLane)
	{
		// NOTE - There is no corresponding AddVehicleApproachingLane() call in this function. See comment above.
		--VehicleControlFragment_Current.NextLane->NumVehiclesApproachingLane;
	}


	// As in MoveVehicleToNextLane, we check here if there is only 1 lane head on the chosen lane and pre-set that
	// as our next lane
	if (Lane_Chosen.NextLanes.Num() == 1)
	{
		VehicleControlFragment_Current.NextLane = Lane_Chosen.NextLanes[0];

		++VehicleControlFragment_Current.NextLane->NumVehiclesApproachingLane;

		// While we're here, update downstream traffic density. 
		Lane_Chosen.UpdateDownstreamFlowDensity(MassTrafficSettings.DownstreamFlowDensityMixtureFraction);

		// If we didn't get a next vehicle ahead on the chosen lane, look to see if there's a Tail on the new next lane
		if (!NextVehicleFragment_Current.HasNextVehicle())
		{
			NextVehicleFragment_Current.SetNextVehicle(Entity_Current, VehicleControlFragment_Current.NextLane->TailVehicle);
		}
	}
	else
	{
		// Make current vehicle re-choose it's next lane (since it's on a different lane now.)
		VehicleControlFragment_Current.NextLane = nullptr;
	}

	
	// Update DistanceToNext on vehicles concerned.
	if (Entity_Chosen_Behind.IsSet())
	{
		const float DistanceToNewNext = FMath::Max((DistanceAlongLane_Chosen - LaneLocationFragment_Chosen_Behind->DistanceAlongLane) - RadiusFragment_Chosen_Behind->Radius - RadiusFragment_Current.Radius, 0.0f);

		AvoidanceFragment_Chosen_Behind->DistanceToNext = FMath::Min(AvoidanceFragment_Chosen_Behind->DistanceToNext, DistanceToNewNext);
	}

	if (Entity_Chosen_Ahead.IsSet())
	{
		const float DistanceToNewNext = FMath::Max((ZoneGraphLaneLocationFragment_Chosen_Ahead->DistanceAlongLane - DistanceAlongLane_Chosen) - AgentRadiusFragment_Chosen_Ahead->Radius - RadiusFragment_Current.Radius, 0.0f);

		AvoidanceFragment_Current.DistanceToNext = FMath::Min(AvoidanceFragment_Current.DistanceToNext, DistanceToNewNext);
	}

	return true;
}

}
