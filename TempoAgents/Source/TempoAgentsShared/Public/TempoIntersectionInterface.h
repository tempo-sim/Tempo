// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsSharedTypes.h"
#include "UObject/Interface.h"
#include "ZoneGraphBuilder.h"

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
	FVector GetTempoIntersectionEntranceLocation(int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoIntersectionEntranceTangent(int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoIntersectionEntranceUpVector(int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	FVector GetTempoIntersectionEntranceRightVector(int32 ConnectionIndex, ETempoCoordinateSpace CoordinateSpace) const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Interface|Queries")
	TArray<FTempoRoadConfigurationInfo> GetTempoRoadConfigurationInfo(int32 SourceConnectionIndex, ETempoRoadConfigurationDescriptor RoadConfigurationDescriptor) const;

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
};
