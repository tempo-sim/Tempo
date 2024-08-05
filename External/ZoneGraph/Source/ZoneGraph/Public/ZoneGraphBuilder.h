// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ZoneGraphTypes.h"
#include "HierarchicalHashGrid2D.h"
#include "ZoneShapeComponent.h"
#include "ZoneShapeUtilities.h"
#include "ZoneGraphBuilder.generated.h"

class AZoneGraphData;
class UZoneShapeComponent;
struct FZoneShapeLaneInternalLink;

typedef THierarchicalHashGrid2D<5, 4> FZoneGraphBuilderHashGrid2D;	// 5 levels of hierarchy, 4 ration between levels (biggest bucket 4^5 = 1024 cells)

USTRUCT()
struct ZONEGRAPH_API FZoneGraphBuilderRegisteredComponent
{
	GENERATED_BODY()

	FZoneGraphBuilderRegisteredComponent() = default;
	FZoneGraphBuilderRegisteredComponent(UZoneShapeComponent* InComponent) : Component(InComponent) {}

	UPROPERTY()
	TObjectPtr<UZoneShapeComponent> Component = nullptr;

	UPROPERTY()
	uint32 ShapeHash = 0;

	FZoneGraphBuilderHashGrid2D::FCellLocation CellLoc;
};

// Build time data for UZoneShapeComponent
USTRUCT()
struct ZONEGRAPH_API FZoneShapeComponentBuildData
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ZoneIndex = 0;

	UPROPERTY()
	TArray<FZoneGraphLaneHandle> Lanes;
};

// Build time data, can be used to map things between editor representation and baked data.
USTRUCT()
struct ZONEGRAPH_API FZoneGraphBuildData
{
	GENERATED_BODY()

	void Reset()
	{
		ZoneShapeComponentBuildData.Reset();
	}

	UPROPERTY()
	TMap<TObjectPtr<const UZoneShapeComponent>, FZoneShapeComponentBuildData> ZoneShapeComponentBuildData;
};

USTRUCT()
struct ZONEGRAPH_API FZoneGraphBuilder
{
	GENERATED_BODY()

public:
	FZoneGraphBuilder();
	virtual ~FZoneGraphBuilder();

	void RegisterZoneShapeComponent(UZoneShapeComponent& ShapeComp);
	void UnregisterZoneShapeComponent(UZoneShapeComponent& ShapeComp);
	void OnZoneShapeComponentChanged(UZoneShapeComponent& ShapeComp);
	const TArray<FZoneGraphBuilderRegisteredComponent>& GetRegisteredZoneShapeComponents() const { return ShapeComponents; }

	bool NeedsRebuild() const { return bIsDirty; }

	/** Builds zone graph for each zone graph data based on registered shapes.
	 * @param AllZoneGraphData All Zone Graph data to update.
	 * @param bForceRebuild If set will build graph even if it inputs have not changed.
	 */
	void BuildAll(const TArray<AZoneGraphData*>& AllZoneGraphData, const bool bForceRebuild);

	void FindShapeConnections(const UZoneShapeComponent& SourceShapeComp, TArray<FZoneShapeConnection>& OutShapeConnections) const;

	/** Converts single zone shape into a zone storage, used in UI for editing and rendering.
	*/
	void BuildSingleShape(const UZoneShapeComponent& ShapeComp, const FMatrix& LocalToWorld, FZoneGraphStorage& OutZoneStorage);

	/** Returns items that potentially touch the bounds in the HashGrid. Operates on grid level, can have false positives.
	 * @param Bounds - Query bounding box.
	 * @param OutResults - Result of the query, IDs of potentially overlapping items.
	 */
	void QueryHashGrid(const FBox& Bounds, TArray<FZoneGraphBuilderHashGrid2D::ItemIDType>& OutResults);

	virtual bool ShouldFilterLaneConnection(const UZoneShapeComponent& PolygonShapeComp, const UZoneShapeComponent& SourceShapeComp, const TArray<FLaneConnectionSlot>& SourceSlots, const int32 SourceSlotQueryIndex, const UZoneShapeComponent& DestShapeComp, const TArray<FLaneConnectionSlot>& DestSlots, const int32 DestSlotQueryIndex) const { return false; };

protected:
	void Build(AZoneGraphData& ZoneGraphData);
	void RequestRebuild();
	void OnLaneProfileChanged(const FZoneLaneProfileRef& ChangedLaneProfileRef);
	uint32 CalculateCombinedShapeHash(const AZoneGraphData& ZoneGraphData) const;
	void AppendShapeToZoneStorage(const UZoneShapeComponent& ShapeComp, const FMatrix& LocalToWorld, FZoneGraphStorage& OutZoneStorage, TArray<FZoneShapeLaneInternalLink>& OutInternalLinks, FZoneGraphBuildData* InBuildData = nullptr);
	static void ConnectLanes(TArray<FZoneShapeLaneInternalLink>& InternalLinks, FZoneGraphStorage& OutZoneStorage);

	UPROPERTY(Transient)
	TArray<FZoneGraphBuilderRegisteredComponent> ShapeComponents;

	UPROPERTY(Transient)
	TArray<int32> ShapeComponentsFreeList;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UZoneShapeComponent>, int32> ShapeComponentToIndex;

	UPROPERTY(Transient)
	FZoneGraphBuildData BuildData;

#if WITH_EDITOR
	FDelegateHandle OnBuildSettingsChangedHandle;
	FDelegateHandle OnTagsChangedHandle;
	FDelegateHandle OnLaneProfileChangedHandle;
#endif

	FZoneGraphBuilderHashGrid2D HashGrid;

	bool bSkipHashCheck = false;
	bool bIsDirty = false;
};