// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsSharedTypes.h"
#include "UObject/Interface.h"

#include "TempoRoadQueryInterface.generated.h"

UINTERFACE(Blueprintable)
class TEMPOAGENTSSHARED_API UTempoRoadQueryInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOAGENTSSHARED_API ITempoRoadQueryInterface
{
	GENERATED_BODY()
	
public:

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	int32 GetNumTempoLanes() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	float GetTempoLaneWidth(int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	EZoneLaneDirection GetTempoLaneDirection(int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	TArray<FName> GetTempoLaneTags(int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	TArray<FName> GetTempoLaneConnectionAnyTags(int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	TArray<FName> GetTempoLaneConnectionAllTags(int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	TArray<FName> GetTempoLaneConnectionNotTags(int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	int32 GetNumTempoControlPoints() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	FVector GetTempoControlPointLocation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	FVector GetTempoControlPointTangent(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	FVector GetTempoControlPointUpVector(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	bool IsTempoLaneClosedLoop() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	FName GetTempoLaneProfileOverrideName() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	int32 GetTempoStartEntranceLocationControlPointIndex() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Query Interface")
	int32 GetTempoEndEntranceLocationControlPointIndex() const;
};
