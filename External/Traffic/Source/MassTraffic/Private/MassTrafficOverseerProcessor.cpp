// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficOverseerProcessor.h"
#include "MassTraffic.h"
#include "MassExecutionContext.h"
#include "MassEntityView.h"
#include "MassClientBubbleHandler.h"
#include "MassCommonFragments.h"
#include "MassNavigationFragments.h"
#include "MassRepresentationFragments.h"
#include "MassTrafficFieldOperations.h"
#include "MassTrafficMovement.h"
#include "MassTrafficInterpolation.h"
#include "MassTrafficLaneChange.h"
#include "MassTrafficVehicleSimulationTrait.h"
#include "MassZoneGraphNavigationFragments.h"
#include "ZoneGraphSubsystem.h"
#include "MassGameplayExternalTraits.h"
#include "Algo/MinElement.h"
#include "Kismet/GameplayStatics.h"

// Stats
DECLARE_DWORD_COUNTER_STAT(TEXT("Empty Lanes"), STAT_Traffic_EmptyLanes, STATGROUP_Traffic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Occupied Lanes"), STAT_Traffic_OccupiedLanes, STATGROUP_Traffic);

UMassTrafficOverseerProcessor::UMassTrafficOverseerProcessor()
	: RecyclableTrafficVehicleEntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::FrameStart;
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficFrameStartFieldOperationsProcessor::StaticClass()->GetFName());
}

void UMassTrafficOverseerProcessor::ConfigureQueries()
{
	RecyclableTrafficVehicleEntityQuery.AddTagRequirement<FMassTrafficRecyclableVehicleTag>(EMassFragmentPresence::All);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadOnly);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FMassTrafficNextVehicleFragment>(EMassFragmentAccess::ReadWrite);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FMassTrafficVehicleLaneChangeFragment>(EMassFragmentAccess::ReadWrite);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadWrite);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FMassTrafficVehicleLightsFragment>(EMassFragmentAccess::ReadWrite);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FMassTrafficInterpolationFragment>(EMassFragmentAccess::ReadWrite);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FMassTrafficObstacleAvoidanceFragment>(EMassFragmentAccess::ReadWrite);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FMassTrafficVehicleDamageFragment>(EMassFragmentAccess::ReadWrite);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadWrite);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FMassTrafficLaneOffsetFragment>(EMassFragmentAccess::ReadWrite);
	RecyclableTrafficVehicleEntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
	RecyclableTrafficVehicleEntityQuery.AddConstSharedRequirement<FMassTrafficVehicleSimulationParameters>();
	RecyclableTrafficVehicleEntityQuery.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
	RecyclableTrafficVehicleEntityQuery.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);

	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
	ProcessorRequirements.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UMassTrafficOverseerProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Skip density management?
	if (GMassTrafficOverseer <= 0)
	{
		return;
	}

	
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("TrafficOverseer"))
	
	UWorld* World = GetWorld();
	const UZoneGraphSubsystem& LocalZoneGraphSubsystem = Context.GetSubsystemChecked<UZoneGraphSubsystem>();
	UMassTrafficSubsystem& LocalMassTrafficSubsystem = Context.GetMutableSubsystemChecked<UMassTrafficSubsystem>();

	// There is no point doing density management if there are no cars to manage.
	if (!LocalMassTrafficSubsystem.HasTrafficVehicleAgents())
	{
		return;			   		 
	}

	// Get player view location. Note: This implementation only supports a single viewer.
	APlayerController* LocalPlayerController = nullptr;
	for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PlayerController = Iterator->Get();
		if (PlayerController && PlayerController->IsLocalController())
		{
			LocalPlayerController = PlayerController;
			break;
		}
	}
	if (!LocalPlayerController)
	{
		return;
	}
	FVector LocalPlayerViewLocation(ForceInitToZero);
	FRotator LocalPlayerViewRotation(ForceInit);
	LocalPlayerController->GetPlayerViewPoint(LocalPlayerViewLocation, LocalPlayerViewRotation);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("FindTransferLanes"))

		// Reset scratch buffers
		BusiestLanes.Reset(MassTrafficSettings->NumBusiestLanesToTransferFrom);
		BusiestLaneDensityExcesses.Reset(MassTrafficSettings->NumBusiestLanesToTransferFrom);
		LeastBusiestLanes.Reset(MassTrafficSettings->NumLeastBusiestLanesToTransferTo);
		LeastBusiestLaneDensities.Reset(MassTrafficSettings->NumLeastBusiestLanesToTransferTo);
		LeastBusiestLaneLocations.Reset(MassTrafficSettings->NumLeastBusiestLanesToTransferTo);

		// Find NumManagedLanes busies & least busiest lanes
		float MinBusiestLaneDensityExcess = TNumericLimits<float>::Max();
		int32 MinBusiestLaneDensityExcessIndex = 0;
		float MaxLeastBusiestLaneDensity = TNumericLimits<float>::Min();
		int32 MaxLeastBusiestLaneDensityIndex = 0;
		for (FMassTrafficZoneGraphData* TrafficZoneGraphData : LocalMassTrafficSubsystem.GetMutableTrafficZoneGraphData())
		{
			// Get partition of lanes to operate on this frame to amortise performance costs across several frames  
			const int32 PartitionSize = FMath::DivideAndRoundUp(TrafficZoneGraphData->TrafficLaneDataArray.Num(), MassTrafficSettings->NumDensityManagementLanePartitions);
			const int32 TrafficLanePartitionStart = PartitionSize * PartitionIndex;
			const TArrayView<FZoneGraphTrafficLaneData> TrafficLanesPartition = MakeArrayView(TrafficZoneGraphData->TrafficLaneDataArray).Mid(TrafficLanePartitionStart, PartitionSize); 
			for (FZoneGraphTrafficLaneData& TrafficLaneData : TrafficLanesPartition) 
			{
				// Make sure this lane is viable for teleporting cars, there are various reasons we can't:
				const bool bOKToTeleport =
					// Don't transfer from / to merging or splitting lanes
					TrafficLaneData.MergingLanes.IsEmpty() && 
					TrafficLaneData.SplittingLanes.IsEmpty() &&
					// Don't transfer from / to lanes with in progress lane changes
					TrafficLaneData.NumVehiclesLaneChangingOffOfLane == 0 &&
					TrafficLaneData.NumVehiclesLaneChangingOntoLane == 0 &&
					// Don't transfer from / to lanes that are downstream from active intersection lanes
					!UE::MassTraffic::AreVehiclesCurrentlyApproachingLaneFromIntersection(TrafficLaneData); 

				if (!bOKToTeleport)
				{
					continue;
				}

				// Sort lanes based on how far above their max densities they are
				const float BasicLaneDensity = TrafficLaneData.BasicDensity();
				const float FunctionalLaneDensity = TrafficLaneData.FunctionalDensity();
				const float LaneDensityExcess = BasicLaneDensity - TrafficLaneData.MaxDensity;

				// Test distance to player
				const float DistanceToPlayer = FMath::Max(FVector::Distance(TrafficLaneData.CenterLocation, LocalPlayerViewLocation) - TrafficLaneData.Radius, 0.0f);
				const bool bIsInBusiestLaneDistanceRange = MassTrafficSettings->BusiestLaneDistanceToPlayerRange.Contains(DistanceToPlayer);
				const bool bIsInLeastBusiestLaneDistanceRange = MassTrafficSettings->LeastBusiestLaneDistanceToPlayerRange.Contains(DistanceToPlayer);
				
				// Collect NumBusiestLanesToTransferFrom of the busiest lanes
				if (
					// Is in range to player 
					bIsInBusiestLaneDistanceRange
					// Is lane in excess of it's max density?
					&& LaneDensityExcess >= 0.0f
					// In the trunk lanes phase, only transfer from trunk lanes so we don't transfer trunk-lane-only
					// vehicles onto non trunk lanes. Outside the trunk lanes phase, we still transfer vehicles off
					// trunk lanes but make sure to skip restricted vehicles  
					&& (!bTrunkLanesPhase || TrafficLaneData.ConstData.bIsTrunkLane)
				)
				{
					if (BusiestLanes.Num() < MassTrafficSettings->NumBusiestLanesToTransferFrom)
					{
						BusiestLanes.Add(&TrafficLaneData);
						BusiestLaneDensityExcesses.Add(LaneDensityExcess);
						if (LaneDensityExcess < MinBusiestLaneDensityExcess)
						{
							MinBusiestLaneDensityExcess = LaneDensityExcess;
							MinBusiestLaneDensityExcessIndex = BusiestLanes.Num() - 1;
						}
					}
					else if (LaneDensityExcess > MinBusiestLaneDensityExcess)
					{
						// Write over the current min, effectively popping it out of the list
						BusiestLanes[MinBusiestLaneDensityExcessIndex] = &TrafficLaneData;
						BusiestLaneDensityExcesses[MinBusiestLaneDensityExcessIndex] = LaneDensityExcess;

						// Find the new min
						MinBusiestLaneDensityExcess = TNumericLimits<float>::Max();
						for (int32 BusiestLanesIndex = 0; BusiestLanesIndex < BusiestLanes.Num(); ++BusiestLanesIndex)
						{
							const float& BusiestLaneDensity = BusiestLaneDensityExcesses[BusiestLanesIndex];
							if (BusiestLaneDensity < MinBusiestLaneDensityExcess)
							{
								MinBusiestLaneDensityExcess = BusiestLaneDensity;
								MinBusiestLaneDensityExcessIndex = BusiestLanesIndex;
							}
						}
					}
				}

				// Collect NumLeastBusiestLanesToTransferTo of the least busiest lanes
				// Note: We don't allow intersection lanes as target lanes to avoid the complexity of obeying
				//		intersection logic.
				if (
					// Is in range to player?
					bIsInLeastBusiestLaneDistanceRange
					// Enough space to bother trying to transfer here?
					&& FunctionalLaneDensity <= MassTrafficSettings->LeastBusiestLaneMaxDensity
					// Only transfer onto open lanes
					&& TrafficLaneData.bIsOpen
					// Never transfer onto intersection lanes
					&& !TrafficLaneData.ConstData.bIsIntersectionLane
					// In the trunk lanes phase, only consider trunk lanes to transfer onto, so we don't put
					// trunk-lane-only vehicles onto non-trunk lanes
					&& (!bTrunkLanesPhase || TrafficLaneData.ConstData.bIsTrunkLane)
				)
				{
					if (LeastBusiestLanes.Num() < MassTrafficSettings->NumLeastBusiestLanesToTransferTo)
					{
						LeastBusiestLanes.Add(&TrafficLaneData);
						LeastBusiestLaneDensities.Add(FunctionalLaneDensity);
						LeastBusiestLaneLocations.Add(TrafficLaneData.CenterLocation);
						if (FunctionalLaneDensity > MaxLeastBusiestLaneDensity)
						{
							MaxLeastBusiestLaneDensity = FunctionalLaneDensity;
							MaxLeastBusiestLaneDensityIndex = LeastBusiestLanes.Num() - 1;
						}
					}
					else if (FunctionalLaneDensity < MaxLeastBusiestLaneDensity)
					{
						// Write over the current max, effectively popping it out of the list
						LeastBusiestLanes[MaxLeastBusiestLaneDensityIndex] = &TrafficLaneData;
						LeastBusiestLaneDensities[MaxLeastBusiestLaneDensityIndex] = FunctionalLaneDensity;
						LeastBusiestLaneLocations[MaxLeastBusiestLaneDensityIndex] = TrafficLaneData.CenterLocation;

						// Find the new min
						MaxLeastBusiestLaneDensity = TNumericLimits<float>::Min();
						for (int32 LeastBusiestLanesIndex = 0; LeastBusiestLanesIndex < LeastBusiestLanes.Num(); ++LeastBusiestLanesIndex)
						{
							const float& LeastBusiestLaneDensity = LeastBusiestLaneDensities[LeastBusiestLanesIndex];
							if (LeastBusiestLaneDensity > MaxLeastBusiestLaneDensity)
							{
								MaxLeastBusiestLaneDensity = LeastBusiestLaneDensity;
								MaxLeastBusiestLaneDensityIndex = LeastBusiestLanesIndex;
							}
						}
					}
				}
			}
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("TransferRecyclableVehicles"))
		RecyclableTrafficVehicleEntityQuery.ForEachEntityChunk(EntityManager, Context, [&EntityManager, World, this](FMassExecutionContext& QueryContext)
		{
			const UZoneGraphSubsystem& ZoneGraphSubsystem = QueryContext.GetSubsystemChecked<UZoneGraphSubsystem>();
			UMassTrafficSubsystem& MassTrafficSubsystem = QueryContext.GetMutableSubsystemChecked<UMassTrafficSubsystem>();

			const int32 NumEntities = QueryContext.GetNumEntities();
			const TConstArrayView<FAgentRadiusFragment> RadiusFragments = QueryContext.GetFragmentView<FAgentRadiusFragment>();
			const TConstArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = QueryContext.GetFragmentView<FMassTrafficRandomFractionFragment>();
			const TArrayView<FMassTrafficNextVehicleFragment> NextVehicleFragments = QueryContext.GetMutableFragmentView<FMassTrafficNextVehicleFragment>();
			const TArrayView<FMassTrafficVehicleLaneChangeFragment> LaneChangeFragments = QueryContext.GetMutableFragmentView<FMassTrafficVehicleLaneChangeFragment>();
			const TArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = QueryContext.GetMutableFragmentView<FMassTrafficVehicleControlFragment>();
			const TArrayView<FMassTrafficVehicleLightsFragment> VehicleLightsFragments = QueryContext.GetMutableFragmentView<FMassTrafficVehicleLightsFragment>();
			const TArrayView<FMassTrafficInterpolationFragment> InterpolationFragments = QueryContext.GetMutableFragmentView<FMassTrafficInterpolationFragment>();
			const TArrayView<FTransformFragment> TransformFragments = QueryContext.GetMutableFragmentView<FTransformFragment>();
			const TArrayView<FMassTrafficVehicleDamageFragment> VehicleDamageFragments = QueryContext.GetMutableFragmentView<FMassTrafficVehicleDamageFragment>();
			const TArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = QueryContext.GetMutableFragmentView<FMassZoneGraphLaneLocationFragment>();
			const TArrayView<FMassTrafficLaneOffsetFragment> LaneOffsetFragments = QueryContext.GetMutableFragmentView<FMassTrafficLaneOffsetFragment>();
			const TArrayView<FMassTrafficObstacleAvoidanceFragment> AvoidanceFragments = QueryContext.GetMutableFragmentView<FMassTrafficObstacleAvoidanceFragment>();
			const TArrayView<FMassRepresentationFragment> RepresentationFragments = QueryContext.GetMutableFragmentView<FMassRepresentationFragment>();
			
			for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
			{
				const FMassEntityHandle RecyclableTrafficVehicle = QueryContext.GetEntity(EntityIndex);
				
				const FAgentRadiusFragment& RadiusFragment = RadiusFragments[EntityIndex];
				const FMassTrafficRandomFractionFragment& RandomFractionFragment = RandomFractionFragments[EntityIndex];
				FMassTrafficNextVehicleFragment& NextVehicleFragment = NextVehicleFragments[EntityIndex];
				FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment = LaneChangeFragments[EntityIndex];
				FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[EntityIndex];
				FMassTrafficVehicleLightsFragment& VehicleLightsFragment = VehicleLightsFragments[EntityIndex];
				FMassTrafficInterpolationFragment& InterpolationFragment = InterpolationFragments[EntityIndex];
				FTransformFragment& TransformFragment = TransformFragments[EntityIndex];
				FMassTrafficVehicleDamageFragment& VehicleDamageFragment = VehicleDamageFragments[EntityIndex];
				FMassZoneGraphLaneLocationFragment& LaneLocationFragment = LaneLocationFragments[EntityIndex];
				FMassTrafficLaneOffsetFragment& LaneOffsetFragment = LaneOffsetFragments[EntityIndex];
				FMassTrafficObstacleAvoidanceFragment& AvoidanceFragment = AvoidanceFragments[EntityIndex];
				FMassRepresentationFragment& RepresentationFragment = RepresentationFragments[EntityIndex];

				const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(LaneLocationFragment.LaneHandle.DataHandle);
				check(ZoneGraphStorage);

				// Only transfer vehicles when in the right phase, we don't want to recycle trucks into the city!
				if (VehicleControlFragment.bRestrictedToTrunkLanesOnly && !bTrunkLanesPhase)
				{
					continue;
				}

				// Make sure we cancel any lane changing before recycling this vehicle
				if (LaneChangeFragment.IsLaneChangeInProgress())
				{
					LaneChangeFragment.EndLaneChangeProgression(VehicleLightsFragment, NextVehicleFragment, EntityManager);
				}

				// Get current lane we're recycling from
				FZoneGraphTrafficLaneData& Vehicle_CurrentLane = MassTrafficSubsystem.GetMutableTrafficLaneDataChecked(LaneLocationFragment.LaneHandle);

				// Get vehicle behind and ahead on Vehicle_CurrentLane 
				FMassEntityHandle PreviousVehicleOnLane;
				FMassEntityHandle NextVehicleOnLane;
				if (!UE::MassTraffic::FindNearbyVehiclesOnLane_RelativeToVehicleEntity(&Vehicle_CurrentLane, RecyclableTrafficVehicle, NextVehicleFragment, PreviousVehicleOnLane, NextVehicleOnLane, EntityManager, /*VisLogOwner*/LogOwner))
				{
					// Error condition. Try again next time.
					continue;
				}
				FMassTrafficNextVehicleFragment* PreviousVehicleOnLane_NextVehicleFragment = PreviousVehicleOnLane.IsSet() ? &EntityManager.GetFragmentDataChecked<FMassTrafficNextVehicleFragment>(PreviousVehicleOnLane) : nullptr;

				// Try and move the vehicle to one of the least busiest lanes off screen
				const bool bTransferred = MoveVehicleToFreeSpaceOnRandomLane(EntityManager, *ZoneGraphStorage,
					RecyclableTrafficVehicle,
					RadiusFragment,
					RandomFractionFragment,
					NextVehicleFragment,
					VehicleControlFragment,
					InterpolationFragment,
					TransformFragment,
					LaneLocationFragment,
					LaneOffsetFragment,
					AvoidanceFragment,
					RepresentationFragment,
					Vehicle_CurrentLane,
					PreviousVehicleOnLane,
					PreviousVehicleOnLane_NextVehicleFragment,
					NextVehicleOnLane,
					LeastBusiestLanes, LeastBusiestLaneLocations);

				// Debug
				UE::MassTraffic::DrawDebugDensityManagementRecyclableVehicle(World, TransformFragment.GetTransform().GetLocation(), bTransferred, /*bVisLog*/false, LogOwner);

				// If the transfer was successful, flip this back to being a full traffic vehicle
				if (bTransferred)
				{
					// Reset damage state 
					VehicleDamageFragment.VehicleDamageState = EMassTrafficVehicleDamageState::None;
						
					// Completed agent recycling, back to business
					QueryContext.Defer().SwapTags<FMassTrafficRecyclableVehicleTag, FMassTrafficVehicleTag>(RecyclableTrafficVehicle);
				}
			}
		});
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("TransferBusiestLaneVehicles"))
		
		// Transfer cars from BusiestLanes to LeastBusiestLanes
		for (FZoneGraphTrafficLaneData* BusiestLane : BusiestLanes)
		{
			const float BusiestLaneBasicDensity = BusiestLane->BasicDensity();
			
			// Sanity checks to prevent division by zero
			if (BusiestLaneBasicDensity <= 0.0f || BusiestLane->NumVehiclesOnLane <= 0)
				continue;

			// Collect vehicles from the busiest lane, to transfer. We collect them into an array
			// by walking along the lane, before we then change / break the links in the next step below
			const float BasicLaneCapacityEstimate = static_cast<float>(BusiestLane->NumVehiclesOnLane) / BusiestLaneBasicDensity;
			const int32 MaxLaneCapacityEstimate = FMath::FloorToInt(BasicLaneCapacityEstimate * BusiestLane->MaxDensity);
			const int32 NumVehiclesToTransfer = FMath::Max(0, BusiestLane->NumVehiclesOnLane - MaxLaneCapacityEstimate);
			BusiestLaneVehiclesToTransfer.Reset(NumVehiclesToTransfer);
			BusiestLane->ForEachVehicleOnLane(EntityManager, [&](const FMassEntityView& BusiestLaneVehicle_EntityView, struct FMassTrafficNextVehicleFragment& BusiestLaneVehicle_NextVehicleFragment, struct FMassZoneGraphLaneLocationFragment& BusiestLaneVehicle_LaneLocationFragment)
			{
				// If there are any > Off LOD vehicles in the chain, we abandon this lane entirely as removing just the
				// invisible vehicles could have still have visible effects on the visible ones.
				const FMassTrafficSimulationLODFragment& SimulationLODFragment = BusiestLaneVehicle_EntityView.GetFragmentData<FMassTrafficSimulationLODFragment>();
				if (SimulationLODFragment.LOD < EMassLOD::Off)
				{
					BusiestLaneVehiclesToTransfer.Reset();
					return false;
				}

				// Don't transfer restricted vehicles off of trunk lanes outside the trunk lanes phase. If we aren't in
				// the trunk lane phase though, we still want to try and transfer non-restricted vehicles off of trunk
				// lanes to spread them out onto non-trunk lanes 
				if (!bTrunkLanesPhase && BusiestLane->ConstData.bIsTrunkLane)
				{
					const FMassTrafficVehicleSimulationParameters& VehicleSimulationParams = BusiestLaneVehicle_EntityView.GetConstSharedFragmentData<FMassTrafficVehicleSimulationParameters>();
					if (VehicleSimulationParams.bRestrictedToTrunkLanesOnly)
					{
						// We can't transfer any more beyond this vehicle as we rely on removing contiguous lines of
						// vehicles to avoid having to sew holes in lane links
						return false;
					}
				}

				BusiestLaneVehiclesToTransfer.Add(BusiestLaneVehicle_EntityView);

				// Continue if there's more vehicles to collect
				return (BusiestLaneVehiclesToTransfer.Num() < NumVehiclesToTransfer);
			});

			// Skip this lane if no vehicles to transfer in the end (e.g: if one of them was visible)
			if (BusiestLaneVehiclesToTransfer.IsEmpty())
			{
				continue;
			}

			// Extract collected vehicles off of BusiestLane
			for (int32 BusiestLaneVehicleIndex = 0; BusiestLaneVehicleIndex < BusiestLaneVehiclesToTransfer.Num(); ++BusiestLaneVehicleIndex)
			{
				const FMassEntityView& BusiestLaneVehicle_EntityView = BusiestLaneVehiclesToTransfer[BusiestLaneVehicleIndex];
				const FAgentRadiusFragment& BusiestLaneVehicle_RadiusFragment = BusiestLaneVehicle_EntityView.GetFragmentData<FAgentRadiusFragment>();
				const FMassTrafficRandomFractionFragment& BusiestLaneVehicle_RandomFractionFragment = BusiestLaneVehicle_EntityView.GetFragmentData<FMassTrafficRandomFractionFragment>();
				FMassTrafficNextVehicleFragment& BusiestLaneVehicle_NextVehicleFragment = BusiestLaneVehicle_EntityView.GetFragmentData<FMassTrafficNextVehicleFragment>();
				FMassTrafficVehicleControlFragment& BusiestLaneVehicle_VehicleControlFragment = BusiestLaneVehicle_EntityView.GetFragmentData<FMassTrafficVehicleControlFragment>();
				FMassTrafficInterpolationFragment& BusiestLaneVehicle_InterpolationFragment = BusiestLaneVehicle_EntityView.GetFragmentData<FMassTrafficInterpolationFragment>();
				FTransformFragment& BusiestLaneVehicle_TransformFragment = BusiestLaneVehicle_EntityView.GetFragmentData<FTransformFragment>();
				FMassZoneGraphLaneLocationFragment& BusiestLaneVehicle_LaneLocationFragment = BusiestLaneVehicle_EntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
				FMassTrafficLaneOffsetFragment& BusiestLaneVehicle_LaneOffsetFragment = BusiestLaneVehicle_EntityView.GetFragmentData<FMassTrafficLaneOffsetFragment>();
				FMassTrafficObstacleAvoidanceFragment& BusiestLaneVehicle_AvoidanceFragment = BusiestLaneVehicle_EntityView.GetFragmentData<FMassTrafficObstacleAvoidanceFragment>();
				FMassRepresentationFragment& BusiestLaneVehicle_RepresentationFragment = BusiestLaneVehicle_EntityView.GetFragmentData<FMassRepresentationFragment>();

				const FZoneGraphStorage* ZoneGraphStorage = LocalZoneGraphSubsystem.GetZoneGraphStorage(BusiestLaneVehicle_LaneLocationFragment.LaneHandle.DataHandle);
				check(ZoneGraphStorage);

				// As we progressively pluck vehicles off the lane starting from the tail, the next one should always
				// be the tail
				check(BusiestLane->TailVehicle == BusiestLaneVehicle_EntityView.GetEntity());

				// Get the next vehicle ahead, but only if it's on BusiestLane
				FMassEntityHandle NextVehicleOnBusiestLane = BusiestLaneVehicle_NextVehicleFragment.GetNextVehicle();
				if (NextVehicleOnBusiestLane.IsSet() && EntityManager.GetFragmentDataChecked<FMassZoneGraphLaneLocationFragment>(NextVehicleOnBusiestLane).LaneHandle != BusiestLane->LaneHandle)
				{
					NextVehicleOnBusiestLane.Reset();
				}
				
				// Try and move the vehicle to one of the least busiest lanes
				const bool bTransferred = MoveVehicleToFreeSpaceOnRandomLane(EntityManager, *ZoneGraphStorage,
				                                                             BusiestLaneVehicle_EntityView.GetEntity(),
				                                                             BusiestLaneVehicle_RadiusFragment,
				                                                             BusiestLaneVehicle_RandomFractionFragment,
				                                                             BusiestLaneVehicle_NextVehicleFragment,
				                                                             BusiestLaneVehicle_VehicleControlFragment,
				                                                             BusiestLaneVehicle_InterpolationFragment,
				                                                             BusiestLaneVehicle_TransformFragment,
				                                                             BusiestLaneVehicle_LaneLocationFragment,
				                                                             BusiestLaneVehicle_LaneOffsetFragment,
				                                                             BusiestLaneVehicle_AvoidanceFragment,
				                                                             BusiestLaneVehicle_RepresentationFragment,
				                                                             // Never anyone behind as we remove vehicles from the tail vehicle forward, so the next transferred vehicle is always the tail
				                                                             /*PreviousVehicleOnLane*/ *BusiestLane, 
				                                                             // Never anyone behind as we remove vehicles from the tail vehicle forward, so the next transferred vehicle is always the tail
				                                                             /*PreviousVehicleOnLane*/ FMassEntityHandle(),
				                                                             /*PreviousVehicleOnLane_NextVehicleFragment*/ nullptr,
				                                                             NextVehicleOnBusiestLane,
				                                                             LeastBusiestLanes, LeastBusiestLaneLocations);
				
				// If we couldn't transfer this vehicle, we implicitly can't transfer the rest as we assume to have
				// always just removed the one prior (and don't have to worry about sewing up holes in the lane)
				if (!bTransferred) 
				{
					// As we decided not to move this vehicle, it should still be the tail 
					check(BusiestLane->TailVehicle == BusiestLaneVehicle_EntityView.GetEntity());
					
					break;
				}
			}
		}
	}

	// Advance frame index for next frame
	PartitionIndex = (PartitionIndex + 1) % MassTrafficSettings->NumDensityManagementLanePartitions;

	// If we've done a full loop of partitions, flip/flop to/from trunk lanes only phase
	if (PartitionIndex == 0)
	{
		bTrunkLanesPhase = !bTrunkLanesPhase;
	}
}

bool UMassTrafficOverseerProcessor::MoveVehicleToFreeSpaceOnRandomLane(
	const FMassEntityManager& EntityManager,
	const FZoneGraphStorage& ZoneGraphStorage,
	const FMassEntityHandle VehicleEntity,
	const FAgentRadiusFragment& Vehicle_RadiusFragment,
	const FMassTrafficRandomFractionFragment& Vehicle_RandomFractionFragment,
	FMassTrafficNextVehicleFragment& Vehicle_NextVehicleFragment,
	FMassTrafficVehicleControlFragment& Vehicle_VehicleControlFragment,
	FMassTrafficInterpolationFragment& Vehicle_InterpolationFragment,
	FTransformFragment& Vehicle_TransformFragment,
	FMassZoneGraphLaneLocationFragment& Vehicle_LaneLocationFragment,
	const FMassTrafficLaneOffsetFragment& Vehicle_LaneOffsetFragment,
	FMassTrafficObstacleAvoidanceFragment& Vehicle_AvoidanceFragment,
	FMassRepresentationFragment& Vehicle_RepresentationFragment,
	FZoneGraphTrafficLaneData& Vehicle_CurrentLane,
	FMassEntityHandle PreviousVehicleOnLane,
	FMassTrafficNextVehicleFragment* PreviousVehicleOnLane_NextVehicleFragment,
	FMassEntityHandle NextVehicleOnLane,
	const TArray<FZoneGraphTrafficLaneData*>& CandidateLanes,
	const TArray<FVector>& CandidateLaneLocations, bool bVisLog
) const
{
	const float VehicleLength = Vehicle_RadiusFragment.Radius * 2.0f;

	// Pick a random lane from CandidateLanes, with a large enough open space, to transfer to 
	bool bTransferred = false;
	const int32 RandomOffset = RandomStream.RandHelper(CandidateLanes.Num());
	for (int32 i = 0; i < CandidateLanes.Num() && !bTransferred; ++i)
	{
		const int32 CandidateLaneIndex = (RandomOffset + i) % CandidateLanes.Num();
		FZoneGraphTrafficLaneData& CandidateLane = *CandidateLanes[CandidateLaneIndex];
		if (CandidateLane.LaneHandle == Vehicle_CurrentLane.LaneHandle)
		{
			continue;
		}

		// Is lane far enough away from this vehicle?
		const float DistanceToLane = FVector::Distance(CandidateLaneLocations[i], Vehicle_TransformFragment.GetTransform().GetLocation());
		if (DistanceToLane < MassTrafficSettings->MinTransferDistance)
		{
			continue;
		}

		// Is lane empty?
		if (!CandidateLane.TailVehicle.IsSet())
		{
			// Pick a random spot along the empty lane
			const float MinDistanceAlongCandidateLane = Vehicle_RadiusFragment.Radius;
			const float MaxDistanceAlongCandidateLane = CandidateLane.Length - Vehicle_RadiusFragment.Radius;
			const float DistanceAlongCandidateLane = RandomStream.FRandRange(MinDistanceAlongCandidateLane, MaxDistanceAlongCandidateLane);

			// Transfer the vehicle to this lane
			const bool bWasTeleportSuccessful = UE::MassTraffic::TeleportVehicleToAnotherLane(
				VehicleEntity,
				Vehicle_CurrentLane,
				Vehicle_VehicleControlFragment,
				Vehicle_RadiusFragment,
				Vehicle_RandomFractionFragment,
				Vehicle_LaneLocationFragment,
				Vehicle_NextVehicleFragment,
				Vehicle_AvoidanceFragment,
				CandidateLane,
				DistanceAlongCandidateLane,
				/*Entity_Current_Behind*/PreviousVehicleOnLane,   
				/*NextVehicleFragment_Current_Behind*/PreviousVehicleOnLane_NextVehicleFragment,
				/*Entity_Current_Ahead*/NextVehicleOnLane,
				/*Entity_Chosen_Behind*/FMassEntityHandle(),
				/*NextVehicleFragment_Chosen_Behind*/nullptr,
				/*AgentRadiusFragment_Chosen_Behind*/nullptr,
				/*ZoneGraphLaneLocationFragment_Chosen_Behind*/nullptr,
				/*AvoidanceFragment_Chosen_Behind*/nullptr,
				/*Entity_Chosen_Ahead*/FMassEntityHandle(),
				/*ZoneGraphLaneLocationFragment_Chosen_Ahead*/nullptr,
				/*AgentRadiusFragment_Chosen_Ahead*/nullptr,
				*MassTrafficSettings,
				EntityManager);

			if (bWasTeleportSuccessful)
			{
				// We should be the tail on the new lane now
				check(CandidateLane.TailVehicle == VehicleEntity);
				
				// We transferred a vehicle
				bTransferred = true;
			}
		}
		else
		{
			// Walk along CandidateLane looking for the first vehicle with enough space in front of it.
			// Note: Even if we find a free spot, we keep looking along the lane to ensure there aren't any
			// subsequent vehicles along the lane already pointing to VehicleEntity as their NextVehicle, as this could
			// create infinite loops.
			FMassEntityView CandidateLaneVehicleBehind_EntityView;
			CandidateLane.ForEachVehicleOnLane(EntityManager, [&](const FMassEntityView& CandidateLaneVehicle_EntityView, const FMassTrafficNextVehicleFragment& CandidateLaneVehicle_NextVehicleFragment, const FMassZoneGraphLaneLocationFragment& CandidateLaneVehicle_LaneLocationFragment)
			{
				// If a vehicle on CandidateLane is already referencing VehicleEntity as its Next, abort the transfer
				// to this lane (and try another) as inserting here would then create an infinite loop. 
				// 
				// Details: When moving vehicles from lane to lane, either here or when lane changing, as we
				// can only look for the previous vehicle on the current lane, we can miss vehicles pointing
				// to us from the previous lane. As such, those previous vehicles are left with
				// their NextVehicle links to us, until they move onto another lane and get a new one.
				// Usually this is fine and we leave it happen by design. 
				//
				// Here though, in rare cases, we can happen to find this old vehicle on CandidateLane,
				// still pointing to us as its NextVehicle, whereupon we would then try and insert
				// ourselves in front of it and try to follow ourselves, creating an infinite loop
				// 
				//  e.g:
				//
				//                  CandidateLaneVehicle_NextVehicle == BusiestLaneVehicle
				//                                            ^
				//                                            |
				//        BusiestLaneVehicle   -------------> | 
				//                                            |
				//                                CandidateLaneVehicle
				//
				// In similarly rare circumstances we could also try to teleport *behind* a vehicle already
				// pointing to VehicleEntity as its next, thus creating a short 2 vehicle infinite loop
				//  e.g:
				//  
				//                CandidateLaneVehicle_NextVehicle's NextVehicle == BusiestLaneVehicle
				//                                            ^
				//                                            |
				//                                            |
				//                                            |
				//                             CandidateLaneVehicle_NextVehicle
				//                                            ^
				//                                            |
				//     BusiestLaneVehicle      -------------> | 
				//                                            |
				//                                CandidateLaneVehicle
				//
				//  Note: If we tracked explicit PreviousVehicle references, we would could properly
				//		  break the link to our previous vehicle even when on the previous lane and
				//		  this wouldn't happen anymore
				if (CandidateLaneVehicle_NextVehicleFragment.GetNextVehicle() == VehicleEntity)
				{
					// Cancel selection of CandidateLaneVehicleBehind to insert ahead of and abort looking any further
					// along this lane.
					CandidateLaneVehicleBehind_EntityView = FMassEntityView();
					return false;
				}
				
				// Skip Off LOD vehicles
				const FMassTrafficSimulationLODFragment& CandidateLaneVehicle_SimulationLODFragment = CandidateLaneVehicle_EntityView.GetFragmentData<FMassTrafficSimulationLODFragment>();
				if (CandidateLaneVehicle_SimulationLODFragment.LOD < EMassLOD::Off)
				{
					return true;
				}

				// Are we still looking for an empty space?
				if (!CandidateLaneVehicleBehind_EntityView.IsSet())
				{
					// Is there space for VehicleEntity in front of CandidateLaneVehicleEntity before its next
					// vehicle or the lane end?
					const FAgentRadiusFragment& CandidateLaneVehicle_RadiusFragment = CandidateLaneVehicle_EntityView.GetFragmentData<FAgentRadiusFragment>();
					const FMassTrafficObstacleAvoidanceFragment& CandidateLaneVehicle_AvoidanceFragment = CandidateLaneVehicle_EntityView.GetFragmentData<FMassTrafficObstacleAvoidanceFragment>();
					if (CandidateLaneVehicle_AvoidanceFragment.DistanceToNext > VehicleLength
						&& CandidateLaneVehicle_LaneLocationFragment.DistanceAlongLane + CandidateLaneVehicle_RadiusFragment.Radius + VehicleLength < CandidateLane.Length)
					{
						CandidateLaneVehicleBehind_EntityView = CandidateLaneVehicle_EntityView;
					}
				}

				// Continue along lane. Note: we continue along even if we've chosen a vehicle to insert ahead of, as
				// we still need to check the rest of the vehicles just in case they are already pointing to
				// VehicleEntity (see above)
				return true;
			});
			
			// Transfer the vehicle to this lane in front of CandidateLaneVehicleBehind_EntityView
			if (CandidateLaneVehicleBehind_EntityView.IsSet())
			{
				const FAgentRadiusFragment& CandidateLaneVehicle_RadiusFragment = CandidateLaneVehicleBehind_EntityView.GetFragmentData<FAgentRadiusFragment>();
				FMassTrafficObstacleAvoidanceFragment& CandidateLaneVehicle_AvoidanceFragment = CandidateLaneVehicleBehind_EntityView.GetFragmentData<FMassTrafficObstacleAvoidanceFragment>();
				FMassTrafficNextVehicleFragment& CandidateLaneVehicle_NextVehicleFragment = CandidateLaneVehicleBehind_EntityView.GetFragmentData<FMassTrafficNextVehicleFragment>();
				FMassZoneGraphLaneLocationFragment& CandidateLaneVehicle_LaneLocationFragment = CandidateLaneVehicleBehind_EntityView.GetFragmentData<FMassZoneGraphLaneLocationFragment>();
				FMassEntityHandle CandidateLaneVehicle_NextVehicle = CandidateLaneVehicle_NextVehicleFragment.GetNextVehicle();
				const FAgentRadiusFragment* NextCandidateLaneVehicle_RadiusFragment = nullptr;
				const FMassZoneGraphLaneLocationFragment* NextCandidateLaneVehicle_LaneLocationFragment = nullptr;
				if (CandidateLaneVehicle_NextVehicle.IsSet())
				{
					FMassEntityView NextMassEntityView(EntityManager, CandidateLaneVehicle_NextVehicle);
					NextCandidateLaneVehicle_RadiusFragment = NextMassEntityView.GetFragmentDataPtr<FAgentRadiusFragment>();
					NextCandidateLaneVehicle_LaneLocationFragment = NextMassEntityView.GetFragmentDataPtr<FMassZoneGraphLaneLocationFragment>();
				}

				// Insert VehicleEntity in space ahead of CandidateLaneVehicleEntity
				const float MinDistanceAlongCandidateLane = CandidateLaneVehicle_LaneLocationFragment.DistanceAlongLane + CandidateLaneVehicle_RadiusFragment.Radius + Vehicle_RadiusFragment.Radius;
				const float MaxDistanceAlongCandidateLane = FMath::Min(
					MinDistanceAlongCandidateLane + CandidateLaneVehicle_AvoidanceFragment.DistanceToNext - Vehicle_RadiusFragment.Radius,
					CandidateLane.Length - Vehicle_RadiusFragment.Radius);
				const float DistanceAlongCandidateLane = RandomStream.FRandRange(MinDistanceAlongCandidateLane, MaxDistanceAlongCandidateLane);
				
				bTransferred = UE::MassTraffic::TeleportVehicleToAnotherLane(
					VehicleEntity,
					Vehicle_CurrentLane,
					Vehicle_VehicleControlFragment,
					Vehicle_RadiusFragment,
					Vehicle_RandomFractionFragment,
					Vehicle_LaneLocationFragment,
					Vehicle_NextVehicleFragment,
					Vehicle_AvoidanceFragment,
					CandidateLane,
					DistanceAlongCandidateLane,
					/*Entity_Current_Behind*/PreviousVehicleOnLane,
					/*NextVehicleFragment_Current_Behind*/PreviousVehicleOnLane_NextVehicleFragment,
					/*Entity_Current_Ahead*/NextVehicleOnLane,
					/*Entity_Chosen_Behind*/CandidateLaneVehicleBehind_EntityView.GetEntity(),
					/*NextVehicleFragment_Chosen_Behind*/&CandidateLaneVehicle_NextVehicleFragment,
					/*AgentRadiusFragment_Chosen_Behind*/&CandidateLaneVehicle_RadiusFragment,
					/*ZoneGraphLaneLocationFragment_Chosen_Behind*/&CandidateLaneVehicle_LaneLocationFragment,
					/*AvoidanceFragment_Chosen_Behind*/&CandidateLaneVehicle_AvoidanceFragment,
					/*Entity_Chosen_Ahead*/CandidateLaneVehicle_NextVehicle,
					/*AgentRadiusFragment_Chosen_Ahead*/NextCandidateLaneVehicle_RadiusFragment,
					/*ZoneGraphLaneLocationFragment_Chosen_Ahead*/NextCandidateLaneVehicle_LaneLocationFragment,
					*MassTrafficSettings,
					EntityManager);
			}
		}
	}

	if (bTransferred)
	{
		// Interpolate new lane location to set as PrevTransform otherwise computed velocity
		// for this frame would be enormous.
		FTransform NewLaneLocationTransform;
		UE::MassTraffic::InterpolatePositionAndOrientationAlongLane(
			ZoneGraphStorage,
			Vehicle_LaneLocationFragment.LaneHandle.Index,
			Vehicle_LaneLocationFragment.DistanceAlongLane,
			ETrafficVehicleMovementInterpolationMethod::CubicBezier,
			Vehicle_InterpolationFragment.LaneLocationLaneSegment,
			NewLaneLocationTransform);
		NewLaneLocationTransform.AddToTranslation(NewLaneLocationTransform.GetRotation().GetRightVector() * Vehicle_LaneOffsetFragment.LateralOffset);

		// Debug
		UE::MassTraffic::DrawDebugDensityManagementTransfer(GetWorld(),
			Vehicle_TransformFragment.GetTransform().GetLocation(),
			NewLaneLocationTransform.GetLocation(),
			FColor::Green,
			bVisLog,
			/*VisLogOwner=*/LogOwner);

		// Set new location PrevTransform otherwise computed velocity for this frame would be enormous.
		Vehicle_RepresentationFragment.PrevTransform = NewLaneLocationTransform;
		Vehicle_TransformFragment.SetTransform(NewLaneLocationTransform);
	}

	return bTransferred;
}
