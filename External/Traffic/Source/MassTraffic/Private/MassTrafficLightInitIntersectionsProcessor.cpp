// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficLightInitIntersectionsProcessor.h"
#include "MassTrafficFragments.h"
#include "MassTrafficDelegates.h"
#include "MassTrafficFieldOperations.h"
#include "MassCrowdSubsystem.h"
#include "MassExecutionContext.h"


UMassTrafficLightInitIntersectionsProcessor::UMassTrafficLightInitIntersectionsProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = false;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UMassTrafficLightInitIntersectionsProcessor::ConfigureQueries()
#else
void UMassTrafficLightInitIntersectionsProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
#endif
{
	EntityQuery.AddRequirement<FMassTrafficLightIntersectionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSubsystemRequirement<UMassCrowdSubsystem>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);

	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficLightInitIntersectionsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Cast AuxData to required FMassTrafficLightIntersectionSpawnData
	FInstancedStruct& AuxInput = Context.GetMutableAuxData();
	if (!ensure(AuxInput.GetPtr<FMassTrafficLightIntersectionSpawnData>()))
	{
		return;
	}
	FMassTrafficLightIntersectionSpawnData& TrafficLightIntersectionsSpawnData = AuxInput.GetMutable<FMassTrafficLightIntersectionSpawnData>();
	
	// Process chunks
	int32 Offset = 0;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& QueryContext)
#else
	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& QueryContext)
#endif
	{
		// Get Mass crowd subsystem.
		UMassCrowdSubsystem* MassCrowdSubsystem = QueryContext.GetMutableSubsystem<UMassCrowdSubsystem>();
		UMassTrafficSubsystem* MassTrafficSubsystem = QueryContext.GetMutableSubsystem<UMassTrafficSubsystem>();

		const int32 NumEntities = QueryContext.GetNumEntities();
		const TArrayView<FMassTrafficLightIntersectionFragment> TrafficLightIntersectionFragments = QueryContext.GetMutableFragmentView<FMassTrafficLightIntersectionFragment>();
		const TArrayView<FTransformFragment> TransformFragments = QueryContext.GetMutableFragmentView<FTransformFragment>();

		// Swap in pre-initialized fragments
		// Note: We do a Memswap here instead of a Memcpy as Memcpy would copy the internal TArray 
		// data pointers from the AuxInput TArrays which will be freed at the end of spawn
		FMemory::Memswap(TrafficLightIntersectionFragments.GetData(), &TrafficLightIntersectionsSpawnData.TrafficLightIntersectionFragments[Offset], sizeof(FMassTrafficLightIntersectionFragment) * NumEntities);

		// Swap in transforms
		check(sizeof(FTransformFragment) == sizeof(FTransform));
		FMemory::Memswap(TransformFragments.GetData(), &TrafficLightIntersectionsSpawnData.TrafficLightIntersectionTransforms[Offset], sizeof(FTransformFragment) * NumEntities);

		// Init intersection lane states -
		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassEntityHandle IntersectionEntityHandle = QueryContext.GetEntity(EntityIndex);
			
			FMassTrafficLightIntersectionFragment& TrafficIntersectionFragment = TrafficLightIntersectionFragments[EntityIndex];

			// Tell each of the traffic light controlled lanes which FMassTrafficLightIntersectionFragment is associated with it.
			for (FMassTrafficPeriod& Period : TrafficIntersectionFragment.Periods)
			{
				for (FZoneGraphTrafficLaneData* TrafficLaneData : Period.VehicleLanes)
				{
					TrafficLaneData->IntersectionEntityHandle = IntersectionEntityHandle;
				}
			}

			// Close all vehicle and pedestrian lanes, and stop all traffic lights, controlled by this intersection.
			// The 'update intersection processor' will take it from here.
			TrafficIntersectionFragment.RestartIntersection(MassCrowdSubsystem);

			// TODO:  In the future, UMassTrafficSubsystem::RegisterTrafficIntersectionEntity and related code should be refactored.
			// UMassTrafficIntersectionSpawnDataGenerator generates intersection spawn data for intersections in all ZoneGraphStorages,
			// but UMassTrafficSubsystem::RegisterTrafficIntersectionEntity takes a ZoneIndex,
			// which is only unique within a single ZoneGraphStorage.
			//
			// This issue is pre-existing and currently outside the scope of this "traffic light / traffic sign intersection" refactor.
			// It also doesn't look like its used outside of the "Mass Traffic Field Operation" code.

			// Cache intersection entities in the traffic coordinator
			MassTrafficSubsystem->RegisterTrafficIntersectionEntity(TrafficIntersectionFragment.ZoneIndex, QueryContext.GetEntity(EntityIndex));
		}

		Offset += NumEntities;
	});

	UMassTrafficSubsystem& MassTrafficSubsystem = Context.GetMutableSubsystemChecked<UMassTrafficSubsystem>();
	// Broadcast intersections initialized
	UE::MassTrafficDelegates::OnPostInitTrafficIntersections.Broadcast(&MassTrafficSubsystem);
	MassTrafficSubsystem.PerformFieldOperation(UMassTrafficRetimeIntersectionPeriodsFieldOperation::StaticClass());
}
