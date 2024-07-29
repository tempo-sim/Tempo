// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsSharedTypes.h"
#include "UObject/Interface.h"

#include "TempoIntersectionQueryInterface.generated.h"

UINTERFACE(Blueprintable)
class TEMPOAGENTSSHARED_API UTempoIntersectionQueryInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOAGENTSSHARED_API ITempoIntersectionQueryInterface
{
	GENERATED_BODY()
	
public:
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Query Interface")
	int32 GetNumTempoConnections() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Query Interface")
	AActor* GetConnectedTempoRoadActor(int32 ConnectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Query Interface")
	TArray<FName> GetTempoIntersectionTags() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Query Interface")
	FVector GetTempoIntersectionEntranceLocation(int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Query Interface")
	FVector GetTempoIntersectionEntranceTangent(int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Query Interface")
	FVector GetTempoIntersectionEntranceUpVector(int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;
};
