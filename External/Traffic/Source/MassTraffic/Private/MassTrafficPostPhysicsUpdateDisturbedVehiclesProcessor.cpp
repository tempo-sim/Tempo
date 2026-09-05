// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficPostPhysicsUpdateDisturbedVehiclesProcessor.h"
#include "MassTrafficMovement.h"
#include "MassTrafficVehicleInterface.h"
#include "MassMovementFragments.h"
#include "MassExecutionContext.h"
#include "MassRepresentationFragments.h"
#include "MassActorSubsystem.h"

UMassTrafficPostPhysicsUpdateDisturbedVehiclesProcessor::UMassTrafficPostPhysicsUpdateDisturbedVehiclesProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DisturbedVehicleQuery(*this)
{
	// Update post-physics transform to be used on the next frame
	bAutoRegisterWithProcessingPhases = true;
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::PostPhysicsUpdateTrafficVehicles;
}

void UMassTrafficPostPhysicsUpdateDisturbedVehiclesProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// "Disturbed" vehicles are parked vehicles that have been driven off by the player or smashed into, i.e. disturbed
	// from their original spawn location. This means they'll have an obstacle tag and a velocity fragment from the
	// FindDeviantParkedVehicles processor.
	DisturbedVehicleQuery.AddTagRequirement<FMassTrafficDisturbedVehicleTag>(EMassFragmentPresence::All);
	DisturbedVehicleQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly);
	DisturbedVehicleQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
	DisturbedVehicleQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	DisturbedVehicleQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	DisturbedVehicleQuery.AddRequirement<FMassTrafficVehicleDamageFragment>(EMassFragmentAccess::ReadWrite);

}

void UMassTrafficPostPhysicsUpdateDisturbedVehiclesProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext)
{
	// The main point of this processor is to update Mass with the location of the actor.
	DisturbedVehicleQuery.ForEachEntityChunk(ExecutionContext, [&](FMassExecutionContext& Context)
	{
		const TConstArrayView<FMassActorFragment> ActorFragments = Context.GetFragmentView<FMassActorFragment>();
		const TArrayView<FMassRepresentationFragment> RepresentationFragments = Context.GetMutableFragmentView<FMassRepresentationFragment>();
		const TArrayView<FTransformFragment> TransformFragments = Context.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FMassTrafficVehicleDamageFragment> VehicleDamageFragments = Context.GetMutableFragmentView<FMassTrafficVehicleDamageFragment>();
		const TArrayView<FMassVelocityFragment> VelocityFragments = Context.GetMutableFragmentView<FMassVelocityFragment>();

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			FMassRepresentationFragment& RepresentationFragment = RepresentationFragments[EntityIt];
			FTransformFragment& TransformFragment = TransformFragments[EntityIt];
			FMassVelocityFragment& VelocityFragment = VelocityFragments[EntityIt];
			FMassTrafficVehicleDamageFragment& VehicleDamageFragment = VehicleDamageFragments[EntityIt];

			const AActor* Actor = ActorFragments[EntityIt].Get();
			if (Actor != nullptr && RepresentationFragments[EntityIt].CurrentRepresentation == EMassRepresentationType::HighResSpawnedActor)
			{
				// Update transform from actor based LOD
				TransformFragment.SetTransform(Actor->GetActorTransform());

				// Update velocity to current vehicle linear velocity
				VelocityFragment.Value = Actor->GetVelocity();

				// Update PrevTransform for the next frame to use. RepresentationFragment has already run so this
				// is for the next frame. This processor runs in PostPhysics.
				RepresentationFragment.PrevTransform = TransformFragment.GetTransform(); 

				// Update damage state
				if (Actor->Implements<UMassTrafficVehicleInterface>())
				{
					VehicleDamageFragment.VehicleDamageState = IMassTrafficVehicleInterface::Execute_GetDamageState(Actor); 
				}
				else
				{
					VehicleDamageFragment.VehicleDamageState = EMassTrafficVehicleDamageState::None;
				}

			}
		}
	});
}
