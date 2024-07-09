// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficFieldOperations.h"

#include "MassSimulationLOD.h"
#include "MassTraffic.h"
#include "MassTrafficFragments.h"
#include "MassTrafficSubsystem.h"
#include "MassTrafficVehicleSimulationLODProcessor.h"
#include "MassTrafficVehicleVisualizationLODProcessor.h"
#include "MassTrafficVehicleVisualizationProcessor.h"

#include "VehicleUtility.h"
#include "DrawDebugHelpers.h"
#include "MassEntityView.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "ZoneGraphQuery.h"
#include "ZoneGraphSubsystem.h"

void FMassTrafficFieldOperationContext::ForEachTrafficLane(FTrafficLaneExecuteFunction ExecuteFunction) const
{
	// Loop over field lanes
	const TArray<FZoneGraphTrafficLaneData*>& TrafficLanes = Field.GetTrafficLanes();
	for (FZoneGraphTrafficLaneData* TrafficLaneData : TrafficLanes)
	{
		check(TrafficLaneData);
		
		// Execute callback
		const bool bContinue = ExecuteFunction(*TrafficLaneData);

		if (!bContinue)
		{
			break;
		}
	}	
}
	
void FMassTrafficFieldOperationContext::ForEachTrafficVehicle(FTrafficVehicleOnLaneExecuteFunction ExecuteFunction) const
{
	// Get field bounds for vehicle transform filtering
	const FBox FieldBounds = Field.Bounds.GetBox();
	
	// Loop over field lanes
	const TArray<FZoneGraphTrafficLaneData*>& TrafficLanes = Field.GetTrafficLanes();
	for (FZoneGraphTrafficLaneData* TrafficLaneData : TrafficLanes)
	{		
		// Loop over vehicles on lane
		bool bContinue = true;

		// Filter by transform?
		if (Field.InclusionMode == EMassTrafficFieldInclusionMode::VehiclesOnLanes)
		{
			TrafficLaneData->ForEachVehicleOnLane(EntityManager, [&](const FMassEntityView& VehicleEntityView, FMassTrafficNextVehicleFragment& NextVehicleFragment, FMassZoneGraphLaneLocationFragment& LaneLocationFragment)
			{
				const FTransformFragment& TransformFragment = VehicleEntityView.GetFragmentData<FTransformFragment>();
				
				// Transform filtering
				if (FieldBounds.IsInside(TransformFragment.GetTransform().GetLocation()))
				{
					bContinue = ExecuteFunction(*TrafficLaneData, VehicleEntityView, NextVehicleFragment, LaneLocationFragment);
					return bContinue;
				}

				return true;
			});
		}
		else
		{
			// ExecuteFunction for all vehicles on lane
			TrafficLaneData->ForEachVehicleOnLane(EntityManager, [&](const FMassEntityView& VehicleEntityView, FMassTrafficNextVehicleFragment& NextVehicleFragment, FMassZoneGraphLaneLocationFragment& LaneLocationFragment)
			{
				bContinue = ExecuteFunction(*TrafficLaneData, VehicleEntityView, NextVehicleFragment, LaneLocationFragment);
				return bContinue;
			});
		}

		if (!bContinue)
		{
			break;
		}
	}	
}

void FMassTrafficFieldOperationContext::ForEachTrafficIntersection(FTrafficIntersectionExecuteFunction ExecuteFunction) const
{
	// Loop over field lanes
	const TArray<FMassEntityHandle>& TrafficIntersectionEntities = Field.GetTrafficIntersectionEntities();
	for (const FMassEntityHandle& TrafficIntersectionEntity : TrafficIntersectionEntities)
	{
		FMassTrafficIntersectionFragment& TrafficIntersectionFragment = EntityManager.GetFragmentDataChecked<FMassTrafficIntersectionFragment>(TrafficIntersectionEntity);

		// Execute callback
		const bool bContinue = ExecuteFunction(TrafficIntersectionEntity, TrafficIntersectionFragment);

		if (!bContinue)
		{
			break;
		}
	}	
}

UMassTrafficFieldOperationsProcessorBase::UMassTrafficFieldOperationsProcessorBase()
{
}

void UMassTrafficFieldOperationsProcessorBase::ConfigureQueries()
{
	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficFieldOperationsProcessorBase::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (UMassTrafficSubsystem* TrafficSubsystem = Context.GetMutableSubsystem<UMassTrafficSubsystem>())
	{
		TrafficSubsystem->PerformFieldOperation(Operation);
	}
}

void UMassTrafficForceTrafficVehicleViewerLODFieldOperation::Execute(FMassTrafficFieldOperationContext& Context)
{
	// Loop vehicles in field lanes
	Context.ForEachTrafficVehicle([this](FZoneGraphTrafficLaneData& TrafficLaneData, const FMassEntityView& VehicleEntityView, struct FMassTrafficNextVehicleFragment& NextVehicleFragment, struct FMassZoneGraphLaneLocationFragment& LaneLocationFragment)
	{
		// Get MassViewerLOD fragment
		FMassRepresentationLODFragment& VisualizationLODFragment = VehicleEntityView.GetFragmentData<FMassRepresentationLODFragment>();

		// Force occupant viewer LOD
		VisualizationLODFragment.LOD = LOD;

		// Continue
		return true;
	});

	// Optional debug display
	if (GMassTrafficDebugViewerLOD)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("DebugDisplayLOD"))

		Context.ForEachTrafficVehicle([this, &Context](FZoneGraphTrafficLaneData& TrafficLaneData, const FMassEntityView& VehicleEntityView, struct FMassTrafficNextVehicleFragment& NextVehicleFragment, struct FMassZoneGraphLaneLocationFragment& LaneLocationFragment)
		{
			const FMassRepresentationLODFragment& VisualizationLODFragment = VehicleEntityView.GetFragmentData<FMassRepresentationLODFragment>();
			const FTransformFragment& TransformFragment = VehicleEntityView.GetFragmentData<FTransformFragment>();

			// Draw a red line through where UMassProcessor_TrafficVehicleSimulationLOD would usually draw the debug
			// point to show it's being overridden 
			DrawDebugLine(Context.EntityManager.GetWorld(), TransformFragment.GetTransform().GetLocation() + FVector(-25.0f, -25.0f, 200.0f), TransformFragment.GetTransform().GetLocation() + FVector(25.0f, 25.0f, 200.0f), FColor::Red, false, -1, 0, 5.0f);

			// Draw a point above to show the override LOD  
			DrawDebugPoint(Context.EntityManager.GetWorld(), TransformFragment.GetTransform().GetLocation() + FVector(0.0f, 0.0f, 250.0f), 10.0f, UE::MassLOD::LODColors[VisualizationLODFragment.LOD]);

			// Continue
			return true;
		});
	}
}

void UMassTrafficSetLaneSpeedLimitFieldOperation::Execute(FMassTrafficFieldOperationContext& Context)
{
	float SpeedLimit = Chaos::MPHToCmS(SpeedLimitMPH);

	// Loop over field lanes
	Context.ForEachTrafficLane([&Context, SpeedLimit](FZoneGraphTrafficLaneData& TrafficLaneData)
	{
		TrafficLaneData.ConstData.SpeedLimit = SpeedLimit;

		// Adjust incoming lane's MinNextLaneSpeedLimit
		TArray<FZoneGraphLinkedLane> IncomingLanes;
		Context.ZoneGraphSubsystem.GetLinkedLanes(TrafficLaneData.LaneHandle, EZoneLaneLinkType::Incoming, EZoneLaneLinkFlags::All, EZoneLaneLinkFlags::None, IncomingLanes);
		for (const FZoneGraphLinkedLane& IncomingLane : IncomingLanes)
		{
			FZoneGraphTrafficLaneData* IncomingTrafficLaneData = Context.MassTrafficSubsystem.GetMutableTrafficLaneData(IncomingLane.DestLane);
			if (IncomingTrafficLaneData)
			{
				IncomingTrafficLaneData->ConstData.AverageNextLanesSpeedLimit = 0.0f;
				if (IncomingTrafficLaneData->NextLanes.Num())
				{
					for (const FZoneGraphTrafficLaneData* NextTrafficLaneData : IncomingTrafficLaneData->NextLanes)
					{
						IncomingTrafficLaneData->ConstData.AverageNextLanesSpeedLimit = IncomingTrafficLaneData->ConstData.AverageNextLanesSpeedLimit + NextTrafficLaneData->ConstData.SpeedLimit;
					}
					IncomingTrafficLaneData->ConstData.AverageNextLanesSpeedLimit = IncomingTrafficLaneData->ConstData.AverageNextLanesSpeedLimit / IncomingTrafficLaneData->NextLanes.Num();
				}
			}
		}

		// Continue
		return true;
	});
}

void UMassTrafficVisualLoggingFieldOperation::Execute(FMassTrafficFieldOperationContext& Context)
{
#if ENABLE_VISUAL_LOG

	Context.ForEachTrafficVehicle([this](FZoneGraphTrafficLaneData& TrafficLaneData, const FMassEntityView& VehicleEntityView, struct FMassTrafficNextVehicleFragment& NextVehicleFragment, struct FMassZoneGraphLaneLocationFragment& LaneLocationFragment)
	{
		// Enable / disable VisLog
		FMassTrafficDebugFragment& TrafficDebugFragment = VehicleEntityView.GetFragmentData<FMassTrafficDebugFragment>();
		TrafficDebugFragment.bVisLog = bVisLog;

		// Continue
		return true;
	});
	
#endif
}

void UMassTrafficRetimeIntersectionPeriodsFieldOperation::Execute(FMassTrafficFieldOperationContext& Context)
{
	// Loop over field intersection
	Context.ForEachTrafficIntersection([this](const FMassEntityHandle TrafficIntersectionEntity, FMassTrafficIntersectionFragment& TrafficIntersectionFragment)
	{
		for (int32 PeriodIndex = 0; PeriodIndex < TrafficIntersectionFragment.Periods.Num(); ++PeriodIndex)
		{
			FMassTrafficPeriod& Period = TrafficIntersectionFragment.Periods[PeriodIndex];
			
			// Vehicles and Pedestrians?
			if (!Period.VehicleLanes.IsEmpty() && !Period.CrosswalkLanes.IsEmpty())
			{
				Period.Duration = Period.Duration * VehicleAndPedestrianPeriodDurationMult;
			}
			else if (!Period.VehicleLanes.IsEmpty())
			{
				Period.Duration = Period.Duration * VehiclesOnlyPeriodDurationMult;
			}
			else if (!Period.CrosswalkLanes.IsEmpty())
			{
				Period.Duration = Period.Duration * PedestriansOnlyPeriodDurationMult;
			}
			else
			{
				Period.Duration = Period.Duration * EmptyPeriodDurationMult;
			}

			// Remove period if we re-timed it to 0
			if (Period.Duration == 0.0f)
			{
				TrafficIntersectionFragment.Periods.RemoveAt(PeriodIndex);
				--PeriodIndex;
			}
		}
		
		// Continue
		return true;
	});
}

UMassTrafficVisualLoggingFieldOperationProcessor::UMassTrafficVisualLoggingFieldOperationProcessor()
{
	bAutoRegisterWithProcessingPhases = false;
	
	Operation = UMassTrafficVisualLoggingFieldOperation::StaticClass();
}


UMassTrafficFrameStartFieldOperationsProcessor::UMassTrafficFrameStartFieldOperationsProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::FrameStart;
	
	Operation = UMassTrafficVisualLoggingFieldOperation::StaticClass();
}

void UMassTrafficFrameStartFieldOperationsProcessor::ConfigureQueries()
{
	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficFrameStartFieldOperationsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Re-cache lanes & intersections for Movable traffic fields
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("UpdateMovableTrafficFields"))

		UMassTrafficSubsystem& TrafficSubsystem = Context.GetMutableSubsystemChecked<UMassTrafficSubsystem>();

		const TArray<UMassTrafficFieldComponent*>& Fields = TrafficSubsystem.GetFields();
		for (UMassTrafficFieldComponent* Field : Fields)
		{
			if (Field->Mobility == EComponentMobility::Movable && Field->bEnabled)
			{
				Field->UpdateOverlappedLanes(TrafficSubsystem);
				Field->UpdateOverlappedIntersections(TrafficSubsystem);
			}
		}
	}

	// Process FrameStart operations
	Super::Execute(EntityManager, Context);
}

UMassTrafficPostCalcVisualizationLODFieldOperationsProcessor::UMassTrafficPostCalcVisualizationLODFieldOperationsProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleVisualizationLOD;
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficVehicleVisualizationLODProcessor::StaticClass()->GetFName());

	Operation = UMassTrafficForceTrafficVehicleViewerLODFieldOperation::StaticClass();
}
