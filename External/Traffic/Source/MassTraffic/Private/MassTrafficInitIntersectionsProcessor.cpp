// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficInitIntersectionsProcessor.h"
#include "MassTrafficFragments.h"
#include "MassTrafficDelegates.h"
#include "MassTrafficFieldOperations.h"
#include "MassCrowdSubsystem.h"
#include "MassExecutionContext.h"


UMassTrafficInitIntersectionsProcessor::UMassTrafficInitIntersectionsProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = false;
}

void UMassTrafficInitIntersectionsProcessor::ConfigureQueries() 
{
	EntityQuery.AddRequirement<FMassTrafficIntersectionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSubsystemRequirement<UMassCrowdSubsystem>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);

	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficInitIntersectionsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Cast AuxData to required FMassTrafficIntersectionsSpawnData
	FInstancedStruct& AuxInput = Context.GetMutableAuxData();
	if (!ensure(AuxInput.GetPtr<FMassTrafficIntersectionsSpawnData>()))
	{
		return;
	}
	FMassTrafficIntersectionsSpawnData& IntersectionsSpawnData = AuxInput.GetMutable<FMassTrafficIntersectionsSpawnData>();
	
	// Process chunks
	int32 Offset = 0;
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& QueryContext)
	{
		// Get Mass crowd subsystem.
		UMassCrowdSubsystem* MassCrowdSubsystem = QueryContext.GetMutableSubsystem<UMassCrowdSubsystem>();
		UMassTrafficSubsystem* MassTrafficSubsystem = QueryContext.GetMutableSubsystem<UMassTrafficSubsystem>();

		const int32 NumEntities = QueryContext.GetNumEntities();
		const TArrayView<FMassTrafficIntersectionFragment> TrafficIntersectionFragments = QueryContext.GetMutableFragmentView<FMassTrafficIntersectionFragment>();
		const TArrayView<FTransformFragment> TransformFragments = QueryContext.GetMutableFragmentView<FTransformFragment>();

		// Swap in pre-initialized fragments
		// Note: We do a Memswap here instead of a Memcpy as Memcpy would copy the internal TArray 
		// data pointers from the AuxInput TArrays which will be freed at the end of spawn
		FMemory::Memswap(TrafficIntersectionFragments.GetData(), &IntersectionsSpawnData.IntersectionFragments[Offset], sizeof(FMassTrafficIntersectionFragment) * NumEntities);

		// Swap in transforms
		check(sizeof(FTransformFragment) == sizeof(FTransform));
		FMemory::Memswap(TransformFragments.GetData(), &IntersectionsSpawnData.IntersectionTransforms[Offset], sizeof(FTransformFragment) * NumEntities);

		// Init intersection lane states -
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			FMassTrafficIntersectionFragment& TrafficIntersectionFragment = TrafficIntersectionFragments[Index];

			// Close all vehicle and pedestrian lanes, and stop all traffic lights, controlled by this intersection.
			// The 'update intersection processor' will take it from here.
			TrafficIntersectionFragment.RestartIntersection(MassCrowdSubsystem);

			// Cache intersection entities in the traffic coordinator
			MassTrafficSubsystem->RegisterTrafficIntersectionEntity(TrafficIntersectionFragment.ZoneIndex, QueryContext.GetEntity(Index));
		}

		Offset += NumEntities;
	});

	UMassTrafficSubsystem& MassTrafficSubsystem = Context.GetMutableSubsystemChecked<UMassTrafficSubsystem>();
	// Broadcast intersections initialized
	UE::MassTrafficDelegates::OnPostInitTrafficIntersections.Broadcast(&MassTrafficSubsystem);
	MassTrafficSubsystem.PerformFieldOperation(UMassTrafficRetimeIntersectionPeriodsFieldOperation::StaticClass());
}
