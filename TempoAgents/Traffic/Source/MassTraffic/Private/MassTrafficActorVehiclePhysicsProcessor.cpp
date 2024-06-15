// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficActorVehiclePhysicsProcessor.h"
#include "MassTraffic.h"
#include "MassTrafficFragments.h"
#include "MassExecutionContext.h"
#include "MassRepresentationFragments.h"
#include "MassTrafficVehicleControlInterface.h"
#include "MassTrafficVehicleControlProcessor.h"

UMassTrafficActorVehiclePhysicsProcessor::UMassTrafficActorVehiclePhysicsProcessor()
	: ChaosPhysicsVehiclesQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleBehavior;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PreVehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleSimulationLOD);
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficVehicleControlProcessor::StaticClass()->GetFName());
}

void UMassTrafficActorVehiclePhysicsProcessor::ConfigureQueries()
{
	ChaosPhysicsVehiclesQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	ChaosPhysicsVehiclesQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	ChaosPhysicsVehiclesQuery.AddRequirement<FMassTrafficPIDVehicleControlFragment>(EMassFragmentAccess::ReadOnly);
	ChaosPhysicsVehiclesQuery.AddRequirement<FMassTrafficVehicleDamageFragment>(EMassFragmentAccess::ReadOnly);
	ChaosPhysicsVehiclesQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
	ChaosPhysicsVehiclesQuery.AddRequirement<FMassTrafficDebugFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
}

void UMassTrafficActorVehiclePhysicsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Pass new vehicle control inputs to HighRes actors 
	ChaosPhysicsVehiclesQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& ComponentSystemExecutionContext)
	{
		const TConstArrayView<FMassRepresentationFragment> RepresentationFragments = Context.GetFragmentView<FMassRepresentationFragment>();
		const TConstArrayView<FMassTrafficPIDVehicleControlFragment> PIDVehicleControlFragments = Context.GetFragmentView<FMassTrafficPIDVehicleControlFragment>();
		const TConstArrayView<FMassTrafficVehicleDamageFragment> VehicleDamageFragments = Context.GetFragmentView<FMassTrafficVehicleDamageFragment>();
		const TArrayView<FMassActorFragment> ActorFragments = Context.GetMutableFragmentView<FMassActorFragment>();

		const int32 NumEntities = Context.GetNumEntities();
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			if (RepresentationFragments[Index].CurrentRepresentation != EMassRepresentationType::HighResSpawnedActor)
			{
				continue;
			}

			const FMassTrafficPIDVehicleControlFragment& PIDVehicleControlFragment = PIDVehicleControlFragments[Index];
			FMassActorFragment& ActorFragment = ActorFragments[Index];

			AActor* Actor = ActorFragment.GetMutable();
			if (Actor != nullptr && Actor->Implements<UMassTrafficVehicleControlInterface>())
			{
				if (VehicleDamageFragments[Index].VehicleDamageState >= EMassTrafficVehicleDamageState::Totaled)
				{
					Context.Defer().PushCommand<FMassDeferredSetCommand>([Actor](FMassEntityManager&)
					{
						// Throttle off, full brake, no handbrake, steering is not set.
						IMassTrafficVehicleControlInterface::Execute_SetVehicleInputs(Actor, 0.0f, 1.0f, false, 0.0f, false);
					});
				}
				else
				{
					Context.Defer().PushCommand<FMassDeferredSetCommand>([Actor, PIDVehicleControlFragment](FMassEntityManager&)
					{
						IMassTrafficVehicleControlInterface::Execute_SetVehicleInputs(Actor,
							PIDVehicleControlFragment.Throttle,
							PIDVehicleControlFragment.Brake,
							PIDVehicleControlFragment.bHandbrake,
							PIDVehicleControlFragment.Steering,
							/*bSetSteering*/ true);
					});
				}
			}
		}
	});
}