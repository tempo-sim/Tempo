// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficPlayerVehicleLODProcessor.h"
#include "MassLODCollectorProcessor.h"
#include "MassTrafficFragments.h"
#include "MassExecutionContext.h"
#include "MassLODFragments.h"

UMassTrafficPlayerVehicleLODProcessor::UMassTrafficPlayerVehicleLODProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = UE::MassTraffic::ProcessorGroupNames::VehicleLODCollector;
	ExecutionOrder.ExecuteBefore.Add(UE::MassTraffic::ProcessorGroupNames::VehicleSimulationLOD);
}

void UMassTrafficPlayerVehicleLODProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddTagRequirement<FMassTrafficPlayerVehicleTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassViewerInfoFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficPlayerVehicleLODProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
		const float LODPlayerVehicleDistanceScaleSq = FMath::Square(GMassTrafficLODPlayerVehicleDistanceScale);
		TArrayView<FMassViewerInfoFragment> LODInfoFragments = Context.GetMutableFragmentView<FMassViewerInfoFragment>();

		for (FMassViewerInfoFragment& LODInfoFragment : LODInfoFragments)
		{
			const float DistanceToViewerSq = LODInfoFragment.ClosestViewerDistanceSq * LODPlayerVehicleDistanceScaleSq;
			LODInfoFragment.ClosestViewerDistanceSq = FMath::Max(DistanceToViewerSq, 0.0f);
			LODInfoFragment.ClosestDistanceToFrustum = 0.0f;
		}
	});
}
