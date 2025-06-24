// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficLaneMetadataProcessor.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
#include "MassGameplayExternalTraits.h"
#endif
#include "MassTrafficMovement.h"
#include "MassTrafficSubsystem.h"
#include "MassZoneGraphNavigationFragments.h"

//----------------------------------------------------------------------//
// UMassTrafficLaneMetadataProcessor
//----------------------------------------------------------------------//

// Important:  UMassTrafficLaneMetadataProcessor is meant to run before
// UMassTrafficVehicleControlProcessor and UMassTrafficCrowdYieldProcessor.
// So, this must be setup in DefaultMass.ini.
UMassTrafficLaneMetadataProcessor::UMassTrafficLaneMetadataProcessor()
	: EntityQuery(*this)
{
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleLaneMetadata;
	
	ExecutionOrder.ExecuteBefore.Add(UE::MassTraffic::ProcessorGroupNames::VehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::FrameStart);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::PreVehicleBehavior);
	ExecutionOrder.ExecuteAfter.Add(UE::MassTraffic::ProcessorGroupNames::VehicleSimulationLOD);
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UMassTrafficLaneMetadataProcessor::ConfigureQueries()
#else
void UMassTrafficLaneMetadataProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
#endif
{
	EntityQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassTrafficVehicleControlFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadOnly);
	
	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficLaneMetadataProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UMassTrafficSubsystem& MassTrafficSubsystem = Context.GetMutableSubsystemChecked<UMassTrafficSubsystem>();

	// First, we need to clear all core vehicle info.
	MassTrafficSubsystem.ClearCoreVehicleInfos();

	// Then, associate the core vehicle info of each vehicle to its lane for later lookup in the merge yield logic.
	// This will allow the merge yield logic to "see" every vehicle on the conflict lanes for a complete evaluation.
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](const FMassExecutionContext& QueryContext)
#else
	EntityQuery.ForEachEntityChunk(Context, [&](const FMassExecutionContext& QueryContext)
#endif
	{
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationFragments = QueryContext.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TConstArrayView<FMassTrafficVehicleControlFragment> VehicleControlFragments = QueryContext.GetFragmentView<FMassTrafficVehicleControlFragment>();
		const TConstArrayView<FAgentRadiusFragment> RadiusFragments = QueryContext.GetFragmentView<FAgentRadiusFragment>();
		const TConstArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = QueryContext.GetFragmentView<FMassTrafficRandomFractionFragment>();
		
		const int32 NumEntities = QueryContext.GetNumEntities();
		
		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassZoneGraphLaneLocationFragment& LaneLocationFragment = LaneLocationFragments[EntityIndex];
			const FMassTrafficVehicleControlFragment& VehicleControlFragment = VehicleControlFragments[EntityIndex];
			const FAgentRadiusFragment& RadiusFragment = RadiusFragments[EntityIndex];
			const FMassTrafficRandomFractionFragment& RandomFractionFragment = RandomFractionFragments[EntityIndex];

			if (LaneLocationFragment.LaneLength <= 0.0f)
			{
				continue;
			}

			FMassTrafficCoreVehicleInfo CoreVehicleInfo;

			CoreVehicleInfo.VehicleEntityHandle = VehicleControlFragment.VehicleEntityHandle;
			CoreVehicleInfo.VehicleDistanceAlongLane = LaneLocationFragment.DistanceAlongLane;
			CoreVehicleInfo.VehicleAccelerationEstimate = VehicleControlFragment.AccelerationEstimate;
			CoreVehicleInfo.VehicleSpeed = VehicleControlFragment.IsYieldingAtIntersection() ? 0.0f : VehicleControlFragment.Speed;
			CoreVehicleInfo.bVehicleIsYielding = VehicleControlFragment.IsYieldingAtIntersection();
			CoreVehicleInfo.VehicleRadius = RadiusFragment.Radius;
			CoreVehicleInfo.VehicleNextLane = VehicleControlFragment.NextLane;
			CoreVehicleInfo.VehicleStopSignIntersectionLane = VehicleControlFragment.StopSignIntersectionLane;
			CoreVehicleInfo.VehicleRandomFraction = RandomFractionFragment.RandomFraction.GetFloat();

			CoreVehicleInfo.VehicleIsNearStopLineAtIntersection = UE::MassTraffic::IsVehicleNearStopLine(
					LaneLocationFragment.DistanceAlongLane,
					LaneLocationFragment.LaneLength,
					RadiusFragment.Radius,
					RandomFractionFragment.RandomFraction,
					MassTrafficSettings->StoppingDistanceRange);

			if (VehicleControlFragment.IsVehicleCurrentlyStopped())
			{
				const UWorld* World = MassTrafficSubsystem.GetWorld();
					
				const float CurrentTimeSeconds = World != nullptr ? World->GetTimeSeconds() : TNumericLimits<float>::Lowest();
				const float VehicleStoppedElapsedTime = CurrentTimeSeconds - VehicleControlFragment.TimeVehicleStopped;
					
				CoreVehicleInfo.VehicleRemainingStopSignRestTime = VehicleControlFragment.MinVehicleStopSignRestTime - VehicleStoppedElapsedTime;
			}

			MassTrafficSubsystem.AddCoreVehicleInfo(LaneLocationFragment.LaneHandle, CoreVehicleInfo);
		}
	});

	TMap<FZoneGraphLaneHandle, TSet<FMassTrafficCoreVehicleInfo>>& CoreVehicleInfoMap = MassTrafficSubsystem.GetMutableCoreVehicleInfoMap();

	// Finally, sort the CoreVehicleInfos for each lane in order of increasing distance along the lane.
	for (TTuple<FZoneGraphLaneHandle, TSet<FMassTrafficCoreVehicleInfo>>& CoreVehicleInfoPair : CoreVehicleInfoMap)
	{
		TSet<FMassTrafficCoreVehicleInfo>& CoreVehicleInfos = CoreVehicleInfoPair.Value;

		CoreVehicleInfos.Sort([](const FMassTrafficCoreVehicleInfo& FirstCoreVehicleInfo, const FMassTrafficCoreVehicleInfo& SecondCoreVehicleInfo)
		{
			return FirstCoreVehicleInfo.VehicleDistanceAlongLane < SecondCoreVehicleInfo.VehicleDistanceAlongLane;
		});
	}
}
