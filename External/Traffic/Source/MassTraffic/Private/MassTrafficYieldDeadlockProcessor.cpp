// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficYieldDeadlockProcessor.h"

#include "MassTraffic.h"
#include "MassTrafficFragments.h"
#include "MassCrowdFragments.h"
#include "MassExecutionContext.h"
#include "MassZoneGraphNavigationFragments.h"
#include "MassTrafficLaneChange.h"
#include "Async/Async.h"
#include "MassGameplayExternalTraits.h"
#include "ZoneGraphSubsystem.h"

namespace
{
	struct DFSStackFrame
	{
		FYieldCycleNode Node;
		TArray<FYieldCycleNode> Neighbors;
		int32 NextIndex;

		DFSStackFrame(const FYieldCycleNode& InNode, const TArray<FYieldCycleNode>& InNeighbors)
			: Node(InNode)
			, Neighbors(InNeighbors)
			, NextIndex(0)
		{
		}
	};
	
	TArray<FYieldCycleNode> FindFirstCycleDFS(
		const TMap<FLaneEntityPair, TSet<FLaneEntityPair>>& YieldMap,
		const FYieldCycleNode& StartNode,
		const TFunction<TOptional<FLaneEntityPair>(const FLaneEntityPair&)>& GetImplicitNeighbor)
	{
		TArray<DFSStackFrame> Stack;
		TArray<FYieldCycleNode> CurrentPath;
		TSet<FYieldCycleNode> CurrentPathSet;

		TArray<FYieldCycleNode> StartNodeNeighbors;
		if (const TSet<FLaneEntityPair>* Neighbors = YieldMap.Find(StartNode.LaneEntityPair))
		{
			for (const FLaneEntityPair& Neighbor : *Neighbors)
			{
				StartNodeNeighbors.Add(Neighbor);
			}
		}

		// The search starts from the StartNode.
		Stack.Add(DFSStackFrame(StartNode, StartNodeNeighbors));
		CurrentPath.Add(StartNode);
		CurrentPathSet.Add(StartNode);

		while (Stack.Num() > 0)
		{
			DFSStackFrame& TopFrame = Stack.Last();

			if (TopFrame.NextIndex < TopFrame.Neighbors.Num())
			{
				FYieldCycleNode& Neighbor = TopFrame.Neighbors[TopFrame.NextIndex];
				TopFrame.NextIndex++;

				// If we found our goal, return the cycle.
				if (Neighbor == StartNode)
				{
					// Add neighbor to complete the path as it might
					// represent an implicit edge from the previous node.
					CurrentPath.Add(Neighbor);
					return CurrentPath;
				}
				else if (!CurrentPathSet.Contains(Neighbor))
				{
					TArray<FYieldCycleNode> NeighborNeighborsArray;
					if (const TSet<FLaneEntityPair>* NeighborNeighbors = YieldMap.Find(Neighbor.LaneEntityPair))
					{
						for (const FLaneEntityPair& NeighborNeighbor : *NeighborNeighbors)
						{
							NeighborNeighborsArray.Add(NeighborNeighbor);
						}
					}
					
					if (const TOptional<FLaneEntityPair> ImplicitNeighbor = GetImplicitNeighbor(Neighbor.LaneEntityPair); ImplicitNeighbor.IsSet())
					{
						FYieldCycleNode ImplicitNeighborNode = ImplicitNeighbor.GetValue();
						ImplicitNeighborNode.bHasImplicitYieldFromPrevNode = true;
						
						NeighborNeighborsArray.Add(ImplicitNeighborNode);
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
		return TArray<FYieldCycleNode>();
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
	CleanupStaleYieldOverrides(MassTrafficSubsystem, EntityManager);

	// Draw yield override indicators, if debug option is enabled.
	if (GMassTrafficDebugYieldDeadlockResolution > 0)
	{
		if (const UWorld* World = GetWorld())
		{
			const TMap<FLaneEntityPair, TSet<FLaneEntityPair>>& YieldOverrideMap = MassTrafficSubsystem.GetYieldOverrideMap();
			
			DrawDebugYieldOverrideIndicators(MassTrafficSubsystem, ZoneGraphSubsystem, EntityManager, PedestrianEntities, FVector(0.0f, 0.0f, 300.0f), FColor::Cyan, FColor::Blue, *World, 0.1f);
		}
	}

	// Draw yield map arrows, if debug option is enabled.
	// This is the complete state of all yielding Entities this frame,
	// and it's the direct input to the yield cycle detector.
	if (GMassTrafficDebugYieldDeadlockResolution > 0)
	{
		if (const UWorld* World = GetWorld())
		{
			DrawDebugYieldMap(MassTrafficSubsystem, ZoneGraphSubsystem, EntityManager, PedestrianEntities, FVector(0.0f, 0.0f, 300.0f), FColor::Silver, FColor::White, *World, 0.1f);
		}
	}

	const TArray<FYieldCycleNode> YieldCycleNodes = FindYieldCycleNodes(MassTrafficSubsystem, EntityManager, VehicleEntities);

	// If we didn't find a yield cycle, ...
	if (YieldCycleNodes.IsEmpty())
	{
		// There is nothing to do here.
		return;
	}

	// Draw yield cycle arrows, if debug option is enabled.
	if (GMassTrafficDebugYieldDeadlockResolution > 0)
	{
		if (const UWorld* World = GetWorld())
		{
			DrawDebugYieldCycleIndicators(ZoneGraphSubsystem, EntityManager, PedestrianEntities, YieldCycleNodes, FVector(0.0f, 0.0f, 300.0f), FColor::Orange, FColor::Purple, FColor::Magenta, *World, 0.5f);
		}
	}

	// Only add Entities to the yield override map, if yield deadlock resolution is enabled.
	if (GMassTrafficResolveYieldDeadlocks <= 0)
	{
		return;
	}

	// Get all the yield override candidates, then select one candidate to override, in order to break this yield cycle.
	// Note:  Once we add a yield override for an Entity in a yield cycle, the same yield cycle
	// should not form again next frame as the Entity will ignore yielding to the specified yield target Entity
	// until the selected yield override candidate clears the lane in which the yield override was initiated.
	const TArray<int32>& YieldOverrideCandidateIndexes = GetYieldOverrideCandidateIndexes(YieldCycleNodes, VehicleEntities, PedestrianEntities);
	const int32& SelectedYieldOverrideCandidateIndex = GetSelectedYieldOverrideCandidateIndex(YieldCycleNodes, EntityManager, YieldOverrideCandidateIndexes);
	
	if (!ensureMsgf(SelectedYieldOverrideCandidateIndex != INDEX_NONE, TEXT("Must get valid SelectedYieldOverrideCandidateIndex in UMassTrafficYieldDeadlockResolutionProcessor::Execute.")))
	{
		return;
	}

	const FYieldCycleNode& SelectedYieldOverrideCandidate = YieldCycleNodes[SelectedYieldOverrideCandidateIndex];
	const FYieldCycleNode& TargetIgnoreYieldCandidate = YieldCycleNodes[SelectedYieldOverrideCandidateIndex + 1];	// Plus 1 is safe here.

	if (!ensureMsgf(!TargetIgnoreYieldCandidate.bHasImplicitYieldFromPrevNode, TEXT("TargetIgnoreYieldCandidate must not have implicit yield from SelectedYieldOverrideCandidate in UMassTrafficYieldDeadlockResolutionProcessor::Execute.")))
	{
		return;
	}

	int32 NumLanesInYieldCycle;
	int32 NumVehicleEntitiesInYieldCycle;
	int32 NumPedestrianEntitiesInYieldCycle;
	
	const int32 NumEntitiesInYieldCycle = GetNumEntitiesInYieldCycle(
		YieldCycleNodes,
		VehicleEntities,
		PedestrianEntities,
		&NumLanesInYieldCycle,
		&NumVehicleEntitiesInYieldCycle,
		&NumPedestrianEntitiesInYieldCycle);

	UE_LOG(LogMassTraffic, Log, TEXT("Breaking yield cycle deadlock with %d total Entities (%d vehicles, %d pedestrians) on %d lanes by overriding the yield logic for SelectedYieldOverrideCandidate -> TargetIgnoreYieldCandidate: (Lane: %d Entity: %d) -> (Lane: %d Entity: %d)."),
		NumEntitiesInYieldCycle, NumVehicleEntitiesInYieldCycle, NumPedestrianEntitiesInYieldCycle, NumLanesInYieldCycle,
		SelectedYieldOverrideCandidate.LaneEntityPair.LaneHandle.Index, SelectedYieldOverrideCandidate.LaneEntityPair.EntityHandle.Index,
		TargetIgnoreYieldCandidate.LaneEntityPair.LaneHandle.Index, TargetIgnoreYieldCandidate.LaneEntityPair.EntityHandle.Index);

	// We break the yield cycle deadlock by adding one *explicit* edge in the yield cycle as a yield override.
	// That is, the SelectedYieldOverrideCandidate is allowed to ignore yielding to TargetIgnoreYieldCandidate
	// until SelectedYieldOverrideCandidate's Entity manages to move onto a new lane.
	MassTrafficSubsystem.AddYieldOverride(
		SelectedYieldOverrideCandidate.LaneEntityPair.LaneHandle,
		SelectedYieldOverrideCandidate.LaneEntityPair.EntityHandle,
		TargetIgnoreYieldCandidate.LaneEntityPair.LaneHandle,
		TargetIgnoreYieldCandidate.LaneEntityPair.EntityHandle);
}

void UMassTrafficYieldDeadlockResolutionProcessor::CleanupStaleYieldOverrides(UMassTrafficSubsystem& MassTrafficSubsystem, const FMassEntityManager& EntityManager) const
{
	TMap<FLaneEntityPair, TSet<FLaneEntityPair>>& YieldOverrideMap = MassTrafficSubsystem.GetMutableYieldOverrideMap();
			
	for (auto YieldOverrideMapItr = YieldOverrideMap.CreateIterator(); YieldOverrideMapItr; ++YieldOverrideMapItr)
	{
		const FLaneEntityPair& YieldOverrideLaneEntityPair = YieldOverrideMapItr.Key();
				
		const FZoneGraphLaneHandle& YieldOverrideLane = YieldOverrideLaneEntityPair.LaneHandle;
		const FMassEntityHandle& YieldOverrideEntity = YieldOverrideLaneEntityPair.EntityHandle;

		FMassEntityView YieldOverrideEntityView(EntityManager, YieldOverrideEntity);
		const FMassZoneGraphLaneLocationFragment& YieldOverrideLaneLocationFragment = YieldOverrideEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();

		// If the YieldOverrideEntity is on a new lane since it was granted any number of yield overrides, ...
		if (YieldOverrideLaneLocationFragment.LaneHandle != YieldOverrideLane)
		{
			// Remove its yield overrides.
			YieldOverrideMapItr.RemoveCurrent();
		}
	}
	
	TSet<FLaneEntityPair>& WildcardYieldOverrideSet = MassTrafficSubsystem.GetMutableWildcardYieldOverrideSet();

	for (auto WildcardYieldOverrideSetItr = WildcardYieldOverrideSet.CreateIterator(); WildcardYieldOverrideSetItr; ++WildcardYieldOverrideSetItr)
	{
		const FZoneGraphLaneHandle& WildcardYieldOverrideLane = WildcardYieldOverrideSetItr->LaneHandle;
		const FMassEntityHandle& WildcardYieldOverrideEntity = WildcardYieldOverrideSetItr->EntityHandle;

		FMassEntityView WildcardYieldOverrideEntityView(EntityManager, WildcardYieldOverrideEntity);
		const FMassZoneGraphLaneLocationFragment& WildcardYieldOverrideLaneLocationFragment = WildcardYieldOverrideEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();

		// If the WildcardYieldOverrideEntity is on a new lane since it was granted a wildcard yield override, ...
		if (WildcardYieldOverrideLaneLocationFragment.LaneHandle != WildcardYieldOverrideLane)
		{
			// Remove its wildcard yield override.
			WildcardYieldOverrideSetItr.RemoveCurrent();
		}
	}
}

TArray<FYieldCycleNode> UMassTrafficYieldDeadlockResolutionProcessor::FindYieldCycleNodes(
	const UMassTrafficSubsystem& MassTrafficSubsystem,
	const FMassEntityManager& EntityManager,
	const TSet<FMassEntityHandle>& VehicleEntities) const
{
	const TMap<FZoneGraphLaneHandle, TSet<FMassEntityHandle>>& YieldingEntitiesMap = MassTrafficSubsystem.GetYieldingEntitiesMap();

	const auto& GetImplicitYieldTarget = [&EntityManager, &YieldingEntitiesMap, &VehicleEntities](const FLaneEntityPair& QueryLaneEntityPair) -> TOptional<FLaneEntityPair>
	{
		if (VehicleEntities.Contains(QueryLaneEntityPair.EntityHandle))
		{
			const FMassEntityView QueryVehicleEntityView(EntityManager, QueryLaneEntityPair.EntityHandle);

			const FMassTrafficVehicleControlFragment& VehicleControlFragment = QueryVehicleEntityView.GetFragmentData<FMassTrafficVehicleControlFragment>();
			const FMassTrafficNextVehicleFragment& NextVehicleFragment = QueryVehicleEntityView.GetFragmentData<FMassTrafficNextVehicleFragment>();

			// If not explicitly yielding, but we're currently stopped, ...
			if (!VehicleControlFragment.IsYieldingAtIntersection() && VehicleControlFragment.IsVehicleCurrentlyStopped())
			{
				// And, we have a vehicle somewhere in front of us, ...
				if (NextVehicleFragment.HasNextVehicle())
				{
					if (const TSet<FMassEntityHandle>* YieldingEntities = YieldingEntitiesMap.Find(QueryLaneEntityPair.LaneHandle))
					{
						// And, that vehicle in front of us is yielding on our lane, ...
						if (const FMassEntityHandle NextVehicle = NextVehicleFragment.GetNextVehicle(); YieldingEntities->Contains(NextVehicle))
						{
							// Then, we are implicitly yielding to that vehicle
							// for the purposes of finding yield cycle deadlocks.
							const FLaneEntityPair ImplicitYieldTarget(QueryLaneEntityPair.LaneHandle, NextVehicle);
							return ImplicitYieldTarget;
						}
					}
				}
			}
		}

		return TOptional<FLaneEntityPair>();
	};

	const TMap<FLaneEntityPair, TSet<FLaneEntityPair>>& YieldMap = MassTrafficSubsystem.GetYieldMap();

	TArray<FLaneEntityPair> YieldingLaneEntityPairs;
	YieldMap.GetKeys(YieldingLaneEntityPairs);

	for (const FLaneEntityPair& SearchStartLaneEntityPair : YieldingLaneEntityPairs)
	{
		const TArray<FYieldCycleNode> YieldCycleNodes = FindFirstCycleDFS(YieldMap, SearchStartLaneEntityPair, GetImplicitYieldTarget);

		if (!YieldCycleNodes.IsEmpty())
		{
			return YieldCycleNodes;
		}
	}

	return TArray<FYieldCycleNode>();
}

int32 UMassTrafficYieldDeadlockResolutionProcessor::GetNumEntitiesInYieldCycle(
	const TArray<FYieldCycleNode>& YieldCycleNodes,
	const TSet<FMassEntityHandle>& VehicleEntities,
	const TSet<FMassEntityHandle>& PedestrianEntities,
	int32* OutNumLanesInYieldCycle,
	int32* OutNumVehicleEntitiesInYieldCycle,
	int32* OutNumPedestrianEntitiesInYieldCycle) const
{
	// We need to subtract 1 in order not to count the "cyclic" node at the end.
	int32 NumEntitiesInYieldCycle = YieldCycleNodes.Num() - 1;
	
	int32 NumVehicleEntitiesInYieldCycle = 0;
	int32 NumPedestrianEntitiesInYieldCycle = 0;

	TSet<FZoneGraphLaneHandle> YieldCycleLanes;
	
	for (int32 YieldCycleIndex = 0; YieldCycleIndex < YieldCycleNodes.Num() - 1; ++YieldCycleIndex)
	{
		const FYieldCycleNode& YieldCycleNode = YieldCycleNodes[YieldCycleIndex];
		
		const FZoneGraphLaneHandle& YieldingLane = YieldCycleNode.LaneEntityPair.LaneHandle;
		const FMassEntityHandle& YieldingEntity = YieldCycleNode.LaneEntityPair.EntityHandle;

		YieldCycleLanes.Add(YieldingLane);
			
		if (VehicleEntities.Contains(YieldingEntity))
		{
			++NumVehicleEntitiesInYieldCycle;
		}

		if (PedestrianEntities.Contains(YieldingEntity))
		{
			++NumPedestrianEntitiesInYieldCycle;
		}
	}

	if (OutNumLanesInYieldCycle != nullptr)
	{
		*OutNumLanesInYieldCycle = YieldCycleLanes.Num();
	}

	if (OutNumVehicleEntitiesInYieldCycle != nullptr)
	{
		*OutNumVehicleEntitiesInYieldCycle = NumVehicleEntitiesInYieldCycle;
	}

	if (OutNumPedestrianEntitiesInYieldCycle != nullptr)
	{
		*OutNumPedestrianEntitiesInYieldCycle = NumPedestrianEntitiesInYieldCycle;
	}

	return NumEntitiesInYieldCycle;
}

TArray<int32> UMassTrafficYieldDeadlockResolutionProcessor::GetYieldOverrideCandidateIndexes(
	const TArray<FYieldCycleNode>& YieldCycleNodes,
	const TSet<FMassEntityHandle>& VehicleEntities,
	const TSet<FMassEntityHandle>& PedestrianEntities) const
{
	TArray<int32> YieldOverrideCandidateIndexes;

	// First, see if any pedestrians are in the yield cycle.
	// If so, all such pedestrians will become our YieldOverrideCandidates.
	for (int32 YieldCycleIndex = 0; YieldCycleIndex < YieldCycleNodes.Num() - 1; ++YieldCycleIndex)
	{
		const FYieldCycleNode& YieldCycleNode = YieldCycleNodes[YieldCycleIndex];
		
		const FMassEntityHandle& YieldingEntity = YieldCycleNode.LaneEntityPair.EntityHandle;
		
		if (PedestrianEntities.Contains(YieldingEntity))
		{
			YieldOverrideCandidateIndexes.Add(YieldCycleIndex);
		}
	}

	// If we found any pedestrians in the yield cycle,
	// return them as our YieldOverrideCandidateLanes.
	if (!YieldOverrideCandidateIndexes.IsEmpty())
	{
		return YieldOverrideCandidateIndexes;
	}

	// Now, look for vehicles.
	for (int32 YieldCycleIndex = 0; YieldCycleIndex < YieldCycleNodes.Num() - 1; ++YieldCycleIndex)
	{
		const FYieldCycleNode& YieldCycleNode = YieldCycleNodes[YieldCycleIndex];
		const FYieldCycleNode& NextYieldCycleNode = YieldCycleNodes[YieldCycleIndex + 1];

		// Vehicles can implicitly yield by stopping behind a vehicle in front of them on their own lane.
		// And, breaking these edges in the yield cycle won't resolve the yield cycle deadlock.
		// So, we skip selecting yield override candidates that are implicitly yielding
		// to the next node in the yield cycle.
		if (NextYieldCycleNode.bHasImplicitYieldFromPrevNode)
		{
			continue;
		}
		
		const FMassEntityHandle& YieldingEntity = YieldCycleNode.LaneEntityPair.EntityHandle;

		if (VehicleEntities.Contains(YieldingEntity))
		{
			YieldOverrideCandidateIndexes.Add(YieldCycleIndex);
		}
	}

	if (!ensureMsgf(YieldCycleNodes.IsEmpty() || !YieldOverrideCandidateIndexes.IsEmpty(), TEXT("YieldCycleNodes has entries.  But, we found no vehicles or pedestrians in the yield cycle in UMassTrafficYieldDeadlockResolutionProcessor::Execute.")))
	{
		return TArray<int32>();
	}

	return YieldOverrideCandidateIndexes;
}

int32 UMassTrafficYieldDeadlockResolutionProcessor::GetSelectedYieldOverrideCandidateIndex(
	const TArray<FYieldCycleNode>& YieldCycleNodes,
	const FMassEntityManager& EntityManager,
	const TArray<int32>& YieldOverrideCandidateIndexes) const
{
	// First, figure-out which candidate Entities are yielding on which lane.
	TMap<FZoneGraphLaneHandle, TSet<FMassEntityHandle>> YieldingCandidatesMap;
	for (const int32& YieldOverrideCandidateIndex : YieldOverrideCandidateIndexes)
	{
		const FYieldCycleNode& YieldOverrideCandidate = YieldCycleNodes[YieldOverrideCandidateIndex];
		
		const FZoneGraphLaneHandle& YieldingLane = YieldOverrideCandidate.LaneEntityPair.LaneHandle;
		const FMassEntityHandle& YieldingEntity = YieldOverrideCandidate.LaneEntityPair.EntityHandle;
		
		TSet<FMassEntityHandle>& YieldingEntities = YieldingCandidatesMap.FindOrAdd(YieldingLane);
		YieldingEntities.Add(YieldingEntity);
	}

	// Then, figure-out which lanes are tied for having the least number of candidate Entities.
	TSet<FZoneGraphLaneHandle> LanesWithMinYieldingEntitiesInYieldOverrideCandidates;
	int32 MinYieldingEntitiesInYieldOverrideCandidates = TNumericLimits<int32>::Max();

	for (TTuple<FZoneGraphLaneHandle, TSet<FMassEntityHandle>> YieldingCandidatePair : YieldingCandidatesMap)
	{
		const FZoneGraphLaneHandle& YieldingLane = YieldingCandidatePair.Key;
		const TSet<FMassEntityHandle>& YieldingEntities = YieldingCandidatePair.Value;

		const int32 NumYieldingEntities = YieldingEntities.Num();

		// Reset LaneWithMinYieldingEntitiesInYieldOverrideCandidates
		// on new MinYieldingEntitiesInYieldOverrideCandidates.
		if (NumYieldingEntities < MinYieldingEntitiesInYieldOverrideCandidates)
		{
			LanesWithMinYieldingEntitiesInYieldOverrideCandidates.Reset();
		}

		if (NumYieldingEntities <= MinYieldingEntitiesInYieldOverrideCandidates)
		{
			MinYieldingEntitiesInYieldOverrideCandidates = NumYieldingEntities;
			LanesWithMinYieldingEntitiesInYieldOverrideCandidates.Add(YieldingLane);
		}
	}

	ensureMsgf(!LanesWithMinYieldingEntitiesInYieldOverrideCandidates.IsEmpty(), TEXT("LanesWithMinYieldingEntitiesInYieldOverrideCandidates must contain at least 1 lane in UMassTrafficYieldDeadlockResolutionProcessor::Execute."));
	
	int32 SelectedYieldOverrideCandidateIndex = INDEX_NONE;
	float MaxNormalizedDistanceAlongLane = TNumericLimits<float>::Lowest();

	// Select the candidate with the Entity furthest along the lane among all the candidate lanes.
	for (const int32& YieldOverrideCandidateIndex : YieldOverrideCandidateIndexes)
	{
		const FYieldCycleNode& YieldOverrideCandidate = YieldCycleNodes[YieldOverrideCandidateIndex];
		
		const FZoneGraphLaneHandle& YieldingLane = YieldOverrideCandidate.LaneEntityPair.LaneHandle;

		// Only consider yield override candidates on lanes
		// tied for having the minimum number of candidate Entities.
		if (!LanesWithMinYieldingEntitiesInYieldOverrideCandidates.Contains(YieldingLane))
		{
			continue;
		}
		
		const FMassEntityHandle& YieldOverrideCandidateEntity = YieldOverrideCandidate.LaneEntityPair.EntityHandle;
		
		FMassEntityView YieldOverrideCandidateEntityView(EntityManager, YieldOverrideCandidateEntity);

		const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = YieldOverrideCandidateEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();

		const float NormalizedDistanceAlongLane = LaneLocationFragment.LaneLength > 0.0f ? LaneLocationFragment.DistanceAlongLane / LaneLocationFragment.LaneLength : 0.0f;

		if (NormalizedDistanceAlongLane > MaxNormalizedDistanceAlongLane)
		{
			MaxNormalizedDistanceAlongLane = NormalizedDistanceAlongLane;
			SelectedYieldOverrideCandidateIndex = YieldOverrideCandidateIndex;
		}
	}

	return SelectedYieldOverrideCandidateIndex;
}

void UMassTrafficYieldDeadlockResolutionProcessor::DrawDebugYieldMap(
	const UMassTrafficSubsystem& MassTrafficSubsystem,
	const UZoneGraphSubsystem& ZoneGraphSubsystem,
	const FMassEntityManager& EntityManager,
	const TSet<FMassEntityHandle>& PedestrianEntities,
	const FVector& IndicatorOffset,
	const FColor& PedestrianYieldColor,
	const FColor& VehicleYieldColor,
	const UWorld& World,
	const float LifeTime) const
{
	TArray<TFunction<void()>> DebugDrawCalls;

	const TMap<FLaneEntityPair, TSet<FLaneEntityPair>>& YieldMap = MassTrafficSubsystem.GetYieldMap();

	for (const TTuple<FLaneEntityPair, TSet<FLaneEntityPair>>& YieldMapPair : YieldMap)
	{
		const FLaneEntityPair& YieldingLaneEntityPair = YieldMapPair.Key;
		const TSet<FLaneEntityPair>& YieldTargets = YieldMapPair.Value;
		
		const FZoneGraphLaneHandle& YieldingLane = YieldingLaneEntityPair.LaneHandle;
		const FMassEntityHandle& YieldingEntity = YieldingLaneEntityPair.EntityHandle;

		const FZoneGraphStorage* YieldingZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(YieldingLane.DataHandle);

		if (YieldingZoneGraphStorage == nullptr)
		{
			continue;
		}

		for (const FLaneEntityPair& YieldTarget : YieldTargets)
		{
			const FZoneGraphLaneHandle& YieldTargetLane = YieldTarget.LaneHandle;
			const FMassEntityHandle& YieldTargetEntity = YieldTarget.EntityHandle;
			
			const FZoneGraphStorage* YieldTargetZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(YieldTargetLane.DataHandle);

			if (YieldTargetZoneGraphStorage == nullptr)
			{
				continue;
			}

			const FMassEntityView YieldingEntityView(EntityManager, YieldingEntity);
			const FMassEntityView YieldTargetEntityView(EntityManager, YieldTargetEntity);
	
			const FMassZoneGraphLaneLocationFragment& YieldingLaneLocationFragment = YieldingEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
			const FMassZoneGraphLaneLocationFragment& YieldTargetLaneLocationFragment = YieldTargetEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
	
			FZoneGraphLaneLocation YieldingEntityZoneGraphLaneLocation;
			FZoneGraphLaneLocation YieldTargetEntityZoneGraphLaneLocation;
			
			if (UE::ZoneGraph::Query::CalculateLocationAlongLane(*YieldingZoneGraphStorage, YieldingLane, YieldingLaneLocationFragment.DistanceAlongLane, YieldingEntityZoneGraphLaneLocation)
				&& UE::ZoneGraph::Query::CalculateLocationAlongLane(*YieldTargetZoneGraphStorage, YieldTargetLane, YieldTargetLaneLocationFragment.DistanceAlongLane, YieldTargetEntityZoneGraphLaneLocation))
			{
				const FColor& YieldColor = PedestrianEntities.Contains(YieldingEntity) ? PedestrianYieldColor : VehicleYieldColor;

				DebugDrawCalls.Add([
					World=&World,
					YieldingEntityPosition=YieldingEntityZoneGraphLaneLocation.Position,
					YieldTargetEntityPosition=YieldTargetEntityZoneGraphLaneLocation.Position,
					IndicatorOffset,
					YieldColor,
					LifeTime]()
				{
					DrawDebugDirectionalArrow(World, YieldingEntityPosition + IndicatorOffset, YieldTargetEntityPosition + IndicatorOffset, 1000.0f, YieldColor, false, LifeTime, 0, 10.0f);
				});
			}
		}
	}
	
	AsyncTask(ENamedThreads::GameThread, [DebugDrawCalls]()
	{
		for (const auto& DebugDrawCall : DebugDrawCalls)
		{
			DebugDrawCall();
		}
	});
}

void UMassTrafficYieldDeadlockResolutionProcessor::DrawDebugYieldCycleIndicators(
	const UZoneGraphSubsystem& ZoneGraphSubsystem,
	const FMassEntityManager& EntityManager,
	const TSet<FMassEntityHandle>& PedestrianEntities,
	const TArray<FYieldCycleNode>& YieldCycleNodes,
	const FVector& IndicatorOffset,
	const FColor& PedestrianYieldColor,
	const FColor& VehicleYieldColor,
	const FColor& VehicleImplicitYieldColor,
	const UWorld& World,
	const float LifeTime) const
{
	TArray<TFunction<void()>> DebugDrawCalls;
	
	for (int32 YieldCycleIndex = 0; YieldCycleIndex < YieldCycleNodes.Num() - 1; ++YieldCycleIndex)
	{
		const FYieldCycleNode& YieldCycleNode = YieldCycleNodes[YieldCycleIndex];
		const FYieldCycleNode& NextYieldCycleNode = YieldCycleNodes[YieldCycleIndex + 1];

		const FZoneGraphLaneHandle& YieldingLane = YieldCycleNode.LaneEntityPair.LaneHandle;
		const FMassEntityHandle& YieldingEntity = YieldCycleNode.LaneEntityPair.EntityHandle;

		const FZoneGraphLaneHandle& NextYieldingLane = NextYieldCycleNode.LaneEntityPair.LaneHandle;
		const FMassEntityHandle& NextYieldingEntity = NextYieldCycleNode.LaneEntityPair.EntityHandle;
		
		const FZoneGraphStorage* YieldingZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(YieldingLane.DataHandle);
		const FZoneGraphStorage* NextYieldingZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(NextYieldingLane.DataHandle);
		
		if (YieldingZoneGraphStorage != nullptr && NextYieldingZoneGraphStorage != nullptr)
		{
			const FMassEntityView YieldingEntityView(EntityManager, YieldingEntity);
			const FMassEntityView NextYieldingEntityView(EntityManager, NextYieldingEntity);
	
			const FMassZoneGraphLaneLocationFragment& YieldingLaneLocationFragment = YieldingEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
			const FMassZoneGraphLaneLocationFragment& NextYieldingLaneLocationFragment = NextYieldingEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
	
			FZoneGraphLaneLocation YieldingEntityZoneGraphLaneLocation;
			FZoneGraphLaneLocation NextYieldingEntityZoneGraphLaneLocation;
			
			if (UE::ZoneGraph::Query::CalculateLocationAlongLane(*YieldingZoneGraphStorage, YieldingLane, YieldingLaneLocationFragment.DistanceAlongLane, YieldingEntityZoneGraphLaneLocation)
				&& UE::ZoneGraph::Query::CalculateLocationAlongLane(*NextYieldingZoneGraphStorage, NextYieldingLane, NextYieldingLaneLocationFragment.DistanceAlongLane, NextYieldingEntityZoneGraphLaneLocation))
			{
				const FColor& YieldColor = PedestrianEntities.Contains(YieldingEntity)
					? PedestrianYieldColor
					: NextYieldCycleNode.bHasImplicitYieldFromPrevNode ? VehicleImplicitYieldColor : VehicleYieldColor;

				DebugDrawCalls.Add([
					World=&World,
					YieldingEntityPosition=YieldingEntityZoneGraphLaneLocation.Position,
					NextYieldingEntityPosition=NextYieldingEntityZoneGraphLaneLocation.Position,
					IndicatorOffset,
					YieldColor,
					LifeTime]()
				{
					DrawDebugDirectionalArrow(World, YieldingEntityPosition + IndicatorOffset, NextYieldingEntityPosition + IndicatorOffset, 1000.0f, YieldColor, false, LifeTime, 0, 10.0f);
				});
			}
		}
	}

	for (const FYieldCycleNode& YieldCycleNode : YieldCycleNodes)
	{
		DebugDrawCalls.Add([
			&ZoneGraphSubsystem,
			YieldCycleLane=YieldCycleNode.LaneEntityPair.LaneHandle,
			World=&World,
			LifeTime]()
		{
			UE::MassTraffic::DrawLaneData(ZoneGraphSubsystem, YieldCycleLane, FColor::Purple, *World, 10.0f, LifeTime);
		});
	}

	AsyncTask(ENamedThreads::GameThread, [DebugDrawCalls]()
	{
		for (const auto& DebugDrawCall : DebugDrawCalls)
		{
			DebugDrawCall();
		}
	});
}

void UMassTrafficYieldDeadlockResolutionProcessor::DrawDebugYieldOverrideIndicators(
	const UMassTrafficSubsystem& MassTrafficSubsystem,
	const UZoneGraphSubsystem& ZoneGraphSubsystem,
	const FMassEntityManager& EntityManager,
	const TSet<FMassEntityHandle>& PedestrianEntities,
	const FVector& IndicatorOffset,
	const FColor& PedestrianYieldOverrideColor,
	const FColor& VehicleYieldOverrideColor,
	const UWorld& World,
	const float LifeTime) const
{
	TArray<TFunction<void()>> DebugDrawCalls;
	
	const TMap<FLaneEntityPair, TSet<FLaneEntityPair>>& YieldOverrideMap = MassTrafficSubsystem.GetYieldOverrideMap();

	for (const TTuple<FLaneEntityPair, TSet<FLaneEntityPair>>& YieldOverridePair : YieldOverrideMap)
	{
		const FLaneEntityPair& YieldOverrideLaneEntityPair = YieldOverridePair.Key;
		const TSet<FLaneEntityPair>& YieldIgnoreTargets = YieldOverridePair.Value;
		
		const FZoneGraphLaneHandle& YieldOverrideLane = YieldOverrideLaneEntityPair.LaneHandle;
		const FMassEntityHandle& YieldOverrideEntity = YieldOverrideLaneEntityPair.EntityHandle;

		const FZoneGraphStorage* YieldOverrideZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(YieldOverrideLane.DataHandle);

		if (YieldOverrideZoneGraphStorage == nullptr)
		{
			continue;
		}

		for (const FLaneEntityPair& YieldIgnoreTarget : YieldIgnoreTargets)
		{
			const FZoneGraphLaneHandle& YieldIgnoreTargetLane = YieldIgnoreTarget.LaneHandle;
			const FMassEntityHandle& YieldIgnoreTargetEntity = YieldIgnoreTarget.EntityHandle;
			
			const FZoneGraphStorage* YieldIgnoreTargetZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(YieldIgnoreTargetLane.DataHandle);

			if (YieldIgnoreTargetZoneGraphStorage == nullptr)
			{
				continue;
			}

			const FMassEntityView YieldOverrideEntityView(EntityManager, YieldOverrideEntity);
			const FMassEntityView YieldIgnoreTargetEntityView(EntityManager, YieldIgnoreTargetEntity);
	
			const FMassZoneGraphLaneLocationFragment& YieldOverrideLaneLocationFragment = YieldOverrideEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
			const FMassZoneGraphLaneLocationFragment& YieldIgnoreTargetLaneLocationFragment = YieldIgnoreTargetEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
	
			FZoneGraphLaneLocation YieldOverrideEntityZoneGraphLaneLocation;
			FZoneGraphLaneLocation YieldIgnoreTargetEntityZoneGraphLaneLocation;
			
			if (UE::ZoneGraph::Query::CalculateLocationAlongLane(*YieldOverrideZoneGraphStorage, YieldOverrideLane, YieldOverrideLaneLocationFragment.DistanceAlongLane, YieldOverrideEntityZoneGraphLaneLocation)
				&& UE::ZoneGraph::Query::CalculateLocationAlongLane(*YieldIgnoreTargetZoneGraphStorage, YieldIgnoreTargetLane, YieldIgnoreTargetLaneLocationFragment.DistanceAlongLane, YieldIgnoreTargetEntityZoneGraphLaneLocation))
			{
				const FColor& YieldOverrideColor = PedestrianEntities.Contains(YieldOverrideEntity) ? PedestrianYieldOverrideColor : VehicleYieldOverrideColor;

				DebugDrawCalls.Add([
					World=&World,
					YieldOverrideEntityPosition=YieldOverrideEntityZoneGraphLaneLocation.Position,
					YieldIgnoreTargetEntityPosition=YieldIgnoreTargetEntityZoneGraphLaneLocation.Position,
					IndicatorOffset,
					YieldOverrideColor,
					LifeTime]()
				{
					DrawDebugDirectionalArrow(World, YieldOverrideEntityPosition + IndicatorOffset, YieldIgnoreTargetEntityPosition + IndicatorOffset, 1000.0f, YieldOverrideColor, false, LifeTime, 0, 10.0f);
				});
			}
		}
	}

	const TSet<FLaneEntityPair>& WildcardYieldOverrideSet = MassTrafficSubsystem.GetWildcardYieldOverrideSet();

	for (const FLaneEntityPair& WildcardYieldOverridePair : WildcardYieldOverrideSet)
	{
		const FZoneGraphLaneHandle& WildcardYieldOverrideLane = WildcardYieldOverridePair.LaneHandle;
		const FMassEntityHandle& WildcardYieldOverrideEntity = WildcardYieldOverridePair.EntityHandle;

		const FZoneGraphStorage* WildcardYieldOverrideZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(WildcardYieldOverrideLane.DataHandle);

		if (WildcardYieldOverrideZoneGraphStorage == nullptr)
		{
			continue;
		}
		
		const FMassEntityView WildcardYieldOverrideEntityView(EntityManager, WildcardYieldOverrideEntity);
		const FMassZoneGraphLaneLocationFragment& WildcardYieldOverrideLaneLocationFragment = WildcardYieldOverrideEntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
		
		FZoneGraphLaneLocation WildcardYieldOverrideEntityZoneGraphLaneLocation;
		
		if (UE::ZoneGraph::Query::CalculateLocationAlongLane(*WildcardYieldOverrideZoneGraphStorage, WildcardYieldOverrideLane, WildcardYieldOverrideLaneLocationFragment.DistanceAlongLane, WildcardYieldOverrideEntityZoneGraphLaneLocation))
		{
			const FColor& YieldOverrideColor = PedestrianEntities.Contains(WildcardYieldOverrideEntity) ? PedestrianYieldOverrideColor : VehicleYieldOverrideColor;

			DebugDrawCalls.Add([
				World=&World,
				WildcardYieldOverrideEntityPosition=WildcardYieldOverrideEntityZoneGraphLaneLocation.Position,
				IndicatorOffset,
				YieldOverrideColor,
				LifeTime]()
			{
				DrawDebugSphere(World, WildcardYieldOverrideEntityPosition + IndicatorOffset, 20.0f, 16, YieldOverrideColor, false, LifeTime, 0, 10.0f);
			});
		}
	}
	
	for (const TTuple<FLaneEntityPair, TSet<FLaneEntityPair>>& YieldOverridePair : YieldOverrideMap)
	{
		const FLaneEntityPair& YieldOverrideLaneEntityPair = YieldOverridePair.Key;
		
		DebugDrawCalls.Add([
			&ZoneGraphSubsystem,
			YieldOverrideLane=YieldOverrideLaneEntityPair.LaneHandle,
			World=&World,
			LifeTime]()
		{
			UE::MassTraffic::DrawLaneData(ZoneGraphSubsystem, YieldOverrideLane, FColor::Blue, *World, 10.0f, LifeTime);
		});
	}

	AsyncTask(ENamedThreads::GameThread, [DebugDrawCalls]()
	{
		for (const auto& DebugDrawCall : DebugDrawCalls)
		{
			DebugDrawCall();
		}
	});
}
