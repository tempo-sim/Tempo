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
	bool IsTempoLaneClosedLoop() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	FName GetTempoLaneProfileOverrideName() const;

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

	// Road Intersection Entrance Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	int32 GetTempoStartEntranceLocationControlPointIndex() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Interface|Queries")
	int32 GetTempoEndEntranceLocationControlPointIndex() const;
};
