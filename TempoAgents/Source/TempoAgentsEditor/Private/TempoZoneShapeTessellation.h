// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Algo/Reverse.h"
#include "TempoIntersectionInterface.h"
#include "TempoZoneGraphSettings.h"
#include "ZoneGraphBVTree.h"
#include "ZoneGraphTypes.h"
#include "ZoneShapeUtilities.h"

class UZoneShapeComponent;
struct FTempoZoneGraphBuilder;
struct FZoneGraphBuildData;

// Tempo's versions of the functions that turn zone shapes into zone graph lanes, which are private
// to the engine's ZoneGraph module. Their definitions are generated when Tempo is built, from the
// engine's source and the edits in TempoAgentsEditor/EngineDerived.
namespace TempoZoneShape
{
	inline const UTempoZoneGraphSettings& TempoSettings()
	{
		return *GetDefault<UTempoZoneGraphSettings>();
	}

	// Unlike FZoneLaneProfile::ReverseLanes, leaves lanes with no direction (spacers) without one.
	inline void ReverseLanes(FZoneLaneProfile& LaneProfile)
	{
		Algo::Reverse(LaneProfile.Lanes);
		for (FZoneLaneDesc& Lane : LaneProfile.Lanes)
		{
			if (Lane.Direction != EZoneLaneDirection::None)
			{
				Lane.Direction = Lane.Direction == EZoneLaneDirection::Forward ? EZoneLaneDirection::Backward : EZoneLaneDirection::Forward;
			}
		}
	}

	void TessellateSplineShape(TConstArrayView<FZoneShapePoint> Points, const FZoneLaneProfile& LaneProfile, const FZoneGraphTagMask ZoneTags, const FMatrix& LocalToWorld,
		FZoneGraphStorage& OutZoneStorage, TArray<FZoneShapeLaneInternalLink>& OutInternalLinks);

	void TessellatePolygonShape(const UZoneShapeComponent& PolygonShapeComp, const FTempoZoneGraphBuilder& ZoneGraphBuilder,
		TConstArrayView<FZoneShapePoint> Points, TConstArrayView<FZoneLaneProfile> LaneProfiles, const FMatrix& LocalToWorld,
		FZoneGraphStorage& OutZoneStorage, TArray<FZoneShapeLaneInternalLink>& OutInternalLinks);

	void AppendShapeToZoneStorage(const FTempoZoneGraphBuilder& ZoneGraphBuilder, const UZoneShapeComponent& ShapeComp, const FMatrix& LocalToWorld,
		FZoneGraphStorage& OutZoneStorage, TArray<FZoneShapeLaneInternalLink>& OutInternalLinks, FZoneGraphBuildData* InBuildData);

	// FZoneGraphBVTree can only be built from inside the ZoneGraph module. This one can be built
	// here, then assigned to an FZoneGraphBVTree.
	struct FZoneBVTreeBuilder : FZoneGraphBVTree
	{
		void Build(TStridedView<const FBox> Boxes);
	};
}
