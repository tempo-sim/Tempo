// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficTypes.h"

#include "ZoneGraphTypes.h"

#include "MassTrafficIntersections.generated.h"


USTRUCT()
struct MASSTRAFFIC_API FMassTrafficIntersectionSide
{
	GENERATED_BODY()

	TArray<FZoneGraphTrafficLaneData*> VehicleIntersectionLanes;

	/** Indices used to construct FZoneGraphLaneHandle(s) for crosswalk lanes. */
	UPROPERTY()
	TSet<int32> CrosswalkLanes;

	/** Indices used to construct FZoneGraphLaneHandle(s) for crosswalk waiting area lanes. */
	UPROPERTY()
	TSet<int32> CrosswalkWaitingLanes;

	UPROPERTY()
	FVector IntersectionLanesBeginMidpoint = FVector::ZeroVector;

	UPROPERTY()
	FVector DirectionIntoIntersection = FVector::ZeroVector;

	UPROPERTY()
	int32 TrafficLightDetailIndex = INDEX_NONE;

	UPROPERTY()
	bool bHasInboundLanesFromFreeway = false;
};

/**
 * See 'NOTE ON HIDDEN SIDES'
 * Struct to temporarily store information about the hidden outbound-only lanes on of an intersection.
 */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficIntersectionHiddenOutboundSideHints
{
	GENERATED_BODY()

	/**
	 * See 'NOTE ON HIDDEN SIDES'
	 * All of the points found on all of the hidden (outbound-only) sides, in no particular order.
	 * Some of these may seem redundant.
	 */
	UPROPERTY()
	TArray<FVector> Points;

	/**
	 * See 'NOTE ON HIDDEN SIDES'
	 * All of the into-intersection-directions found on all of the hidden (outbound-only) sides, in no particular order.
	 * Some of these may seem redundant.
	 */
	UPROPERTY()
	TArray<FVector> DirectionsIntoIntersection;

	/** See 'NOTE ON HIDDEN SIDES'. */
	UPROPERTY()
	TSet<int32> CrosswalkLanes;

	UPROPERTY()
	TSet<int32> CrosswalkWaitingLanes;
};




USTRUCT()
struct MASSTRAFFIC_API FMassTrafficIntersectionDetail
{
	GENERATED_BODY()

	static const float MinMostlySquareAdjacentSideAngleDeg;
	static const float MaxMostlySquareAdjacentSideCos;

	static const float MinLaneSideConnectionAngleDeg;
	static const float MaxLaneSideConnectionCos;

	static const float MinHiddenSideIntoDirectionAngleDeg;
	static const float MaxHiddenSideIntoDirectionCos;

	UPROPERTY()
	FVector SidesCenter = FVector::ZeroVector;

	UPROPERTY()
	TArray<FMassTrafficIntersectionSide> Sides;

	UPROPERTY()
	bool bSidesAreOrderedClockwise = false;

	UPROPERTY()
	int32 ZoneIndex = INDEX_NONE;

	UPROPERTY()
	bool bHasTrafficLights = false;

	FMassTrafficIntersectionSide& AddSide();

	/** Important. Call this after inbound sides are added and given their lanes. */
	void Build(
		const int32 InZoneIndex,
		const UE::MassTraffic::FMassTrafficBasicHGrid& CrosswalkLaneMidpoint_HGrid, const float IntersectionSideToCrosswalkSearchDistance,
		// Hash grid containing the midpoints of all vehicle intersection inbound sides. And traffic light details and search distance.
		const UE::MassTraffic::FMassTrafficBasicHGrid& IntersectionSideHGrid, const TArray<struct FMassTrafficLightInstanceDesc>* TrafficLightDetails, float TrafficLightSearchDistance,
		// And..
		const FZoneGraphStorage& ZoneGraphStorage,
		UWorld* World);

	bool IsMostlySquare() const;

	/**
	 * Gets vehicle lane fragments that begin at one inbound side and end at another.
	 * Note - Lanes don't actually end at the inbound part of that side. The 'end' inbound side is used as a reference.
	 */
	int32 GetTrafficLanesConnectingSides(int32 StartSideIndex, int32 EndSideIndex, const FZoneGraphStorage& ZoneGraphStorage, TArray<FZoneGraphTrafficLaneData*>& OutVehicleTrafficLanes) const;

	bool HasSideWithInboundLanesFromFreeway() const;

	/**
	 * NOTE ON HIDDEN SIDES -
	 * Right before the Alpha release, some intersections (including the one at player start) were found where two bad
	 * things were happening - 
	 *		(1) In some intersections identified as being 2-sided or 4-sided (meaning having 2 or 4 inbound sides),
	 *		    more than one side would open for traffic at the same time, and some of the vehicles would exit the
	 *		    intersection through one or more *other hidden sides* that I never accounted for, at the same time,
	 *		    sometimes colliding. (2 and 4 sided intersections can have more than one inbound side open for traffic.)
	 *		(2) The pedestrian crosswalk lanes on those sides were also never being opened to pedestrians when the
	 *			traffic cleared, so pedestrians would be waiting forever there.
	 * These cases happened because the 'other hidden sides' never had an associated inbound side built for them, since
	 * these sides don't have any inbound lanes. And only inbound sides knew which pedestrian lanes they blocked and
	 * unblocked - where their lanes were inbound.
	 * To fix this, I added the notion of 'hidden sides'. Now I can identify those intersections that have these hidden
	 * sides, and find which points / into-intersection directions are part of those hidden sides.
	 * Then, to solve the problems related to hidden sides -
	 *		(1) These intersections with hidden (outbound-only) sides always now have their periods built as general
	 *			(round-robin) intersections. Only one inbound side will ever be open, so traffic won't collide on the
	 *			hidden (outbound-only) sides.
	 *		(2) For these intersections with hidden (outbound-only) sides, the pedestrian lanes that vehicles cross when
	 *			travelling over these hidden sides will be included in opening of the pedestrians lanes.
	 * Overwhelmingly, most intersections *do not* end up with hidden (outbound-only) sides.
	 */
	FMassTrafficIntersectionHiddenOutboundSideHints HiddenOutboundSideHints;

	// See 'NOTE ON HIDDEN SIDES'
	bool HasHiddenSides() const;
};
