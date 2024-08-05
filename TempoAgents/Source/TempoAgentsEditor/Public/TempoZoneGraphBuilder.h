// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "ZoneGraphBuilder.h"
#include "TempoIntersectionInterface.h"
#include "TempoZoneGraphBuilder.generated.h"

USTRUCT()
struct TEMPOAGENTSEDITOR_API FTempoZoneGraphBuilder : public FZoneGraphBuilder
{
	GENERATED_BODY()

public:
	
	virtual bool ShouldFilterLaneConnection(const UZoneShapeComponent& PolygonShapeComp, const UZoneShapeComponent& SourceShapeComp, const TArray<FLaneConnectionSlot>& SourceSlots, const int32 SourceSlotQueryIndex, const UZoneShapeComponent& DestShapeComp, const TArray<FLaneConnectionSlot>& DestSlots, const int32 DestSlotQueryIndex) const override;

protected:

	TArray<FTempoLaneConnectionInfo> GenerateTempoLaneConnectionInfoArray(const TArray<FLaneConnectionSlot>& Slots) const;

	AActor* GetIntersectionQueryActor(const UZoneShapeComponent& ZoneShapeComponent) const;
	AActor* GetRoadQueryActor(const UZoneShapeComponent& ZoneShapeComponent) const;
};
