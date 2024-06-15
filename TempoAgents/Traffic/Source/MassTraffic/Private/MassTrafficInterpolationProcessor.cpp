// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficInterpolationProcessor.h"
#include "MassTrafficChooseNextLaneProcessor.h"
#include "MassTrafficDebugHelpers.h"
#include "MassTrafficFragments.h"
#include "MassTrafficInterpolation.h"
#include "MassTrafficLaneChange.h"
#include "MassTrafficLaneChangingProcessor.h"
#include "MassTrafficVehicleSimulationTrait.h"
#include "MassExecutionContext.h"
#include "MassLODUtils.h"
#include "MassZoneGraphNavigationFragments.h"
#include "ZoneGraphSubsystem.h"
#include "MassGameplayExternalTraits.h"


UMassTrafficInterpolationProcessor::UMassTrafficInterpolationProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, EntityQueryNonOffLOD_Conditional(*this)
	, EntityQueryOffLOD_Conditional(*this)
{
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleBehavior;
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PreVehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleSimulationLOD);
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficChooseNextLaneProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteAfter.Add(UMassTrafficLaneChangingProcessor::StaticClass()->GetFName());
}

void UMassTrafficInterpolationProcessor::ConfigureQueries()
{
	// the following are the common requirements for both both queries
	EntityQueryNonOffLOD_Conditional.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::Any);
	EntityQueryNonOffLOD_Conditional.AddRequirement<FMassTrafficPIDVehicleControlFragment>(EMassFragmentAccess::None, EMassFragmentPresence::None);
	EntityQueryNonOffLOD_Conditional.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQueryNonOffLOD_Conditional.AddRequirement<FMassTrafficLaneOffsetFragment>(EMassFragmentAccess::ReadOnly);
	EntityQueryNonOffLOD_Conditional.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadOnly);
	EntityQueryNonOffLOD_Conditional.AddRequirement<FMassTrafficInterpolationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQueryNonOffLOD_Conditional.AddRequirement<FMassTrafficVehicleLaneChangeFragment>(EMassFragmentAccess::ReadOnly);
	EntityQueryNonOffLOD_Conditional.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQueryNonOffLOD_Conditional.AddRequirement<FMassTrafficDebugFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	
	// Variable tick rate, not that this condition will be copied to EntityQueryOffLOD_Conditional in the following lines
	EntityQueryNonOffLOD_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
	EntityQueryNonOffLOD_Conditional.SetChunkFilter(FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);

	EntityQueryNonOffLOD_Conditional.AddSubsystemRequirement<UZoneGraphSubsystem>(EMassFragmentAccess::ReadOnly);

	EntityQueryOffLOD_Conditional = EntityQueryNonOffLOD_Conditional;

	// non-off-LOD requirements
	EntityQueryNonOffLOD_Conditional.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::None);
	EntityQueryNonOffLOD_Conditional.AddConstSharedRequirement<FMassTrafficVehicleSimulationParameters>();

	// off-LOD requirements
	EntityQueryOffLOD_Conditional.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::All);
}

void UMassTrafficInterpolationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQueryNonOffLOD_Conditional.ForEachEntityChunk(EntityManager, Context, [&, World = EntityManager.GetWorld()](FMassExecutionContext& QueryContext)
	{
		const UZoneGraphSubsystem& ZoneGraphSubsystem = QueryContext.GetSubsystemChecked<UZoneGraphSubsystem>();

		// Get fragment lists
		const int32 NumEntities = Context.GetNumEntities();
		const FMassTrafficVehicleSimulationParameters& SimulationParams = QueryContext.GetConstSharedFragment<FMassTrafficVehicleSimulationParameters>();
		const TConstArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = QueryContext.GetFragmentView<FMassTrafficVehicleControlFragment>();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = QueryContext.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TConstArrayView<FMassTrafficLaneOffsetFragment> LaneOffsetFragments = QueryContext.GetFragmentView<FMassTrafficLaneOffsetFragment>();
		const TConstArrayView<FMassTrafficVehicleLaneChangeFragment> LaneChangeFragments = QueryContext.GetFragmentView<FMassTrafficVehicleLaneChangeFragment>(); 
		const TConstArrayView<FMassTrafficDebugFragment> DebugFragments = QueryContext.GetFragmentView<FMassTrafficDebugFragment>();
		const TArrayView<FMassTrafficInterpolationFragment> VehicleMovementInterpolationFragments = QueryContext.GetMutableFragmentView<FMassTrafficInterpolationFragment>();
		const TArrayView<FTransformFragment> TransformFragments = QueryContext.GetMutableFragmentView<FTransformFragment>();

		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			const FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[Index];
			const FMassZoneGraphLaneLocationFragment& ZoneGraphLaneLocationFragment = LaneLocationFragments[Index];
			const FMassTrafficLaneOffsetFragment& LaneOffsetFragment = LaneOffsetFragments[Index];
			const FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment = LaneChangeFragments[Index]; 
			FMassTrafficInterpolationFragment& VehicleMovementInterpolationFragment = VehicleMovementInterpolationFragments[Index];
			FTransformFragment& TransformFragment = TransformFragments[Index];

			// Debug
			const bool bVisLog = DebugFragments.IsEmpty() ? false : DebugFragments[Index].bVisLog > 0;

			// Get FZoneGraphStorage for lanes
			check(!VehicleControlFragment.NextLane || VehicleControlFragment.NextLane->LaneHandle.DataHandle == ZoneGraphLaneLocationFragment.LaneHandle.DataHandle);
			const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(ZoneGraphLaneLocationFragment.LaneHandle.DataHandle);
			check(ZoneGraphStorage);
		
			// Interpolate rear axle position
			FTransform RearAxleTransform;
			UE::MassTraffic::InterpolatePositionAndOrientationAlongContinuousLanes(
				*ZoneGraphStorage,
				VehicleControlFragment.PreviousLaneIndex,
				VehicleControlFragment.PreviousLaneLength,
				ZoneGraphLaneLocationFragment.LaneHandle.Index,
				ZoneGraphLaneLocationFragment.LaneLength,
				VehicleControlFragment.NextLane ? VehicleControlFragment.NextLane->LaneHandle.Index : INDEX_NONE,
				ZoneGraphLaneLocationFragment.DistanceAlongLane + SimulationParams.RearAxleX,
				ETrafficVehicleMovementInterpolationMethod::CubicBezier,
				VehicleMovementInterpolationFragment.LaneLocationLaneSegment,
				RearAxleTransform);

			// Interpolate front axle position
			FTransform FrontAxleTransform;
			UE::MassTraffic::InterpolatePositionAndOrientationAlongContinuousLanes(
				*ZoneGraphStorage,
				VehicleControlFragment.PreviousLaneIndex,
				VehicleControlFragment.PreviousLaneLength,
				ZoneGraphLaneLocationFragment.LaneHandle.Index,
				ZoneGraphLaneLocationFragment.LaneLength,
				VehicleControlFragment.NextLane ? VehicleControlFragment.NextLane->LaneHandle.Index : INDEX_NONE,
				ZoneGraphLaneLocationFragment.DistanceAlongLane + SimulationParams.FrontAxleX,
				ETrafficVehicleMovementInterpolationMethod::CubicBezier,
				VehicleMovementInterpolationFragment.LaneLocationLaneSegment,
				FrontAxleTransform);

			// @todo This will thrash VehicleMovementInterpolationFragment.LaneLocationLaneSegment

			// Debug
			UE::MassTraffic::DrawDebugInterpolatedAxles(World, FrontAxleTransform.GetLocation(), RearAxleTransform.GetLocation(), bVisLog, LogOwner);

			// Find center point between
			const float AxleInterpolationAlpha = -SimulationParams.RearAxleX / (SimulationParams.FrontAxleX - SimulationParams.RearAxleX);
			const FVector InterpolatedLocation = FMath::Lerp(RearAxleTransform.GetLocation(), FrontAxleTransform.GetLocation(), AxleInterpolationAlpha);
			const FVector InterpolatedForwardDirection = FrontAxleTransform.GetLocation() - RearAxleTransform.GetLocation();
			const FVector InterpolatedUpVector = FMath::Lerp(RearAxleTransform.GetRotation().GetUpVector(), FrontAxleTransform.GetRotation().GetUpVector(), AxleInterpolationAlpha);
			TransformFragment.GetMutableTransform().SetLocation(InterpolatedLocation);
			TransformFragment.GetMutableTransform().SetRotation(FRotationMatrix::MakeFromXZ(InterpolatedForwardDirection, InterpolatedUpVector).ToQuat());
			
			// Apply lateral offset
			TransformFragment.GetMutableTransform().AddToTranslation(TransformFragment.GetTransform().GetRotation().GetRightVector() * LaneOffsetFragment.LateralOffset);

			// When lane changing, apply lateral offsets to smoothly transition into the target lane
			UE::MassTraffic::AdjustVehicleTransformDuringLaneChange(LaneChangeFragment, ZoneGraphLaneLocationFragment.DistanceAlongLane, TransformFragment.GetMutableTransform(), World, bVisLog, LogOwner);

			// Debug
			UE::MassTraffic::DrawDebugLaneSegment(World, VehicleMovementInterpolationFragment.LaneLocationLaneSegment, bVisLog, LogOwner);
		}
	});

	EntityQueryOffLOD_Conditional.ForEachEntityChunk(EntityManager, Context, [&, World = EntityManager.GetWorld()](FMassExecutionContext& QueryContext)
	{
		const UZoneGraphSubsystem& ZoneGraphSubsystem = QueryContext.GetSubsystemChecked<UZoneGraphSubsystem>();

		// Get fragment lists
		const int32 NumEntities = Context.GetNumEntities();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = QueryContext.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TConstArrayView<FMassTrafficVehicleLaneChangeFragment> LaneChangeFragments = QueryContext.GetFragmentView<FMassTrafficVehicleLaneChangeFragment>();
		const TConstArrayView<FMassTrafficDebugFragment> DebugFragments = QueryContext.GetFragmentView<FMassTrafficDebugFragment>();
		const TArrayView<FMassTrafficInterpolationFragment> VehicleMovementInterpolationFragments = QueryContext.GetMutableFragmentView<FMassTrafficInterpolationFragment>();
		const TArrayView<FTransformFragment> TransformFragments = QueryContext.GetMutableFragmentView<FTransformFragment>();

		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			const FMassZoneGraphLaneLocationFragment& ZoneGraphLaneLocationFragment = LaneLocationFragments[Index];
			FMassTrafficInterpolationFragment& VehicleMovementInterpolationFragment = VehicleMovementInterpolationFragments[Index];
			const FMassTrafficVehicleLaneChangeFragment& LaneChangeFragment = LaneChangeFragments[Index];
			FTransformFragment& TransformFragment = TransformFragments[Index];

			// Debug
			const bool bVisLog = DebugFragments.IsEmpty() ? false : (DebugFragments[Index].bVisLog > 0);

			// Get FZoneGraphStorage for lanes
			const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(ZoneGraphLaneLocationFragment.LaneHandle.DataHandle);
			check(ZoneGraphStorage);

			// Interpolate position & orientation
			UE::MassTraffic::InterpolatePositionAndOrientationAlongLane(*ZoneGraphStorage, ZoneGraphLaneLocationFragment.LaneHandle.Index
				, ZoneGraphLaneLocationFragment.DistanceAlongLane, ETrafficVehicleMovementInterpolationMethod::Linear
				, VehicleMovementInterpolationFragment.LaneLocationLaneSegment, TransformFragment.GetMutableTransform());
			
			// When lane changing, apply lateral offsets to smoothly transition into the target lane
			UE::MassTraffic::AdjustVehicleTransformDuringLaneChange(LaneChangeFragment, ZoneGraphLaneLocationFragment.DistanceAlongLane
				, TransformFragment.GetMutableTransform(), World, bVisLog, LogOwner);

			// Debug
			UE::MassTraffic::DrawDebugLaneSegment(World, VehicleMovementInterpolationFragment.LaneLocationLaneSegment, bVisLog, LogOwner);
		}
	});
}