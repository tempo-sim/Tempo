// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficFindDeviantParkedVehiclesProcessor.h"
#include "MassTrafficFragments.h"

#include "MassNavigationFragments.h"
#include "MassMovementFragments.h"
#include "MassCrowdFragments.h"
#include "MassCommandBuffer.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassLookAtFragments.h"
#include "MassTrafficVehicleSimulationTrait.h"
#include "VisualLogger/VisualLogger.h"


UMassTrafficFindDeviantParkedVehiclesProcessor::UMassTrafficFindDeviantParkedVehiclesProcessor()
	: NominalParkedVehicleEntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::ParkedVehicleBehavior;
}

void UMassTrafficFindDeviantParkedVehiclesProcessor::ConfigureQueries()
{
	NominalParkedVehicleEntityQuery.AddTagRequirement<FMassTrafficParkedVehicleTag>(EMassFragmentPresence::All);
	NominalParkedVehicleEntityQuery.AddTagRequirement<FMassTrafficDisturbedVehicleTag>(EMassFragmentPresence::None);
	NominalParkedVehicleEntityQuery.AddTagRequirement<FMassVisibilityCulledByDistanceTag>(EMassFragmentPresence::None);
	NominalParkedVehicleEntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	NominalParkedVehicleEntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly);
	NominalParkedVehicleEntityQuery.AddConstSharedRequirement<FMassTrafficVehicleSimulationParameters>();
}

void UMassTrafficFindDeviantParkedVehiclesProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Look for deviant vehicles
	NominalParkedVehicleEntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& QueryContext)
	{
		const FMassTrafficVehicleSimulationParameters& SimulationParams = QueryContext.GetConstSharedFragment<FMassTrafficVehicleSimulationParameters>();
		const TConstArrayView<FTransformFragment> TransformFragments = QueryContext.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassActorFragment> ActorFragments = QueryContext.GetFragmentView<FMassActorFragment>();

		// Loop obstacles
		const int32 NumEntities = QueryContext.GetNumEntities();
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			const FTransformFragment& TransformFragment = TransformFragments[Index];
			const FMassActorFragment& ActorFragment = ActorFragments[Index];
			const AActor* Actor = ActorFragment.Get();

			if (Actor != nullptr)
			{
				// Has the entity transform and actual simulated actor transform deviated significantly and if so
				// this parked vehicle is deviant
				const FVector ActorLocation = Actor->GetActorLocation();
				const float Deviation = FVector::Distance(TransformFragment.GetTransform().GetLocation(), ActorLocation);
				if (Deviation > MassTrafficSettings->ParkedVehicleDeviationTolerance)
				{
					const FMassEntityHandle ParkedVehicleEntity = QueryContext.GetEntity(Index);

					// Add an FTagFragment_MassTrafficObstacle tag to it so it's considered for obstacle avoidance.
					QueryContext.Defer().AddTag<FMassTrafficObstacleTag>(ParkedVehicleEntity);
					// Add a disturbed vehicle tag so we can update the entity with the actor transform if it's a complex LOD.
					QueryContext.Defer().AddTag<FMassTrafficDisturbedVehicleTag>(ParkedVehicleEntity);

					// Add fragments to allow both traffic and crowd systems to notice this vehicle as an obstacle. 
					QueryContext.Defer().AddTag<FMassLookAtTargetTag>(ParkedVehicleEntity);
					QueryContext.Defer().PushCommand<FMassCommandAddFragments<
						FMassNavigationObstacleGridCellLocationFragment		// Needed to become a crowd avoidance obstacle
						, FMassCrowdObstacleFragment						// Needed to be a zone graph dynamic obstacle
						, FMassVelocityFragment								// Add velocity to make it a valid obstacle
						, FMassTrafficVehicleDamageFragment>>					// So we can keep track of damage.
						(ParkedVehicleEntity);

					// Add avoidance collider data for crowd system.
					FMassPillCollider Pill(SimulationParams.HalfWidth, SimulationParams.HalfLength);
					FMassAvoidanceColliderFragment ColliderFragment(Pill);
					// Add the vehicle radius fragment for obstacle avoidance.
					FAgentRadiusFragment RadiusFragment;
					RadiusFragment.Radius = SimulationParams.HalfLength;
					QueryContext.Defer().PushCommand<FMassCommandAddFragmentInstances>(ParkedVehicleEntity, ColliderFragment, RadiusFragment);

					// Debug
					UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Deviants"), Log, ActorLocation, 10.0f, FColor::Red, TEXT("%d Deviated by %f"), QueryContext.GetEntity(Index).Index, Deviation);
					UE_VLOG_SEGMENT_THICK(LogOwner, TEXT("MassTraffic Deviants"), Log, ActorLocation, TransformFragment.GetTransform().GetLocation(), FColor::Red, 3.0f, TEXT(""));
				}
			}
		}
	});
}
