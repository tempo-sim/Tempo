// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficUpdateTrailersProcessor.h"
#include "MassTrafficFragments.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "MassRepresentationFragments.h"

UMassTrafficUpdateTrailersProcessor::UMassTrafficUpdateTrailersProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::TrailerBehavior;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleVisualization);
}

void UMassTrafficUpdateTrailersProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassTrafficConstrainedVehicleFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassTrafficVehiclePhysicsFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery.AddRequirement<FMassTrafficSimulationLODFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassTrafficAngularVelocityFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FMassTrafficVehiclePhysicsSharedParameters>();
}

void UMassTrafficUpdateTrailersProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Advance agents
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& ComponentSystemExecutionContext)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const FMassTrafficVehiclePhysicsSharedParameters& PhysicsParams = Context.GetConstSharedFragment<FMassTrafficVehiclePhysicsSharedParameters>();
		const TConstArrayView<FMassTrafficConstrainedVehicleFragment> ConstrainedVehicleFragments = Context.GetFragmentView<FMassTrafficConstrainedVehicleFragment>();
		const TConstArrayView<FMassTrafficVehiclePhysicsFragment> SimpleVehiclePhysicsFragments = Context.GetFragmentView<FMassTrafficVehiclePhysicsFragment>();
		const TArrayView<FTransformFragment> TransformFragments = Context.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FMassVelocityFragment> VelocityFragments = Context.GetMutableFragmentView<FMassVelocityFragment>();
		const TArrayView<FMassTrafficAngularVelocityFragment> AngularVelocityFragments = Context.GetMutableFragmentView<FMassTrafficAngularVelocityFragment>();
		const TArrayView<FMassRepresentationLODFragment> RepresentationLODFragments = Context.GetMutableFragmentView<FMassRepresentationLODFragment>();
		const TArrayView<FMassTrafficSimulationLODFragment> SimulationLODFragments = Context.GetMutableFragmentView<FMassTrafficSimulationLODFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassTrafficConstrainedVehicleFragment& ConstrainedVehicleFragment = ConstrainedVehicleFragments[EntityIndex];
			FTransformFragment& TransformFragment = TransformFragments[EntityIndex];
			FMassVelocityFragment& VelocityFragment = VelocityFragments[EntityIndex];
			FMassTrafficAngularVelocityFragment& AngularVelocityFragment = AngularVelocityFragments[EntityIndex];
			FMassRepresentationLODFragment& RepresentationLODFragment = RepresentationLODFragments[EntityIndex];
			FMassTrafficSimulationLODFragment& SimulationLODFragment = SimulationLODFragments[EntityIndex];

			// Sanity check
			if (!ensure(EntityManager.IsEntityValid(ConstrainedVehicleFragment.Vehicle)))
			{
				continue;
			}
		
			FMassEntityView VehicleMassEntityView(EntityManager, ConstrainedVehicleFragment.Vehicle);

			// Copy viewer LOD from vehicle
			RepresentationLODFragment = VehicleMassEntityView.GetFragmentData<FMassRepresentationLODFragment>();

			// Copy simulation LOD from vehicle
			SimulationLODFragment = VehicleMassEntityView.GetFragmentData<FMassTrafficSimulationLODFragment>();

			// Medium or High Simulation LOD?
			if (SimulationLODFragment.LOD <= EMassLOD::Medium)
			{
				// LOD Change from Low / Off LOD?
				if (SimulationLODFragment.PrevLOD >= EMassLOD::Low)
				{
					// Add simulation fragment
					if (SimpleVehiclePhysicsFragments.IsEmpty())
					{
						if (PhysicsParams.Template)
						{
							Context.Defer().PushCommand<FMassCommandAddFragmentInstances>(Context.GetEntity(EntityIndex), PhysicsParams.Template->SimpleVehiclePhysicsFragmentTemplate);
						}
					}
				}
			}
			// Low or Off
			else
			{
				// Remove simulation fragment 
				if (!SimpleVehiclePhysicsFragments.IsEmpty())
				{
					Context.Defer().RemoveFragment<FMassTrafficVehiclePhysicsFragment>(Context.GetEntity(EntityIndex));
				}
			}

			// Simply copy transform & velocity from vehicle when not simulating
			// 
			// Note: This must be gated based on the presence of the simulation fragments, rather than checking
			// SimulationLODFragment.LOD, which doesn't happen until the frame after we request their addition above.
			// This matches TrafficVehicleControl's behaviour of choosing movement methods based on simulation
			// fragment presence.
			if (SimpleVehiclePhysicsFragments.IsEmpty())
			{
				TransformFragment = VehicleMassEntityView.GetFragmentData<FTransformFragment>();
				VelocityFragment = VehicleMassEntityView.GetFragmentData<FMassVelocityFragment>();
				AngularVelocityFragment = VehicleMassEntityView.GetFragmentData<FMassTrafficAngularVelocityFragment>();
			}
		}
	});
}
