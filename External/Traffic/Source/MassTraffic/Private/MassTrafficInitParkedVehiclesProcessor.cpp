// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficInitParkedVehiclesProcessor.h"
#include "MassCommonFragments.h"
#include "MassTrafficFragments.h"
#include "MassReplicationSubsystem.h"
#include "MassExecutionContext.h"
#include "MassRepresentationFragments.h"


UMassTrafficInitParkedVehiclesProcessor::UMassTrafficInitParkedVehiclesProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = false;
}

void UMassTrafficInitParkedVehiclesProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficInitParkedVehiclesProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Cast AuxData to required FMassTrafficParkedVehiclesSpawnData
	const FInstancedStruct& AuxInput = Context.GetAuxData();
	if (!ensure(AuxInput.GetPtr<FMassTrafficParkedVehiclesSpawnData>()))
	{
		return;
	}
	const FMassTrafficParkedVehiclesSpawnData& VehiclesSpawnData = AuxInput.Get<FMassTrafficParkedVehiclesSpawnData>();

	// Reset random stream used to seed FDataFragment_RandomFraction::RandomFraction
	RandomStream.Reset();

	// Init dynamic vehicle data 
	int32 VehicleIndex = 0;
	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Context)
	{
		TArrayView<FTransformFragment> TransformFragments = Context.GetMutableFragmentView<FTransformFragment>();
		TArrayView<FMassRepresentationFragment> VisualizationFragments = Context.GetMutableFragmentView<FMassRepresentationFragment>();
		TArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = Context.GetMutableFragmentView<FMassTrafficRandomFractionFragment>();

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			check(VehiclesSpawnData.Transforms.IsValidIndex(VehicleIndex));
			
			// Init transform
			TransformFragments[EntityIt].GetMutableTransform() = VehiclesSpawnData.Transforms[VehicleIndex];

			// Init PrevTransform here too as we expect it to stay static, so we set it here initally once and don't
			// need to update it after that  
			VisualizationFragments[EntityIt].PrevTransform = VehiclesSpawnData.Transforms[VehicleIndex];

			// Init random fraction
			RandomFractionFragments[EntityIt].RandomFraction = RandomStream.GetFraction();

			// Advance through spawn data
			++VehicleIndex;
		}
	});
}
