// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ZoneGraphTypes.h"
#include "ZoneShapeUtilities.generated.h"

struct FZoneGraphBuilder;

/** Struct describing a link for a specified lane, used during building */
USTRUCT()
struct FZoneShapeLaneInternalLink
{
	GENERATED_BODY()

	FZoneShapeLaneInternalLink() = default;
	FZoneShapeLaneInternalLink(const int32 InLaneIdex, const FZoneLaneLinkData InLinkData) : LaneIndex(InLaneIdex), LinkData(InLinkData) {}

	bool operator<(const FZoneShapeLaneInternalLink& RHS) const { return LaneIndex < RHS.LaneIndex; }

	/** Lane index to which the link belongs to */
	UPROPERTY()
	int32 LaneIndex = 0;
	
	/** Link details */
	UPROPERTY()
	FZoneLaneLinkData LinkData = {};
};

struct FLaneConnectionSlot
{
	FVector Position = FVector::ZeroVector;
	FVector Forward = FVector::ZeroVector;
	FVector Up = FVector::ZeroVector;
	FZoneLaneDesc LaneDesc;
	int32 PointIndex = 0;	// Index in dest point array
	int32 Index = 0;		// Index within an entry
	uint16 EntryID = 0;		// Entry ID from source data
	const FZoneLaneProfile* Profile = nullptr;
	EZoneShapeLaneConnectionRestrictions Restrictions = EZoneShapeLaneConnectionRestrictions::None;
	float DistanceFromProfileEdge = 0.0f;	// Distance from lane profile edge
	float DistanceFromFarProfileEdge = 0.0f; // Distance to other lane profile edge
	float InnerTurningRadius = 0.0f; // Inner/minimum turning radius when using Arc routing.
};

namespace UE { namespace ZoneShape { namespace Utilities
{

// Converts spline shape to a zone data.
ZONEGRAPH_API void TessellateSplineShape(TConstArrayView<FZoneShapePoint> Points, const FZoneLaneProfile& LaneProfile, const FZoneGraphTagMask ZoneTags, const FMatrix& LocalToWorld,
										 FZoneGraphStorage& OutZoneStorage, TArray<FZoneShapeLaneInternalLink>& OutInternalLinks);

// Converts polygon shape to a zone data.
ZONEGRAPH_API void TessellatePolygonShape(const UZoneShapeComponent& SourceShapeComp, const FZoneGraphBuilder& ZoneGraphBuilder,
										  TConstArrayView<FZoneShapePoint> Points, TConstArrayView<FZoneLaneProfile> LaneProfiles, const FMatrix& LocalToWorld,
										  FZoneGraphStorage& OutZoneStorage, TArray<FZoneShapeLaneInternalLink>& OutInternalLinks);

// Returns cubic bezier points for give shape segment. Adjusts end points based on shape point types. 
ZONEGRAPH_API void GetCubicBezierPointsFromShapeSegment(const FZoneShapePoint& StartShapePoint, const FZoneShapePoint& EndShapePoint, const FMatrix& LocalToWorld,
														FVector& OutStartPoint, FVector& OutStartControlPoint, FVector& OutEndControlPoint, FVector& OutEndPoint);

}}} // UE::ZoneShape::Utilities
