// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficYieldDeadlockProcessor.h"

#include "MassTraffic.h"
#include "MassTrafficFragments.h"
#include "MassCrowdFragments.h"
#include "MassExecutionContext.h"
#include "MassZoneGraphNavigationFragments.h"
#include "MassTrafficLaneChange.h"

namespace
{
	struct DFSStackFrame
	{
		FZoneGraphLaneHandle Node;
		TArray<FZoneGraphLaneHandle> Neighbors;
		int32 NextIndex;

		DFSStackFrame(const FZoneGraphLaneHandle& InNode, const TArray<FZoneGraphLaneHandle>& InNeighbors)
			: Node(InNode)
			, Neighbors(InNeighbors)
			, NextIndex(0)
		{
		}
	};
	
	TArray<FZoneGraphLaneHandle> FindFirstCycleDFS(
	const TMap<FZoneGraphLaneHandle, TSet<FZoneGraphLaneHandle>>& LaneYieldMap,
	const FZoneGraphLaneHandle& StartLane)
	{
		TArray<DFSStackFrame> Stack;
		TArray<FZoneGraphLaneHandle> CurrentPath;
		TSet<FZoneGraphLaneHandle> CurrentPathSet;

		TArray<FZoneGraphLaneHandle> StartNodeNeighbors;
		if (const TSet<FZoneGraphLaneHandle>* Neighbors = LaneYieldMap.Find(StartLane))
		{
			for (const FZoneGraphLaneHandle& Neighbor : *Neighbors)
			{
				StartNodeNeighbors.Add(Neighbor);
			}
		}

		// The search starts from the StartLane.
		Stack.Add(DFSStackFrame(StartLane, StartNodeNeighbors));
		CurrentPath.Add(StartLane);
		CurrentPathSet.Add(StartLane);

		while (Stack.Num() > 0)
		{
			DFSStackFrame& TopFrame = Stack.Last();

			if (TopFrame.NextIndex < TopFrame.Neighbors.Num())
			{
				FZoneGraphLaneHandle& Neighbor = TopFrame.Neighbors[TopFrame.NextIndex];
				TopFrame.NextIndex++;

				// If we found our goal, return the cycle.
				if (Neighbor == StartLane)
				{
					CurrentPath.Add(StartLane);
					return CurrentPath;
				}
				else if (!CurrentPathSet.Contains(Neighbor))
				{
					TArray<FZoneGraphLaneHandle> NeighborNeighborsArray;
					if (const TSet<FZoneGraphLaneHandle>* NeighborNeighbors = LaneYieldMap.Find(Neighbor))
					{
						for (const FZoneGraphLaneHandle& NeighborNeighbor : *NeighborNeighbors)
						{
							NeighborNeighborsArray.Add(NeighborNeighbor);
						}
					}

					// Push neighbor onto stack. 
					Stack.Add(DFSStackFrame(Neighbor, NeighborNeighborsArray));
					CurrentPath.Add(Neighbor);
					CurrentPathSet.Add(Neighbor);
				}
			}
			else
			{
				// No more neighbors to process.  So, we backtrack.
				Stack.Pop();
				CurrentPath.Pop();
				CurrentPathSet.Remove(TopFrame.Node);
			}
		}

		// No cycle found.
		return TArray<FZoneGraphLaneHandle>();
	}
}

/*
 * UMassTrafficYieldDeadlockFrameInitProcessor
 */

UMassTrafficYieldDeadlockFrameInitProcessor::UMassTrafficYieldDeadlockFrameInitProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::YieldDeadlockFrameInit;

	// Run before all processors that determine the yield state of Entities.
	ExecutionOrder.ExecuteBefore.Add(UE::MassTraffic::ProcessorGroupNames::VehicleBehavior);
	ExecutionOrder.ExecuteBefore.Add(UE::MassTraffic::ProcessorGroupNames::CrowdYieldBehavior);
	
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PreVehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleSimulationLOD);
}

void UMassTrafficYieldDeadlockFrameInitProcessor::ConfigureQueries()
{
	// Note:  This processor doesn't actually need an EntityQuery.
	// But, we need to create one, in order for the processor's Execute function to get called.
	// So, we just add this simple EntityQuery, here.
	EntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
	
	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
}


void UMassTrafficYieldDeadlockFrameInitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UMassTrafficSubsystem& MassTrafficSubsystem = Context.GetMutableSubsystemChecked<UMassTrafficSubsystem>();
	MassTrafficSubsystem.ClearYieldInfo();
}

/*
 * UMassTrafficYieldDeadlockResolutionProcessor
 */

UMassTrafficYieldDeadlockResolutionProcessor::UMassTrafficYieldDeadlockResolutionProcessor()
	: EntityQuery_Vehicles(*this)
	, EntityQuery_Pedestrians(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::YieldDeadlockResolution;

	// Run after all processors that determine the yield state of Entities.
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::CrowdYieldBehavior);
}

void UMassTrafficYieldDeadlockResolutionProcessor::ConfigureQueries()
{
	EntityQuery_Vehicles.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
	
	EntityQuery_Pedestrians.AddTagRequirement<FMassCrowdTag>(EMassFragmentPresence::All);
	
	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
	ProcessorRequirements.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
}


void UMassTrafficYieldDeadlockResolutionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UMassTrafficSubsystem& MassTrafficSubsystem = Context.GetMutableSubsystemChecked<UMassTrafficSubsystem>();
	const UZoneGraphSubsystem& ZoneGraphSubsystem = Context.GetSubsystemChecked<UZoneGraphSubsystem>();

	// Gather all Vehicle Entities.
	TSet<FMassEntityHandle> VehicleEntities;
	EntityQuery_Vehicles.ForEachEntityChunk(EntityManager, Context, [&](const FMassExecutionContext& QueryContext)
	{
		const int32 NumEntities = QueryContext.GetNumEntities();
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			VehicleEntities.Add(QueryContext.GetEntity(Index));
		}
	});

	// Gather all Pedestrian Entities.
	TSet<FMassEntityHandle> PedestrianEntities;
	EntityQuery_Pedestrians.ForEachEntityChunk(EntityManager, Context, [&](const FMassExecutionContext& QueryContext)
	{
		const int32 NumEntities = QueryContext.GetNumEntities();
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			PedestrianEntities.Add(QueryContext.GetEntity(Index));
		}
	});

	// First, clean-up any stale yield overrides before potentially adding any new ones.
	{
		TMap<FZoneGraphLaneHandle, TSet<FMassEntityHandle>>& LaneYieldOverrideMap = MassTrafficSubsystem.GetMutableLaneYieldOverrideMap();
		
		for (auto LaneYieldOverrideMapItr = LaneYieldOverrideMap.CreateIterator(); LaneYieldOverrideMapItr; ++LaneYieldOverrideMapItr)
		{
			const FZoneGraphLaneHandle& YieldOverrideLane = LaneYieldOverrideMapItr.Key();
			TSet<FMassEntityHandle>& YieldOverrideEntities = LaneYieldOverrideMapItr.Value();

			for (auto YieldOverrideEntityItr = YieldOverrideEntities.CreateIterator(); YieldOverrideEntityItr; ++YieldOverrideEntityItr)
			{
				const FMassEntityHandle& YieldOverrideEntity = *YieldOverrideEntityItr;
				FMassEntityView YieldOverrideCandidateEntityView(EntityManager, YieldOverrideEntity);
				const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = YieldOverrideCandidateEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();

				// If this Entity is on a new lane since it was granted a yield override, ...
				if (LaneLocationFragment.LaneHandle != YieldOverrideLane)
				{
					YieldOverrideEntityItr.RemoveCurrent();
				}
			}

			if (YieldOverrideEntities.IsEmpty())
			{
				LaneYieldOverrideMapItr.RemoveCurrent();
			}
		}
	}

	const auto& DrawDebugIndicatorsForEntities = [&ZoneGraphSubsystem, &EntityManager, &PedestrianEntities, this](const TArray<FZoneGraphLaneHandle>& SourceLanes, const TMap<FZoneGraphLaneHandle, TSet<FMassEntityHandle>>& EntitiesMap, const FVector& IndicatorOffset, const FColor& PedestrianColor, const FColor& VehicleColor, const float LifeTime)
	{
		for (const FZoneGraphLaneHandle& SourceLane : SourceLanes)
		{
			if (const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(SourceLane.DataHandle))
			{
				if (const TSet<FMassEntityHandle>* Entities = EntitiesMap.Find(SourceLane))
				{
					for (const FMassEntityHandle& Entity : *Entities)
					{
						FMassEntityView EntityView(EntityManager, Entity);

						const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = EntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();

						FZoneGraphLaneLocation EntityZoneGraphLaneLocation;
						if (UE::ZoneGraph::Query::CalculateLocationAlongLane(*ZoneGraphStorage, SourceLane, LaneLocationFragment.DistanceAlongLane, EntityZoneGraphLaneLocation))
						{
							const FColor& EntityColor = PedestrianEntities.Contains(Entity) ? PedestrianColor : VehicleColor;
							DrawDebugSphere(GetWorld(), EntityZoneGraphLaneLocation.Position + IndicatorOffset, 50.0f, 16, EntityColor, false, LifeTime, 0, 10.0f);
						}
					}
				}
			}
		}
	};

	// Draw spheres for each Entity with a yield override, if debug option is enabled.
	if (GMassTrafficDebugYieldDeadlockResolution > 0)
	{
		const TMap<FZoneGraphLaneHandle, TSet<FMassEntityHandle>>& LaneYieldOverrideMap = MassTrafficSubsystem.GetLaneYieldOverrideMap();

		TArray<FZoneGraphLaneHandle> YieldOverrideLanes;
		LaneYieldOverrideMap.GetKeys(YieldOverrideLanes);
		
		DrawDebugIndicatorsForEntities(YieldOverrideLanes, LaneYieldOverrideMap, FVector(0.0f, 0.0f, 300.0f), FColor::Cyan, FColor::Blue, 0.1f);
	}

	const auto& FindYieldCycleLanes = [&MassTrafficSubsystem]()
	{
		const TMap<FZoneGraphLaneHandle, TSet<FZoneGraphLaneHandle>>& LaneYieldMap = MassTrafficSubsystem.GetLaneYieldMap();

		TArray<FZoneGraphLaneHandle> YieldingLanes;
		LaneYieldMap.GetKeys(YieldingLanes);

		for (const FZoneGraphLaneHandle& SearchStartLane : YieldingLanes)
		{
			const TArray<FZoneGraphLaneHandle> YieldCycleLanes = FindFirstCycleDFS(LaneYieldMap, SearchStartLane);

			if (!YieldCycleLanes.IsEmpty())
			{
				return YieldCycleLanes;
			}
		}

		return TArray<FZoneGraphLaneHandle>();
	};

	const TArray<FZoneGraphLaneHandle> YieldCycleLanes = FindYieldCycleLanes();

	// If we didn't find a yield cycle, ...
	if (YieldCycleLanes.IsEmpty())
	{
		// There is nothing to do here.
		return;
	}

	const TMap<FZoneGraphLaneHandle, TSet<FMassEntityHandle>>& YieldingEntitiesMap = MassTrafficSubsystem.GetYieldingEntitiesMap();

	// Draw spheres for each Entity in a yield cycle, if debug option is enabled.
	if (GMassTrafficDebugYieldDeadlockResolution > 0)
	{
		DrawDebugIndicatorsForEntities(YieldCycleLanes, YieldingEntitiesMap, FVector(0.0f, 0.0f, 300.0f), FColor::Magenta, FColor::Purple, 0.5f);
	}

	const auto& GetYieldOverrideCandidateLanes = [&YieldCycleLanes, &YieldingEntitiesMap, &VehicleEntities, &PedestrianEntities]()
	{
		TSet<FZoneGraphLaneHandle> YieldOverrideCandidateLanes;

		// First, see if any pedestrians are in the yield cycle.
		// If so, all such pedestrians will become our YieldOverrideCandidates.
		for (const FZoneGraphLaneHandle& YieldCycleLane : YieldCycleLanes)
		{
			if (const TSet<FMassEntityHandle>* YieldingEntities = YieldingEntitiesMap.Find(YieldCycleLane))
			{
				if (!YieldingEntities->Intersect(PedestrianEntities).IsEmpty())
				{
					YieldOverrideCandidateLanes.Add(YieldCycleLane);
				}
			}
		}

		// If we found any pedestrians in the yield cycle,
		// return their associated lanes as our YieldOverrideCandidateLanes.
		if (!YieldOverrideCandidateLanes.IsEmpty())
		{
			return YieldOverrideCandidateLanes;
		}

		// Now, look for vehicles.
		for (const FZoneGraphLaneHandle& YieldCycleLane : YieldCycleLanes)
		{
			if (const TSet<FMassEntityHandle>* YieldingEntities = YieldingEntitiesMap.Find(YieldCycleLane))
			{
				if (!YieldingEntities->Intersect(VehicleEntities).IsEmpty())
				{
					YieldOverrideCandidateLanes.Add(YieldCycleLane);
				}
			}
		}

		if (!ensureMsgf(!YieldOverrideCandidateLanes.IsEmpty(), TEXT("YieldCycleLanes had entries.  But, we found no vehicles or pedestrians in the yield cycle in UMassTrafficYieldDeadlockResolutionProcessor::Execute.")))
		{
			return TSet<FZoneGraphLaneHandle>();
		}

		return YieldOverrideCandidateLanes;
	};

	const auto& GetSelectedYieldOverrideCandidateLane = [&EntityManager, &YieldingEntitiesMap](const TSet<FZoneGraphLaneHandle>& YieldOverrideCandidateLanes)
	{
		FZoneGraphLaneHandle SelectedYieldOverrideCandidateLane;
		float MaxNormalizedDistanceAlongLane = TNumericLimits<float>::Lowest();

		// Select the lane with the Entity furthest along the lane among all the candidate lanes.
		for (const FZoneGraphLaneHandle& YieldOverrideCandidateLane : YieldOverrideCandidateLanes)
		{
			if (const TSet<FMassEntityHandle>* YieldOverrideCandidateEntities = YieldingEntitiesMap.Find(YieldOverrideCandidateLane))
			{
				for (const FMassEntityHandle& YieldOverrideCandidateEntity : *YieldOverrideCandidateEntities)
				{
					FMassEntityView YieldOverrideCandidateEntityView(EntityManager, YieldOverrideCandidateEntity);

					const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = YieldOverrideCandidateEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();

					const float NormalizedDistanceAlongLane = LaneLocationFragment.LaneLength > 0.0f ? LaneLocationFragment.DistanceAlongLane / LaneLocationFragment.LaneLength : 0.0f;

					if (NormalizedDistanceAlongLane > MaxNormalizedDistanceAlongLane)
					{
						MaxNormalizedDistanceAlongLane = NormalizedDistanceAlongLane;
						SelectedYieldOverrideCandidateLane = YieldOverrideCandidateLane;
					}
				}
			}
		}

		return SelectedYieldOverrideCandidateLane;
	};

	// Only add Entities to the yield override map, if yield deadlock resolution is enabled.
	if (GMassTrafficResolveYieldDeadlocks <= 0)
	{
		return;
	}

	// Get all the yield candidate lanes, then select one candidate lane to override, in order to break this yield cycle.
	// Note:  Once we add yield overrides for Entities on a lane in a yield cycle, the same yield cycle
	// should not form again next frame as the Entities will skip their yield logic altogether
	// until they clear the lane in which the yield override was initiated.
	const TSet<FZoneGraphLaneHandle>& YieldOverrideCandidateLanes = GetYieldOverrideCandidateLanes();
	const FZoneGraphLaneHandle& SelectedYieldOverrideCandidateLane = GetSelectedYieldOverrideCandidateLane(YieldOverrideCandidateLanes);

	// If our selected yield override candidate lane is valid, ...
	if (SelectedYieldOverrideCandidateLane.IsValid())
	{
		if (const TSet<FMassEntityHandle>* SelectedYieldOverrideEntities = YieldingEntitiesMap.Find(SelectedYieldOverrideCandidateLane))
		{
			UE_LOG(LogMassTraffic, Log, TEXT("Breaking yield cycle deadlock by overriding the yield logic for %d Entities on SelectedYieldOverrideCandidateLane.Index: %d SelectedYieldOverrideCandidateLane.DataHandle.Index: %d SelectedYieldOverrideCandidateLane.DataHandle.Generation: %d"),
				SelectedYieldOverrideEntities->Num(), SelectedYieldOverrideCandidateLane.Index, SelectedYieldOverrideCandidateLane.DataHandle.Index, SelectedYieldOverrideCandidateLane.DataHandle.Generation);
			
			for (const FMassEntityHandle& SelectedYieldOverrideEntity : *SelectedYieldOverrideEntities)
			{
				// Add all the Entities on the selected candidate lane.
				// These Entities will be allowed to ignore their yield logic to break the deadlock.
				MassTrafficSubsystem.AddEntityToLaneYieldOverrideMap(SelectedYieldOverrideCandidateLane, SelectedYieldOverrideEntity);
			}
		}
	}
}
