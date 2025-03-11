// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ZoneGraphTypes.h"
#include "TempoCrosswalkInterface.h"
#include "TempoIntersectionInterface.h"
#include "TempoIntersectionQueryComponent.generated.h"

using TempoZoneGraphTagMaskGroups = TMap<FZoneGraphTagMask, TArray<FTempoLaneConnectionInfo>>;

UCLASS( ClassGroup=(TempoIntersections), meta = (BlueprintSpawnableComponent))
class TEMPOAGENTSSHARED_API UTempoIntersectionQueryComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetIntersectionEntranceControlPointIndex(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32& OutIntersectionEntranceControlPointIndex) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetIntersectionEntranceLocation(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionEntranceLocation) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetIntersectionEntranceTangent(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionEntranceTangent) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetIntersectionEntranceUpVector(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionEntranceUpVector) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetIntersectionEntranceRightVector(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionEntranceRightVector) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool ShouldFilterLaneConnection(const AActor* SourceConnectionActor, const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos, const int32 SourceLaneConnectionQueryIndex,
									const AActor* DestConnectionActor, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos, const int32 DestLaneConnectionQueryIndex, const TArray<FLaneConnectionCandidate>& AllCandidates) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionConnectorInfo(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, FCrosswalkIntersectionConnectorInfo& OutCrosswalkIntersectionConnectorInfo) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGenerateCrosswalkRoadModuleMap(const AActor* IntersectionQueryActor, const TArray<FName>& CrosswalkRoadModuleAnyTags, const TArray<FName>& CrosswalkRoadModuleAllTags, const TArray<FName>& CrosswalkRoadModuleNotTags, TMap<int32, AActor*>& OutCrosswalkRoadModuleMap) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionConnectorInfoEntry(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, AActor*& OutCrosswalkRoadModule, float &OutCrosswalkIntersectionConnectorDistance) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceLocation(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceLocation) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceTangent(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceTangent) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceUpVector(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceUpVector) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceRightVector(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceRightVector) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetNumCrosswalkIntersectionEntranceLanes(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, int32& OutNumCrosswalkIntersectionEntranceLanes) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceLaneProfileOverrideName(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FName& OutCrosswalkIntersectionEntranceLaneProfileOverrideName) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceLaneWidth(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, float& OutCrosswalkIntersectionEntranceLaneWidth) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceLaneDirection(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, EZoneLaneDirection& OutCrosswalkIntersectionEntranceLaneDirection) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceLaneTags(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, TArray<FName>& OutCrosswalkIntersectionEntranceLaneTags) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetNumCrosswalkIntersectionConnectorLanes(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, int32& OutNumCrosswalkIntersectionConnectorLanes) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FName& OutCrosswalkIntersectionConnectorLaneProfileOverrideName) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionConnectorLaneWidth(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, float& OutCrosswalkIntersectionConnectorLaneWidth) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionConnectorLaneDirection(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, EZoneLaneDirection& OutCrosswalkIntersectionConnectorLaneDirection) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionConnectorLaneTags(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, TArray<FName>& OutCrosswalkIntersectionConnectorLaneTags) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkControlPointLocation(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutCrosswalkControlPointLocation) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkControlPointTangent(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutCrosswalkControlPointTangent) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkControlPointUpVector(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutCrosswalkControlPointUpVector) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkControlPointRightVector(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutCrosswalkControlPointRightVector) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionConnectorInfo(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, FCrosswalkIntersectionConnectorInfo& OutCrosswalkIntersectionConnectorInfo) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGenerateCrosswalkRoadModuleMap(const AActor* IntersectionQueryActor, const TArray<FName>& CrosswalkRoadModuleAnyTags, const TArray<FName>& CrosswalkRoadModuleAllTags, const TArray<FName>& CrosswalkRoadModuleNotTags, TMap<int32, AActor*>& OutCrosswalkRoadModuleMap) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionConnectorInfoEntry(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, AActor*& OutCrosswalkRoadModule, float &OutCrosswalkIntersectionConnectorDistance) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceLocation(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceLocation) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceTangent(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceTangent) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceUpVector(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceUpVector) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceRightVector(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FVector& OutCrosswalkIntersectionEntranceRightVector) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetNumCrosswalkIntersectionEntranceLanes(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, int32& OutNumCrosswalkIntersectionEntranceLanes) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceLaneProfileOverrideName(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FName& OutCrosswalkIntersectionEntranceLaneProfileOverrideName) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceLaneWidth(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, float& OutCrosswalkIntersectionEntranceLaneWidth) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceLaneDirection(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, EZoneLaneDirection& OutCrosswalkIntersectionEntranceLaneDirection) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionEntranceLaneTags(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, TArray<FName>& OutCrosswalkIntersectionEntranceLaneTags) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetNumCrosswalkIntersectionConnectorLanes(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, int32& OutNumCrosswalkIntersectionConnectorLanes) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionConnectorLaneProfileOverrideName(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, FName& OutCrosswalkIntersectionConnectorLaneProfileOverrideName) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionConnectorLaneWidth(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, float& OutCrosswalkIntersectionConnectorLaneWidth) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionConnectorLaneDirection(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, EZoneLaneDirection& OutCrosswalkIntersectionConnectorLaneDirection) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkIntersectionConnectorLaneTags(const AActor* IntersectionQueryActor, int32 CrosswalkRoadModuleIndex, int32 LaneIndex, const TMap<int32, FCrosswalkIntersectionConnectorInfo>& CrosswalkIntersectionConnectorInfoMap, TArray<FName>& OutCrosswalkIntersectionConnectorLaneTags) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkControlPointLocation(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutCrosswalkControlPointLocation) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkControlPointTangent(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutCrosswalkControlPointTangent) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkControlPointUpVector(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutCrosswalkControlPointUpVector) const;
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCrosswalkControlPointRightVector(const AActor* IntersectionQueryActor, int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutCrosswalkControlPointRightVector) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool IsConnectedRoadActor(const AActor* RoadQueryActor) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual int32 GetConnectionIndexFromCrosswalkIntersectionIndex(const AActor* IntersectionQueryActor, int32 CrosswalkIntersectionIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool TryGetCanonicalStartingConnectionIndex(const AActor* IntersectionQueryActor, int32& OutStartingConnectionIndex) const;
	
protected:
	
	virtual bool TryGetNearestRoadControlPointIndex(const AActor& IntersectionQueryActor, int32 ConnectionIndex, int32& OutNearestRoadControlPointIndex) const;
	virtual AActor* GetConnectedRoadActor(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const;
	
	virtual TempoZoneGraphTagMaskGroups GroupLaneConnectionsByTags(const AActor* SourceConnectionActor, const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos) const;

	virtual FZoneGraphTagFilter GetLaneConnectionTagFilter(const AActor* SourceConnectionActor, const FTempoLaneConnectionInfo& SourceLaneConnectionInfo) const;

	virtual bool TryGetMinLaneIndexInLaneConnections(const TArray<FTempoLaneConnectionInfo>& LaneConnectionInfos, int32& OutMinLaneIndex) const;
	virtual bool TryGetMaxLaneIndexInLaneConnections(const TArray<FTempoLaneConnectionInfo>& LaneConnectionInfos, int32& OutMaxLaneIndex) const;

	// These return the indices, relative to the start or end of the spline, of a connected road Actor's spline that
	// correspond to the intersection entrance point when that road Actor's spline starts or ends at this intersection.
	virtual int32 GetIntersectionEntranceStartOffsetIndex() const;
	virtual int32 GetIntersectionEntranceEndOffsetIndex() const;

	const AActor* GetOwnerIntersectionQueryActor() const;
};
