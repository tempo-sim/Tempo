// Copyright Tempo Simulation, LLC. All Rights Reserved


#include "TempoZoneGraphBuilder.h"

#include "TempoRoadInterface.h"
#include "ZoneShapeUtilities.h"

#include "TempoCoreUtils.h"

bool FTempoZoneGraphBuilder::ShouldFilterLaneConnection(const UZoneShapeComponent& PolygonShapeComp, const UZoneShapeComponent& SourceShapeComp, const TArray<FLaneConnectionSlot>& SourceSlots, const int32 SourceSlotQueryIndex, const UZoneShapeComponent& DestShapeComp, const TArray<FLaneConnectionSlot>& DestSlots, const int32 DestSlotQueryIndex) const
{
	const AActor* IntersectionQueryActor = GetIntersectionQueryActor(PolygonShapeComp);

	const AActor* SourceRoadQueryActor = GetRoadQueryActor(SourceShapeComp);
	const AActor* DestRoadQueryActor = GetRoadQueryActor(DestShapeComp);

	if (IntersectionQueryActor == nullptr || SourceRoadQueryActor == nullptr || DestRoadQueryActor == nullptr)
	{
		return false;
	}

	const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos = GenerateTempoLaneConnectionInfoArray(SourceSlots);
	const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos = GenerateTempoLaneConnectionInfoArray(DestSlots);

	bool bShouldFilterLaneConnection;
	{
		bShouldFilterLaneConnection = UTempoCoreUtils::CallBlueprintFunction(IntersectionQueryActor, ITempoIntersectionInterface::Execute_ShouldFilterTempoLaneConnection, SourceRoadQueryActor, SourceLaneConnectionInfos, SourceSlotQueryIndex, DestRoadQueryActor, DestLaneConnectionInfos, DestSlotQueryIndex);
	}

	return bShouldFilterLaneConnection;
}

TArray<FTempoLaneConnectionInfo> FTempoZoneGraphBuilder::GenerateTempoLaneConnectionInfoArray(const TArray<FLaneConnectionSlot>& Slots) const
{
	TArray<FTempoLaneConnectionInfo> TempoLaneConnectionInfos;
	
	for (const auto& Slot : Slots)
	{
		const int32 LaneIndex = Slot.Index;
		FTempoLaneConnectionInfo LaneConnectionInfo(Slot, LaneIndex);
		
		TempoLaneConnectionInfos.Add(LaneConnectionInfo);
	}

	return TempoLaneConnectionInfos;
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
