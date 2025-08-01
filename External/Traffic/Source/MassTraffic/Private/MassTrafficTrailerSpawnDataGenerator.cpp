// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficTrailerSpawnDataGenerator.h"
#include "MassTrafficConstrainedTrailerTrait.h"
#include "MassTrafficFragments.h"
#include "MassTrafficInitTrailersProcessor.h"
#include "MassExecutionContext.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityManager.h"
#include "MassCommonFragments.h"
#include "MassEntityUtils.h"


void UMassTrafficTrailerSpawnDataGenerator::Generate(UObject& QueryOwner,
	TConstArrayView<FMassSpawnedEntityType> EntityTypes, int32 Count,
	FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("MassTrafficTrailerSpawnDataGenerator"))

	// Get subsystems
	UWorld* World = GetWorld();
	check(World);
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(*World);

	// Configure Query
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	FMassEntityQuery VehicleQuery;
#else
	TSharedRef<FMassEntityManager> EntityManagerRef(&EntityManager);
	FMassEntityQuery VehicleQuery(EntityManagerRef);
#endif
	VehicleQuery.AddTagRequirement<FMassTrafficVehicleTag>(EMassFragmentPresence::All);
	VehicleQuery.AddConstSharedRequirement<FMassTrafficConstrainedTrailerParameters>(EMassFragmentPresence::All);
	VehicleQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::All); // Need at least 1 fragment access for valid query

	// Prepare spawn data
	TArray<FMassEntitySpawnDataGeneratorResult> Results;
	BuildResultsFromEntityTypes(Count, EntityTypes, Results);

	// Find vehicle to spawn trailers for
	FMassExecutionContext ExecutionContext(EntityManager, 0.0f);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	VehicleQuery.ForEachEntityChunk(EntityManager, ExecutionContext, [&Results, &EntityTypes](FMassExecutionContext& QueryContext)
#else
	VehicleQuery.ForEachEntityChunk(ExecutionContext, [&Results, &EntityTypes](FMassExecutionContext& QueryContext)
#endif
	{
		const FMassTrafficConstrainedTrailerParameters& TrailerSimulationParams = QueryContext.GetConstSharedFragment<FMassTrafficConstrainedTrailerParameters>();

		// Find matching trailer to spawn for these vehicles
		for (FMassEntitySpawnDataGeneratorResult& Result : Results)
		{
			if (EntityTypes[Result.EntityConfigIndex].EntityConfig.Get() == TrailerSimulationParams.TrailerAgentConfigAsset.Get())
			{
				// Initialize spawn data for trailer
				FMassTrafficVehicleTrailersSpawnData* TrailersSpawnData = Result.SpawnData.GetMutablePtr<FMassTrafficVehicleTrailersSpawnData>();
				if (!TrailersSpawnData)
				{
					Result.SpawnData.InitializeAs<FMassTrafficVehicleTrailersSpawnData>();
					Result.SpawnDataProcessor = UMassTrafficInitTrailersProcessor::StaticClass();
					TrailersSpawnData = Result.SpawnData.GetMutablePtr<FMassTrafficVehicleTrailersSpawnData>();
				}

				// Add vehicles to attach spawned trailers to
				check(TrailersSpawnData);
				const TConstArrayView<FMassEntityHandle> VehicleEntities = QueryContext.GetEntities();
				TrailersSpawnData->TrailerVehicles.Append(VehicleEntities.GetData(), VehicleEntities.Num());
			}
		}
	});

	// Set final spawn counts to match vehicles
	for (FMassEntitySpawnDataGeneratorResult& Result : Results)
	{
		if (Result.SpawnData.IsValid())
		{
			Result.NumEntities = Result.SpawnData.GetMutable<FMassTrafficVehicleTrailersSpawnData>().TrailerVehicles.Num();
		}
		else
		{
			UE_LOG(LogMassTraffic, Warning, TEXT("No vehicles with FMassTrafficConstrainedTrailerParameters.TrailerAgentConfigAsset = %s to spawn this type of trailer on. No trailers of this type will be spawned."), *EntityTypes[Result.EntityConfigIndex].EntityConfig.ToString());
			Result.NumEntities = 0;
		}
	}
	
	// Return results
	FinishedGeneratingSpawnPointsDelegate.Execute(Results);
}
