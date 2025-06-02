// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsSharedTypes.h"
#include "UObject/Interface.h"

#include "TempoRoadInterface.generated.h"

class USplineComponent;

UINTERFACE(BlueprintType)
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
	void TestVoidFunction(bool& ReturnVar) const;
	virtual void TestVoidFunction_Implementation(bool& ReturnVar) const;

	UFUNCTION(BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	bool ShouldGenerateZoneShapesForTempoRoad() const;
	virtual bool ShouldGenerateZoneShapesForTempoRoad_Implementation() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	USplineComponent* GetRoadSpline() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	float GetTempoRoadWidth() const;
	float GetTempoRoadWidth_Implementation() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	float GetTempoRoadShoulderWidth(ETempoRoadLateralDirection LateralSide) const;
	float GetTempoRoadShoulderWidth_Implementation(ETempoRoadLateralDirection LateralSide) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	bool IsTempoRoadClosedLoop() const;
	bool IsTempoRoadClosedLoop_Implementation() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FName GetTempoLaneProfileOverrideName() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	float GetTempoRoadSampleDistanceStepSize() const;
	float GetTempoRoadSampleDistanceStepSize_Implementation() const;

	// Road Lane Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	int32 GetNumTempoLanes() const;
	int32 GetNumTempoLanes_Implementation() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	float GetTempoLaneWidth(int32 LaneIndex) const;
	float GetTempoLaneWidth_Implementation(int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	EZoneLaneDirection GetTempoLaneDirection(int32 LaneIndex) const;
	EZoneLaneDirection GetTempoLaneDirection_Implementation(int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	TArray<FName> GetTempoLaneTags(int32 LaneIndex) const;
	TArray<FName> GetTempoLaneTags_Implementation(int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	TArray<FName> GetTempoLaneConnectionAnyTags(int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	TArray<FName> GetTempoLaneConnectionAllTags(int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	TArray<FName> GetTempoLaneConnectionNotTags(int32 LaneIndex) const;

	// Road Control Point Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	int32 GetNumTempoControlPoints() const;
	int32 GetNumTempoControlPoints_Implementation() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetTempoControlPointLocation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	FVector GetTempoControlPointLocation_Implementation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetTempoControlPointTangent(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	FVector GetTempoControlPointTangent_Implementation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetTempoControlPointUpVector(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	FVector GetTempoControlPointUpVector_Implementation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetTempoControlPointRightVector(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	FVector GetTempoControlPointRightVector_Implementation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FRotator GetTempoControlPointRotation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	FRotator GetTempoControlPointRotation_Implementation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	float GetDistanceAlongTempoRoadAtControlPoint(int32 ControlPointIndex) const;
	float GetDistanceAlongTempoRoadAtControlPoint_Implementation(int32 ControlPointIndex) const;

	// Road Distance Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	float GetDistanceAlongTempoRoadClosestToWorldLocation(FVector QueryLocation) const;
	float GetDistanceAlongTempoRoadClosestToWorldLocation_Implementation(FVector QueryLocation) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetLocationAtDistanceAlongTempoRoad(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;
	FVector GetLocationAtDistanceAlongTempoRoad_Implementation(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetTangentAtDistanceAlongTempoRoad(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;
	FVector GetTangentAtDistanceAlongTempoRoad_Implementation(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetUpVectorAtDistanceAlongTempoRoad(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;
	FVector GetUpVectorAtDistanceAlongTempoRoad_Implementation(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FVector GetRightVectorAtDistanceAlongTempoRoad(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;
	FVector GetRightVectorAtDistanceAlongTempoRoad_Implementation(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FRotator GetRotationAtDistanceAlongTempoRoad(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;
	FRotator GetRotationAtDistanceAlongTempoRoad_Implementation(float DistanceAlongRoad, ETempoCoordinateSpace CoordinateSpace) const;

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
