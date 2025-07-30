// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "MassEntityManager.h"
#include "ZoneGraphTypes.h"
#include "MassTraffic.h"
#include "MassTrafficTypes.h"


// Forward declarations
struct FZoneGraphTrafficLaneData;
struct FMassTrafficLaneDensity;
struct FMassTrafficVehicleSpawnFilter;

class UMassTrafficSettings;
class UMassTrafficSubsystem;
class UZoneGraphSubsystem;

/**
* Helpful functions for determining lane directions from Zone Graph data.
*/
namespace UE::MassTraffic {


/**
 * Simple normalized cubic spline.
 *		0.0  -> 0.0
 *		0.25 -> 0.15625
 *		0.5  -> 0.5
 *		0.75 -> 0.84375
 *		1.0  -> 1.0
 */
FORCEINLINE constexpr float SimpleNormalizedCubicSpline(float Alpha)
{
	if (Alpha < 0.0f) Alpha = 0.0f;
	if (Alpha > 1.0f) Alpha = 1.0f;
	return -2.0f * Alpha * Alpha * Alpha + 3.0f * Alpha * Alpha;   
}

FORCEINLINE constexpr float SimpleNormalizedCubicSplineDerivative(float Alpha)
{
	if (Alpha < 0.0f) Alpha = 0.0f;
	if (Alpha > 1.0f) Alpha = 1.0f;
	return -6.0f * Alpha * Alpha + 6.0f * Alpha;   
}

FORCEINLINE constexpr float SimpleNormalizedCubicSplineSecondDerivative(float Alpha)
{
	if (Alpha < 0.0f) Alpha = 0.0f;
	if (Alpha > 1.0f) Alpha = 1.0f;
	return -12.0f * Alpha + 6.0f;   
}

/**
 * A wrapper around FRandomStream that produces discrete random values with a given probability 
 * distribution.
 */
template <typename T = int32>
struct MASSTRAFFIC_API TDiscreteRandomStream
{
	TDiscreteRandomStream(const TArray<T>& ProbabilityWeights)
	{
		check(ProbabilityWeights.Num() > 0);

		ChanceRangeEnd.SetNumUninitialized(ProbabilityWeights.Num());
		T ChanceRangeStart = 0;
		for (int32 ChoiceIndex = 0; ChoiceIndex < ProbabilityWeights.Num(); ++ChoiceIndex)
		{
			const T ProbabilityWeight = ProbabilityWeights[ChoiceIndex];
			ChanceRangeEnd[ChoiceIndex] = ChanceRangeStart + ProbabilityWeight;
			ChanceRangeStart += ProbabilityWeight;
		}
	}

	int32 RandChoice(const FRandomStream& RandomStream) const
	{
		const float RandomChance = RandomStream.FRandRange(0.0f, ChanceRangeEnd.Last());
		for (int32 ChoiceIndex = 0; ChoiceIndex < ChanceRangeEnd.Num(); ++ChoiceIndex)
		{
			if (RandomChance <= ChanceRangeEnd[ChoiceIndex])
			{
				return ChoiceIndex;
			}
		}

		checkNoEntry();
		return -1;
	}

	TArray<T> ChanceRangeEnd;
};


/** Used for spawning. */
MASSTRAFFIC_API void FindNearestVehiclesInLane(const FMassEntityManager& EntityManager,
												const FZoneGraphTrafficLaneData& TrafficLaneData,
												float Distance,
												FMassEntityHandle& OutPreviousVehicle,
												FMassEntityHandle& OutNextVehicle);

MASSTRAFFIC_API FVector GetLaneBeginPoint(const uint32 LaneIndex,
									const FZoneGraphStorage& ZoneGraphStorage,
									const uint32 CountFromBegin = 0,
									bool* bIsValid = nullptr);
MASSTRAFFIC_API FVector GetLaneEndPoint(const uint32 LaneIndex,
									const FZoneGraphStorage& ZoneGraphStorage,
									const uint32 CountFromEnd = 0,
									bool* bIsValid = nullptr);
MASSTRAFFIC_API FVector GetLaneMidPoint(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage);

MASSTRAFFIC_API float GetLaneBeginToEndDistance(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage);
MASSTRAFFIC_API FVector GetLaneBeginDirection(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage);
MASSTRAFFIC_API FVector GetLaneEndDirection(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage);
MASSTRAFFIC_API FVector GetLaneBeginToEndDirection(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage);

MASSTRAFFIC_API float GetDistanceAlongLaneNearestToPoint(const uint32 LaneIndex, const FVector& QueryPoint, const FZoneGraphStorage& ZoneGraphStorage);
MASSTRAFFIC_API FVector GetDirectionAtDistanceAlongLane(const uint32 LaneIndex, float Distance, const FZoneGraphStorage& ZoneGraphStorage);
MASSTRAFFIC_API FVector GetPointAtDistanceAlongLane(const uint32 LaneIndex,  float Distance, const FZoneGraphStorage& ZoneGraphStorage);

MASSTRAFFIC_API float GetLaneStraightness(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage);

MASSTRAFFIC_API LaneTurnType GetLaneTurnType(const uint32 LaneIndex, const FZoneGraphStorage& ZoneGraphStorage);

int32 GetLanePriority(
	const FZoneGraphTrafficLaneData* TrafficLaneData,
	const FMassTrafficLanePriorityFilters& LanePriorityFilters,
	const FZoneGraphStorage& ZoneGraphStorage);

/** Lane search functions. */
MASSTRAFFIC_API bool PointIsNearSegment(
	const FVector& Point, 
	const FVector& SegmentStartPoint, const FVector& SegmentEndPoint,
	const float MaxDistance);


/** Get the interpolated speed limit in cm/s at DistanceAlongLane */
FORCEINLINE float GetSpeedLimitAlongLane(const float Length, const float SpeedLimit, const float MinNextLaneSpeedLimit, const float DistanceAlongLane, const float CurrentSpeed, const float TimeToBlendFromLaneEnd = 2.0f)
{
	const float TimeLeftOnLane = (Length - DistanceAlongLane) / CurrentSpeed; 
	const float SpeedScale = 1.0f - FMath::Clamp(TimeLeftOnLane / TimeToBlendFromLaneEnd, 0.0f, 1.0f);
	return FMath::Lerp(SpeedLimit, MinNextLaneSpeedLimit, SpeedScale) IF_MASSTRAFFIC_ENABLE_DEBUG( * GMassTrafficSpeedLimitScale );
}

void DrawLaneData(
	const UZoneGraphSubsystem& ZoneGraphSubsystem,
	const FZoneGraphLaneHandle& LaneHandle,
	const FColor LaneColor,
	const UWorld& World,
	const float ZOffset = 10.0f,
	const float LifeTime = 0.1f);

bool TryGetVehicleEnterAndExitTimesForIntersection(
	const FZoneGraphTrafficLaneData& VehicleCurrentLaneData,
	const FZoneGraphTrafficLaneData& IntersectionLaneData,
	const float VehicleDistanceAlongCurrentLane,
	const float VehicleEffectiveSpeed,
	const float VehicleRadius,
	float& OutVehicleEnterTime,
	float& OutVehicleExitTime,
	float* OutVehicleDistanceToEnterIntersectionLane = nullptr,
	float* OutVehicleDistanceToExitIntersectionLane = nullptr);

MASSTRAFFIC_API bool TryGetEnterAndExitDistancesAlongQueryLane(
	const UMassTrafficSubsystem& MassTrafficSubsystem,
	const UMassTrafficSettings& MassTrafficSettings,
	const FZoneGraphStorage& ZoneGraphStorage,
	const FZoneGraphLaneHandle& QueryLane,
	const FZoneGraphLaneHandle& OtherLane,
	float& OutEnterDistanceAlongQueryLane,
	float& OutExitDistanceAlongQueryLane,
	bool bExpectIntersection=true);

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
	float* OutVehicleEnterDistance = nullptr,
	float* OutVehicleExitDistance = nullptr);

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
	float* OutEntityEnterDistance = nullptr,
	float* OutEntityExitDistance = nullptr);

void GetEnterAndExitTimeForYieldingEntity(
	const float EnterDistance,
	const float ExitDistance,
	const float Acceleration,
	float& OutEntityEnterTime,
	float& OutEntityExitTime);

}
