// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTraffic.h"
#include "MassTrafficFragments.h"
#include "VehicleUtility.h"
#include "ZoneGraphTypes.h"
#include "ZoneGraphQuery.h"
#include "MassEntityManager.h"
#include "MassCommonFragments.h"
#include "MassTrafficSettings.h"
#include "MassTrafficVehicleSimulationTrait.h"


/** Forward declarations */
struct FZoneGraphTrafficLaneData;
class UMassTrafficSubsystem;

namespace UE::MassTraffic
{
	
/** Noise */
	
FORCEINLINE float CalculateNoiseValue(const float NoiseInput, const float NoisePeriod)
{
	return FMath::PerlinNoise1D(NoiseInput / NoisePeriod);
}

/** Braking & stopping distances */
	
FORCEINLINE float GetDistanceAlongLaneToStopAt(const float Radius, const float LaneLength, const float RandomFraction, const FVector2D& StoppingDistanceFromLaneEndRange)
{
	return LaneLength - Radius - FMath::Lerp(StoppingDistanceFromLaneEndRange.X, StoppingDistanceFromLaneEndRange.Y, RandomFraction);	
}
	
FORCEINLINE float GetDistanceAlongLaneToBrakeFrom(const float SpeedLimit, const float Radius, const float LaneLength, const float BrakingTime, const float DistanceAlongLaneToStopAt)
{
	float DistanceAlongLaneToBrakeFrom = LaneLength - Radius - (BrakingTime * SpeedLimit);
	return FMath::Min(DistanceAlongLaneToBrakeFrom, DistanceAlongLaneToStopAt);
}

/** Distance to obstacle. */

FORCEINLINE float GeObstacleAvoidanceBrakingTime(const float RandomFraction, const FVector2D& ObstacleAvoidanceBrakingTimeRange)
{
	return FMath::Lerp(ObstacleAvoidanceBrakingTimeRange.X, ObstacleAvoidanceBrakingTimeRange.Y, RandomFraction);
}

FORCEINLINE float GetMinimumDistanceToObstacle(const float RandomFraction, const FVector2D& MinimumDistanceToObstacleRange)
{
	return FMath::Lerp(MinimumDistanceToObstacleRange.X, MinimumDistanceToObstacleRange.Y, RandomFraction);
}

FORCEINLINE float GetIdealDistanceToObstacle(const float Speed, const float RandomFraction, const FVector2D& IdealTimeToObstacleRange, const float MinimumDistanceToObstacle)
{
	float IdealDistanceToNextVehicle = FMath::Lerp(IdealTimeToObstacleRange.X, IdealTimeToObstacleRange.Y, RandomFraction) * Speed;
	return FMath::Max(IdealDistanceToNextVehicle, MinimumDistanceToObstacle);
}

/** Space taken by vehicle. */

FORCEINLINE float GetSpaceTakenByVehicleOnLane(const float Radius, const float RandomFraction, const FVector2D& MinimumDistanceToNextVehicleRange)
{
	return 2.0f * Radius + GetMinimumDistanceToObstacle(RandomFraction, MinimumDistanceToNextVehicleRange);
}

/** Can stop quickly? */

FORCEINLINE bool CanStopQuickly(const float Speed, const float MaxQuickStopSpeedMPH)
{
	return Chaos::CmSToMPH(Speed) <= MaxQuickStopSpeedMPH;
}

/** Look-ahead distance. */

FORCEINLINE float GetVehicleLookAheadDistance(const float SpeedControlLaneLookAheadTime, const float SteeringControlLaneLookAheadTime, const float SpeedControlMinLookAheadDistance, const float SteeringControlMinLookAheadDistance, const float Speed)
{
	const float LookAheadTime = FMath::Max(SpeedControlLaneLookAheadTime, SteeringControlLaneLookAheadTime);	
	const float LookAheadDistance = FMath::Max(SpeedControlMinLookAheadDistance, SteeringControlMinLookAheadDistance);	
	return FMath::Max(Speed * LookAheadTime, LookAheadDistance);
}

/** Vehicle speed */

FORCEINLINE float VarySpeedLimit(const float SpeedLimit, const float SpeedLimitVariancePct, const float SpeedVariancePct, const float RandomFraction, const float NoiseValue)
{
	const float DesiredSpeedVariancePct = SpeedLimitVariancePct * RandomFraction + SpeedVariancePct * NoiseValue;
	return SpeedLimit * (1.0f - DesiredSpeedVariancePct);
}

FORCEINLINE float GetObstacleAvoidanceBrakingSpeedFactor(const float DistanceToObstacle, const float MinimumDistanceToObstacle, const float BrakingDistanceFromObstacle, const float ObstacleAvoidanceBrakingPower)
{
	float SpeedFactor = FMath::Clamp(FMath::GetRangePct(MinimumDistanceToObstacle, BrakingDistanceFromObstacle, DistanceToObstacle), 0.0f, 1.0f);
	SpeedFactor = FMath::Pow(SpeedFactor, ObstacleAvoidanceBrakingPower);
	return SpeedFactor;
}
	
FORCEINLINE float GetStopSignBrakingSpeedFactor(const float DistanceAlongLaneToStopAt, const float DistanceAlongLaneToBrakeFrom, const float DistanceAlongLane, const float StoppingPower)
{
	float SpeedFactor = 1.0f - FMath::Clamp(FMath::GetRangePct(DistanceAlongLaneToBrakeFrom, DistanceAlongLaneToStopAt, DistanceAlongLane), 0.0f, 1.0f);
	SpeedFactor = FMath::Pow(SpeedFactor, StoppingPower);
	return SpeedFactor; 
}

MASSTRAFFIC_API float CalculateTargetSpeed(
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
	float NextVehicleAvoidanceBrakingPower, // 3.0f  @todo Better param name 
	const FVector2D& ObstacleAvoidanceBrakingTimeRange,
	const FVector2D& MinimumDistanceToObstacleRange,
	float ObstacleAvoidanceBrakingPower, // 3.0f  @todo Better param name
	float StopSignBrakingTime,
	FVector2D StoppingDistanceFromLaneEndRange,
	float StopSignBrakingPower, // 0.5f  @todo Better param name
	bool bStopAtLaneExit
#if WITH_MASSTRAFFIC_DEBUG
	, bool bVisLog = false
	, const UObject* VisLogOwner = nullptr
	, const FTransform* VisLogTransform = nullptr
#endif
);

MASSTRAFFIC_API bool ShouldStopAtLaneExit(
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
	, bool bVisLog = false
	, const UObject* VisLogOwner = nullptr
	, const FTransform* VisLogTransform = nullptr
#endif
	, const UWorld* World = nullptr // ..for debugging
	, const FVector* VehicleLocation = nullptr // ..for debuging
);

/** Avoidance */
	
MASSTRAFFIC_API float TimeToCollision(const FVector& AgentLocation, const FVector& AgentVelocity, float AgentRadius, const FVector& ObstacleLocation, const FVector& ObstacleVelocity, float ObstacleRadius);

/** Lane location eval */

FORCEINLINE void CalculateOffsetLocationAlongLane(const FZoneGraphStorage& ZoneGraphStorage, int32 CurrentLaneIndex, float DistanceAlongCurrentLane, float LateralOffset, FZoneGraphLaneLocation& OutLaneLocation)
{
	// Calculate base location
	UE::ZoneGraph::Query::CalculateLocationAlongLane(ZoneGraphStorage, CurrentLaneIndex, DistanceAlongCurrentLane, OutLaneLocation);

	// Offset position by LateralOffset
	const FVector Offset = (OutLaneLocation.Up ^ OutLaneLocation.Direction) * LateralOffset;
	OutLaneLocation.Position += Offset;
}

FORCEINLINE void CalculateLocationAlongContinuousLanes(const FZoneGraphStorage& ZoneGraphStorage, int32 CurrentLaneIndex, float CurrentLaneLength, int32 NextLaneIndex, float DistanceAlongCurrentLane, FZoneGraphLaneLocation& OutLaneLocation)
{
	if (DistanceAlongCurrentLane > CurrentLaneLength && NextLaneIndex != INDEX_NONE)
	{
		UE::ZoneGraph::Query::CalculateLocationAlongLane(ZoneGraphStorage, NextLaneIndex, DistanceAlongCurrentLane - CurrentLaneLength, OutLaneLocation);
	}
	else
	{
		UE::ZoneGraph::Query::CalculateLocationAlongLane(ZoneGraphStorage, CurrentLaneIndex, DistanceAlongCurrentLane, OutLaneLocation);
	}
}

FORCEINLINE void CalculateOffsetLocationAlongContinuousLanes(const FZoneGraphStorage& ZoneGraphStorage, int32 CurrentLaneIndex, float CurrentLaneLength, int32 NextLaneIndex, float DistanceAlongCurrentLane, float LateralOffset, FZoneGraphLaneLocation& OutLaneLocation)
{
	// Calculate base location
	CalculateLocationAlongContinuousLanes(ZoneGraphStorage, CurrentLaneIndex, CurrentLaneLength, NextLaneIndex, DistanceAlongCurrentLane, OutLaneLocation);

	// Offset position by LateralOffset
	const FVector Offset = (OutLaneLocation.Up ^ OutLaneLocation.Direction) * LateralOffset;
	OutLaneLocation.Position += Offset;
}

/** Lane progression */

MASSTRAFFIC_API void MoveVehicleToNextLane(
	FMassEntityManager& EntityManager,
	UMassTrafficSubsystem& MassTrafficSubsystem,
	const FMassEntityHandle VehicleEntity,
	const FAgentRadiusFragment& AgentRadiusFragment,
	const FMassTrafficRandomFractionFragment& RandomFractionFragment,
	FMassTrafficVehicleControlFragment& VehicleControlFragment,
	FMassTrafficVehicleLightsFragment& VehicleLightsFragment,
	FMassZoneGraphLaneLocationFragment& LaneLocationFragment,
	FMassTrafficNextVehicleFragment& NextVehicleFragment,
	FMassTrafficVehicleLaneChangeFragment* LaneChangeFragment, bool& bIsVehicleStuck);

/** Instantly moves a vehicle onto another lane. This is not an animated over time. */
MASSTRAFFIC_API bool TeleportVehicleToAnotherLane(
	const FMassEntityHandle Entity_Current,
	FZoneGraphTrafficLaneData& TrafficLaneData_Current,
	FMassTrafficVehicleControlFragment& VehicleControlFragment_Current,
	const FAgentRadiusFragment& RadiusFragment_Current,
	const FMassTrafficRandomFractionFragment& RandomFractionFragment_Current,
	FMassZoneGraphLaneLocationFragment& LaneLocationFragment_Current,
	FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
	FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment_Current,
	// The chosen lane which Entity_Current will be inserted into
	FZoneGraphTrafficLaneData& Lane_Chosen,
	const float DistanceAlongLane_Chosen,
	// The vehicle whose Next is Entity_Current / the vehicle behind Entity_Current on the current lane. 
	// Must be on the current lane. 
	FMassEntityHandle Entity_Current_Behind,
	FMassTrafficNextVehicleFragment* NextVehicleFragment_Current_Behind,
	// Entity_Current's Next vehicle. Must be on the current lane. Will become the new current lane tail if
	// !Entity_Current_Behind.IsSet()
	FMassEntityHandle Entity_Current_Ahead,
	// The vehicle directly behind DistanceAlongLane_Chosen on the chosen lane that will point to Entity_Current as it's Next
	// Must be on the chosen lane
	FMassEntityHandle Entity_Chosen_Behind,
	FMassTrafficNextVehicleFragment* NextVehicleFragment_Chosen_Behind,
	const FAgentRadiusFragment* RadiusFragment_Chosen_Behind,
	const FMassZoneGraphLaneLocationFragment* LaneLocationFragment_Chosen_Behind,
	FMassTrafficObstacleAvoidanceFragment* AvoidanceFragment_Chosen_Behind,
	// The vehicle ahead of DistanceAlongLane_Chosen on the chosen lane that will become Entity_Current's new Next.
	// Doesn't have to be on the chosen lane e.g: could be on the next lane  
	FMassEntityHandle Entity_Chosen_Ahead,
	const FAgentRadiusFragment* AgentRadiusFragment_Chosen_Ahead,
	const FMassZoneGraphLaneLocationFragment* ZoneGraphLaneLocationFragment_Chosen_Ahead,
	//
	const UMassTrafficSettings& MassTrafficSettings,
	const FMassEntityManager& EntityManager);

}
