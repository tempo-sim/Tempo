// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficPostPhysicsUpdateTrafficVehiclesProcessor.h"
#include "MassTrafficLaneChange.h"
#include "MassTrafficDamage.h"
#include "MassTrafficMovement.h"
#include "MassTrafficVehicleInterface.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassRepresentationFragments.h"
#include "MassActorSubsystem.h"
#include "MassZoneGraphNavigationFragments.h"
#include "ZoneGraphSubsystem.h"
#include "MassGameplayExternalTraits.h"
#include "VisualLogger/VisualLogger.h"

UMassTrafficPostPhysicsUpdateTrafficVehiclesProcessor::UMassTrafficPostPhysicsUpdateTrafficVehiclesProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, PIDControlTrafficVehicleQuery(*this)
{
	// Update post-physics transform to be used on the next frame
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	// @todo required due to Actor->SetActorTransform, could be addressed by turning it into a command.
	bRequiresGameThreadExecution = true;
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All); 
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::PostPhysicsUpdateTrafficVehicles;
}

void UMassTrafficPostPhysicsUpdateTrafficVehiclesProcessor::ConfigureQueries()
{
	PIDControlTrafficVehicleQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
	PIDControlTrafficVehicleQuery.AddRequirement<FMassTrafficPIDVehicleControlFragment>(EMassFragmentAccess::None, EMassFragmentPresence::All);
	PIDControlTrafficVehicleQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	PIDControlTrafficVehicleQuery.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadOnly);
	PIDControlTrafficVehicleQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
	PIDControlTrafficVehicleQuery.AddRequirement<FMassTrafficNextVehicleFragment>(EMassFragmentAccess::ReadWrite);
	PIDControlTrafficVehicleQuery.AddRequirement<FMassTrafficVehicleDamageFragment>(EMassFragmentAccess::ReadWrite);
	PIDControlTrafficVehicleQuery.AddRequirement<FMassTrafficVehicleLaneChangeFragment>(EMassFragmentAccess::ReadWrite);
	PIDControlTrafficVehicleQuery.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadWrite);
	PIDControlTrafficVehicleQuery.AddRequirement<FMassTrafficVehicleLightsFragment>(EMassFragmentAccess::ReadWrite);
	PIDControlTrafficVehicleQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	PIDControlTrafficVehicleQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadWrite);
	PIDControlTrafficVehicleQuery.AddRequirement<FMassTrafficAngularVelocityFragment>(EMassFragmentAccess::ReadWrite);
	PIDControlTrafficVehicleQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
	PIDControlTrafficVehicleQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	PIDControlTrafficVehicleQuery.AddConstSharedRequirement<FMassTrafficVehicleSimulationParameters>();
	PIDControlTrafficVehicleQuery.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
	PIDControlTrafficVehicleQuery.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
	PIDControlTrafficVehicleQuery.RequireMutatingWorldAccess(); // due to mutating actor's location/physics
}

void UMassTrafficPostPhysicsUpdateTrafficVehiclesProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Advance agents
	PIDControlTrafficVehicleQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& Context)
	{
		UMassTrafficSubsystem& MassTrafficSubsystem = Context.GetMutableSubsystemChecked<UMassTrafficSubsystem>();
		const UZoneGraphSubsystem& ZoneGraphSubsystem = Context.GetSubsystemChecked<UZoneGraphSubsystem>();

		const int32 NumEntities = Context.GetNumEntities();
		const TConstArrayView<FAgentRadiusFragment> AgentRadiusFragments = Context.GetFragmentView<FAgentRadiusFragment>();
		const TConstArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = Context.GetFragmentView<FMassTrafficRandomFractionFragment>();
		const TArrayView<FMassActorFragment> ActorFragments = Context.GetMutableFragmentView<FMassActorFragment>();
		const TArrayView<FMassTrafficVehicleDamageFragment> VehicleDamageFragments = Context.GetMutableFragmentView<FMassTrafficVehicleDamageFragment>();
		const TArrayView<FMassTrafficVehicleLaneChangeFragment> LaneChangeFragments = Context.GetMutableFragmentView<FMassTrafficVehicleLaneChangeFragment>();
		const TArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = Context.GetMutableFragmentView<FMassTrafficVehicleControlFragment>();
		const TArrayView<FMassTrafficVehicleLightsFragment> VehicleLightsFragments = Context.GetMutableFragmentView<FMassTrafficVehicleLightsFragment>();
		const TArrayView<FTransformFragment> TransformFragments = Context.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = Context.GetMutableFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TArrayView<FMassTrafficAngularVelocityFragment> AngularVelocityFragments = Context.GetMutableFragmentView<FMassTrafficAngularVelocityFragment>();
		const TArrayView<FMassRepresentationFragment> RepresentationFragments = Context.GetMutableFragmentView<FMassRepresentationFragment>();
		const TArrayView<FMassVelocityFragment> VelocityFragments = Context.GetMutableFragmentView<FMassVelocityFragment>();

		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[Index];
			FMassTrafficVehicleLightsFragment& VehicleLightsFragment = VehicleLightsFragments[Index];
			FMassZoneGraphLaneLocationFragment& LaneLocationFragment = LaneLocationFragments[Index];
			FMassTrafficAngularVelocityFragment& AngularVelocityFragment = AngularVelocityFragments[Index];
			FMassRepresentationFragment& RepresentationFragment = RepresentationFragments[Index];
			FMassVelocityFragment& VelocityFragment = VelocityFragments[Index];
			FMassActorFragment& ActorFragment = ActorFragments[Index];
			FTransformFragment& TransformFragment = TransformFragments[Index];
			FMassTrafficVehicleDamageFragment& VehicleDamageFragment = VehicleDamageFragments[Index];

			AActor* Actor = ActorFragment.GetMutable();
			if (Actor != nullptr && RepresentationFragment.CurrentRepresentation == EMassRepresentationType::HighResSpawnedActor)
			{
				// Update transform from High LOD chaos vehicle simulation
				TransformFragment.SetTransform(Actor->GetActorTransform());

				// Also update PrevTransform for the next frame to use
				RepresentationFragment.PrevTransform = TransformFragment.GetTransform(); 

				UPrimitiveComponent* RootComponent = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
				if (ensure(RootComponent))
				{
					// Update velocity to current vehicle linear velocity
					VelocityFragment.Value = RootComponent->GetPhysicsLinearVelocity();

					// Update angular velocity
					AngularVelocityFragment.AngularVelocity = RootComponent->GetPhysicsAngularVelocityInRadians();
				}

				// Update speed
				VehicleControlFragment.Speed = VelocityFragment.Value.Size();

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
			
			// Get new distance along lane after simulation of both ChaosVehiclePhysics and SimpleVehiclePhysics
			FBox SearchLocationAndExtent = FBox::BuildAABB(TransformFragment.GetTransform().GetLocation(), FVector(100000.0f));
			FZoneGraphLaneLocation NearestLaneLocation;
			float DistanceSq;
			if (ZoneGraphSubsystem.FindNearestLocationOnLane(LaneLocationFragment.LaneHandle, SearchLocationAndExtent, NearestLaneLocation, DistanceSq))
			{
				// Advance distance based noise before updating LaneLocationFragment.DistanceAlongLane
				VehicleControlFragment.NoiseInput += NearestLaneLocation.DistanceAlongLane - LaneLocationFragment.DistanceAlongLane;
				
				// Update distance along lane after simulation from the previous frame
				LaneLocationFragment.DistanceAlongLane = NearestLaneLocation.DistanceAlongLane;
			}
			else
			{
				#if WITH_MASSTRAFFIC_DEBUG
					UE_VLOG_LOCATION(&MassTrafficSubsystem, LogMassTraffic, Error, TransformFragment.GetTransform().GetLocation(), 50.0f, FColor::Red, TEXT("PostPhysicsUpdateTrafficVehicles FindNearestLocationOnLane failed"));
				#endif
			}
			
			// Overran the lane?
			if (LaneLocationFragment.DistanceAlongLane >= LaneLocationFragment.LaneLength && VehicleControlFragment.NextLane)
			{
				// Check we are allowed into the next lane
				const bool bCanProceed = VehicleControlFragment.NextLane->bIsOpen || VehicleControlFragment.Speed > Chaos::MPHToCmS(5.0f);
				if (bCanProceed)
				{
					// Proceed onto next chosen lane

					bool bHasVehicleBecomeStuck_Ignored = false;
					
					UE::MassTraffic::MoveVehicleToNextLane(
						EntityManager,
						MassTrafficSubsystem,
						Context.GetEntity(Index),
						AgentRadiusFragments[Index],
						RandomFractionFragments[Index],
						VehicleControlFragment,
						VehicleLightsFragment,
						LaneLocationFragment,
						Context.GetMutableFragmentView<FMassTrafficNextVehicleFragment>()[Index],
						&LaneChangeFragments[Index],
						bHasVehicleBecomeStuck_Ignored/*out*/);

					// Re-eval position on next lane
					ZoneGraphSubsystem.FindNearestLocationOnLane(LaneLocationFragment.LaneHandle, SearchLocationAndExtent, NearestLaneLocation, DistanceSq);
					LaneLocationFragment.DistanceAlongLane = NearestLaneLocation.DistanceAlongLane;

					// Advance distance based noise
					VehicleControlFragment.NoiseInput += LaneLocationFragment.DistanceAlongLane;
				}
			}
		}
	});
}
