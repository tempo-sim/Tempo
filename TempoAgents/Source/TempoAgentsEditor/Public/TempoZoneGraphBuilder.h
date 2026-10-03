// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoIntersectionInterface.h"

class AZoneGraphData;
class UZoneShapeComponent;
struct FZoneGraphBuildData;
struct FZoneGraphBuilder;

/**
 * Rebuilds zone graph data the way Tempo lays lanes, in place of what the engine's builder produced.
 *
 * The engine's UZoneGraphSubsystem owns its builder and offers no way to replace it, so Tempo lets
 * it build, then builds again from the same zone shapes and overwrites the result.
 */
struct TEMPOAGENTSEDITOR_API FTempoZoneGraphBuilder
{
	// Rebuild all zone graph data whenever the engine's builder has.
	void StartRebuildingAfterEngineBuilds();
	void StopRebuildingAfterEngineBuilds();

	// Whether the intersection that owns PolygonShapeComp rejects the candidate lane from the source
	// road's lane at SourceSlotQueryIndex to the dest road's lane at DestSlotQueryIndex.
	bool ShouldFilterLaneConnection(const UZoneShapeComponent& PolygonShapeComp,
		const UZoneShapeComponent& SourceShapeComp, const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos, const int32 SourceSlotQueryIndex,
		const UZoneShapeComponent& DestShapeComp, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos, const int32 DestSlotQueryIndex,
		const TArray<FTempoLaneConnectionCandidate>& AllCandidates) const;

protected:
	void OnEngineBuildDone(const FZoneGraphBuildData& EngineBuildData);
	void Build(AZoneGraphData& ZoneGraphData, const FZoneGraphBuilder& EngineBuilder, FZoneGraphBuildData& OutBuildData) const;

	AActor* GetIntersectionQueryActor(const UZoneShapeComponent& ZoneShapeComponent) const;
	AActor* GetRoadQueryActor(const UZoneShapeComponent& ZoneShapeComponent) const;

	FDelegateHandle OnEngineBuildDoneHandle;
	bool bIsBroadcastingBuildDone = false;
};
