// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TempoRoadQueryComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOAGENTSSHARED_API UTempoRoadQueryComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionConnectorInfo(const AActor* RoadQueryActor, int32 CrosswalkRoadModuleIndex, FCrosswalkIntersectionConnectorInfo& OutCrosswalkIntersectionConnectorInfo) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGenerateCrosswalkRoadModuleMap(const AActor* RoadQueryActor, const TArray<FName>& CrosswalkRoadModuleAnyTags, const TArray<FName>& CrosswalkRoadModuleAllTags, const TArray<FName>& CrosswalkRoadModuleNotTags, TMap<int32, AActor*>& OutCrosswalkRoadModuleMap) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionConnectorInfoEntry(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, AActor*& OutCrosswalkRoadModule, float &OutCrosswalkIntersectionConnectorDistance) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionEntranceLocation(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceLocation) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionEntranceTangent(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceTangent) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionEntranceUpVector(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceUpVector) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionEntranceRightVector(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceRightVector) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetNumCrosswalkIntersectionEntranceLanes(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, int32& OutNumCrosswalkIntersectionEntranceLanes) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionEntranceLaneProfileOverrideName(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FName& OutCrosswalkIntersectionEntranceLaneProfileOverrideName) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionEntranceLaneWidth(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, float& OutCrosswalkIntersectionEntranceLaneWidth) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionEntranceLaneDirection(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, EZoneLaneDirection& OutCrosswalkIntersectionEntranceLaneDirection) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionEntranceLaneTags(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, TArray<FName>& OutCrosswalkIntersectionEntranceLaneTags) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetNumCrosswalkIntersectionConnectorLanes(const AActor* RoadQueryActor, int32 CrosswalkRoadModuleIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, int32& OutNumCrosswalkIntersectionConnectorLanes) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName(const AActor* RoadQueryActor, int32 CrosswalkRoadModuleIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FName& OutCrosswalkIntersectionConnectorLaneProfileOverrideName) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionConnectorLaneWidth(const AActor* RoadQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, float& OutCrosswalkIntersectionConnectorLaneWidth) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionConnectorLaneDirection(const AActor* RoadQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, EZoneLaneDirection& OutCrosswalkIntersectionConnectorLaneDirection) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual bool TryGetCrosswalkIntersectionConnectorLaneTags(const AActor* RoadQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, TArray<FName>& OutCrosswalkIntersectionConnectorLaneTags) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Roads")
	virtual int32 GetCrosswalkIndexFromCrosswalkIntersectionIndex(const AActor* RoadQueryActor, int32 CrosswalkIntersectionIndex) const;
};
