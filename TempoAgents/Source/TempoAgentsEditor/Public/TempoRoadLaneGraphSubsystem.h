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
	FZoneShapePoint CreateZoneShapePointForRoadControlPoint(const AActor& RoadQueryActor, int32 ControlPointIndex, bool bQueryActorIsRoadModule) const;
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

	// Shared functions
	bool TryRegisterZoneShapeComponentWithActor(AActor& Actor, UZoneShapeComponent& ZoneShapeComponent) const;
	void DestroyZoneShapeComponents(AActor& Actor) const;
	FZoneGraphTag GetTagByName(const FName TagName) const;

	UTempoZoneGraphSettings* GetMutableTempoZoneGraphSettings() const;
	virtual UWorld* GetWorld() const override;

	FTempoZoneGraphBuilder TempoZoneGraphBuilder;
};
