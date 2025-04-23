// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsSharedTypes.h"
#include "UObject/Interface.h"

#include "TempoRoadInterface.generated.h"

UINTERFACE(Blueprintable)
class TEMPOAGENTSSHARED_API UTempoRoadInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOAGENTSSHARED_API ITempoRoadInterface
{
	GENERATED_BODY()
	
public:

	// Road General Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	bool ShouldGenerateZoneShapesForTempoRoad() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	float GetTempoRoadWidth() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	float GetTempoRoadShoulderWidth(ETempoRoadLateralDirection LateralSide) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	bool IsTempoLaneClosedLoop() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FName GetTempoLaneProfileOverrideName() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	float GetTempoRoadSampleDistanceStepSize() const;

	// Road Lane Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	int32 GetNumTempoLanes() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	float GetTempoLaneWidth(int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	EZoneLaneDirection GetTempoLaneDirection(int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	TArray<FName> GetTempoLaneTags(int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	TArray<FName> GetTempoLaneConnectionAnyTags(int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	TArray<FName> GetTempoLaneConnectionAllTags(int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	TArray<FName> GetTempoLaneConnectionNotTags(int32 LaneIndex) const;

	// Road Control Point Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	int32 GetNumTempoControlPoints() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetTempoControlPointLocation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetTempoControlPointTangent(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetTempoControlPointUpVector(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetTempoControlPointRightVector(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FRotator GetTempoControlPointRotation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	float GetDistanceAlongTempoRoadAtControlPoint(int32 ControlPointIndex) const;

	// Road Distance Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	float GetDistanceAlongTempoRoadClosestToWorldLocation(FVector QueryLocation) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetLocationAtDistanceAlongTempoRoad(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetTangentAtDistanceAlongTempoRoad(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetUpVectorAtDistanceAlongTempoRoad(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetRightVectorAtDistanceAlongTempoRoad(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FRotator GetRotationAtDistanceAlongTempoRoad(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;

	// Road Intersection Entrance Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	int32 GetTempoStartEntranceLocationControlPointIndex() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	int32 GetTempoEndEntranceLocationControlPointIndex() const;

	// Parking Location Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	bool ShouldSpawnParkedVehiclesForTempoRoad() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	int32 GetMaxAllowedParkingSpawnPointsOnTempoRoad() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	TSet<TSoftObjectPtr<UMassEntityConfigAsset>> GetEntityTypesAllowedToParkOnTempoRoad() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	TArray<FName> GetTempoParkingLocationAnchorNames() const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetTempoParkingLocation(const FName ParkingLocationAnchorName, const float NormalizedDistanceAlongRoad, const float HalfWidth, const float NormalizedLateralVariationScalar, const ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FRotator GetTempoParkingRotation(const FName ParkingLocationAnchorName, const float NormalizedDistanceAlongRoad, const float NormalizedAngularVariationScalar, const ETempoCoordinateSpace CoordinateSpace) const;

	// Connected Road Module Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	int32 GetNumConnectedTempoRoadModules() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	AActor* GetConnectedTempoRoadModuleActor(int32 ConnectedRoadModuleIndex) const;
};
