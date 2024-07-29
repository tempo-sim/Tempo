// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficPostPhysicsUpdateTrailersProcessor.h"
#include "MassTrafficMovement.h"
#include "MassTrafficVehicleInterface.h"
#include "MassMovementFragments.h"
#include "MassExecutionContext.h"
#include "MassRepresentationFragments.h"
#include "MassActorSubsystem.h"
#include "MassTrafficPostPhysicsUpdateTrafficVehiclesProcessor.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

UMassTrafficPostPhysicsUpdateTrailersProcessor::UMassTrafficPostPhysicsUpdateTrailersProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, EntityQuery(*this)
{
	// Update post-physics transform to be used on the next frame
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::PostPhysicsUpdateTrafficVehicles;
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficPostPhysicsUpdateTrafficVehiclesProcessor::StaticClass()->GetFName());
}

void UMassTrafficPostPhysicsUpdateTrailersProcessor::ConfigureQueries()
{
	EntityQuery.AddTagRequirement<FMassTrafficVehicleTrailerTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassTrafficAngularVelocityFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassTrafficConstrainedVehicleFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficPostPhysicsUpdateTrailersProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// The main point of this processor is to update Mass with the location of the actor.
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [](FMassExecutionContext& QueryContext)
	{
		const TArrayView<FMassActorFragment> TrailerActorFragments = QueryContext.GetMutableFragmentView<FMassActorFragment>();
		const TArrayView<FMassRepresentationFragment> TrailerRepresentationFragments = QueryContext.GetMutableFragmentView<FMassRepresentationFragment>();
		const TArrayView<FTransformFragment> TrailerTransformFragments = QueryContext.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FMassVelocityFragment> TrailerVelocityFragments = QueryContext.GetMutableFragmentView<FMassVelocityFragment>();
		const TArrayView<FMassTrafficAngularVelocityFragment> TrailerAngularVelocityFragments = QueryContext.GetMutableFragmentView<FMassTrafficAngularVelocityFragment>();
		const TArrayView<FMassTrafficConstrainedVehicleFragment> TrailerConstrainedVehicleFragments = QueryContext.GetMutableFragmentView<FMassTrafficConstrainedVehicleFragment>();

		for (int32 Index = 0; Index < QueryContext.GetNumEntities(); ++Index)
		{
			FMassRepresentationFragment& TrailerRepresentationFragment = TrailerRepresentationFragments[Index];
			FTransformFragment& TrailerTransformFragment = TrailerTransformFragments[Index];
			FMassVelocityFragment& TrailerVelocityFragment = TrailerVelocityFragments[Index];
			FMassTrafficAngularVelocityFragment& TrailerAngularVelocityFragment = TrailerAngularVelocityFragments[Index]; 
			FMassTrafficConstrainedVehicleFragment& TrailerConstrainedVehicleFragment = TrailerConstrainedVehicleFragments[Index]; 

			AActor* TrailerActor = TrailerActorFragments[Index].GetMutable();
			if (IsValid(TrailerActor) && TrailerRepresentationFragments[Index].CurrentRepresentation == EMassRepresentationType::HighResSpawnedActor)
			{
				// Check to make sure we are still constrained to the vehicle. It may be that the vehicle was destroyed
				// and we are no longer constrained e.g if we are dropping back LOD, the vehicle destruction was
				// processed first and the destruction time allocation was filled, deferring trailer deletion to a later
				// frame. In this case we want to override this unconstrained simulation frame and use our medium LOD
				// simulation which is always constrained.
				bool bConstrained = false;
				if (UPhysicsConstraintComponent* PhysicsConstraintComponent = TrailerConstrainedVehicleFragment.PhysicsConstraintComponent.Get())
				{
					bConstrained = !PhysicsConstraintComponent->IsBroken();
				}

				// If we're still constrained, the simulation is valid and we sync the new transform and velocities back
				// into mass
				if (bConstrained)
				{
					// Update transform from actor based LOD
					TrailerTransformFragment.SetTransform(TrailerActor->GetActorTransform());

					UPrimitiveComponent* RootComponent = Cast<UPrimitiveComponent>(TrailerActor->GetRootComponent());
					if (ensure(RootComponent))
					{
						// Update velocity to current vehicle linear velocity
						TrailerVelocityFragment.Value = RootComponent->GetPhysicsLinearVelocity();

						// Update angular velocity
						TrailerAngularVelocityFragment.AngularVelocity = RootComponent->GetPhysicsAngularVelocityInRadians();
					}

					// Update PrevTransform for the next frame to use. RepresentationFragment has already run so this
					// is for the next frame. This processor runs in PostPhysics.
					TrailerRepresentationFragment.PrevTransform = TrailerTransformFragment.GetTransform();
				}
				// If we aren't constrained, discard / override this simulation frame and use the constrained medium
				// LOD simulation transform instead. The main vehicle actor was probably destroyed this frame and we're
				// waiting to be destroyed ourselves
				else
				{
					TrailerActor->SetActorTransform(TrailerTransformFragment.GetTransform(), /*bSweep*/false, /*OutHitResult*/nullptr, ETeleportType::TeleportPhysics);
				}
			}
		}
	});
}
