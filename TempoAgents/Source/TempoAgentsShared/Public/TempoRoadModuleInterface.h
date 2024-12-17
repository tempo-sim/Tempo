// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsSharedTypes.h"
#include "UObject/Interface.h"

#include "TempoRoadModuleInterface.generated.h"

UINTERFACE(Blueprintable)
class TEMPOAGENTSSHARED_API UTempoRoadModuleInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOAGENTSSHARED_API ITempoRoadModuleInterface
{
	GENERATED_BODY()
	
public:

	// Road Module General Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	bool ShouldGenerateZoneShapesForTempoRoadModule() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	float GetTempoRoadModuleWidth() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	float GetTempoRoadModuleLength() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	bool IsTempoRoadModuleClosedLoop() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	FName GetTempoRoadModuleLaneProfileOverrideName() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	AActor* GetTempoRoadModuleParentActor() const;

	// Road Module Lane Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	int32 GetNumTempoRoadModuleLanes() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	float GetTempoRoadModuleLaneWidth(int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	EZoneLaneDirection GetTempoRoadModuleLaneDirection(int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	TArray<FName> GetTempoRoadModuleLaneTags(int32 LaneIndex) const;

	// Road Module Control Point Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	int32 GetNumTempoRoadModuleControlPoints() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	FVector GetTempoRoadModuleControlPointLocation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	FVector GetTempoRoadModuleControlPointTangent(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	FVector GetTempoRoadModuleControlPointUpVector(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	FVector GetTempoRoadModuleControlPointRightVector(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	FRotator GetTempoRoadModuleControlPointRotation(int32 ControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	float GetDistanceAlongTempoRoadModuleAtControlPoint(int32 ControlPointIndex) const;

	// Road Module Distance Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	float GetDistanceAlongTempoRoadModuleClosestToWorldLocation(FVector QueryLocation) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	FVector GetLocationAtDistanceAlongTempoRoadModule(float DistanceAlongRoadModule, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	FVector GetTangentAtDistanceAlongTempoRoadModule(float DistanceAlongRoadModule, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	FVector GetUpVectorAtDistanceAlongTempoRoadModule(float DistanceAlongRoadModule, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	FVector GetRightVectorAtDistanceAlongTempoRoadModule(float DistanceAlongRoadModule, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Road Module Interface|Queries")
	FRotator GetRotationAtDistanceAlongTempoRoadModule(float DistanceAlongRoadModule, ETempoCoordinateSpace CoordinateSpace) const;
};
