// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ZoneGraphTypes.h"
#include "TempoIntersectionInterface.h"
#include "TempoIntersectionQueryComponent.generated.h"

using TempoZoneGraphTagFilterMap = TMap<FZoneGraphTagFilter, TTuple<TArray<FTempoLaneConnectionInfo>, TArray<FTempoLaneConnectionInfo>>>;

UCLASS( ClassGroup=(TempoIntersections), meta = (BlueprintSpawnableComponent))
class TEMPOAGENTSSHARED_API UTempoIntersectionQueryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	bool TryGetIntersectionEntranceLocation(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionEntranceLocation) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	bool TryGetIntersectionEntranceTangent(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionEntranceTangent) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	bool TryGetIntersectionEntranceUpVector(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionEntranceUpVector) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	bool TryGetIntersectionEntranceRightVector(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionEntranceRightVector) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	bool ShouldFilterLaneConnection(const AActor* SourceConnectionActor, const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos, const int32 SourceLaneConnectionQueryIndex,
									const AActor* DestConnectionActor, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos, const int32 DestLaneConnectionQueryIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	bool IsConnectedRoadActor(const AActor* RoadQueryActor) const;
	
protected:

	bool TryGetIntersectionEntranceControlPointIndex(const AActor& IntersectionQueryActor, int32 ConnectionIndex, int32& OutIntersectionEntranceControlPointIndex) const;
	bool TryGetNearestRoadControlPointIndex(const AActor& IntersectionQueryActor, int32 ConnectionIndex, int32& OutNearestRoadControlPointIndex) const;
	AActor* GetConnectedRoadActor(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const;

	bool TryGetTagFilteredLaneConnections(const AActor* SourceConnectionActor, const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos,
										  TempoZoneGraphTagFilterMap& OutZoneGraphTagFilterMap) const;

	FZoneGraphTagFilter GetLaneConnectionTagFilter(const AActor* SourceConnectionActor, const FTempoLaneConnectionInfo& SourceLaneConnectionInfo) const;
	FZoneGraphTagMask GenerateTagMaskFromTagNames(const TArray<FName>& TagNames) const;
	
	bool TryGetMinLaneIndexInLaneConnections(const TArray<FTempoLaneConnectionInfo>& LaneConnectionInfos, int32& OutMinLaneIndex) const;
	bool TryGetMaxLaneIndexInLaneConnections(const TArray<FTempoLaneConnectionInfo>& LaneConnectionInfos, int32& OutMaxLaneIndex) const;
	
	const AActor* GetOwnerIntersectionQueryActor() const;
};
