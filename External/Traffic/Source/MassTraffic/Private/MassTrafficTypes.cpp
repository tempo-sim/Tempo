// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficTypes.h"
#include "MassTrafficFragments.h"
#include "MassTrafficDebugHelpers.h"
#include "MassTrafficSubsystem.h"

#include "MassCommonFragments.h"
#include "MassEntityView.h"
#include "MassTrafficLaneChange.h"
#include "MassZoneGraphNavigationFragments.h"

// Theoretically, it's possible for more than one small vehicle to get close enough to the intersection to ready it.
// And, perhaps 2 small vehicles are yielding in the crosswalk, waiting to merge, with another vehicle behind them,
// ready to use the intersection lane.
// So, we'll allow a max of 3 vehicles to ready intersection lanes.
// However, in practice, NumVehiclesReadyToUseIntersectionLane never goes above one with the current configuration
// of everything.
int8 FZoneGraphTrafficLaneData::MaxAllowedVehiclesReadyToUseIntersectionLane = 3;	// (See all READYLANE.)

// Allow up to 10 vehicles to yield on lanes.
int8 FZoneGraphTrafficLaneData::MaxAllowedYieldingVehiclesOnLane = 10;

FZoneGraphTrafficLaneData::FZoneGraphTrafficLaneData():
	bIsOpen(true),
	bIsAboutToClose(false),
	bTurnsLeft(false),
	bTurnsRight(false),
	bIsRightMostLane(false),
	bHasTransverseLaneAdjacency(false),
	bIsDownstreamFromIntersection(false),
	bIsStoppedVehicleInPreviousLaneOverlappingThisLane(false),
	MaxDensity(1.0f)
{
}

bool FZoneGraphTrafficLaneData::HasYieldSignAlongRoad(float DistanceAlongLane) const
{
	if (const TOptional<TPair<float, FMassTrafficControllerType>> TrafficControllerDistanceAndType = ConstData.TryGetTrafficControllerType(DistanceAlongLane))
	{
		const FMassTrafficControllerType TrafficControllerType = TrafficControllerDistanceAndType->Value;
		return !TrafficControllerType.GetIsTrafficLightControlled()
				&& TrafficControllerType.GetTrafficControllerSignType() == EMassTrafficControllerSignType::YieldSign;
	}
	return false;
}

bool FZoneGraphTrafficLaneData::HasYieldSignThatRequiresStopAlongRoad(float DistanceAlongLane) const
{
	if (const TOptional<TPair<float, FMassTrafficControllerType>> TrafficControllerDistanceAndType = ConstData.TryGetTrafficControllerType(DistanceAlongLane))
	{
		const float TrafficControllerDistance = TrafficControllerDistanceAndType->Key;
		const FMassTrafficControllerType TrafficControllerType = TrafficControllerDistanceAndType->Value;
		const bool bHasPedestriansWaitingToCross = HasPedestriansWaitingToCross.Contains(TrafficControllerDistance) && HasPedestriansWaitingToCross[TrafficControllerDistance];
		const bool bHasPedestriansInDownstreamCrosswalkLane = HasPedestriansInDownstreamCrosswalkLanes.Contains(TrafficControllerDistance) && HasPedestriansInDownstreamCrosswalkLanes[TrafficControllerDistance];
		return !TrafficControllerType.GetIsTrafficLightControlled()
				&& (TrafficControllerType.GetTrafficControllerSignType() == EMassTrafficControllerSignType::YieldSign && (bHasPedestriansWaitingToCross || bHasPedestriansInDownstreamCrosswalkLane));
	}
	return false;
}

bool FZoneGraphTrafficLaneData::HasYieldSignAtEntrance() const
{
	if (const TOptional<FMassTrafficControllerType> TrafficControllerType = ConstData.TryGetTrafficControllerTypeAtEntrance())
	{
		return !TrafficControllerType->GetIsTrafficLightControlled() && TrafficControllerType->GetTrafficControllerSignType() == EMassTrafficControllerSignType::YieldSign;
	}
	return false;
}

bool FZoneGraphTrafficLaneData::HasStopSignAtEntrance() const
{
	if (const TOptional<FMassTrafficControllerType> TrafficControllerType = ConstData.TryGetTrafficControllerTypeAtEntrance())
	{
		return !TrafficControllerType->GetIsTrafficLightControlled() && TrafficControllerType->GetTrafficControllerSignType() == EMassTrafficControllerSignType::StopSign;
	}
	return false;
}

bool FZoneGraphTrafficLaneData::HasStopSignOrYieldSignAtEntrance() const
{
	if (const TOptional<FMassTrafficControllerType> TrafficControllerType = ConstData.TryGetTrafficControllerTypeAtEntrance())
	{
		return !TrafficControllerType->GetIsTrafficLightControlled() &&
			TrafficControllerType->GetTrafficControllerSignType() == EMassTrafficControllerSignType::YieldSign ||
			TrafficControllerType->GetTrafficControllerSignType() == EMassTrafficControllerSignType::StopSign;
	}
	return false;
}

bool FZoneGraphTrafficLaneData::HasTrafficLightAtEntrance() const
{
	if (const TOptional<FMassTrafficControllerType> TrafficControllerType = ConstData.TryGetTrafficControllerTypeAtEntrance())
	{
		return TrafficControllerType->GetIsTrafficLightControlled();
	}
	return false;
}

bool FZoneGraphTrafficLaneData::HasTrafficSignThatRequiresStopAtEntrance() const
{
	if (const TOptional<FMassTrafficControllerType> TrafficControllerType = ConstData.TryGetTrafficControllerTypeAtEntrance())
	{
		const bool bHasPedestriansWaitingToCrossAtIntersectionEntrance = HasPedestriansWaitingToCross.Contains(0.0) && HasPedestriansWaitingToCross[0.0];
		const bool bHasPedestriansInDownstreamCrosswalkLanesAtIntersectionEntrance = HasPedestriansInDownstreamCrosswalkLanes.Contains(0.0) && HasPedestriansInDownstreamCrosswalkLanes[0.0];
		return !TrafficControllerType->GetIsTrafficLightControlled()
				&& (TrafficControllerType->GetTrafficControllerSignType() == EMassTrafficControllerSignType::StopSign
				|| (TrafficControllerType->GetTrafficControllerSignType() == EMassTrafficControllerSignType::YieldSign && (bHasPedestriansWaitingToCrossAtIntersectionEntrance || bHasPedestriansInDownstreamCrosswalkLanesAtIntersectionEntrance)));
	}
	return false;
}

float FZoneGraphTrafficLaneData::LaneLengthAtNextTrafficControl(float DistanceAlongLane) const
{
	if (const TOptional<float> NextTrafficControllerDistance = ConstData.TryGetLaneLengthAtNextTrafficControl(DistanceAlongLane))
	{
		return NextTrafficControllerDistance.GetValue();
	}
	return Length;
}

void FZoneGraphTrafficLaneData::ClearVehicles()
{
	ClearVehicleOccupancy();
		
	TailVehicle = FMassEntityHandle();
	GhostTailVehicle_FromLaneChangingVehicle = FMassEntityHandle();
	GhostTailVehicle_FromSplittingLaneVehicle = FMassEntityHandle();
	GhostTailVehicle_FromMergingLaneVehicle = FMassEntityHandle();
	DownstreamFlowDensity = 0.0f;
	NumVehiclesApproachingLane = 0;
	NumVehiclesLaneChangingOntoLane = 0;
	NumVehiclesLaneChangingOffOfLane = 0;
	NumReservedVehiclesOnLane = 0;
}

void FZoneGraphTrafficLaneData::ForEachVehicleOnLane(const FMassEntityManager& EntityManager, FTrafficVehicleExecuteFunction Function) const
{
	int32 IterationCounter = 0;

	FMassEntityHandle VehicleEntity = TailVehicle;
	while (VehicleEntity.IsSet())
	{
		FMassEntityView VehicleEntityView(EntityManager, VehicleEntity);
		FMassZoneGraphLaneLocationFragment& LaneLocationFragment = VehicleEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		if (LaneLocationFragment.LaneHandle != LaneHandle)
		{
			break;
		}
		FMassTrafficNextVehicleFragment& NextVehicleFragment = VehicleEntityView.GetFragmentData<FMassTrafficNextVehicleFragment>();

		// Execute callback
		const bool bContinue = Function(VehicleEntityView, NextVehicleFragment, LaneLocationFragment);
		if (!bContinue)
		{
			break;
		}

		// Infinite loop check
		if (!ensureMsgf(VehicleEntity != NextVehicleFragment.GetNextVehicle(), TEXT("ForEachVehicleOnLane on %d detected an infinite loop where a vehicle's NextVehicle is itself (%d). The loop will now be terminated"), LaneHandle.Index, VehicleEntity.Index))
		{
			break;
		}

		// Advance to next vehicle
		VehicleEntity = NextVehicleFragment.GetNextVehicle();

		// Infinite loop check
		if (VehicleEntity == TailVehicle)
		{
			// Infinite following loop detected along a single lane. This can happen legally if traffic can
			// travel in a small loop but should be extremely rare in realistic traffic scenarios.
			break;
		}

		if (!ensureMsgf(++IterationCounter < 10000, TEXT("ForEachVehicleOnLane on %d reached iteration limit %d, which likely indicates an infinite loop bug. The loop will now be terminated."), LaneHandle.Index, IterationCounter))
		{
			const UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(EntityManager.GetWorld());
			UE::MassTraffic::VisLogMalformedNextLaneLinks(EntityManager, LaneHandle.Index, TailVehicle, FMassEntityHandle(), /*MarchEjectAt*/10000, MassTrafficSubsystem);
			
			break;
		}
	}
}


void FZoneGraphTrafficLaneData::ClearVehicleOccupancy()
{
	NumVehiclesOnLane = 0;
	SpaceAvailable = Length;
}

void FZoneGraphTrafficLaneData::RemoveVehicleOccupancy(const float SpaceToAdd)
{
	if (SpaceAvailable + SpaceToAdd > Length + /*Fudge*/1.0f)
	{
		UE_LOG(LogMassTraffic, Warning, TEXT("%s -- Lane %s -- SpaceAvailable %f = OldSpaceAvailable %f + SpaceToAdd %f > LaneLength %f. Num Veh on lane: %d. Num Approach: %d. Reserved: %d. Chng on: %d, off: %d"),
			ANSI_TO_TCHAR(__FUNCTION__), *LaneHandle.ToString(),
			SpaceAvailable + SpaceToAdd, SpaceAvailable, SpaceToAdd, Length, NumVehiclesOnLane, NumVehiclesApproachingLane, NumReservedVehiclesOnLane,
			NumVehiclesLaneChangingOntoLane, NumVehiclesLaneChangingOffOfLane);
	}

	--NumVehiclesOnLane;
	SpaceAvailable += SpaceToAdd;

	// In case we went over the length, clamp it so we aren't making up space on the lane that
	// doesn't exist.
	if (SpaceAvailable > Length)
	{
		SpaceAvailable = Length;
	}
}

void FZoneGraphTrafficLaneData::AddVehicleOccupancy(const float SpaceToRemove)
{
	++NumVehiclesOnLane;
	
	// if (SpaceAvailable - SpaceToRemove < 0.0f) ... 
	// This is OK. It might happen in lanes changes, when a vehicle changes lanes into a lane that doesn't have enough
	// room. Space available is allowed to be negative. It's just not allowed to go above the lane length.
	SpaceAvailable -= SpaceToRemove;
}

float FZoneGraphTrafficLaneData::SpaceAvailableFromStartOfLaneForVehicle(const FMassEntityManager& EntityManager, const bool bCheckLaneChangeGhostVehicles, const bool bCheckSplittingAndMergingGhostTailVehicles) const 
{
	float SpaceAvailableFromStartOfLane = SpaceAvailable;

	if (NumVehiclesLaneChangingOffOfLane > 0 || NumVehiclesLaneChangingOntoLane > 0)
	{
		// If vehicles are changing lanes off of this lane, they have already removed their space from the this lane,
		// and added it to another lane. But, they can still block traffic coming onto this lane from behind. Regular
		// SpaceAvailable can give us the impression there is more space on the lane then there actually is. So we
		// need to find how much space from the start of the lane there really is.
		// ..and..
		// If vehicles are changing lanes on to this lane, there's actually less space than we think there is.
		// (See all INTERSTRAND1.)

		auto Combine = [&](const FMassEntityHandle Entity) -> void
		{
			if (!Entity.IsSet())
			{
				return;	
			}
				
			const FMassEntityView EntityView(EntityManager, Entity);
			const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = EntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
			const FAgentRadiusFragment& RadiusFragment = EntityView.GetFragmentData<FAgentRadiusFragment>();
			
			const float DistanceAlongLane = LaneLocationFragment.DistanceAlongLane - RadiusFragment.Radius; 
			if (DistanceAlongLane < SpaceAvailableFromStartOfLane)
			{
				SpaceAvailableFromStartOfLane = DistanceAlongLane;
			}
		};

			
		Combine(TailVehicle);
		if (bCheckLaneChangeGhostVehicles)
		{
			Combine(GhostTailVehicle_FromLaneChangingVehicle);
		}
		if (bCheckSplittingAndMergingGhostTailVehicles)
		{
			Combine(GhostTailVehicle_FromSplittingLaneVehicle);
			Combine(GhostTailVehicle_FromMergingLaneVehicle);
		}
	}
		
	return SpaceAvailableFromStartOfLane;
}

bool FZoneGraphTrafficLaneData::TryGetDistanceFromStartOfLaneToTailVehicle(const FMassEntityManager& EntityManager, float& OutDistanceToTailVehicle) const
{
	if (!TailVehicle.IsSet())
	{
		return false;	
	}
	
	const FMassEntityView TailVehicleEntityView(EntityManager, TailVehicle);
	const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = TailVehicleEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
	const FAgentRadiusFragment& RadiusFragment = TailVehicleEntityView.GetFragmentData<FAgentRadiusFragment>();
	
	const float DistanceAlongLaneToTailVehicle = LaneLocationFragment.DistanceAlongLane - RadiusFragment.Radius;

	OutDistanceToTailVehicle = DistanceAlongLaneToTailVehicle;
	return true;
}


void FZoneGraphTrafficLaneData::UpdateDownstreamFlowDensity(float DownstreamFlowDensityMixtureFraction)
{
	float NextLanesDownstreamFlowDensity_Total = 0.0f;
	float NextLanesDownstreamFlowDensity_Count = 0.0f;
	for (const FZoneGraphTrafficLaneData* NextTrafficLaneData : NextLanes)
	{
		if (NextTrafficLaneData->ConstData.bIsIntersectionLane)
		{
			// NOTE - Intersection lanes are skipped in density calculations, and only ever have one next lane. So if the
			// incoming lane is an intersection lane, we get the density value from the lane after it.
			
			if (NextTrafficLaneData->NextLanes.IsEmpty())
			{
				continue;
			}

			NextLanesDownstreamFlowDensity_Total += NextTrafficLaneData->NextLanes[0]->GetDownstreamFlowDensity();
			NextLanesDownstreamFlowDensity_Count += 1.0f;
		}
		else
		{
			NextLanesDownstreamFlowDensity_Total += NextTrafficLaneData->GetDownstreamFlowDensity();
			NextLanesDownstreamFlowDensity_Count += 1.0f;
		}
	}

	if (NextLanesDownstreamFlowDensity_Count > 0.0f)
	{
		const float AverageNextLanesDownstreamFlowDensity = NextLanesDownstreamFlowDensity_Total / NextLanesDownstreamFlowDensity_Count;

		// Intersection lanes should be skipped in density calculations.
		if (ConstData.bIsIntersectionLane)
		{
			return;
		}
	
		const float FunctionalDensity_ThisLane = FunctionalDensity();
		const float Alpha = FMath::Clamp(DownstreamFlowDensityMixtureFraction, 0.0f, 1.0f);

		DownstreamFlowDensity = FMath::Lerp(
			FunctionalDensity_ThisLane,
			AverageNextLanesDownstreamFlowDensity, 
			Alpha);
	}
}
