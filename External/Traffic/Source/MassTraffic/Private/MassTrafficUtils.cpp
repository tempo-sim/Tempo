// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficUtils.h"
#include "MassTraffic.h"
#include "MassTrafficTypes.h"
#include "MassTrafficSettings.h"
#include "MassTrafficFragments.h"

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



}
