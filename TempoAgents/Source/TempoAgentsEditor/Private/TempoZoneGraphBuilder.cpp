// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoZoneGraphBuilder.h"

#include "TempoRoadInterface.h"
#include "TempoZoneShapeTessellation.h"

#include "TempoCoreUtils.h"

#include "ZoneGraphBuilder.h"
#include "ZoneGraphData.h"
#include "ZoneGraphDelegates.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneShapeComponent.h"

namespace
{
	// FZoneGraphBuilder::ConnectLanes is exported from the ZoneGraph module, but protected.
	struct FZoneGraphBuilderAccess : FZoneGraphBuilder
	{
		using FZoneGraphBuilder::ConnectLanes;
	};
}

void FTempoZoneGraphBuilder::StartRebuildingAfterEngineBuilds()
{
	if (!OnEngineBuildDoneHandle.IsValid())
	{
		OnEngineBuildDoneHandle = UE::ZoneGraphDelegates::OnZoneGraphDataBuildDone.AddRaw(this, &FTempoZoneGraphBuilder::OnEngineBuildDone);
	}
}

void FTempoZoneGraphBuilder::StopRebuildingAfterEngineBuilds()
{
	UE::ZoneGraphDelegates::OnZoneGraphDataBuildDone.Remove(OnEngineBuildDoneHandle);
	OnEngineBuildDoneHandle.Reset();
}

void FTempoZoneGraphBuilder::OnEngineBuildDone(const FZoneGraphBuildData& EngineBuildData)
{
	if (bIsBroadcastingBuildDone)
	{
		return;
	}

	// The build data only says which world was built through the zone shapes in it.
	TSet<UWorld*> BuiltWorlds;
	for (const TPair<TObjectPtr<const UZoneShapeComponent>, FZoneShapeComponentBuildData>& ComponentBuildData : EngineBuildData.ZoneShapeComponentBuildData)
	{
		if (ComponentBuildData.Key)
		{
			BuiltWorlds.Add(ComponentBuildData.Key->GetWorld());
		}
	}

	FZoneGraphBuildData BuildData;
	for (UWorld* World : BuiltWorlds)
	{
		UZoneGraphSubsystem* ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(World);
		if (ZoneGraphSubsystem == nullptr)
		{
			continue;
		}

		for (const FRegisteredZoneGraphData& Registered : ZoneGraphSubsystem->GetRegisteredZoneGraphData())
		{
			if (Registered.bInUse && Registered.ZoneGraphData)
			{
				Build(*Registered.ZoneGraphData, ZoneGraphSubsystem->GetBuilder(), BuildData);
			}
		}
	}

	if (BuiltWorlds.IsEmpty())
	{
		return;
	}

	// Everything that reacted to the engine's build may have done so before the rebuild above.
	TGuardValue<bool> BroadcastingGuard(bIsBroadcastingBuildDone, true);
	UE::ZoneGraphDelegates::OnZoneGraphDataBuildDone.Broadcast(BuildData);
}

void FTempoZoneGraphBuilder::Build(AZoneGraphData& ZoneGraphData, const FZoneGraphBuilder& EngineBuilder, FZoneGraphBuildData& OutBuildData) const
{
	FScopeLock StorageLock(&ZoneGraphData.GetStorageLock());

	const ULevel* Level = ZoneGraphData.GetLevel();
	ZoneGraphData.Modify();

	FZoneGraphStorage& ZoneStorage = ZoneGraphData.GetStorageMutable();
	ZoneStorage.Reset();

	TArray<FZoneShapeLaneInternalLink> InternalLinks;
	for (const FZoneGraphBuilderRegisteredComponent& Registered : EngineBuilder.GetRegisteredZoneShapeComponents())
	{
		if (Registered.Component && Registered.Component->GetComponentLevel() == Level)
		{
			TempoZoneShape::AppendShapeToZoneStorage(*this, *Registered.Component, Registered.Component->GetComponentTransform().ToMatrixWithScale(), ZoneStorage, InternalLinks, &OutBuildData);
		}
	}

	FZoneGraphBuilderAccess::ConnectLanes(InternalLinks, ZoneStorage);

	ZoneStorage.Bounds = FBox(ForceInit);
	for (const FZoneData& Zone : ZoneStorage.Zones)
	{
		ZoneStorage.Bounds += Zone.Bounds;
	}

	TempoZoneShape::FZoneBVTreeBuilder ZoneBVTreeBuilder;
	ZoneBVTreeBuilder.Build(MakeStridedView(ZoneStorage.Zones, &FZoneData::Bounds));
	ZoneStorage.ZoneBVTree = static_cast<const FZoneGraphBVTree&>(ZoneBVTreeBuilder);

	// The engine's build already recorded the hash of the zone shapes, which these lanes came from too.
	ZoneGraphData.UpdateDrawing();
}

bool FTempoZoneGraphBuilder::ShouldFilterLaneConnection(const UZoneShapeComponent& PolygonShapeComp,
	const UZoneShapeComponent& SourceShapeComp, const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos, const int32 SourceSlotQueryIndex,
	const UZoneShapeComponent& DestShapeComp, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos, const int32 DestSlotQueryIndex,
	const TArray<FTempoLaneConnectionCandidate>& AllCandidates) const
{
	const AActor* IntersectionQueryActor = GetIntersectionQueryActor(PolygonShapeComp);

	const AActor* SourceRoadQueryActor = GetRoadQueryActor(SourceShapeComp);
	const AActor* DestRoadQueryActor = GetRoadQueryActor(DestShapeComp);

	if (IntersectionQueryActor == nullptr || SourceRoadQueryActor == nullptr || DestRoadQueryActor == nullptr)
	{
		return false;
	}

	return UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_ShouldFilterTempoLaneConnection, SourceRoadQueryActor, SourceLaneConnectionInfos, SourceSlotQueryIndex, DestRoadQueryActor, DestLaneConnectionInfos, DestSlotQueryIndex, AllCandidates);
}

AActor* FTempoZoneGraphBuilder::GetIntersectionQueryActor(const UZoneShapeComponent& ZoneShapeComponent) const
{
	AActor* IntersectionQueryActor = ZoneShapeComponent.GetOwner();

	if (IntersectionQueryActor == nullptr)
	{
		return nullptr;
	}

	if (!IntersectionQueryActor->Implements<UTempoIntersectionInterface>())
	{
		return nullptr;
	}

	return IntersectionQueryActor;
}

AActor* FTempoZoneGraphBuilder::GetRoadQueryActor(const UZoneShapeComponent& ZoneShapeComponent) const
{
	AActor* RoadQueryActor = ZoneShapeComponent.GetOwner();

	if (RoadQueryActor == nullptr)
	{
		return nullptr;
	}

	if (!RoadQueryActor->Implements<UTempoRoadInterface>())
	{
		return nullptr;
	}

	return RoadQueryActor;
}
