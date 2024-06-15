// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once


#include "MassTrafficFragments.h"

#include "MassCommonFragments.h"
#include "ZoneGraphQuery.h"

#include "MassTrafficLaneChange.generated.h"


USTRUCT()
struct MASSTRAFFIC_API FMassTrafficLaneChangeFitReport
{
	GENERATED_BODY()
	
	FMassTrafficLaneChangeFitReport() :
		bIsClearOfVehicleBehind(true),
		bIsClearOfVehicleAhead(true),
		bIsClearOfLaneStart(true),	
		bIsClearOfLaneEnd(true)
	{
	}
	
	bool bIsClearOfVehicleBehind : 1;
	bool bIsClearOfVehicleAhead : 1;
	bool bIsClearOfLaneStart : 1;	
	bool bIsClearOfLaneEnd : 1;
	
	FORCEINLINE bool IsClear() const
	{
		return bIsClearOfVehicleBehind && bIsClearOfVehicleAhead && bIsClearOfLaneStart && bIsClearOfLaneEnd;
	}

	FORCEINLINE void ClearAll()
	{
		bIsClearOfVehicleBehind = true;
		bIsClearOfVehicleAhead = true;
		bIsClearOfLaneStart = true;	
		bIsClearOfLaneEnd = true;
	}

	FORCEINLINE void BlockAll()
	{
		bIsClearOfVehicleBehind = false;
		bIsClearOfVehicleAhead = false;
		bIsClearOfLaneStart = false;	
		bIsClearOfLaneEnd = false;
	}
};


namespace UE::MassTraffic {

	
enum EMassTrafficLaneChangeRecommendationLevel
{
	StayOnCurrentLane_RetryNormal = 0,
	StayOnCurrentLane_RetrySoon = 1,
	
	NormalLaneChange = 2,
	TransversingLaneChange = 3
};


struct FMassTrafficLaneChangeRecommendation
{
	EMassTrafficLaneChangeRecommendationLevel Level = StayOnCurrentLane_RetryNormal;
	bool bChoseLaneOnLeft = false;
	bool bChoseLaneOnRight = false;
	FZoneGraphTrafficLaneData* Lane_Chosen = nullptr;
	bool bNoLaneChangesUntilNextLane = false;
};

	
bool TrunkVehicleLaneCheck(const FZoneGraphTrafficLaneData* TrafficLaneData, const FMassTrafficVehicleControlFragment& VehicleControlFragment);

	
FORCEINLINE bool AreVehiclesCurrentlyApproachingLaneFromIntersection(const FZoneGraphTrafficLaneData& TrafficLaneData) 
{
	return TrafficLaneData.bIsDownstreamFromIntersection && TrafficLaneData.NumVehiclesApproachingLane > 0;
}	


/**
 * Finds nearest vehicles behind and ahead of a distance along the lane.
 * Can optionally ignore a particular vehicle.
 * Returns true if no problems were found.
 */
	
bool FindNearbyVehiclesOnLane_RelativeToDistanceAlongLane(
	const FZoneGraphTrafficLaneData* TrafficLaneData,
	const float DistanceAlongLane, 
	FMassEntityHandle& OutEntity_Behind, FMassEntityHandle& OutEntity_Ahead,
	const FMassEntityManager& EntityManager);


/**
 * Finds nearest vehicles behind and ahead of a vehicle entity on a lane.
 * Can optionally ignore a particular vehicle.
 * Returns true if no problems were found.
 */

bool FindNearbyVehiclesOnLane_RelativeToVehicleEntity(
	const FZoneGraphTrafficLaneData* TrafficLaneData,
	const FMassEntityHandle Entity_Current,
	const FMassTrafficNextVehicleFragment& NextVehicleFragment_Current,
	FMassEntityHandle& OutEntity_Behind, FMassEntityHandle& OutEntity_Ahead,
	const FMassEntityManager& EntityManager,
	const UObject* VisLogOwner = nullptr);


enum class EMassTrafficFindNextLaneVehicleType : uint8
{
	Any = 0,
	Tail = 1,
	LaneChangeGhostTail = 2,
	SplittingLaneGhostTail = 3,
	MergingLaneGhostTail = 4
};


FMassEntityHandle FindNearestTailVehicleOnNextLanes(
	const FZoneGraphTrafficLaneData& CurrentTrafficLaneData,
	const FVector& VehiclePosition,
	const FMassEntityManager& EntityManager,
	const EMassTrafficFindNextLaneVehicleType Type);


/**
 * NOTE - If OutLaneChangeReasons is null, this will run quicker, as it doesn't need to run all tests to provide a
 * valid set of reasons. If it's not null, then all tests need to be run.
 */

void CanVehicleLaneChangeToFitOnChosenLane(
	const float DistanceAlongLane_Chosen, const float LaneLength_Chosen, const float DeltaDistanceAlongLaneForLaneChange_Chosen,
	//
	const FMassTrafficVehicleControlFragment& VehicleControlFragment_Current,
	const FAgentRadiusFragment& RadiusFragment_Current,
	const FMassTrafficRandomFractionFragment& RandomFractionFragment_Current,
	//
	const bool bIsValid_Behind, // .means all these fragments are non-null
	const FAgentRadiusFragment* RadiusFragment_Chosen_Behind,
	const FMassZoneGraphLaneLocationFragment* ZoneGraphLaneLocation_Chosen_Behind,
	//
	const bool bIsValid_Ahead, // .means all these fragments are non-null
	const FMassTrafficVehicleControlFragment* VehicleControlFragment_Chosen_Ahead,
	const FAgentRadiusFragment* RadiusFragment_Chosen_Ahead,
	const FMassZoneGraphLaneLocationFragment* ZoneGraphLaneLocation_Chosen_Ahead,
	//
	const FVector2D MinimumDistanceToNextVehicleRange,
	//
	FMassTrafficLaneChangeFitReport& OutLaneChangeFitReport);


void AdjustVehicleTransformDuringLaneChange(
	const FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment,
	const float InDistanceAlongLane,
	FTransform& Transform,
	UWorld* World=nullptr, // ..for debug drawing only
	const bool bVisLog = false,
	UObject* VisLogOwner=nullptr);


/**
 * Assumes that the progressive distance between the lanes is always monotonic (always decreasing or increasing.)
 */
FORCEINLINE float GetMaxDistanceBetweenLanes(const int32 LaneIndex1, const int32 LaneIndex2, const FZoneGraphStorage& ZoneGraphStorage)
{
	const FZoneLaneData& LaneData1 = ZoneGraphStorage.Lanes[LaneIndex1];
	const FVector& LaneBeginPoint1 = ZoneGraphStorage.LanePoints[LaneData1.PointsBegin];
	const FVector& LaneEndPoint1 = ZoneGraphStorage.LanePoints[LaneData1.PointsEnd - 1];

	const FZoneLaneData& LaneData2 = ZoneGraphStorage.Lanes[LaneIndex2];
	const FVector& LaneBeginPoint2 = ZoneGraphStorage.LanePoints[LaneData2.PointsBegin];
	const FVector& LaneEndPoint2 = ZoneGraphStorage.LanePoints[LaneData2.PointsEnd - 1];

	return FMath::Max(FVector::Dist(LaneBeginPoint1, LaneBeginPoint2), FVector::Dist(LaneEndPoint1, LaneEndPoint2));
}

	
FZoneGraphLaneLocation GetClosestLocationOnLane(
	const FVector& Location,
	const int32 LaneIndex,
	const float MaxSearchDistance,
	const FZoneGraphStorage& ZoneGraphStorage,
	float* OutDistanceSquared = nullptr);
	

void ChooseLaneForLaneChange(
	const float DistanceAlongCurrentLane_Initial,
	const FZoneGraphTrafficLaneData* TrafficLaneData_Initial,
	const FAgentRadiusFragment& AgentRadiusFragment,
	const FMassTrafficRandomFractionFragment& RandomFractionFragment,
	const FMassTrafficVehicleControlFragment& VehicleControlFragment,
	const FRandomStream& RandomStream,
	const UMassTrafficSettings& MassTrafficSettings,
	FMassTrafficLaneChangeRecommendation& OutRecommendation);

bool CheckNextVehicle(const FMassEntityHandle Entity, const FMassEntityHandle NextEntity, const FMassEntityManager& EntityManager);

};
