// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoIntersectionInterface.h"
#include "MassTrafficControllerRegistrySubsystem.h"

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "TempoIntersectionControlComponent.generated.h"

UCLASS( ClassGroup=(TempoTrafficControl), meta=(BlueprintSpawnableComponent) )
class TEMPOAGENTSSHARED_API UTempoIntersectionControlComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Agents|Intersection Control")
	void SetupTrafficControllers();

	UFUNCTION(BlueprintCallable, Category = "Tempo Agents|Intersection Control")
	void SetupTrafficControllerMeshData();

	UFUNCTION(BlueprintCallable, Category = "Tempo Agents|Intersection Control")
	void SetupTrafficControllerRuntimeData();
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Agents|Intersection Control")
	void DestroyTrafficControllerMeshData();

	UFUNCTION(BlueprintCallable, Category = "Tempo Agents|Intersection Control")
	bool TryGetIntersectionTrafficControllerLocation(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoTrafficControllerType TrafficControllerType, const FTempoTrafficControllerMeshInfo& TrafficControllerMeshInfo, ETempoCoordinateSpace CoordinateSpace, FVector& OutIntersectionTrafficControllerLocation) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Agents|Intersection Control")
	bool TryGetIntersectionTrafficControllerRotation(const AActor* IntersectionQueryActor, int32 ConnectionIndex, ETempoTrafficControllerType TrafficControllerType, const FTempoTrafficControllerMeshInfo& TrafficControllerMeshInfo, ETempoCoordinateSpace CoordinateSpace, FRotator& OutIntersectionTrafficControllerRotation) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Agents|Intersection Control")
	bool TryGetRoadConfigurationInfoForIntersection(const AActor* IntersectionQueryActor, int32 SourceConnectionIndex, ETempoRoadConfigurationDescriptor RoadConfigurationDescriptor, TArray<FTempoRoadConfigurationInfo>& OutRoadConfigurationInfos) const;

	UFUNCTION(BlueprintCallable, Category = "Tempo Agents|Intersection Control")
	bool TryGetStraightestConnectionIndexForIntersection(const AActor* IntersectionQueryActor, int32 SourceConnectionIndex, int32& OutDestConnectionIndex, float& OutConnectionDotProduct) const;

protected:
	
	int32 GetTrafficLightAnchorConnectionIndex(const AActor& IntersectionQueryActor, int32 SourceConnectionIndex, ETempoRoadConfigurationDescriptor& OutRoadConfigurationDescriptor) const;
	FTempoRoadConfigurationInfo GetPrioritizedRoadConfigurationInfo(const AActor& IntersectionQueryActor, int32 SourceConnectionIndex, const TArray<ETempoRoadConfigurationDescriptor>& PrioritizedRoadConfigurationDescriptors, const TFunction<bool(const FTempoRoadConfigurationInfo&, const FTempoRoadConfigurationInfo&)>& SortPredicate) const;

	float GetLateralOffsetFromControlPoint(const AActor& RoadQueryActor, ETempoRoadOffsetOrigin LateralOffsetOrigin, float InLateralOffset) const;

	void SetupTrafficLightRuntimeData(AActor& OwnerActor, UMassTrafficControllerRegistrySubsystem& TrafficControllerRegistrySubsystem);
	void SetupTrafficSignRuntimeData(AActor& OwnerActor, UMassTrafficControllerRegistrySubsystem& TrafficControllerRegistrySubsystem);

	AActor* GetConnectedRoadActor(const AActor& IntersectionQueryActor, int32 ConnectionIndex) const;
	
};
