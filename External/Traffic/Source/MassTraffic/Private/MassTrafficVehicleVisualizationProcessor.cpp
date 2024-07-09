// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficVehicleVisualizationProcessor.h"
#include "MassTrafficVehicleComponent.h"
#include "MassTrafficSubsystem.h"
#include "MassTrafficDamageRepairProcessor.h"
#include "MassTrafficParkedVehicleVisualizationProcessor.h"
#include "MassRepresentationSubsystem.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "MassActorSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "VisualLogger/VisualLogger.h"

//----------------------------------------------------------------------//
// FMassTrafficVehicleInstanceCustomData 
//----------------------------------------------------------------------//
FMassTrafficVehicleInstanceCustomData::FMassTrafficVehicleInstanceCustomData(const FMassTrafficPackedVehicleInstanceCustomData& PackedCustomData)
{
	const uint32& PackedParam1AsUint32 = reinterpret_cast<const uint32&>(PackedCustomData.PackedParam1);

	// Unpack half precision random fraction 
	FFloat16 HalfPrecisionRandomFraction;
	HalfPrecisionRandomFraction.Encoded = PackedParam1AsUint32;
	RandomFraction = HalfPrecisionRandomFraction;

	// Get light state bits
	bFrontLeftRunningLights = PackedParam1AsUint32 & 1UL << (16 + 0);
	bFrontRightRunningLights = PackedParam1AsUint32 & 1UL << (16 + 1);
	bRearLeftRunningLights = PackedParam1AsUint32 & 1UL << (16 + 2);
	bRearRightRunningLights = PackedParam1AsUint32 & 1UL << (16 + 3);
	bLeftBrakeLights = PackedParam1AsUint32 & 1UL << (16 + 4);
	bRightBrakeLights = PackedParam1AsUint32 & 1UL << (16 + 5);
	bLeftTurnSignalLights = PackedParam1AsUint32 & 1UL << (16 + 6);
	bRightTurnSignalLights = PackedParam1AsUint32 & 1UL << (16 + 7);
	bLeftHeadlight = PackedParam1AsUint32 & 1UL << (16 + 8);
	bRightHeadlight = PackedParam1AsUint32 & 1UL << (16 + 9);
	bReversingLights = PackedParam1AsUint32 & 1UL << (16 + 10);
	bAccessoryLights = PackedParam1AsUint32 & 1UL << (16 + 11); 
}

FMassTrafficVehicleInstanceCustomData FMassTrafficVehicleInstanceCustomData::MakeTrafficVehicleCustomData(
	const FMassTrafficVehicleLightsFragment& VehicleStateFragment,
	const FMassTrafficRandomFractionFragment& RandomFractionFragment)
{
	// Random fraction, running lights on with dynamic brake lights & turn signals  
	FMassTrafficVehicleInstanceCustomData CustomData;
	CustomData.RandomFraction = RandomFractionFragment.RandomFraction;
	CustomData.bFrontLeftRunningLights = true;
	CustomData.bFrontRightRunningLights = true;
	CustomData.bRearLeftRunningLights = true;
	CustomData.bRearRightRunningLights = true;
	CustomData.bLeftBrakeLights = VehicleStateFragment.bBrakeLights;
	CustomData.bRightBrakeLights = VehicleStateFragment.bBrakeLights;
	CustomData.bLeftTurnSignalLights = VehicleStateFragment.bLeftTurnSignalLights;
	CustomData.bRightTurnSignalLights = VehicleStateFragment.bRightTurnSignalLights;
	CustomData.bLeftHeadlight = true;
	CustomData.bRightHeadlight = true;
	CustomData.bReversingLights = false;
	CustomData.bAccessoryLights = false;
	
	return MoveTemp(CustomData);
}

FMassTrafficVehicleInstanceCustomData FMassTrafficVehicleInstanceCustomData::MakeParkedVehicleCustomData(
	const FMassTrafficRandomFractionFragment& RandomFractionFragment)
{
	// Random fraction, all lights off
	FMassTrafficVehicleInstanceCustomData CustomData;
	CustomData.RandomFraction = RandomFractionFragment.RandomFraction;
	
	return MoveTemp(CustomData);
}

FMassTrafficVehicleInstanceCustomData FMassTrafficVehicleInstanceCustomData::MakeTrafficVehicleTrailerCustomData(
	const FMassTrafficRandomFractionFragment& RandomFractionFragment)
{
	// Random fraction, running lights on
	FMassTrafficVehicleInstanceCustomData CustomData;
	CustomData.RandomFraction = RandomFractionFragment.RandomFraction;
	
	CustomData.bFrontLeftRunningLights = true;
	CustomData.bFrontRightRunningLights = true;
	CustomData.bRearLeftRunningLights = true;
	CustomData.bRearRightRunningLights = true;
	
	return MoveTemp(CustomData);
}

FMassTrafficPackedVehicleInstanceCustomData::FMassTrafficPackedVehicleInstanceCustomData(const FMassTrafficVehicleInstanceCustomData& UnpackedCustomData)
{
	uint32& PackedParam1AsUint32 = reinterpret_cast<uint32&>(PackedParam1);

	// Encode RandomFraction as 16-bit float in 16 least significant bits
	const FFloat16 HalfPrecisionRandomFraction = UnpackedCustomData.RandomFraction;
	PackedParam1AsUint32 = static_cast<uint32>(HalfPrecisionRandomFraction.Encoded);
	
	// Set light state bits
	if (UnpackedCustomData.bFrontLeftRunningLights)
	{
		PackedParam1AsUint32 |= 1UL << (16 + 0);
	}
	if (UnpackedCustomData.bFrontRightRunningLights)
	{
		PackedParam1AsUint32 |= 1UL << (16 + 1);
	}
	if (UnpackedCustomData.bRearLeftRunningLights)
	{
		PackedParam1AsUint32 |= 1UL << (16 + 2);
	}
	if (UnpackedCustomData.bRearRightRunningLights)
	{
		PackedParam1AsUint32 |= 1UL << (16 + 3);
	}
	if (UnpackedCustomData.bLeftBrakeLights)
	{
		PackedParam1AsUint32 |= 1UL << (16 + 4);
	}
	if (UnpackedCustomData.bRightBrakeLights)
	{
		PackedParam1AsUint32 |= 1UL << (16 + 5);
	}
	if (UnpackedCustomData.bLeftTurnSignalLights)
	{
		PackedParam1AsUint32 |= 1UL << (16 + 6);
	}
	if (UnpackedCustomData.bRightTurnSignalLights)
	{
		PackedParam1AsUint32 |= 1UL << (16 + 7);
	}
	if (UnpackedCustomData.bLeftHeadlight)
	{
		PackedParam1AsUint32 |= 1UL << (16 + 8);
	}
	if (UnpackedCustomData.bRightHeadlight)
	{
		PackedParam1AsUint32 |= 1UL << (16 + 9);
	}
	if (UnpackedCustomData.bReversingLights)
	{
		PackedParam1AsUint32 |= 1UL << (16 + 10);
	}
	if (UnpackedCustomData.bAccessoryLights)
	{
		PackedParam1AsUint32 |= 1UL << (16 + 11); 
	}
}

//----------------------------------------------------------------------//
// UMassTrafficVehicleVisualizationProcessor 
//----------------------------------------------------------------------//
UMassTrafficVehicleVisualizationProcessor::UMassTrafficVehicleVisualizationProcessor()
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	bRequiresGameThreadExecution = true;
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleVisualization;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleVisualizationLOD);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::TrafficIntersectionVisualization);
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficParkedVehicleVisualizationProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficDamageRepairProcessor::StaticClass()->GetFName());
}

void UMassTrafficVehicleVisualizationProcessor::ConfigureQueries()
{
	Super::ConfigureQueries();

	EntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
}

//----------------------------------------------------------------------//
// UMassTrafficVehicleUpdateCustomVisualizationProcessor 
//----------------------------------------------------------------------//
UMassTrafficVehicleUpdateCustomVisualizationProcessor::UMassTrafficVehicleUpdateCustomVisualizationProcessor()
	: EntityQuery(*this)
#if WITH_MASSTRAFFIC_DEBUG
	, DebugEntityQuery(*this)
#endif // WITH_MASSTRAFFIC_DEBUG
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	bRequiresGameThreadExecution = true;
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleVisualization;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleVisualizationLOD);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::TrafficIntersectionVisualization);
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficVehicleVisualizationProcessor::StaticClass()->GetFName());
}

void UMassTrafficVehicleUpdateCustomVisualizationProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
#if WITH_MASSTRAFFIC_DEBUG
	LogOwner = UWorld::GetSubsystem<UMassTrafficSubsystem>(Owner.GetWorld());
#endif // WITH_MASSTRAFFIC_DEBUG
}

void UMassTrafficVehicleUpdateCustomVisualizationProcessor::ConfigureQueries()
{
	EntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
	
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddChunkRequirement<FMassVisualizationChunkFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSharedRequirement<FMassRepresentationSubsystemSharedFragment>(EMassFragmentAccess::ReadWrite);

	EntityQuery.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassTrafficVehicleLightsFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassTrafficVehiclePhysicsFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);

#if WITH_MASSTRAFFIC_DEBUG
	DebugEntityQuery = EntityQuery;
	DebugEntityQuery.AddRequirement<FMassTrafficDebugFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
#endif // WITH_MASSTRAFFIC_DEBUG

	EntityQuery.SetChunkFilter(&FMassVisualizationChunkFragment::AreAnyEntitiesVisibleInChunk);
}

void UMassTrafficVehicleUpdateCustomVisualizationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
	{
		// Get mutable ISMInfos to append instances & custom data to
		UMassRepresentationSubsystem* RepresentationSubsystem = Context.GetMutableSharedFragment<FMassRepresentationSubsystemSharedFragment>().RepresentationSubsystem;
		check(RepresentationSubsystem);
		const FMassInstancedStaticMeshInfoArrayView ISMInfo = RepresentationSubsystem->GetMutableInstancedStaticMeshInfos();

		const TConstArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = Context.GetFragmentView<FMassTrafficRandomFractionFragment>();
		const TConstArrayView<FMassTrafficVehiclePhysicsFragment> SimpleVehiclePhysicsFragments = Context.GetFragmentView<FMassTrafficVehiclePhysicsFragment>();
		const TConstArrayView<FMassTrafficVehicleLightsFragment> VehicleStateFragments = Context.GetFragmentView<FMassTrafficVehicleLightsFragment>();
		const TConstArrayView<FTransformFragment> TransformFragments = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassRepresentationLODFragment> RepresentationLODFragments = Context.GetFragmentView<FMassRepresentationLODFragment>();
		const TArrayView<FMassActorFragment> ActorFragments = Context.GetMutableFragmentView<FMassActorFragment>();
		const TArrayView<FMassRepresentationFragment> VisualizationFragments = Context.GetMutableFragmentView<FMassRepresentationFragment>();

		const int32 NumEntities = Context.GetNumEntities();
		for (int32 EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
		{
			const FMassEntityHandle Entity = Context.GetEntity(EntityIdx);

			const FMassTrafficRandomFractionFragment& RandomFractionFragment = RandomFractionFragments[EntityIdx];
			const FMassTrafficVehicleLightsFragment& VehicleStateFragment = VehicleStateFragments[EntityIdx];
			const FTransformFragment& TransformFragment = TransformFragments[EntityIdx];
			const FMassRepresentationLODFragment& RepresentationLODFragment = RepresentationLODFragments[EntityIdx];
			FMassActorFragment& ActorFragment = ActorFragments[EntityIdx];
			FMassRepresentationFragment& RepresentationFragment = VisualizationFragments[EntityIdx];

			AActor* Actor = ActorFragment.GetMutable();
			
			// Update active representation
			{
				// Update current representation
				switch (RepresentationFragment.CurrentRepresentation)
				{
					case EMassRepresentationType::StaticMeshInstance:
					{
						// Add ISMC instance with custom data
						if (RepresentationFragment.StaticMeshDescHandle.IsValid())
						{
							ISMInfo[RepresentationFragment.StaticMeshDescHandle.ToIndex()].AddBatchedTransform(Entity
								, TransformFragment.GetTransform(), RepresentationFragment.PrevTransform, RepresentationLODFragment.LODSignificance);

							const FMassTrafficPackedVehicleInstanceCustomData PackedCustomData = FMassTrafficVehicleInstanceCustomData::MakeTrafficVehicleCustomData(VehicleStateFragment, RandomFractionFragment);
							ISMInfo[RepresentationFragment.StaticMeshDescHandle.ToIndex()].AddBatchedCustomData(PackedCustomData, RepresentationLODFragment.LODSignificance);
						}
						break;
					}
					case EMassRepresentationType::LowResSpawnedActor:
					{
						// We should always have a PersistentActor if CurrentRepresentation is LowResSpawnedActor
						ensureMsgf(Actor, TEXT("Traffic actor deleted outside of Mass"));

						if (Actor)
						{
							// Teleport actor to simulated position
							const FTransform NewActorTransform = TransformFragment.GetTransform();
							Context.Defer().PushCommand<FMassDeferredSetCommand>([Actor, NewActorTransform](FMassEntityManager& System)
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
									// Update wheel component transforms from simple vehicle physics sim
									Context.Defer().PushCommand<FMassDeferredSetCommand>([MassTrafficVehicleComponent, Entity](FMassEntityManager& CallbackEntitySubsystem)
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
							const FMassTrafficPackedVehicleInstanceCustomData PackedCustomData = FMassTrafficVehicleInstanceCustomData::MakeTrafficVehicleCustomData(VehicleStateFragment, RandomFractionFragment);
							Actor->ForEachComponent<UPrimitiveComponent>(/*bIncludeFromChildActors*/true, [&PackedCustomData](UPrimitiveComponent* PrimitiveComponent)
							{
								PrimitiveComponent->SetCustomPrimitiveDataFloat(/*DataIndex*/1, PackedCustomData.PackedParam1);
							});
						}

						break;
					}
					case EMassRepresentationType::HighResSpawnedActor:
					{
						ensureMsgf(Actor, TEXT("Traffic actor deleted outside of Mass"));

						// We should always have an Actor if CurrentRepresentation is HighResSpawnedActor   
						if (Actor)
						{
							// Update primitive component custom data
							const FMassTrafficPackedVehicleInstanceCustomData PackedCustomData = FMassTrafficVehicleInstanceCustomData::MakeTrafficVehicleCustomData(VehicleStateFragment, RandomFractionFragment);
							Actor->ForEachComponent<UPrimitiveComponent>(/*bIncludeFromChildActors*/true, [&PackedCustomData](UPrimitiveComponent* PrimitiveComponent)
							{
								PrimitiveComponent->SetCustomPrimitiveDataFloat(/*DataIndex*/1, PackedCustomData.PackedParam1);
							});
						}

						break;
					}
					case EMassRepresentationType::None:
						break;
					default:
						checkf(false, TEXT("Unsupported visual type"));
						break;
				}
			}

			RepresentationFragment.PrevTransform = TransformFragment.GetTransform();
		}
	});

#if WITH_MASSTRAFFIC_DEBUG
	// Debug draw current visualization
	if (GMassTrafficDebugVisualization && LogOwner.IsValid())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("DebugDisplayVisualization")) 

		UWorld* World = EntityManager.GetWorld();
		const UObject* LogOwnerPtr = LogOwner.Get();

		DebugEntityQuery.ForEachEntityChunk(EntityManager, Context, [World, LogOwnerPtr](FMassExecutionContext& Context)
			{
				const int32 NumEntities = Context.GetNumEntities();
				const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
				const TConstArrayView<FMassTrafficDebugFragment> TrafficDebugFragments = Context.GetFragmentView<FMassTrafficDebugFragment>();
				const TArrayView<FMassRepresentationFragment> VisualizationList = Context.GetMutableFragmentView<FMassRepresentationFragment>();

				for (int EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
				{
					const FTransformFragment& TransformFragment = TransformList[EntityIdx];
					FMassRepresentationFragment& Visualization = VisualizationList[EntityIdx];
					const int32 CurrentVisualIdx = static_cast<int32>(Visualization.CurrentRepresentation);
					DrawDebugPoint(World, TransformFragment.GetTransform().GetLocation() + FVector(50.0f, 0.0f, 200.0f), 10.0f, UE::MassLOD::LODColors[CurrentVisualIdx]);

					const bool bVisLogEvenIfOff = TrafficDebugFragments.Num() > 0 && TrafficDebugFragments[EntityIdx].bVisLog;
					if (((Visualization.CurrentRepresentation != EMassRepresentationType::None || bVisLogEvenIfOff) && GMassTrafficDebugVisualization >= 2) || GMassTrafficDebugVisualization >= 3)
					{
						UE_VLOG_LOCATION(LogOwnerPtr, TEXT("MassTraffic Vis"), Log, TransformFragment.GetTransform().GetLocation() + FVector(50.0f, 0.0f, 200.0f), /*Radius*/ 10.0f, UE::MassLOD::LODColors[CurrentVisualIdx], TEXT("%d %d"), CurrentVisualIdx, Context.GetEntity(EntityIdx).Index);
					}
				}
			});
	}
#endif // WITH_MASSTRAFFIC_DEBUG
}