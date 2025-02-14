// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoAgentsSharedTypes.h"

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "TempoCrosswalkInterface.generated.h"

USTRUCT(BlueprintType)
struct FCrosswalkIntersectionConnectorSegmentInfo
{
	GENERATED_BODY()
	
	FCrosswalkIntersectionConnectorSegmentInfo() = default;

	FCrosswalkIntersectionConnectorSegmentInfo(
		float InCrosswalkIntersectionConnectorStartDistance,
		float InCrosswalkIntersectionConnectorEndDistance)
			: CrosswalkIntersectionConnectorStartDistance(InCrosswalkIntersectionConnectorStartDistance)
			, CrosswalkIntersectionConnectorEndDistance(InCrosswalkIntersectionConnectorEndDistance)
	{
	}
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Road Lane Graph|Lane Connection Info")
	float CrosswalkIntersectionConnectorStartDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Road Lane Graph|Lane Connection Info")
	float CrosswalkIntersectionConnectorEndDistance = 0.0f;
};

USTRUCT(BlueprintType)
struct FCrosswalkIntersectionConnectorInfo
{
	GENERATED_BODY()
	
	FCrosswalkIntersectionConnectorInfo() = default;

	FCrosswalkIntersectionConnectorInfo(AActor* InCrosswalkRoadModule)
		: CrosswalkRoadModule(InCrosswalkRoadModule)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Road Lane Graph|Lane Connection Info")
	AActor* CrosswalkRoadModule = nullptr;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Road Lane Graph|Lane Connection Info")
	TArray<FCrosswalkIntersectionConnectorSegmentInfo> CrosswalkIntersectionConnectorSegmentInfos;
};

UINTERFACE(Blueprintable)
class TEMPOAGENTSSHARED_API UTempoCrosswalkInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOAGENTSSHARED_API ITempoCrosswalkInterface
{
	GENERATED_BODY()
	
public:

	// Crosswalk Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	bool ShouldGenerateZoneShapesForTempoCrosswalk(int32 ConnectionIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	int32 GetNumConnectedTempoCrosswalkRoadModules() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	AActor* GetConnectedTempoCrosswalkRoadModuleActor(int32 CrosswalkRoadModuleIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	float GetTempoCrosswalkWidth(int32 ConnectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	TArray<FName> GetTempoCrosswalkTags(int32 ConnectionIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	int32 GetNumTempoCrosswalkLanes(int32 ConnectionIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	int32 GetTempoCrosswalkStartEntranceLocationControlPointIndex(int32 ConnectionIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	int32 GetTempoCrosswalkEndEntranceLocationControlPointIndex(int32 ConnectionIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	FName GetLaneProfileOverrideNameForTempoCrosswalk(int32 ConnectionIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	float GetTempoCrosswalkLaneWidth(int32 ConnectionIndex, int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	EZoneLaneDirection GetTempoCrosswalkLaneDirection(int32 ConnectionIndex, int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	TArray<FName> GetTempoCrosswalkLaneTags(int32 ConnectionIndex, int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	FVector GetTempoCrosswalkControlPointLocation(int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	FVector GetTempoCrosswalkControlPointTangent(int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	FVector GetTempoCrosswalkControlPointUpVector(int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	FVector GetTempoCrosswalkControlPointRightVector(int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	// Crosswalk Intersection Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	int32 GetNumTempoCrosswalkIntersections() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	TArray<FName> GetTempoCrosswalkIntersectionTags(int32 CrosswalkIntersectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	int32 GetNumTempoCrosswalkIntersectionConnections(int32 CrosswalkIntersectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	int32 GetNumTempoCrosswalkIntersectionEntranceLanes(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	FName GetTempoCrosswalkIntersectionEntranceLaneProfileOverrideName(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	float GetTempoCrosswalkIntersectionEntranceLaneWidth(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	EZoneLaneDirection GetTempoCrosswalkIntersectionEntranceLaneDirection(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	TArray<FName> GetTempoCrosswalkIntersectionEntranceLaneTags(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	FVector GetTempoCrosswalkIntersectionEntranceLocation(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	FVector GetTempoCrosswalkIntersectionEntranceTangent(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	FVector GetTempoCrosswalkIntersectionEntranceUpVector(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	FVector GetTempoCrosswalkIntersectionEntranceRightVector(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	// Crosswalk Intersection Connector Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	FCrosswalkIntersectionConnectorInfo GetTempoCrosswalkIntersectionConnectorInfo(int32 CrosswalkRoadModuleIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	int32 GetNumTempoCrosswalkIntersectionConnectorLanes(int32 CrosswalkRoadModuleIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	FName GetTempoCrosswalkIntersectionConnectorLaneProfileOverrideName(int32 CrosswalkRoadModuleIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	float GetTempoCrosswalkIntersectionConnectorLaneWidth(int32 CrosswalkRoadModuleIndex, int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	EZoneLaneDirection GetTempoCrosswalkIntersectionConnectorLaneDirection(int32 CrosswalkRoadModuleIndex, int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	TArray<FName> GetTempoCrosswalkIntersectionConnectorLaneTags(int32 CrosswalkRoadModuleIndex, int32 LaneIndex) const;

	// Crosswalk Indexing Conversion Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	int32 GetTempoCrosswalkIntersectionIndexFromCrosswalkRoadModuleIndex(int32 CrosswalkRoadModuleIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Crosswalk Interface|Queries")
	int32 GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex(int32 CrosswalkIntersectionIndex) const;
};
