// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficUtils.h"
#include "MassTraffic.h"
#include "MassTrafficTypes.h"
#include "MassTrafficSettings.h"
#include "MassTrafficFragments.h"
#include "MassTrafficSubsystem.h"

#include "ZoneGraphTypes.h"
#include "ZoneGraphQuery.h"
#include "MassZoneGraphNavigationFragments.h"

namespace UE::MassTraffic {

void FindNearestVehiclesInLane(const FMassEntityManager& EntityManager, const FZoneGraphTrafficLaneData& TrafficLaneData, float Distance, FMassEntityHandle& OutPreviousVehicle, FMassEntityHandle& OutNextVehicle)
{
	// No other cars in the lane? 
	if (!TrafficLaneData.TailVehicle.IsSet())
	{
		OutPreviousVehicle.Reset();
		OutNextVehicle.Reset();
	}
	else
	{
		// Is Distance before the tail vehicle?
		const FMassZoneGraphLaneLocationFragment& TailVehicleLaneLocationFragment = EntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(TrafficLaneData.TailVehicle);
		if (Distance <= TailVehicleLaneLocationFragment.DistanceAlongLane)
		{
			OutPreviousVehicle.Reset();
			OutNextVehicle = TrafficLaneData.TailVehicle;
		}
		// We are ahead of the current tail
		else
		{
			// Look along the lane to find the first car ahead of Distance (and implicitly the one behind)
			OutPreviousVehicle = TrafficLaneData.TailVehicle;
			OutNextVehicle = EntityManager.GetFragmentDataChecked<FMassTrafficNextVehicleFragment>(OutPreviousVehicle).GetNextVehicle();
			int32 LoopCount = 0;
			while (OutPreviousVehicle.IsSet())
			{
				if (OutNextVehicle.IsSet())
				{
					const FMassZoneGraphLaneLocationFragment& NextVehicleLaneLocationFragment = EntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(OutNextVehicle);
					
					// Have we gone too far into the next lane?
					if (NextVehicleLaneLocationFragment.LaneHandle != TrafficLaneData.LaneHandle)
					{
						// OutPreviousVehicle is behind Distance and nothing else ahead of Distance on this lane
						OutNextVehicle.Reset();
						break;
					}

					// Next vehicle is ahead?
					if (Distance <= NextVehicleLaneLocationFragment.DistanceAlongLane)
					{
						break;
					}
					else
					{
						OutPreviousVehicle = OutNextVehicle;
						OutNextVehicle = EntityManager.GetFragmentDataChecked<FMassTrafficNextVehicleFragment>(OutPreviousVehicle).GetNextVehicle();

						// Infinite loop check
						// 
						// If the next vehicle is the tail, we've looped back around. Infinite NextVehicle loops
						// are valid, but in this case as we are in a block that can assume the tail is behind Distance, 
						// if we've not yet found a vehicle ahead of Distance, then OutPreviousVehicle must be the last
						// in the lane and must be behind Distance with nothing else in front
						if (OutNextVehicle == TrafficLaneData.TailVehicle)
						{
							// OutPreviousVehicle is behind Distance and there is nothing after
							OutNextVehicle.Reset();
							return;
						}

						// Infinite loop check
						// 
						// Vehicles should never be able to follow themselves, but if they do somehow, it will cause
						// an infinite loop so we check here to avoid that
						if (!ensure(OutPreviousVehicle != OutNextVehicle))
						{
							UE_LOG(LogMassTraffic, Error, TEXT("Infinite loop detected in FindNearestVehiclesInLane. Vehicle %d is following itself!"), OutPreviousVehicle.Index);
							OutNextVehicle.Reset();
							return;
						}
					}
				}
				else
				{
					break;
				}

				// Infinite loop check
				++LoopCount;
				if (!ensure(LoopCount < 1000))
				{
					UE_LOG(LogMassTraffic, Error, TEXT("Infinite loop detected in FindNearestVehiclesInLane. LoopCount > 1000"));
					return;
				}
			}
		}
	}
}


bool PointIsNearSegment(
	const FVector& Point, 
	const FVector& SegmentStartPoint, const FVector& SegmentEndPoint,
	const float MaxDistance)
{
	const FVector ClosestPointOnLane = FMath::ClosestPointOnSegment(Point, SegmentStartPoint, SegmentEndPoint);

	return FVector::Distance(Point, ClosestPointOnLane) <= MaxDistance;
}


// Lane points.

FVector GetLaneBeginPoint(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage, const uint32 CountFromBegin, bool* bIsValid)
{
	const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[LaneIndex];
	const int32 LanePointsIndex = int32(LaneData.PointsBegin + CountFromBegin);
	if (LanePointsIndex >= LaneData.PointsEnd)
	{
		if (bIsValid) *bIsValid = false;
		return FVector(0.0f, 0.0f, 0.0f);
	}
	if (bIsValid) *bIsValid = true;
	return ZoneGraphStorage.LanePoints[LanePointsIndex];
}

FVector GetLaneEndPoint(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage, const uint32 CountFromEnd, bool* bIsValid) 
{
	const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[LaneIndex];
	const int32 LanePointsIndex = int32(LaneData.PointsEnd + CountFromEnd) - 1;
	if (LanePointsIndex < 0)
	{
		if (bIsValid) *bIsValid = false;
		return FVector(0.0f, 0.0f, 0.0f);
	}
	if (bIsValid) *bIsValid = true;
	return ZoneGraphStorage.LanePoints[LanePointsIndex];
}

FVector GetLaneMidPoint(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage)
{
	const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[LaneIndex];
	return 0.5f * (ZoneGraphStorage.LanePoints[LaneData.PointsBegin] + ZoneGraphStorage.LanePoints[LaneData.PointsEnd - 1]);
}


// Lane distances.
float GetLaneBeginToEndDistance(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage)
{
	const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[LaneIndex];
	const FVector LaneBeginPoint = ZoneGraphStorage.LanePoints[LaneData.PointsBegin];
	const FVector LaneEndPoint = ZoneGraphStorage.LanePoints[LaneData.PointsEnd - 1];
	return FVector::Distance(LaneBeginPoint, LaneEndPoint);
}


// Lane directions.
FVector GetLaneBeginDirection(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage) 
{
	const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[LaneIndex];
	const FVector LaneBeginPoint = ZoneGraphStorage.LanePoints[LaneData.PointsBegin];
	const FVector LaneSecondPoint = ZoneGraphStorage.LanePoints[LaneData.PointsBegin + 1];
	const FVector LaneBeginDirection = (LaneSecondPoint - LaneBeginPoint).GetSafeNormal();
	return LaneBeginDirection;
}

FVector GetLaneEndDirection(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage) 
{
	const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[LaneIndex];
	const FVector LaneSecondToEndPoint = ZoneGraphStorage.LanePoints[LaneData.PointsEnd - 2];
	const FVector LaneEndPoint = ZoneGraphStorage.LanePoints[LaneData.PointsEnd - 1];
	const FVector LaneEndDirection = (LaneEndPoint - LaneSecondToEndPoint).GetSafeNormal();
	return LaneEndDirection;
}

FVector GetLaneBeginToEndDirection(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage) 
{
	const FZoneLaneData& LaneData = ZoneGraphStorage.Lanes[LaneIndex];
	const FVector LaneBeginPoint = ZoneGraphStorage.LanePoints[LaneData.PointsBegin];
	const FVector LaneEndPoint = ZoneGraphStorage.LanePoints[LaneData.PointsEnd - 1];
	const FVector LaneBeginToEndDirection = (LaneEndPoint - LaneBeginPoint).GetSafeNormal();
	return LaneBeginToEndDirection;
}

// Lane straightness.
float GetLaneStraightness(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage)
{
	const FVector LaneBeginDirection = GetLaneBeginDirection(LaneIndex, ZoneGraphStorage);  
	const FVector LaneOverallDirection = GetLaneBeginToEndDirection(LaneIndex, ZoneGraphStorage);
	return FVector::DotProduct(LaneBeginDirection, LaneOverallDirection);
}


// Lane turn type.

LaneTurnType GetLaneTurnType(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage)
{
	const FVector BeginDirection = GetLaneBeginDirection(LaneIndex, ZoneGraphStorage);
	const FVector EndDirection = GetLaneEndDirection(LaneIndex, ZoneGraphStorage);

	constexpr float LaneTurnThresholdAngleDeg = 30.0f;
	const float LaneTurnThresholdCosine = FMath::Cos(FMath::DegreesToRadians(LaneTurnThresholdAngleDeg));

	const float Dot = FVector::DotProduct(BeginDirection, EndDirection);
	const bool bTurnAngleIsSignificant = (Dot <= LaneTurnThresholdCosine);
	if (!bTurnAngleIsSignificant)
	{
		return LaneTurnType::Straight;
	}

	const FVector Cross = FVector::CrossProduct(BeginDirection, EndDirection);
	if (Cross.Z < 0.0f)
	{
		return LaneTurnType::LeftTurn;
	}
	else
	{
		return LaneTurnType::RightTurn;
	}
}

int32 GetLanePriority(
	const FZoneGraphTrafficLaneData* TrafficLaneData,
	const FMassTrafficLanePriorityFilters& LanePriorityFilters,
	const FZoneGraphStorage& ZoneGraphStorage)
{
	if (TrafficLaneData == nullptr)
	{
		return INDEX_NONE;
	}

	FZoneGraphTagMask LaneTagMask;
	if (!UE::ZoneGraph::Query::GetLaneTags(ZoneGraphStorage, TrafficLaneData->LaneHandle, LaneTagMask))
	{
		return INDEX_NONE;
	}

	for (int32 PriorityIndex = 0; PriorityIndex < LanePriorityFilters.LaneTagFilters.Num(); ++PriorityIndex)
	{
		const FZoneGraphTagFilter& LaneChangePriorityFilter = LanePriorityFilters.LaneTagFilters[PriorityIndex];

		if (LaneChangePriorityFilter.Pass(LaneTagMask))
		{
			return LanePriorityFilters.LaneTagFilters.Num() - 1 - PriorityIndex;
		}
	}

	return INDEX_NONE;
}

bool TryGetVehicleEnterAndExitTimesForIntersection(
	const FZoneGraphTrafficLaneData& VehicleCurrentLaneData,
	const FZoneGraphTrafficLaneData& IntersectionLaneData,
	const float VehicleDistanceAlongCurrentLane,
	const float VehicleEffectiveSpeed,
	const float VehicleRadius,
	float& OutVehicleEnterTime,
	float& OutVehicleExitTime,
	float* OutVehicleDistanceToEnterIntersectionLane,
	float* OutVehicleDistanceToExitIntersectionLane)
{
	if (!ensureMsgf(VehicleCurrentLaneData.Length > 0.0f, TEXT("CurrentLane should have a length greater than zero.")))
	{
		return false;
	}

	if (!ensureMsgf(IntersectionLaneData.Length > 0.0f, TEXT("IntersectionLaneData should have a length greater than zero.")))
	{
		return false;
	}
	
	const float VehicleDistanceToIntersectionLane = VehicleCurrentLaneData.LaneHandle != IntersectionLaneData.LaneHandle ? VehicleCurrentLaneData.Length - VehicleDistanceAlongCurrentLane : -VehicleDistanceAlongCurrentLane;
	
	const float VehicleDistanceToEnterIntersectionLane = VehicleDistanceToIntersectionLane - VehicleRadius;
	const float VehicleDistanceToExitIntersectionLane = VehicleDistanceToEnterIntersectionLane + IntersectionLaneData.Length + VehicleRadius;

	// Note:  Vehicles currently don't have support for going in reverse.  So, we don't consider it, here.
	const float TimeForVehicleToEnterIntersectionLane = VehicleEffectiveSpeed > 0.0f ? VehicleDistanceToEnterIntersectionLane / VehicleEffectiveSpeed : TNumericLimits<float>::Max();
	const float TimeForVehicleToExitIntersectionLane = VehicleEffectiveSpeed > 0.0f ? VehicleDistanceToExitIntersectionLane / VehicleEffectiveSpeed : TNumericLimits<float>::Max();

	OutVehicleEnterTime = TimeForVehicleToEnterIntersectionLane;
	OutVehicleExitTime = TimeForVehicleToExitIntersectionLane;

	if (OutVehicleDistanceToEnterIntersectionLane != nullptr)
	{
		*OutVehicleDistanceToEnterIntersectionLane = VehicleDistanceToEnterIntersectionLane;
	}

	if (OutVehicleDistanceToExitIntersectionLane != nullptr)
	{
		*OutVehicleDistanceToExitIntersectionLane = VehicleDistanceToExitIntersectionLane;
	}
	
	return true;
}

bool TryGetEnterAndExitDistancesAlongQueryLane(
	const UMassTrafficSubsystem& MassTrafficSubsystem,
	const UMassTrafficSettings& MassTrafficSettings,
	const FZoneGraphStorage& ZoneGraphStorage,
	const FZoneGraphLaneHandle& QueryLane,
	const FZoneGraphLaneHandle& OtherLane,
	float& OutEnterDistanceAlongQueryLane,
	float& OutExitDistanceAlongQueryLane)
{
	// First, see if we can get the enter/exit distances from the cache.
	// Otherwise, we'll compute it.
	if (FMassTrafficLaneIntersectionInfo LaneIntersectionInfo; MassTrafficSubsystem.TryGetLaneIntersectionInfo(QueryLane, OtherLane, LaneIntersectionInfo))
	{
		OutEnterDistanceAlongQueryLane = LaneIntersectionInfo.EnterDistance;
		OutExitDistanceAlongQueryLane = LaneIntersectionInfo.ExitDistance;
		
		return true;
	}
	
	float QueryLaneLength = 0.0f;
	const bool bFoundQueryLaneLength = UE::ZoneGraph::Query::GetLaneLength(ZoneGraphStorage, QueryLane, QueryLaneLength);

	if (!ensureMsgf(bFoundQueryLaneLength, TEXT("Must find QueryLaneLength in TryGetEnterAndExitDistancesAlongQueryLane.")))
	{
		return false;
	}

	float OtherLaneWidth = 0.0f;
	const bool bFoundOtherLaneWidth = UE::ZoneGraph::Query::GetLaneWidth(ZoneGraphStorage, OtherLane, OtherLaneWidth);

	if (!ensureMsgf(bFoundOtherLaneWidth, TEXT("Must find OtherLaneWidth in TryGetEnterAndExitDistancesAlongQueryLane.")))
	{
		return false;
	}

	const float OtherLaneHalfWidth = OtherLaneWidth * 0.5f;

	float DistanceAlongIntersectionLaneToOtherLaneLeftSide = 0.0f;
	int32 IntersectionLaneIntersectionSegmentIndexLeftSide = 0;
	float NormalizedDistanceAlongIntersectionLaneIntersectionSegmentLeftSide = 0.0f;
	
	const bool bFoundIntersectionQueryToOtherLaneLeftSide = UE::ZoneGraph::Query::FindFirstIntersectionBetweenLanes(
		ZoneGraphStorage,
		QueryLane,
		OtherLane,
		-OtherLaneHalfWidth,
		DistanceAlongIntersectionLaneToOtherLaneLeftSide,
		&IntersectionLaneIntersectionSegmentIndexLeftSide,
		&NormalizedDistanceAlongIntersectionLaneIntersectionSegmentLeftSide,
		MassTrafficSettings.AcceptableLaneIntersectionDistance);

	float DistanceAlongIntersectionLaneToOtherLaneRightSide = 0.0f;
	int32 IntersectionLaneIntersectionSegmentIndexRightSide = 0;
	float NormalizedDistanceAlongIntersectionLaneIntersectionSegmentRightSide = 0.0f;
	
	const bool bFoundIntersectionQueryToOtherLaneRightSide = UE::ZoneGraph::Query::FindFirstIntersectionBetweenLanes(
		ZoneGraphStorage,
		QueryLane,
		OtherLane,
		OtherLaneHalfWidth,
		DistanceAlongIntersectionLaneToOtherLaneRightSide,
		&IntersectionLaneIntersectionSegmentIndexRightSide,
		&NormalizedDistanceAlongIntersectionLaneIntersectionSegmentRightSide,
		MassTrafficSettings.AcceptableLaneIntersectionDistance);

	if (!ensureMsgf(bFoundIntersectionQueryToOtherLaneLeftSide || bFoundIntersectionQueryToOtherLaneRightSide, TEXT("Expected to find left side and/or right side intersections between QueryLane and OtherLane in TryGetEnterAndExitDistancesAlongQueryLane.  Either we're testing against the wrong OtherLane, or AcceptableLaneIntersectionDistance is not high enough to detect the intersection.  MassTrafficSettings.AcceptableLaneIntersectionDistance: %f."), MassTrafficSettings.AcceptableLaneIntersectionDistance))
	{
		return false;
	}
	
	// The enter distance will be the *min* of the intersection distances (if there are 2),
	// otherwise it'll be the distance to the left side or right side intersection (whichever one exists).
	const float DistanceAlongQueryLaneToOtherLaneEntrance = bFoundIntersectionQueryToOtherLaneLeftSide && bFoundIntersectionQueryToOtherLaneRightSide
		? FMath::Min(DistanceAlongIntersectionLaneToOtherLaneLeftSide, DistanceAlongIntersectionLaneToOtherLaneRightSide)
		: bFoundIntersectionQueryToOtherLaneLeftSide ? DistanceAlongIntersectionLaneToOtherLaneLeftSide
		: DistanceAlongIntersectionLaneToOtherLaneRightSide;

	// The exit distance will be the *max* of the intersection distances (if there are 2),
	// otherwise it'll be the length of the QueryLane.
	const float DistanceAlongQueryLaneToOtherLaneExit = bFoundIntersectionQueryToOtherLaneLeftSide && bFoundIntersectionQueryToOtherLaneRightSide
		? FMath::Max(DistanceAlongIntersectionLaneToOtherLaneLeftSide, DistanceAlongIntersectionLaneToOtherLaneRightSide)
		: QueryLaneLength;

	OutEnterDistanceAlongQueryLane = DistanceAlongQueryLaneToOtherLaneEntrance;
	OutExitDistanceAlongQueryLane = DistanceAlongQueryLaneToOtherLaneExit;

	return true;
}

bool TryGetVehicleEnterAndExitTimesForCrossingLane(
	const UMassTrafficSubsystem& MassTrafficSubsystem,
	const UMassTrafficSettings& MassTrafficSettings,
	const FZoneGraphStorage& ZoneGraphStorage,
	const FZoneGraphTrafficLaneData& VehicleCurrentLaneData,
	const FZoneGraphTrafficLaneData& VehicleQueryLaneData,
	const FZoneGraphLaneHandle& CrossingLane,
	const float VehicleDistanceAlongCurrentLane,
	const float VehicleEffectiveSpeed,
	const float VehicleRadius,
	float& OutVehicleEnterTime,
	float& OutVehicleExitTime,
	float* OutVehicleEnterDistance,
	float* OutVehicleExitDistance)
{
	if (!ensureMsgf(VehicleCurrentLaneData.NextLanes.Contains(&VehicleQueryLaneData) || VehicleCurrentLaneData.LaneHandle == VehicleQueryLaneData.LaneHandle, TEXT("VehicleQueryLaneData must be one of VehicleCurrentLaneData's NextLanes (or they must be the same lane) in TryGetVehicleEnterAndExitTimesForCrossingLane.")))
	{
		return false;
	}
	
	const float VehicleDistanceAlongQueryLane = VehicleCurrentLaneData.LaneHandle != VehicleQueryLaneData.LaneHandle ? -(VehicleCurrentLaneData.Length - VehicleDistanceAlongCurrentLane) : VehicleDistanceAlongCurrentLane;

	return TryGetEntityEnterAndExitTimesForCrossingLane(
		MassTrafficSubsystem,
		MassTrafficSettings,
		ZoneGraphStorage,
		VehicleQueryLaneData.LaneHandle,
		CrossingLane,
		VehicleDistanceAlongQueryLane,
		VehicleEffectiveSpeed,
		VehicleRadius,
		OutVehicleEnterTime,
		OutVehicleExitTime,
		OutVehicleEnterDistance,
		OutVehicleExitDistance
		);
}

bool TryGetEntityEnterAndExitTimesForCrossingLane(
	const UMassTrafficSubsystem& MassTrafficSubsystem,
	const UMassTrafficSettings& MassTrafficSettings,
	const FZoneGraphStorage& ZoneGraphStorage,
	const FZoneGraphLaneHandle& EntityQueryLane,
	const FZoneGraphLaneHandle& CrossingLane,
	const float EntityDistanceAlongQueryLane,
	const float EntitySpeed,
	const float EntityRadius,
	float& OutEntityEnterTime,
	float& OutEntityExitTime,
	float* OutEntityEnterDistance,
	float* OutEntityExitDistance)
{
	float DistanceAlongEntityLaneToCrossingLaneEntrance;
	float DistanceAlongEntityLaneToCrossingLaneExit;
	
	if (!TryGetEnterAndExitDistancesAlongQueryLane(
		MassTrafficSubsystem,
		MassTrafficSettings,
		ZoneGraphStorage,
		EntityQueryLane,
		CrossingLane,
		DistanceAlongEntityLaneToCrossingLaneEntrance,
		DistanceAlongEntityLaneToCrossingLaneExit))
	{
		return false;
	}
	
	const float EntityDistanceToEnterCrossingLane = DistanceAlongEntityLaneToCrossingLaneEntrance - EntityDistanceAlongQueryLane - EntityRadius;
	const float EntityDistanceToExitCrossingLane = DistanceAlongEntityLaneToCrossingLaneExit - EntityDistanceAlongQueryLane + EntityRadius;

	// Note:  We don't consider going in reverse, here.  We'll add it when a use-case comes up.
	const float TimeForEntityToEnterCrossingLane = EntitySpeed > 0.0f ? EntityDistanceToEnterCrossingLane / EntitySpeed : TNumericLimits<float>::Max();
	const float TimeForEntityToExitCrossingLane = EntitySpeed > 0.0f ? EntityDistanceToExitCrossingLane / EntitySpeed : TNumericLimits<float>::Max();

	OutEntityEnterTime = TimeForEntityToEnterCrossingLane;
	OutEntityExitTime = TimeForEntityToExitCrossingLane;

	if (OutEntityEnterDistance != nullptr)
	{
		*OutEntityEnterDistance = EntityDistanceToEnterCrossingLane;
	}

	if (OutEntityExitDistance != nullptr)
	{
		*OutEntityExitDistance = EntityDistanceToExitCrossingLane;
	}
	
	return true;
}

void GetEnterAndExitTimeForYieldingEntity(
	const float EnterDistance,
    const float ExitDistance,
    const float Acceleration,
    float& OutEntityEnterTime,
    float& OutEntityExitTime)
{
	if (Acceleration > 0.0f)
	{
		const float EntityEnterTimeSquared = 2.0f * EnterDistance / Acceleration;
		const float EntityExitTimeSquared = 2.0f * ExitDistance / Acceleration;

		const float EntityEnterTime = FMath::Sqrt(FMath::Abs(EntityEnterTimeSquared)) * FMath::Sign(EntityEnterTimeSquared);
		const float EntityExitTime = FMath::Sqrt(FMath::Abs(EntityExitTimeSquared)) * FMath::Sign(EntityExitTimeSquared);

		OutEntityEnterTime = EntityEnterTime;
		OutEntityExitTime = EntityExitTime;
	}
	else
	{
		OutEntityEnterTime = TNumericLimits<float>::Max();
		OutEntityExitTime = TNumericLimits<float>::Max();
	}
}

}
