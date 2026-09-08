// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficFindDeviantTrafficVehiclesProcessor.h"
#include "MassActorSubsystem.h"
#include "MassTrafficFragments.h"
#include "MassTrafficInterpolation.h"
#include "MassTrafficLaneChange.h"
#include "MassTrafficUpdateVelocityProcessor.h"
#include "MassCommandBuffer.h"
#include "MassExecutionContext.h"
#include "MassEntityView.h"
#include "MassLookAtFragments.h"
#include "MassNavigationFragments.h"
#include "MassNavigationSubsystem.h"
#include "MassRepresentationFragments.h"
#include "MassTrafficVehicleSimulationTrait.h"
#include "MassCrowdFragments.h"
#include "MassZoneGraphNavigationFragments.h"
#include "VisualLogger/VisualLogger.h"
#include "ZoneGraphSubsystem.h"


UMassTrafficFindDeviantTrafficVehiclesProcessor::UMassTrafficFindDeviantTrafficVehiclesProcessor()
	: NominalTrafficVehicleEntityQuery(*this)
	, DeviantTrafficVehicleEntityQuery(*this)
	, CorrectedTrafficVehicleEntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleBehavior;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PreVehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleSimulationLOD);
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficUpdateVelocityProcessor::StaticClass()->GetFName());
}

void UMassTrafficFindDeviantTrafficVehiclesProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// High LOD physics vehicles which haven't been marked as deviant obstacles to check for deviation
	NominalTrafficVehicleEntityQuery.AddTagRequirement<FMassTrafficObstacleTag>(EMassFragmentPresence::None);
	NominalTrafficVehicleEntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
	NominalTrafficVehicleEntityQuery.AddRequirement<FMassTrafficPIDVehicleControlFragment>(EMassFragmentAccess::None, EMassFragmentPresence::All);
	NominalTrafficVehicleEntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	NominalTrafficVehicleEntityQuery.AddRequirement<FMassTrafficVehicleLightsFragment>(EMassFragmentAccess::ReadWrite);
	NominalTrafficVehicleEntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	NominalTrafficVehicleEntityQuery.AddRequirement<FMassTrafficLaneOffsetFragment>(EMassFragmentAccess::ReadOnly);
	NominalTrafficVehicleEntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly);
	NominalTrafficVehicleEntityQuery.AddRequirement<FMassTrafficInterpolationFragment>(EMassFragmentAccess::ReadWrite);
	NominalTrafficVehicleEntityQuery.AddRequirement<FMassTrafficVehicleLaneChangeFragment>(EMassFragmentAccess::ReadWrite);
	NominalTrafficVehicleEntityQuery.AddRequirement<FMassTrafficNextVehicleFragment>(EMassFragmentAccess::ReadWrite);
	NominalTrafficVehicleEntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	NominalTrafficVehicleEntityQuery.AddConstSharedRequirement<FMassTrafficVehicleSimulationParameters>();
	NominalTrafficVehicleEntityQuery.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);

	// Known deviant physics vehicles which we check for correction
	DeviantTrafficVehicleEntityQuery.AddTagRequirement<FMassTrafficObstacleTag>(EMassFragmentPresence::All);
	DeviantTrafficVehicleEntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
	DeviantTrafficVehicleEntityQuery.AddRequirement<FMassTrafficPIDVehicleControlFragment>(EMassFragmentAccess::None, EMassFragmentPresence::All);
	DeviantTrafficVehicleEntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	DeviantTrafficVehicleEntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	DeviantTrafficVehicleEntityQuery.AddRequirement<FMassTrafficLaneOffsetFragment>(EMassFragmentAccess::ReadOnly);
	DeviantTrafficVehicleEntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly);
	DeviantTrafficVehicleEntityQuery.AddRequirement<FMassTrafficVehicleLaneChangeFragment>(EMassFragmentAccess::ReadOnly);
	DeviantTrafficVehicleEntityQuery.AddRequirement<FMassTrafficInterpolationFragment>(EMassFragmentAccess::ReadWrite);
	DeviantTrafficVehicleEntityQuery.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);
	DeviantTrafficVehicleEntityQuery.AddSubsystemRequirement<UMassNavigationSubsystem>(EMassFragmentAccess::ReadWrite);

	// Implicitly corrected vehicles (low LOD vehicles can't deviate)
	CorrectedTrafficVehicleEntityQuery.AddTagRequirement<FMassTrafficObstacleTag>(EMassFragmentPresence::All);
	CorrectedTrafficVehicleEntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
	CorrectedTrafficVehicleEntityQuery.AddRequirement<FMassTrafficPIDVehicleControlFragment>(EMassFragmentAccess::None, EMassFragmentPresence::None);
	// here to make the query valid - a query needs at least 1 required fragment to be valid. This is a current limitation of the system. To be addressed @todo.
	CorrectedTrafficVehicleEntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	CorrectedTrafficVehicleEntityQuery.AddSubsystemRequirement<UMassNavigationSubsystem>(EMassFragmentAccess::ReadWrite);
}

static void RemoveDeviantFragments(const FMassEntityManager& EntityManager, const FMassExecutionContext& Context, UMassNavigationSubsystem& MovementSubsystem, const int32 EntityIt)
{
	// This vehicle is no longer deviant, remove the FTagFragment_MassTrafficObstacle tag from it so it's
	// no longer considered for obstacle avoidance.
	const FMassEntityHandle Entity = Context.GetEntity(EntityIt);
	Context.Defer().RemoveTag<FMassTrafficObstacleTag>(Entity);
	Context.Defer().RemoveFragment<FMassLookAtTargetFragment>(Entity);

	// Manually do the work of UMassAvoidanceObstacleRemoverFragmentDestructor because it's not called on fragment removal.
	const FMassEntityView EntityView(EntityManager, Entity);
	if (const FMassNavigationObstacleGridCellLocationFragment* GridCellLocationList = EntityView.GetFragmentDataPtr<FMassNavigationObstacleGridCellLocationFragment>())
	{
		FMassNavigationObstacleItem ObstacleItem;
		ObstacleItem.Entity = Entity;
		MovementSubsystem.GetObstacleGridMutable().Remove(ObstacleItem, GridCellLocationList[EntityIt].CellLoc);
	}

	Context.Defer().PushCommand<FMassCommandRemoveFragments<
		FMassNavigationObstacleGridCellLocationFragment	// Not an avoidance obstacle anymore
		, FMassCrowdObstacleFragment					// Not a zone graph dynamic obstacle anymore
		, FMassAvoidanceColliderFragment>>
		(Entity);
}

void UMassTrafficFindDeviantTrafficVehiclesProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Look for deviant vehicles
	NominalTrafficVehicleEntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& QueryContext)
	{
		const UZoneGraphSubsystem& ZoneGraphSubsystem = QueryContext.GetSubsystemChecked<UZoneGraphSubsystem>();

		const FMassTrafficVehicleSimulationParameters& SimulationParams = QueryContext.GetConstSharedFragment<FMassTrafficVehicleSimulationParameters>();
		const TConstArrayView<FMassActorFragment> ActorFragments = QueryContext.GetFragmentView<FMassActorFragment>();
		const TConstArrayView<FMassRepresentationFragment> RepresentationFragments = QueryContext.GetFragmentView<FMassRepresentationFragment>();
		const TConstArrayView<FMassTrafficLaneOffsetFragment> LaneOffsetFragments = QueryContext.GetFragmentView<FMassTrafficLaneOffsetFragment>();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> ZoneGraphLaneLocationFragments = QueryContext.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TArrayView<FMassTrafficInterpolationFragment> VehicleMovementInterpolationFragments = QueryContext.GetMutableFragmentView<FMassTrafficInterpolationFragment>();
		const TArrayView<FMassTrafficNextVehicleFragment> NextVehicleFragments = QueryContext.GetMutableFragmentView<FMassTrafficNextVehicleFragment>();
		const TArrayView<FMassTrafficVehicleLaneChangeFragment> LaneChangeFragments = QueryContext.GetMutableFragmentView<FMassTrafficVehicleLaneChangeFragment>();
		const TArrayView<FMassTrafficVehicleLightsFragment> VehicleLightsFragments = QueryContext.GetMutableFragmentView<FMassTrafficVehicleLightsFragment>();

		// Loop obstacles
		for (FMassExecutionContext::FEntityIterator EntityIt = QueryContext.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const FMassRepresentationFragment& RepresentationFragment = RepresentationFragments[EntityIt];
			const FMassActorFragment& ActorFragment = ActorFragments[EntityIt];
			
			const AActor* Actor = ActorFragment.Get();
			if (Actor != nullptr && RepresentationFragment.CurrentRepresentation == EMassRepresentationType::HighResSpawnedActor)
			{
				FMassTrafficVehicleLightsFragment& VehicleLightsFragment = VehicleLightsFragments[EntityIt];
				const FMassZoneGraphLaneLocationFragment& ZoneGraphLaneLocationFragment = ZoneGraphLaneLocationFragments[EntityIt];
				const FMassTrafficLaneOffsetFragment& LaneOffsetFragment = LaneOffsetFragments[EntityIt];
				FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment = LaneChangeFragments[EntityIt];
				FMassTrafficInterpolationFragment& VehicleMovementInterpolationFragment = VehicleMovementInterpolationFragments[EntityIt];
				FMassTrafficNextVehicleFragment& NextVehicleFragment = NextVehicleFragments[EntityIt];

				const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(ZoneGraphLaneLocationFragment.LaneHandle.DataHandle);
				check(ZoneGraphStorage);

				// Get simulated location 
				const FVector ActorLocation = Actor->GetActorLocation();

				// Get pure lane location
				FTransform LaneLocationTransform;
				UE::MassTraffic::InterpolatePositionAndOrientationAlongLane(*ZoneGraphStorage, ZoneGraphLaneLocationFragment.LaneHandle.Index, ZoneGraphLaneLocationFragment.DistanceAlongLane, ETrafficVehicleMovementInterpolationMethod::Linear, VehicleMovementInterpolationFragment.LaneLocationLaneSegment, LaneLocationTransform);
				
				// Apply lateral offset
				LaneLocationTransform.AddToTranslation(LaneLocationTransform.GetRotation().GetRightVector() * LaneOffsetFragment.LateralOffset);
				
				// Adjust lane location for lane changing
				UE::MassTraffic::AdjustVehicleTransformDuringLaneChange(LaneChangeFragment, ZoneGraphLaneLocationFragment.DistanceAlongLane, LaneLocationTransform);

				// Has the entity transform and actual simulated actor transform deviated significantly
				const float Deviation = FVector::Distance(LaneLocationTransform.GetLocation(), ActorLocation);
				const float VehicleDeviationTolerance = MassTrafficSettings->VehicleDeviationTolerance *
					(LaneChangeFragment.IsLaneChangeInProgress() ? 1.25f : 1.0f); // ..give a little more tolerance for lane changes (See all LANECHANGEPHYSICS1.)
				if (Deviation > VehicleDeviationTolerance)
				{
					// IMPORTANT!
					// Make sure we reset the lane change fragment, so it -
					//		(1) Stops changing the transform of the vehicle.
					//		(2) Removes any of it's own next-vehicle fragments it might have put on entities.
					LaneChangeFragment.EndLaneChangeProgression(VehicleLightsFragment, NextVehicleFragment, EntityManager);

					// This vehicle is deviant, add an FTagFragment_MassTrafficObstacle tag to it so it's
					// considered for obstacle avoidance.
					const FMassEntityHandle Entity = QueryContext.GetEntity(EntityIt);
					QueryContext.Defer().AddTag<FMassTrafficObstacleTag>(Entity);
					QueryContext.Defer().AddFragment<FMassLookAtTargetFragment>(Entity);

					QueryContext.Defer().PushCommand<FMassCommandAddFragments<
						FMassNavigationObstacleGridCellLocationFragment		// Needed to become an avoidance obstacle
						, FMassCrowdObstacleFragment>>						// Needed to be a zone graph dynamic obstacle
						(Entity);

					FMassPillCollider Pill(SimulationParams.HalfWidth, SimulationParams.HalfLength);
					FMassAvoidanceColliderFragment ColliderFragment(Pill);
					QueryContext.Defer().PushCommand<FMassCommandAddFragmentInstances>(Entity, ColliderFragment);

					// Debug
					UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Deviants"), Log, ActorLocation, 10.0f, FColor::Red, TEXT("%d Deviated by %f"), QueryContext.GetEntity(EntityIt).Index, Deviation);
					UE_VLOG_SEGMENT_THICK(LogOwner, TEXT("MassTraffic Deviants"), Log, ActorLocation, LaneLocationTransform.GetLocation(), FColor::Red, 3.0f, TEXT(""));
				}
			}
		}
	});

	// Check known deviant vehicles to see if they're still deviant
	DeviantTrafficVehicleEntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& QueryContext)
	{
		UMassNavigationSubsystem& NavigationSubsystem = QueryContext.GetMutableSubsystemChecked<UMassNavigationSubsystem>();
		const UZoneGraphSubsystem& ZoneGraphSubsystem = QueryContext.GetSubsystemChecked<UZoneGraphSubsystem>();

		const TConstArrayView<FMassZoneGraphLaneLocationFragment> ZoneGraphLaneLocationFragments = QueryContext.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TConstArrayView<FMassTrafficLaneOffsetFragment> LaneOffsetFragments = QueryContext.GetFragmentView<FMassTrafficLaneOffsetFragment>();
		const TConstArrayView<FMassRepresentationFragment> RepresentationFragments = QueryContext.GetFragmentView<FMassRepresentationFragment>();
		const TConstArrayView<FMassActorFragment> ActorFragments = QueryContext.GetFragmentView<FMassActorFragment>();
		const TConstArrayView<FMassTrafficVehicleLaneChangeFragment> LaneChangeFragments = QueryContext.GetFragmentView<FMassTrafficVehicleLaneChangeFragment>();
		const TArrayView<FMassTrafficInterpolationFragment> VehicleMovementInterpolationFragments = QueryContext.GetMutableFragmentView<FMassTrafficInterpolationFragment>();
				
		// Loop obstacles
		for (FMassExecutionContext::FEntityIterator EntityIt = QueryContext.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const FMassRepresentationFragment& RepresentationFragment = RepresentationFragments[EntityIt];
			const FMassActorFragment& ActorFragment = ActorFragments[EntityIt];

			bool bDeviant = false;
			
			const AActor* Actor = ActorFragment.Get();
			if (Actor != nullptr && RepresentationFragment.CurrentRepresentation == EMassRepresentationType::HighResSpawnedActor)
			{
				const FMassZoneGraphLaneLocationFragment& ZoneGraphLaneLocationFragment = ZoneGraphLaneLocationFragments[EntityIt];
				const FMassTrafficLaneOffsetFragment& LaneOffsetFragment = LaneOffsetFragments[EntityIt];
				const FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment = LaneChangeFragments[EntityIt];
				FMassTrafficInterpolationFragment& VehicleMovementInterpolationFragment = VehicleMovementInterpolationFragments[EntityIt];

				const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(ZoneGraphLaneLocationFragment.LaneHandle.DataHandle);
				check(ZoneGraphStorage);
				
				// Get simulated location 
				const FVector ActorLocation = Actor->GetActorLocation();

				// Get pure lane location
				FTransform LaneLocationTransform;
				UE::MassTraffic::InterpolatePositionAndOrientationAlongLane(*ZoneGraphStorage, ZoneGraphLaneLocationFragment.LaneHandle.Index, ZoneGraphLaneLocationFragment.DistanceAlongLane, ETrafficVehicleMovementInterpolationMethod::Linear, VehicleMovementInterpolationFragment.LaneLocationLaneSegment, LaneLocationTransform);
				
				// Apply lateral offset
				LaneLocationTransform.AddToTranslation(LaneLocationTransform.GetRotation().GetRightVector() * LaneOffsetFragment.LateralOffset);

				// Adjust lane location for lane changing
				UE::MassTraffic::AdjustVehicleTransformDuringLaneChange(LaneChangeFragment, ZoneGraphLaneLocationFragment.DistanceAlongLane, LaneLocationTransform);
				
				// Has the entity transform and actual simulated actor transform deviated significantly
				const float Deviation = FVector::Distance(LaneLocationTransform.GetLocation(), ActorLocation);
				if (Deviation > MassTrafficSettings->VehicleDeviationTolerance)
				{
					bDeviant = true;
				}
				else
				{
					// Debug
					UE_VLOG_LOCATION(LogOwner, TEXT("MassTraffic Deviants"), Log, ActorLocation, 10.0f, FColor::Green, TEXT("%d Corrected"), QueryContext.GetEntity(EntityIt).Index);
				}
			}
			else
			{
				// Debug
				UE_VLOG(LogOwner, TEXT("MassTraffic Deviants"), Log, TEXT("%d Corrected"), QueryContext.GetEntity(EntityIt).Index);
			}

			if (!bDeviant)
			{
				RemoveDeviantFragments(EntityManager, QueryContext, NavigationSubsystem, EntityIt);
			}
		}
	});

	// Remove obstacle fragment from implicitly corrected vehicles
	CorrectedTrafficVehicleEntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& QueryContext)
	{
		UMassNavigationSubsystem& NavigationSubsystem = QueryContext.GetMutableSubsystemChecked<UMassNavigationSubsystem>();

		for (FMassExecutionContext::FEntityIterator EntityIt = QueryContext.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			RemoveDeviantFragments(EntityManager, QueryContext, NavigationSubsystem, EntityIt);
		}
	});
}
