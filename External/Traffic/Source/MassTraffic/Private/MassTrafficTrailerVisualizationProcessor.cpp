// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficTrailerVisualizationProcessor.h"
#include "MassTrafficVehicleVisualizationProcessor.h"
#include "MassTrafficSubsystem.h"
#include "MassTrafficVehicleComponent.h"
#include "MassEntityView.h"
#include "MassRepresentationSubsystem.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "MassActorSubsystem.h"
#include "VisualLogger/VisualLogger.h"
#include "Components/PrimitiveComponent.h"


//----------------------------------------------------------------------//
// UMassTrafficTrailerVisualizationProcessor 
//----------------------------------------------------------------------//
UMassTrafficTrailerVisualizationProcessor::UMassTrafficTrailerVisualizationProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	bRequiresGameThreadExecution = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::TrailerVisualization;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::TrailerBehavior);
}

void UMassTrafficTrailerVisualizationProcessor::ConfigureQueries()
{
	Super::ConfigureQueries();
	EntityQuery.AddTagRequirement<FMassTrafficVehicleTrailerTag>(EMassFragmentPresence::All);
}

//----------------------------------------------------------------------//
// UMassTrafficTrailerUpdateCustomVisualizationProcessor 
//----------------------------------------------------------------------//
UMassTrafficTrailerUpdateCustomVisualizationProcessor::UMassTrafficTrailerUpdateCustomVisualizationProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	bRequiresGameThreadExecution = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::TrailerVisualization;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::TrailerBehavior);
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficTrailerVisualizationProcessor::StaticClass()->GetFName());
}

void UMassTrafficTrailerUpdateCustomVisualizationProcessor::ConfigureQueries()
{
	EntityQuery.AddTagRequirement<FMassTrafficVehicleTrailerTag>(EMassFragmentPresence::All);

	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddChunkRequirement<FMassVisualizationChunkFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSharedRequirement<FMassRepresentationSubsystemSharedFragment>(EMassFragmentAccess::ReadWrite);

	EntityQuery.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassTrafficVehiclePhysicsFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery.AddRequirement<FMassTrafficConstrainedVehicleFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.SetChunkFilter(&FMassVisualizationChunkFragment::AreAnyEntitiesVisibleInChunk);
#if ENABLE_VISUAL_LOG
	EntityQuery.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadOnly);
#endif // ENABLE_VISUAL_LOG
}

void UMassTrafficTrailerUpdateCustomVisualizationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// As we are using the same Visualization.StaticMeshDescHandle here as traffic vehicles, we must
	// add custom float values for trailer instances too.
	// 
	// Otherwise the total mesh instance count (e.g: 7 traffic + 3 parked) would be mismatched with the
	// total custom data count (e.g: 7 traffic + 0 parked)
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this, &EntityManager](FMassExecutionContext& QueryContext)
	{
		UMassRepresentationSubsystem* RepresentationSubsystem = QueryContext.GetMutableSharedFragment<FMassRepresentationSubsystemSharedFragment>().RepresentationSubsystem;
		check(RepresentationSubsystem);
		FMassInstancedStaticMeshInfoArrayView ISMInfo = RepresentationSubsystem->GetMutableInstancedStaticMeshInfos();

		const int32 NumEntities = QueryContext.GetNumEntities();
		const TConstArrayView<FMassTrafficConstrainedVehicleFragment> ConstrainedVehicleFragments = QueryContext.GetFragmentView<FMassTrafficConstrainedVehicleFragment>();
		const TConstArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = QueryContext.GetFragmentView<FMassTrafficRandomFractionFragment>();
		const TConstArrayView<FMassRepresentationLODFragment> RepresentationLODFragments = QueryContext.GetFragmentView<FMassRepresentationLODFragment>();
		const TConstArrayView<FTransformFragment> TransformFragments = QueryContext.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassTrafficVehiclePhysicsFragment> SimpleVehiclePhysicsFragments = QueryContext.GetFragmentView<FMassTrafficVehiclePhysicsFragment>();
		const TArrayView<FMassRepresentationFragment> RepresentationFragments = QueryContext.GetMutableFragmentView<FMassRepresentationFragment>();
		const TArrayView<FMassActorFragment> ActorFragments = QueryContext.GetMutableFragmentView<FMassActorFragment>();
		for (int32 EntityIndex = 0; EntityIndex < NumEntities; EntityIndex++)
		{
			const FMassTrafficConstrainedVehicleFragment& ConstrainedVehicleFragment = ConstrainedVehicleFragments[EntityIndex];
			const FMassTrafficRandomFractionFragment& RandomFractionFragment = RandomFractionFragments[EntityIndex];
			const FMassRepresentationLODFragment& RepresentationLODFragment = RepresentationLODFragments[EntityIndex];
			const FTransformFragment& TransformFragment = TransformFragments[EntityIndex];
			FMassRepresentationFragment& RepresentationFragment = RepresentationFragments[EntityIndex];
			FMassActorFragment& ActorFragment = ActorFragments[EntityIndex];

			// Prepare custom instance data. All we really need this for is to toggle break lights.
			FMassEntityView VehicleMassEntityView(EntityManager, ConstrainedVehicleFragment.Vehicle);
			if (!ensure(EntityManager.IsEntityValid(ConstrainedVehicleFragment.Vehicle)))
			{
				continue;
			}
			const FMassTrafficVehicleLightsFragment& VehicleLightsFragment = VehicleMassEntityView.GetFragmentData<FMassTrafficVehicleLightsFragment>();
			const FMassTrafficPackedVehicleInstanceCustomData PackedCustomData = FMassTrafficVehicleInstanceCustomData::MakeTrafficVehicleCustomData(VehicleLightsFragment, RandomFractionFragment);

			switch (RepresentationFragment.CurrentRepresentation)
			{
				case EMassRepresentationType::LowResSpawnedActor:
				{
					if (AActor* Actor = ActorFragment.GetMutable())
					{
						// Update actor transform
						QueryContext.Defer().PushCommand<FMassDeferredSetCommand>([Actor, NewActorTransform = TransformFragment.GetTransform()](FMassEntityManager& System)
						{
							Actor->SetActorTransform(NewActorTransform);
						});
						
						// Has simple vehicle physics?
						if (!SimpleVehiclePhysicsFragments.IsEmpty())
						{
							// Has a UMassTrafficVehicleComponent with wheel mesh references? 
							UMassTrafficVehicleComponent* MassTrafficVehicleComponent = Actor->FindComponentByClass<UMassTrafficVehicleComponent>();
							if (MassTrafficVehicleComponent)
							{
								// Update wheel component transforms from simple vehicle physics sim.
								// This should be safe to reference SimpleVehiclePhysicsFragment directly as
								// we should be done writing to the VehicleSim this frame.
								QueryContext.Defer().PushCommand<FMassDeferredSetCommand>([MassTrafficVehicleComponent, Entity = QueryContext.GetEntity(EntityIndex)](FMassEntityManager& CallbackEntitySubsystem)
								{
									if (CallbackEntitySubsystem.IsEntityValid(Entity))
									{
										// If the simulation LOD changed this frame, removal of the
										// FDataFragment_SimpleVehiclePhysics would have been queued and executed 
										// before this deferred command, thus actually removing the fragment we
										// thought we had via the check above. So we safely check again here for
										// FDataFragment_SimpleVehiclePhysics using an FMassEntityView    
										const FMassTrafficVehiclePhysicsFragment* SimpleVehiclePhysicsFragment = CallbackEntitySubsystem.GetFragmentDataPtr<FMassTrafficVehiclePhysicsFragment>(Entity);
										if (SimpleVehiclePhysicsFragment)
										{
											// Init offsets?
											if (MassTrafficVehicleComponent->WheelOffsets.IsEmpty())
											{
												MassTrafficVehicleComponent->InitWheelAttachmentOffsets(SimpleVehiclePhysicsFragment->VehicleSim);
											}
							
											// Update
											MassTrafficVehicleComponent->UpdateWheelComponents(SimpleVehiclePhysicsFragment->VehicleSim);
										}
									}
								});
							}
						}

						// Update primitive component custom data
						Actor->ForEachComponent<UPrimitiveComponent>(/*bIncludeFromChildActors*/true, [&PackedCustomData](UPrimitiveComponent* PrimitiveComponent)
						{
							PrimitiveComponent->SetCustomPrimitiveDataFloat(/*DataIndex*/1, PackedCustomData.PackedParam1);
						});
					}

					break;
				}
				case EMassRepresentationType::StaticMeshInstance:
				{
					// Add batched instance transform & custom data
					ISMInfo[RepresentationFragment.StaticMeshDescHandle.ToIndex()].AddBatchedTransform(QueryContext.GetEntity(EntityIndex)
						, TransformFragment.GetTransform(), RepresentationFragment.PrevTransform, RepresentationLODFragment.LODSignificance);
					ISMInfo[RepresentationFragment.StaticMeshDescHandle.ToIndex()].AddBatchedCustomData(PackedCustomData, RepresentationLODFragment.LODSignificance);

					break;
				}
				default:
				{
					break;
				}
			}
			
			RepresentationFragment.PrevTransform = TransformFragment.GetTransform();
		}
	});

#if ENABLE_VISUAL_LOG
	
	// Debug draw current visualization
	if (GMassTrafficDebugVisualization)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("DebugDisplayVisualization")) 

		EntityQuery.ForEachEntityChunk(EntityManager, Context, [this, World = EntityManager.GetWorld()](FMassExecutionContext& Context)
		{
			const UMassTrafficSubsystem* MassTrafficSubsystem = Context.GetSubsystem<UMassTrafficSubsystem>();

			const int32 NumEntities = Context.GetNumEntities();
			const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
			const TArrayView<FMassRepresentationFragment> VisualizationList = Context.GetMutableFragmentView<FMassRepresentationFragment>();

			for (int Index = 0; Index < NumEntities; Index++)
			{
				const FTransformFragment& TransformFragment = TransformList[Index];
				FMassRepresentationFragment& Visualization = VisualizationList[Index];
				const int32 CurrentVisualIdx = static_cast<int32>(Visualization.CurrentRepresentation);

				if (Visualization.CurrentRepresentation != EMassRepresentationType::None || GMassTrafficDebugVisualization >= 2)
				{
					DrawDebugPoint(World, TransformFragment.GetTransform().GetLocation() + FVector(50.0f, 0.0f, 200.0f), 10.0f, UE::MassLOD::LODColors[CurrentVisualIdx]);
				}

				if ((Visualization.CurrentRepresentation != EMassRepresentationType::None && GMassTrafficDebugVisualization >= 2) || GMassTrafficDebugVisualization >= 3)
				{
					UE_VLOG_LOCATION(MassTrafficSubsystem, TEXT("MassTraffic Trailer Vis"), Log, TransformFragment.GetTransform().GetLocation() + FVector(50.0f, 0.0f, 200.0f), /*Radius*/ 10.0f, UE::MassLOD::LODColors[CurrentVisualIdx], TEXT("%d"), CurrentVisualIdx);
				}
			}
		});
	}
	
#endif
}
