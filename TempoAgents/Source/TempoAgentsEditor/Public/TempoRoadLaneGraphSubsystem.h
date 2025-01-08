// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "ZoneGraphTypes.h"
#include "GameFramework/Actor.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "TempoZoneGraphSettings.h"
#include "TempoZoneGraphBuilder.h"
#include "TempoRoadLaneGraphSubsystem.generated.h"

UCLASS()
class TEMPOAGENTSEDITOR_API UTempoRoadLaneGraphSubsystem : public UUnrealEditorSubsystem
{
	GENERATED_BODY()

public:

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
	bool TryWriteDynamicLaneProfile(const FZoneLaneProfile& LaneProfile) const;
	bool TryGetDynamicLaneProfileFromSettings(const FZoneLaneProfile& InLaneProfile, FZoneLaneProfile& OutLaneProfileFromSettings) const;
	FZoneLaneProfile GetLaneProfile(const AActor& RoadQueryActor, bool bQueryActorIsRoadModule) const;
	FZoneLaneProfile GetLaneProfileByName(FName LaneProfileName) const;

	// Intersection functions
	bool TryGenerateAndRegisterZoneShapeComponentsForIntersection(AActor& IntersectionQueryActor) const;
	bool TryCreateZoneShapePointForIntersectionEntranceLocation(const AActor& IntersectionQueryActor, int32 ConnectionIndex, UZoneShapeComponent& ZoneShapeComponent, FZoneShapePoint& OutZoneShapePoint) const;
	AActor* GetConnectedRoadActor(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const;
	
	// Crosswalk functions
	bool TryGenerateAndRegisterZoneShapeComponentsForCrosswalksAtIntersections(AActor& IntersectionQueryActor) const;
	bool TryGenerateAndRegisterZoneShapeComponentsForCrosswalkIntersectionConnectorSegments(AActor& IntersectionQueryActor) const;
	bool TryGenerateAndRegisterZoneShapeComponentsForCrosswalkIntersections(AActor& IntersectionQueryActor) const;
	bool TryCreateZoneShapePointForCrosswalkIntersectionEntranceLocation(const AActor& IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, UZoneShapeComponent& ZoneShapeComponent, FZoneShapePoint& OutZoneShapePoint) const;
	bool TryCreateZoneShapePointForCrosswalkControlPoint(const AActor& IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, FZoneShapePoint& OutZoneShapePoint) const;

	FZoneLaneProfile CreateDynamicLaneProfileForCrosswalk(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const;
	FZoneLaneProfile GetLaneProfileForCrosswalk(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const;
	
	FZoneLaneProfile CreateCrosswalkIntersectionConnectorDynamicLaneProfile(const AActor& IntersectionQueryActor, int32 CrosswalkRoadModuleIndex) const;
	FZoneLaneProfile GetCrosswalkIntersectionConnectorLaneProfile(const AActor& IntersectionQueryActor, int32 CrosswalkRoadModuleIndex) const;
	
	FZoneLaneProfile CreateCrosswalkIntersectionEntranceDynamicLaneProfile(const AActor& IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex) const;
	FZoneLaneProfile GetCrosswalkIntersectionEntranceLaneProfile(const AActor& IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex) const;

	// Shared functions
	bool TryRegisterZoneShapeComponentWithActor(AActor& Actor, UZoneShapeComponent& ZoneShapeComponent) const;
	void DestroyZoneShapeComponents(AActor& Actor) const;
	FZoneGraphTag GetTagByName(const FName TagName) const;

	UTempoZoneGraphSettings* GetMutableTempoZoneGraphSettings() const;
	virtual UWorld* GetWorld() const override;

	FTempoZoneGraphBuilder TempoZoneGraphBuilder;
};
