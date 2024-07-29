// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficFindObstaclesProcessor.h"
#include "MassTrafficFragments.h"
#include "MassTrafficUtils.h"
#include "MassTraffic.h"

#include "MassCommandBuffer.h"
#include "MassCommonFragments.h"
#include "ZoneGraphQuery.h"
#include "DrawDebugHelpers.h"
#include "MassEntityView.h"
#include "MassExecutionContext.h"
#include "MassTrafficVehicleSimulationTrait.h"
#include "ZoneGraphSubsystem.h"
#include "MassGameplayExternalTraits.h"
#include "VisualLogger/VisualLogger.h"

void FindNearbyLanes(const FZoneGraphStorage& Storage, const FBox& Bounds, const FZoneGraphTagFilter TagFilter, TArray<int32>& OutLanes)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("FindNearbyLanes"))
	
	for (const FZoneData& Zone : Storage.Zones)
	{
		if (Bounds.Intersect(Zone.Bounds))
		{
			for (int32 LaneIndex = Zone.LanesBegin; LaneIndex < Zone.LanesEnd; LaneIndex++)
			{
				const FZoneLaneData& Lane = Storage.Lanes[LaneIndex];
				if (TagFilter.Pass(Lane.Tags))
				{
					OutLanes.Add(LaneIndex);
				}
			}
		}
	}
}

UMassTrafficFindObstaclesProcessor::UMassTrafficFindObstaclesProcessor()
	: ObstacleEntityQuery(*this)
	, ObstacleAvoidingEntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::PreVehicleBehavior;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleSimulationLOD);
}

void UMassTrafficFindObstaclesProcessor::ConfigureQueries()
{
	// Main query used to find obstacle entities
	ObstacleEntityQuery.AddTagRequirement<FMassTrafficObstacleTag>(EMassFragmentPresence::All);
	ObstacleEntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	ObstacleEntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	ObstacleEntityQuery.AddConstSharedRequirement<FMassTrafficVehicleSimulationParameters>(EMassFragmentPresence::Optional);
	ObstacleEntityQuery.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
	ObstacleEntityQuery.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadOnly);

	// Secondary query to find obstacle lists to reset before filling in the main process
	ObstacleAvoidingEntityQuery.AddRequirement<FMassTrafficObstacleListFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficFindObstaclesProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	{
		// Reset obstacle lists
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("ResetObstacleLists"))
		
		ObstacleAvoidingEntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& QueryContext)
		{
			const TArrayView<FMassTrafficObstacleListFragment> ObstacleListFragments = QueryContext.GetMutableFragmentView<FMassTrafficObstacleListFragment>();
			for (FMassTrafficObstacleListFragment& ObstacleListFragment : ObstacleListFragments)
			{
				ObstacleListFragment.Obstacles.Reset();
			}
		});
	}

	{
		// Re-bind obstacles to vehicles on nearby lanes
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("FindVehiclesForObstacles"))

		TMap<FMassEntityHandle, TArray<FMassEntityHandle>> ObstacleListsToAdd;
		
		ObstacleEntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& QueryContext)
		{
			const UMassTrafficSubsystem& MassTrafficSubsystem = QueryContext.GetSubsystemChecked<UMassTrafficSubsystem>();
			const UZoneGraphSubsystem& ZoneGraphSubsystem = QueryContext.GetSubsystemChecked<UZoneGraphSubsystem>();

			const FMassTrafficVehicleSimulationParameters* VehicleSimulationParams = QueryContext.GetConstSharedFragmentPtr<FMassTrafficVehicleSimulationParameters>();
			const TConstArrayView<FAgentRadiusFragment> AgentRadiusFragments = QueryContext.GetFragmentView<FAgentRadiusFragment>();
			const TConstArrayView<FTransformFragment> TransformFragments = QueryContext.GetFragmentView<FTransformFragment>();

			// Loop obstacles and find affected vehicles
			const int32 NumEntities = QueryContext.GetNumEntities();
			for (int32 Index = 0; Index < NumEntities; ++Index)
			{
				FMassEntityHandle ObstacleEntity = QueryContext.GetEntity(Index);
				const FAgentRadiusFragment& AgentRadiusFragment = AgentRadiusFragments[Index];
				const FTransformFragment& TransformFragment = TransformFragments[Index];

				const float AgentWidth = VehicleSimulationParams ? VehicleSimulationParams->HalfWidth : AgentRadiusFragment.Radius;

				// Debug draw obstacle
				#if WITH_MASSTRAFFIC_DEBUG
					if (GMassTrafficDebugObstacleAvoidance)
					{
						DrawDebugPoint(GetWorld(), TransformFragment.GetTransform().GetLocation() + FVector(0,0,500), 10.0f, FColor::Yellow);

						DrawDebugBox(GetWorld(),
							TransformFragment.GetTransform().GetLocation(),
							FVector(AgentRadiusFragment.Radius, AgentWidth, AgentWidth),
							TransformFragment.GetTransform().GetRotation(),
							FColor::Red);

						if (GMassTrafficDebugObstacleAvoidance > 1)
						{
							UE_VLOG_LOCATION(&MassTrafficSubsystem, TEXT("MassTraffic Avoidance"), Log, TransformFragment.GetTransform().GetLocation(), AgentRadiusFragment.Radius, FColor::Yellow, TEXT("%d Obstacle"), ObstacleEntity.Index);
						}
					}
				#endif

				// Find nearby lanes for this obstacle
				TArray<FZoneGraphLaneHandle> NearbyLanes;
				FBox SearchBox = FBox::BuildAABB(TransformFragment.GetTransform().GetLocation(), FVector(FVector2D(MassTrafficSettings->ObstacleSearchRadius), MassTrafficSettings->ObstacleSearchHeight));
				ZoneGraphSubsystem.FindOverlappingLanes(SearchBox, GetDefault<UMassTrafficSettings>()->TrafficLaneFilter, NearbyLanes);

				// Loop over nearby lanes
				for (const FZoneGraphLaneHandle NearbyLane : NearbyLanes)
				{
					// Get nearest point on lane
					FZoneGraphLaneLocation NearestLocationOnLane;
					float DistanceSq;
					ZoneGraphSubsystem.FindNearestLocationOnLane(NearbyLane, SearchBox, NearestLocationOnLane, DistanceSq);
					if (NearestLocationOnLane.IsValid())
					{
						// Debug draw nearby lanes
						#if WITH_MASSTRAFFIC_DEBUG
							if (GMassTrafficDebugObstacleAvoidance)
							{
								DrawDebugPoint(GetWorld(), NearestLocationOnLane.Position + FVector(0,0,50), 10.0f, FColor::Magenta);
							}
							if (GMassTrafficDebugObstacleAvoidance > 1)
							{
								UE_VLOG_LOCATION(&MassTrafficSubsystem, TEXT("MassTraffic Avoidance"), Log, NearestLocationOnLane.Position, 10.0f, FColor::Magenta, TEXT("%d Nearby Lane"), ObstacleEntity.Index);
							}
						#endif
						
						// Get lane data
						const FZoneGraphTrafficLaneData* NearbyTrafficLane = MassTrafficSubsystem.GetTrafficLaneData(NearbyLane);
						if (!NearbyTrafficLane)
						{
							continue;
						}

						// Find nearest vehicle ahead of and behind this point on the lane
						FMassEntityHandle PreviousVehicle;
						FMassEntityHandle NextVehicle;
						UE::MassTraffic::FindNearestVehiclesInLane(EntityManager, *NearbyTrafficLane, NearestLocationOnLane.DistanceAlongLane, PreviousVehicle, NextVehicle);

						// Is there a vehicle behind us?
						if (PreviousVehicle.IsSet() && PreviousVehicle != ObstacleEntity)
						{
							// Debug draw line from avoiding vehicle -> obstacle
							#if WITH_MASSTRAFFIC_DEBUG
								if (GMassTrafficDebugObstacleAvoidance)
								{
									FMassEntityView PreviousVehicleEntityView(EntityManager, PreviousVehicle);
									FVector AvoidingVehicleLocation = PreviousVehicleEntityView.GetFragmentData<FTransformFragment>().GetTransform().GetLocation();
									DrawDebugLine(GetWorld(), AvoidingVehicleLocation, TransformFragment.GetTransform().GetLocation(), FColor::Yellow, false, -1, 0, /*Thickness*/5.0f);
									if (GMassTrafficDebugObstacleAvoidance > 1)
									{
										UE_VLOG_SEGMENT_THICK(&MassTrafficSubsystem, TEXT("MassTraffic Avoidance"), Log, AvoidingVehicleLocation, TransformFragment.GetTransform().GetLocation(), FColor::Yellow, 5.0f, TEXT("%d Avoiding %d"), PreviousVehicle.Index, ObstacleEntity.Index);
										const float Radius = PreviousVehicleEntityView.GetFragmentData<FAgentRadiusFragment>().Radius;
										const float HalfWidth = PreviousVehicleEntityView.GetSharedFragmentData<FMassTrafficVehicleSimulationParameters>().HalfWidth;

										DrawDebugBox(GetWorld(),
											TransformFragment.GetTransform().GetLocation(),
											FVector(Radius, HalfWidth, HalfWidth),
											TransformFragment.GetTransform().GetRotation(),
											FColor::Orange);

									}
								}
							#endif
							
							FMassTrafficObstacleListFragment* ExistingObstacleListFragment = EntityManager.GetFragmentDataPtr<FMassTrafficObstacleListFragment>(PreviousVehicle);
							if (ExistingObstacleListFragment)
							{
								ExistingObstacleListFragment->Obstacles.Add(ObstacleEntity);
							}
							else
							{
								// We can't use Context.Defer().PushCommand(FMassCommandAddFragmentInstance) here as we
								// might find multiple obstacles for a single vehicle this frame which would result in
								// multiple FMassCommandAddFragmentInstance to be queued. So instead we collect all the
								// obstacles per vehicle and add the compiled list together 
								ObstacleListsToAdd.FindOrAdd(PreviousVehicle).Add(ObstacleEntity);
							}
						}
					}
				}
			}
		});

		// Add obstacle list fragments
		for (const auto& VehicleToObstacles : ObstacleListsToAdd)
		{
			FMassTrafficObstacleListFragment NewObstacleListFragment;
			NewObstacleListFragment.Obstacles = VehicleToObstacles.Value;
			Context.Defer().PushCommand<FMassCommandAddFragmentInstances>(VehicleToObstacles.Key, NewObstacleListFragment);
		}
	}
}