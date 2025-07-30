// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficSignInitIntersectionsProcessor.h"
#include "MassTrafficFragments.h"
#include "MassTrafficDelegates.h"
#include "MassTrafficFieldOperations.h"
#include "MassCrowdSubsystem.h"
#include "MassExecutionContext.h"


UMassTrafficSignInitIntersectionsProcessor::UMassTrafficSignInitIntersectionsProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = false;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UMassTrafficSignInitIntersectionsProcessor::ConfigureQueries()
#else
void UMassTrafficSignInitIntersectionsProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
#endif
{
	EntityQuery.AddRequirement<FMassTrafficSignIntersectionFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSubsystemRequirement<UMassCrowdSubsystem>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);

	ProcessorRequirements.AddSubsystemRequirement<UMassTrafficSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassTrafficSignInitIntersectionsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Cast AuxData to required FMassTrafficSignIntersectionSpawnData
	FInstancedStruct& AuxInput = Context.GetMutableAuxData();
	if (!ensure(AuxInput.GetPtr<FMassTrafficSignIntersectionSpawnData>()))
	{
		return;
	}
	FMassTrafficSignIntersectionSpawnData& TrafficSignIntersectionsSpawnData = AuxInput.GetMutable<FMassTrafficSignIntersectionSpawnData>();
	
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

		if (!ensureMsgf(MassCrowdSubsystem != nullptr, TEXT("Must get valid MassCrowdSubsystem in UMassTrafficSignInitIntersectionsProcessor::Execute.")))
		{
			return;
		}
		
		UMassTrafficSubsystem* MassTrafficSubsystem = QueryContext.GetMutableSubsystem<UMassTrafficSubsystem>();

		if (!ensureMsgf(MassTrafficSubsystem != nullptr, TEXT("Must get valid MassTrafficSubsystem in UMassTrafficSignInitIntersectionsProcessor::Execute.")))
		{
			return;
		}

		const int32 NumEntities = QueryContext.GetNumEntities();
		const TArrayView<FMassTrafficSignIntersectionFragment> TrafficSignIntersectionFragments = QueryContext.GetMutableFragmentView<FMassTrafficSignIntersectionFragment>();
		const TArrayView<FTransformFragment> TransformFragments = QueryContext.GetMutableFragmentView<FTransformFragment>();

		// Swap in pre-initialized fragments
		// Note: We do a Memswap here instead of a Memcpy as Memcpy would copy the internal TArray 
		// data pointers from the AuxInput TArrays which will be freed at the end of spawn
		FMemory::Memswap(TrafficSignIntersectionFragments.GetData(), &TrafficSignIntersectionsSpawnData.TrafficSignIntersectionFragments[Offset], sizeof(FMassTrafficSignIntersectionFragment) * NumEntities);

		// Swap in transforms
		check(sizeof(FTransformFragment) == sizeof(FTransform));
		FMemory::Memswap(TransformFragments.GetData(), &TrafficSignIntersectionsSpawnData.TrafficSignIntersectionTransforms[Offset], sizeof(FTransformFragment) * NumEntities);

		// Init intersection lane states -
		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassEntityHandle IntersectionEntityHandle = QueryContext.GetEntity(EntityIndex);
			
			FMassTrafficSignIntersectionFragment& TrafficSignIntersectionFragment = TrafficSignIntersectionFragments[EntityIndex];

			// Tell each of the traffic sign controlled lanes which FMassTrafficSignIntersectionFragment is associated with it.
			for (FMassTrafficSignIntersectionSide& IntersectionSide : TrafficSignIntersectionFragment.IntersectionSides)
			{
				for (auto& VehicleIntersectionLane : IntersectionSide.VehicleIntersectionLanes)
				{
					VehicleIntersectionLane.Key->IntersectionEntityHandle = IntersectionEntityHandle;
				}
			}

			TrafficSignIntersectionFragment.RestartIntersection(*MassCrowdSubsystem);
		}

		Offset += NumEntities;
	});
}
