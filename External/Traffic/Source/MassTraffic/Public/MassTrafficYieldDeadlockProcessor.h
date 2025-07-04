// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficYieldDeadlockProcessor.generated.h"

struct FYieldCycleNode
{
	FYieldCycleNode() = default;
	
	FYieldCycleNode(const FLaneEntityPair& InLaneEntityPair)
		: LaneEntityPair(InLaneEntityPair)
	{
	}

	FLaneEntityPair LaneEntityPair;
	bool bHasImplicitYieldFromPrevNode = false;

	bool operator==(const FYieldCycleNode& Other) const
	{
		return LaneEntityPair == Other.LaneEntityPair
			&& bHasImplicitYieldFromPrevNode == Other.bHasImplicitYieldFromPrevNode;
	}

	friend uint32 GetTypeHash(const FYieldCycleNode& YieldCycleNode)
	{
		uint32 Hash = GetTypeHash(YieldCycleNode.LaneEntityPair);
		
		Hash = HashCombine(Hash, GetTypeHash(YieldCycleNode.bHasImplicitYieldFromPrevNode));
			
		return Hash;
	}
};

UCLASS()
class MASSTRAFFIC_API UMassTrafficYieldDeadlockFrameInitProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficYieldDeadlockFrameInitProcessor();

protected:
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};


UCLASS()
class MASSTRAFFIC_API UMassTrafficYieldDeadlockResolutionProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficYieldDeadlockResolutionProcessor();

protected:
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	virtual void ConfigureQueries() override;
#else
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#endif
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	void CleanupStaleYieldOverrides(UMassTrafficSubsystem& MassTrafficSubsystem, const FMassEntityManager& EntityManager) const;

	TArray<FYieldCycleNode> FindYieldCycleNodes(
		const UMassTrafficSubsystem& MassTrafficSubsystem,
		const FMassEntityManager& EntityManager,
		const TSet<FMassEntityHandle>& VehicleEntities) const;

	int32 GetNumEntitiesInYieldCycle(
		const TArray<FYieldCycleNode>& YieldCycleNodes,
		const TSet<FMassEntityHandle>& VehicleEntities,
		const TSet<FMassEntityHandle>& PedestrianEntities,
		int32* OutNumLanesInYieldCycle = nullptr,
		int32* OutNumVehicleEntitiesInYieldCycle = nullptr,
		int32* OutNumPedestrianEntitiesInYieldCycle = nullptr) const;

	TArray<int32> GetYieldOverrideCandidateIndexes(
		const TArray<FYieldCycleNode>& YieldCycleNodes,
		const TSet<FMassEntityHandle>& VehicleEntities,
		const TSet<FMassEntityHandle>& PedestrianEntities) const;

	int32 GetSelectedYieldOverrideCandidateIndex(
		const TArray<FYieldCycleNode>& YieldCycleNodes,
		const FMassEntityManager& EntityManager,
		const TArray<int32>& YieldOverrideCandidateIndexes) const;

	void DrawDebugYieldMap(
		const UMassTrafficSubsystem& MassTrafficSubsystem,
		const UZoneGraphSubsystem& ZoneGraphSubsystem,
		const FMassEntityManager& EntityManager,
		const TSet<FMassEntityHandle>& PedestrianEntities,
		const FVector& IndicatorOffset,
		const FColor& PedestrianYieldColor,
		const FColor& VehicleYieldColor,
		const UWorld& World,
		const float LifeTime) const;

	void DrawDebugYieldCycleIndicators(
		const UZoneGraphSubsystem& ZoneGraphSubsystem,
		const FMassEntityManager& EntityManager,
		const TSet<FMassEntityHandle>& PedestrianEntities,
		const TArray<FYieldCycleNode>& YieldCycleNodes,
		const FVector& IndicatorOffset,
		const FColor& PedestrianYieldColor,
		const FColor& VehicleYieldColor,
		const FColor& VehicleImplicitYieldColor,
		const UWorld& World,
		const float LifeTime) const;

	void DrawDebugYieldOverrideIndicators(
		const UMassTrafficSubsystem& MassTrafficSubsystem,
		const UZoneGraphSubsystem& ZoneGraphSubsystem,
		const FMassEntityManager& EntityManager,
		const TSet<FMassEntityHandle>& PedestrianEntities,
		const FVector& IndicatorOffset,
		const FColor& PedestrianYieldOverrideColor,
		const FColor& VehicleYieldOverrideColor,
		const UWorld& World,
		const float LifeTime) const;

	FMassEntityQuery EntityQuery_Vehicles;
	FMassEntityQuery EntityQuery_Pedestrians;
};
