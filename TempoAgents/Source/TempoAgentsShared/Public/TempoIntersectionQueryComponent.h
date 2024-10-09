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
									const AActor* DestConnectionActor, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos, const int32 DestLaneConnectionQueryIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Intersections")
	virtual bool IsConnectedRoadActor(const AActor* RoadQueryActor) const;
	
protected:
	
	virtual bool TryGetNearestRoadControlPointIndex(const AActor& IntersectionQueryActor, int32 ConnectionIndex, int32& OutNearestRoadControlPointIndex) const;
	virtual AActor* GetConnectedRoadActor(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const;

	virtual bool TryGetTagFilteredLaneConnections(const AActor* SourceConnectionActor, const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos,
										  TempoZoneGraphTagFilterMap& OutZoneGraphTagFilterMap) const;

	virtual FZoneGraphTagFilter GetLaneConnectionTagFilter(const AActor* SourceConnectionActor, const FTempoLaneConnectionInfo& SourceLaneConnectionInfo) const;
	virtual FZoneGraphTagMask GenerateTagMaskFromTagNames(const TArray<FName>& TagNames) const;
	
	virtual bool TryGetMinLaneIndexInLaneConnections(const TArray<FTempoLaneConnectionInfo>& LaneConnectionInfos, int32& OutMinLaneIndex) const;
	virtual bool TryGetMaxLaneIndexInLaneConnections(const TArray<FTempoLaneConnectionInfo>& LaneConnectionInfos, int32& OutMaxLaneIndex) const;

	// These return the indices, relative to the start or end of the spline, of a connected road Actor's spline that
	// correspond to the intersection entrance point when that road Actor's spline starts or ends at this intersection.
	virtual int32 GetIntersectionEntranceStartIndex() const;
	virtual int32 GetIntersectionEntranceEndIndex() const;
	
	const AActor* GetOwnerIntersectionQueryActor() const;
};
