// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoAgentsSharedTypes.h"

#include "ZoneGraphBuilder.h"
#include "ZoneShapeUtilities.h"

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "TempoIntersectionInterface.generated.h"

UENUM(BlueprintType)
enum class ETempoRoadConfigurationDescriptor : uint8
{
	ThroughRoad = 0,
	LeftTurn = 1,
	RightTurn = 2,
};

UENUM(BlueprintType)
enum class ETempoTrafficControllerType : uint8
{
	StopSign = 0,
	TrafficLight = 1
};

USTRUCT(BlueprintType)
struct FTempoTrafficControllerMeshInfo
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Traffic Controller|Mesh Info")
	UStaticMesh* TrafficControllerMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Traffic Controller|Mesh Info")
	ETempoRoadOffsetOrigin LateralOffsetOrigin;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Traffic Controller|Mesh Info")
	float LongitudinalOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Traffic Controller|Mesh Info")
	float LateralOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Traffic Controller|Mesh Info")
	float VerticalOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Traffic Controller|Mesh Info")
	float YawOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Traffic Controller|Mesh Info")
	FVector MeshScale = FVector::OneVector;
};

USTRUCT(BlueprintType)
struct FTempoRoadConfigurationInfo
{
	GENERATED_BODY()

	FTempoRoadConfigurationInfo() = default;
	
	FTempoRoadConfigurationInfo(int32 InSourceConnectionIndex, int32 InDestConnectionIndex, float InConnectionDotProduct, ETempoRoadConfigurationDescriptor InRoadConfigurationDescriptor)
		: SourceConnectionIndex(InSourceConnectionIndex)
		, DestConnectionIndex(InDestConnectionIndex)
		, ConnectionDotProduct(InConnectionDotProduct)
		, RoadConfigurationDescriptor(InRoadConfigurationDescriptor)
	{
	}

	bool IsValid() const { return SourceConnectionIndex != InvalidConnectionIndex && DestConnectionIndex != InvalidConnectionIndex; }
	
	static constexpr int32 InvalidConnectionIndex = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo Agents|Road Configuration|Info")
	int32 SourceConnectionIndex = InvalidConnectionIndex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo Agents|Road Configuration|Info")
	int32 DestConnectionIndex = InvalidConnectionIndex;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo Agents|Road Configuration|Info")
	float ConnectionDotProduct = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo Agents|Road Configuration|Info")
	ETempoRoadConfigurationDescriptor RoadConfigurationDescriptor = ETempoRoadConfigurationDescriptor::ThroughRoad;
};

USTRUCT(BlueprintType)
struct FTempoLaneConnectionInfo
{
	GENERATED_BODY()

	FTempoLaneConnectionInfo() = default;

	FTempoLaneConnectionInfo(const FLaneConnectionSlot& Slot, int32 InLaneIndex)
		: Position(Slot.Position)
		, Forward(Slot.Forward)
		, Up(Slot.Up)
		, LaneDesc(Slot.LaneDesc)
		, Restrictions(Slot.Restrictions)
		, LaneIndex(InLaneIndex)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Road Lane Graph|Lane Connection Info")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Road Lane Graph|Lane Connection Info")
	FVector Forward = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Road Lane Graph|Lane Connection Info")
	FVector Up = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Road Lane Graph|Lane Connection Info")
	FZoneLaneDesc LaneDesc;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Road Lane Graph|Lane Connection Info")
	EZoneShapeLaneConnectionRestrictions Restrictions = EZoneShapeLaneConnectionRestrictions::None;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo Agents|Road Lane Graph|Lane Connection Info")
	int32 LaneIndex = -1;
};

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
class TEMPOAGENTSSHARED_API UTempoIntersectionInterface : public UInterface
{
	GENERATED_BODY()
};

class TEMPOAGENTSSHARED_API ITempoIntersectionInterface
{
	GENERATED_BODY()
	
public:

	// Connected Road Queries
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetNumTempoConnections() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	AActor* GetConnectedTempoRoadActor(int32 ConnectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	TArray<FName> GetTempoIntersectionTags() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetTempoIntersectionEntranceControlPointIndex(int32 ConnectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoIntersectionEntranceLocation(int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoIntersectionEntranceTangent(int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoIntersectionEntranceUpVector(int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoIntersectionEntranceRightVector(int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	TArray<FTempoRoadConfigurationInfo> GetTempoRoadConfigurationInfo(int32 SourceConnectionIndex, ETempoRoadConfigurationDescriptor RoadConfigurationDescriptor) const;

	// Connected Road Module Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetNumConnectedTempoRoadModules() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	AActor* GetConnectedTempoRoadModuleActor(int32 ConnectedRoadModuleIndex) const;

	// Crosswalk Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	bool ShouldGenerateZoneShapesForTempoCrosswalk(int32 ConnectionIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetNumConnectedTempoCrosswalkRoadModules() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	AActor* GetConnectedTempoCrosswalkRoadModuleActor(int32 CrosswalkRoadModuleIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	float GetTempoCrosswalkWidth(int32 ConnectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	TArray<FName> GetTempoCrosswalkTags(int32 ConnectionIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetNumTempoCrosswalkLanes(int32 ConnectionIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetTempoCrosswalkStartEntranceLocationControlPointIndex(int32 ConnectionIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetTempoCrosswalkEndEntranceLocationControlPointIndex(int32 ConnectionIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FName GetLaneProfileOverrideNameForTempoCrosswalk(int32 ConnectionIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	float GetTempoCrosswalkLaneWidth(int32 ConnectionIndex, int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	EZoneLaneDirection GetTempoCrosswalkLaneDirection(int32 ConnectionIndex, int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	TArray<FName> GetTempoCrosswalkLaneTags(int32 ConnectionIndex, int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoCrosswalkControlPointLocation(int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoCrosswalkControlPointTangent(int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoCrosswalkControlPointUpVector(int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoCrosswalkControlPointRightVector(int32 ConnectionIndex, int32 CrosswalkControlPointIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	// Crosswalk Intersection Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetNumTempoCrosswalkIntersections() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	TArray<FName> GetTempoCrosswalkIntersectionTags(int32 CrosswalkIntersectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetNumTempoCrosswalkIntersectionConnections(int32 CrosswalkIntersectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetNumTempoCrosswalkIntersectionEntranceLanes(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FName GetTempoCrosswalkIntersectionEntranceLaneProfileOverrideName(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	float GetTempoCrosswalkIntersectionEntranceLaneWidth(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	EZoneLaneDirection GetTempoCrosswalkIntersectionEntranceLaneDirection(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	TArray<FName> GetTempoCrosswalkIntersectionEntranceLaneTags(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, int32 LaneIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoCrosswalkIntersectionEntranceLocation(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoCrosswalkIntersectionEntranceTangent(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoCrosswalkIntersectionEntranceUpVector(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoCrosswalkIntersectionEntranceRightVector(int32 CrosswalkIntersectionIndex, int32 CrosswalkIntersectionConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	// Crosswalk Intersection Connector Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FCrosswalkIntersectionConnectorInfo GetTempoCrosswalkIntersectionConnectorInfo(int32 CrosswalkRoadModuleIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetNumTempoCrosswalkIntersectionConnectorLanes(int32 CrosswalkRoadModuleIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FName GetTempoCrosswalkIntersectionConnectorLaneProfileOverrideName(int32 CrosswalkRoadModuleIndex) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	float GetTempoCrosswalkIntersectionConnectorLaneWidth(int32 CrosswalkRoadModuleIndex, int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	EZoneLaneDirection GetTempoCrosswalkIntersectionConnectorLaneDirection(int32 CrosswalkRoadModuleIndex, int32 LaneIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	TArray<FName> GetTempoCrosswalkIntersectionConnectorLaneTags(int32 CrosswalkRoadModuleIndex, int32 LaneIndex) const;

	// Crosswalk Indexing Conversion Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetTempoCrosswalkIntersectionIndexFromCrosswalkRoadModuleIndex(int32 CrosswalkRoadModuleIndex) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	int32 GetTempoCrosswalkRoadModuleIndexFromCrosswalkIntersectionIndex(int32 CrosswalkIntersectionIndex) const;

	// Lane Filtering Queries

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	bool ShouldFilterTempoLaneConnection(const AActor* SourceConnectionActor, const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos, const int32 SourceSlotQueryIndex, const AActor* DestConnectionActor, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos, const int32 DestSlotQueryIndex) const;

	// Traffic Controller Queries
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	ETempoTrafficControllerType GetTempoTrafficControllerType() const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FTempoTrafficControllerMeshInfo GetTempoTrafficControllerMeshInfo(int32 ConnectionIndex, ETempoTrafficControllerType TrafficControllerType) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoTrafficControllerLocation(int32 ConnectionIndex, ETempoTrafficControllerType TrafficControllerType, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FRotator GetTempoTrafficControllerRotation(int32 ConnectionIndex, ETempoTrafficControllerType TrafficControllerType, ETempoCoordinateSpace CoordinateSpace) const;

	// Traffic Controller Commands

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Commands")
	void SetupTempoTrafficControllers();
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Commands")
	void SetupTempoIntersectionData();
};
