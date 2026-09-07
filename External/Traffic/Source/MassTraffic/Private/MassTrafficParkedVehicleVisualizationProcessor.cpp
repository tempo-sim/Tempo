// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficParkedVehicleVisualizationProcessor.h"

#include "MassCommonFragments.h"
#include "MassTrafficVehicleVisualizationProcessor.h"
#include "MassTrafficSubsystem.h"
#include "MassRepresentationSubsystem.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "VisualLogger/VisualLogger.h"

//----------------------------------------------------------------------//
// UMassTrafficParkedVehicleVisualizationProcessor 
//----------------------------------------------------------------------//
UMassTrafficParkedVehicleVisualizationProcessor::UMassTrafficParkedVehicleVisualizationProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	bRequiresGameThreadExecution = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleVisualization;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleVisualizationLOD);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PreVehicleVisualization);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::TrafficIntersectionVisualization);
}

void UMassTrafficParkedVehicleVisualizationProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Super::ConfigureQueries(EntityManager);

	EntityQuery.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::All);
}

//----------------------------------------------------------------------//
// UMassTrafficParkedVehicleUpdateCustomVisualizationProcessor 
//----------------------------------------------------------------------//
UMassTrafficParkedVehicleUpdateCustomVisualizationProcessor::UMassTrafficParkedVehicleUpdateCustomVisualizationProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	bRequiresGameThreadExecution = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleVisualization;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleVisualizationLOD);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PreVehicleVisualization);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::TrafficIntersectionVisualization);
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficParkedVehicleVisualizationProcessor::StaticClass()->GetFName());
}

void UMassTrafficParkedVehicleUpdateCustomVisualizationProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadOnly);

	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddSharedRequirement<FMassRepresentationSubsystemSharedFragment>(EMassFragmentAccess::ReadWrite);

	EntityQuery.AddChunkRequirement<FMassVisualizationChunkFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.SetChunkFilter(&FMassVisualizationChunkFragment::AreAnyEntitiesVisibleInChunk);
#if ENABLE_VISUAL_LOG
	EntityQuery.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadOnly);
#endif // ENABLE_VISUAL_LOG
}

void UMassTrafficParkedVehicleUpdateCustomVisualizationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// As we are using the same Visualization.StaticMeshDescHandle here as traffic vehicles, we must
	// add custom float values for parked instances too.
	// 
	// Otherwise the total mesh instance count (e.g: 7 traffic + 3 parked) would be mismatched with the
	// total custom data count (e.g: 7 traffic + 0 parked)
	EntityQuery.ForEachEntityChunk(Context, [this](FMassExecutionContext& Context)
		{
			UMassRepresentationSubsystem* RepresentationSubsystem = Context.GetMutableSharedFragment<FMassRepresentationSubsystemSharedFragment>().RepresentationSubsystem;
			check(RepresentationSubsystem);
			FMassInstancedStaticMeshInfoArrayView ISMInfo = RepresentationSubsystem->GetMutableInstancedStaticMeshInfos();

			TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
			TConstArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = Context.GetFragmentView<FMassTrafficRandomFractionFragment>();
			TConstArrayView<FMassRepresentationLODFragment> VisualizationLODFragments = Context.GetFragmentView<FMassRepresentationLODFragment>();
			TArrayView<FMassRepresentationFragment> VisualizationFragments = Context.GetMutableFragmentView<FMassRepresentationFragment>();

			for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
			{
				const FTransformFragment& TransformFragment = TransformList[EntityIt];
				const FMassTrafficRandomFractionFragment& RandomFractionFragment = RandomFractionFragments[EntityIt];
				FMassRepresentationFragment& Visualization = VisualizationFragments[EntityIt];
				const FMassRepresentationLODFragment& VisualizationLODFragment = VisualizationLODFragments[EntityIt];
				if (Visualization.CurrentRepresentation == EMassRepresentationType::StaticMeshInstance)
				{
					const FMassTrafficPackedVehicleInstanceCustomData PackedCustomData = FMassTrafficVehicleInstanceCustomData::MakeParkedVehicleCustomData(RandomFractionFragment);

					const int32 ISMInfoIndex = Visualization.StaticMeshDescHandle.ToIndex();
					if (ensureMsgf(ISMInfo.IsValidIndex(ISMInfoIndex), TEXT("Invalid handle index %u for ISMInfosView"), ISMInfoIndex))
					{
						ISMInfo[ISMInfoIndex].AddBatchedTransform(Context.GetEntity(EntityIt)
							, TransformFragment.GetTransform(), Visualization.PrevTransform, VisualizationLODFragment.LODSignificance);
						ISMInfo[ISMInfoIndex].AddBatchedCustomData(PackedCustomData, VisualizationLODFragment.LODSignificance);
					}
				}
				Visualization.PrevTransform = TransformFragment.GetTransform();
			}
		});

#if ENABLE_VISUAL_LOG
	
	// Debug draw current visualization
	if (GMassTrafficDebugVisualization)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("DebugDisplayVisualization")) 

		EntityQuery.ForEachEntityChunk(Context, [this, InWorld = EntityManager.GetWorld()](FMassExecutionContext& Context)
		{
			const UMassTrafficSubsystem* MassTrafficSubsystem = Context.GetSubsystem<UMassTrafficSubsystem>();

			TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
			TArrayView<FMassRepresentationFragment> VisualizationList = Context.GetMutableFragmentView<FMassRepresentationFragment>();

			for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
			{
				const FTransformFragment& TransformFragment = TransformList[EntityIt];
				FMassRepresentationFragment& Visualization = VisualizationList[EntityIt];
				const int32 CurrentVisualIdx = static_cast<int32>(Visualization.CurrentRepresentation);

				if (Visualization.CurrentRepresentation != EMassRepresentationType::None || GMassTrafficDebugVisualization >= 2)
				{
					DrawDebugPoint(InWorld, TransformFragment.GetTransform().GetLocation() + FVector(50.0f, 0.0f, 200.0f), 10.0f, UE::MassLOD::LODColors[CurrentVisualIdx]);
				}

				if ((Visualization.CurrentRepresentation != EMassRepresentationType::None && GMassTrafficDebugVisualization >= 2) || GMassTrafficDebugVisualization >= 3)
				{
					UE_VLOG_LOCATION(MassTrafficSubsystem, TEXT("MassTraffic Parked Vis"), Log, TransformFragment.GetTransform().GetLocation() + FVector(50.0f, 0.0f, 200.0f), /*Radius*/ 10.0f, UE::MassLOD::LODColors[CurrentVisualIdx], TEXT("%d"), CurrentVisualIdx);
				}
			}
		});
	}
	
#endif
}
