// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficInitParkedVehiclesProcessor.h"
#include "MassTraffic.h"
#include "MassTrafficFragments.h"
#include "MassReplicationSubsystem.h"
#include "MassExecutionContext.h"


UMassTrafficInitParkedVehiclesProcessor::UMassTrafficInitParkedVehiclesProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = false;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UMassTrafficInitParkedVehiclesProcessor::ConfigureQueries()
#else
void UMassTrafficInitParkedVehiclesProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
#endif
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
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& Context)
#else
	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Context)
#endif
	{
		const int32 NumEntities = Context.GetNumEntities();
		TArrayView<FTransformFragment> TransformFragments = Context.GetMutableFragmentView<FTransformFragment>();
		TArrayView<FMassRepresentationFragment> VisualizationFragments = Context.GetMutableFragmentView<FMassRepresentationFragment>();
		TArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = Context.GetMutableFragmentView<FMassTrafficRandomFractionFragment>();

		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			check(VehiclesSpawnData.Transforms.IsValidIndex(VehicleIndex));
			
			// Init transform
			TransformFragments[Index].GetMutableTransform() = VehiclesSpawnData.Transforms[VehicleIndex];

			// Init PrevTransform here too as we expect it to stay static, so we set it here initally once and don't
			// need to update it after that  
			VisualizationFragments[Index].PrevTransform = VehiclesSpawnData.Transforms[VehicleIndex];

			// Init random fraction
			RandomFractionFragments[Index].RandomFraction = RandomStream.GetFraction();

			// Advance through spawn data
			++VehicleIndex;
		}
	});
}
