// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "ZoneGraphTypes.h"
#include "GameFramework/Actor.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "TempoZoneGraphBuilder.h"
#include "TempoRoadLaneGraphSubsystem.generated.h"

class AZoneGraphData;

UCLASS()
class TEMPOAGENTSEDITOR_API UTempoRoadLaneGraphSubsystem : public UUnrealEditorSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Tempo Agents")
	void SetupZoneGraphBuilder();

	UFUNCTION(BlueprintCallable, Category = "Tempo Agents")
	bool TryGenerateZoneShapeComponents() const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Agents")
	void BuildZoneGraph() const;

protected:

	// Road functions
	bool TryGenerateAndRegisterZoneShapeComponentsForRoad(AActor& RoadQueryActor, bool bQueryActorIsRoadModule = false) const;
	bool TryGenerateZoneShapeComponentBetweenDistancesAlongRoad(AActor& RoadQueryActor, float StartDistanceAlongRoad, float EndDistanceAlongRoad, float TargetSampleDistanceStepSize, const FZoneLaneProfile& LaneProfile, bool bQueryActorIsRoadModule, UZoneShapeComponent*& OutZoneShapeComponent, float* InOutPrevSampleDistance = nullptr, AActor* OverrideZoneShapeComponentOwnerActor = nullptr) const;

	FZoneShapePoint CreateZoneShapePointAtDistanceAlongRoad(const AActor& RoadQueryActor, float DistanceAlongRoad, float TangentLength, bool bQueryActorIsRoadModule) const;
	FZoneLaneProfile CreateDynamicLaneProfile(const AActor& RoadQueryActor, bool bQueryActorIsRoadModule) const;
	FZoneLaneDesc CreateZoneLaneDesc(const float LaneWidth, const EZoneLaneDirection LaneDirection, const TArray<FName>& LaneTagNames) const;
	FName GenerateDynamicLaneProfileName(const FZoneLaneProfile& LaneProfile) const;
	const FZoneLaneProfile* GetLaneProfile(const AActor& RoadQueryActor, bool bQueryActorIsRoadModule) const;
	const FZoneLaneProfile* GetLaneProfileByName(FName LaneProfileName) const;

	// Intersection functions
	bool TryGenerateAndRegisterZoneShapeComponentsForIntersection(AActor& IntersectionQueryActor) const;
	bool TryCreateZoneShapePointForIntersectionEntranceLocation(const AActor& IntersectionQueryActor, int32 ConnectionIndex, UZoneShapeComponent& ZoneShapeComponent, FZoneShapePoint& OutZoneShapePoint) const;
	AActor* GetConnectedRoadActor(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const;

	// Crosswalk functions
	bool TryGenerateAndRegisterZoneShapeComponentsForCrosswalks(AActor& IntersectionQueryActor) const;
	bool TryGenerateAndRegisterZoneShapeComponentsForCrosswalkIntersectionConnectorSegments(AActor& IntersectionQueryActor) const;
	bool TryGenerateAndRegisterZoneShapeComponentsForCrosswalkIntersections(AActor& IntersectionQueryActor) const;
	bool TryCreateZoneShapePointForCrosswalkIntersectionEntranceLocation(const AActor& IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, UZoneShapeComponent& ZoneShapeComponent, FZoneShapePoint& OutZoneShapePoint) const;
	bool TryCreateZoneShapePointForCrosswalkControlPoint(const AActor& IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, FZoneShapePoint& OutZoneShapePoint) const;

	FZoneLaneProfile CreateDynamicLaneProfileForCrosswalk(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const;
	const FZoneLaneProfile* GetLaneProfileForCrosswalk(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const;

	FZoneLaneProfile CreateCrosswalkIntersectionConnectorDynamicLaneProfile(const AActor& IntersectionQueryActor, int32 CrosswalkRoadModuleIndex) const;
	const FZoneLaneProfile* GetCrosswalkIntersectionConnectorLaneProfile(const AActor& IntersectionQueryActor, int32 CrosswalkRoadModuleIndex) const;

	FZoneLaneProfile CreateCrosswalkIntersectionEntranceDynamicLaneProfile(const AActor& IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex) const;
	const FZoneLaneProfile* GetCrosswalkIntersectionEntranceLaneProfile(const AActor& IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex) const;

	// Shared functions
	bool TryRegisterZoneShapeComponentWithActor(AActor& Actor, UZoneShapeComponent& ZoneShapeComponent) const;
	void DestroyZoneShapeComponents(AActor& Actor) const;
	FZoneGraphTag GetTagByName(const FName TagName) const;

	virtual UWorld* GetWorld() const override;

	// Tempo's old modifications to the engine's ZoneGraph saved the lane profiles Tempo generated on
	// AZoneGraphData. An engine that still has them still loads those, and this moves them to the
	// level's lane profile store, where an unmodified engine will find them too once the level is saved.
	void AdoptLaneProfilesSavedOn(const AZoneGraphData* ZoneGraphData);

	// The stored lane profile with the same lanes as LaneProfile, storing it with the level first if there is none.
	const FZoneLaneProfile* FindOrAddDynamicLaneProfile(const FZoneLaneProfile& LaneProfile) const;

	FTempoZoneGraphBuilder TempoZoneGraphBuilder;

	FDelegateHandle OnZoneGraphDataAddedHandle;

	// Copies of the lane profiles handed out while generating zone shapes. Adding a lane profile can
	// move the ones in the ZoneGraph settings, so pointers into those would not stay valid.
	mutable TIndirectArray<FZoneLaneProfile> LaneProfileCache;
};
