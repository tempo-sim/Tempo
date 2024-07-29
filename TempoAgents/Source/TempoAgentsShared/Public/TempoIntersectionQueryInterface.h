// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsSharedTypes.h"
#include "UObject/Interface.h"
#include "ZoneGraphBuilder.h"

#include "TempoIntersectionQueryInterface.generated.h"

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

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Tempo Agents|Intersection Query Interface")
	bool ShouldFilterTempoLaneConnection(const AActor* SourceConnectionActor, const TArray<FTempoLaneConnectionInfo>& SourceLaneConnectionInfos, const int32 SourceSlotQueryIndex, const AActor* DestConnectionActor, const TArray<FTempoLaneConnectionInfo>& DestLaneConnectionInfos, const int32 DestSlotQueryIndex) const;
};
