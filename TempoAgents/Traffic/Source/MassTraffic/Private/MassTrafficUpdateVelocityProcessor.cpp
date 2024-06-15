// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficUpdateVelocityProcessor.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "MassRepresentationFragments.h"
#include "MassSimulationLOD.h"
#include "MassTrafficInterpolationProcessor.h"

UMassTrafficUpdateVelocityProcessor::UMassTrafficUpdateVelocityProcessor()
	: EntityQuery_Conditional(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleBehavior;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PreVehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleSimulationLOD);
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficInterpolationProcessor::StaticClass()->GetFName());
}

void UMassTrafficUpdateVelocityProcessor::ConfigureQueries()
{
	EntityQuery_Conditional.AddRequirement<FMassTrafficPIDVehicleControlFragment>(EMassFragmentAccess::None, EMassFragmentPresence::None);
	EntityQuery_Conditional.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FMassTrafficAngularVelocityFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FMassSimulationVariableTickFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.SetChunkFilter(FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);
}

void UMassTrafficUpdateVelocityProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Advance agents
	EntityQuery_Conditional.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& ComponentSystemExecutionContext)
		{
			const int32 NumEntities = Context.GetNumEntities();
		
			const TConstArrayView<FTransformFragment> TransformFragments = Context.GetFragmentView<FTransformFragment>();
			const TConstArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = Context.GetFragmentView<FMassTrafficVehicleControlFragment>();
			const TConstArrayView<FMassRepresentationFragment> RepresentationFragments = Context.GetFragmentView<FMassRepresentationFragment>();
			const TConstArrayView<FMassSimulationVariableTickFragment> SimulationVariableTickFragments = Context.GetFragmentView<FMassSimulationVariableTickFragment>();
			const TArrayView<FMassVelocityFragment> VelocityFragments = Context.GetMutableFragmentView<FMassVelocityFragment>();
			const TArrayView<FMassTrafficAngularVelocityFragment> AngularVelocityFragments = Context.GetMutableFragmentView<FMassTrafficAngularVelocityFragment>();

			for (int32 Index = 0; Index < NumEntities; ++Index)
			{
				const FTransformFragment& TransformFragment = TransformFragments[Index];
				const FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[Index];
				const FMassRepresentationFragment& RepresentationFragment = RepresentationFragments[Index];
				const FMassSimulationVariableTickFragment& SimulationVariableTickFragment = SimulationVariableTickFragments[Index];
				FMassVelocityFragment& VelocityFragment = VelocityFragments[Index];
				FMassTrafficAngularVelocityFragment& AngularVelocityFragment = AngularVelocityFragments[Index];
			
				// Init velocity to current lane location direction * current speed 
				VelocityFragment.Value = TransformFragment.GetTransform().GetRotation().GetForwardVector() * VehicleControlFragment.Speed;

				// Compute instantaneous angular velocity from PrevTransform
				// Note: PrevTransform is updated by the representation processors and therefore only valid for visible
				// entities. Simply falling back to 0 angular velocity for invisible entities seems to be fine.
				if (RepresentationFragment.CurrentRepresentation < EMassRepresentationType::None && SimulationVariableTickFragment.DeltaTime > 0.0f)
				{
					AngularVelocityFragment.AngularVelocity = Chaos::FRotation3::CalculateAngularVelocity(RepresentationFragment.PrevTransform.GetRotation(), TransformFragment.GetTransform().GetRotation(), SimulationVariableTickFragment.DeltaTime);
				}
				else
				{
					AngularVelocityFragment.AngularVelocity = FVector::ZeroVector;
				}
			}
		});
}