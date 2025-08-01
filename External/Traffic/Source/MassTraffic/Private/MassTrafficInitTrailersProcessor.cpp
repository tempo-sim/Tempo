// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficInitTrailersProcessor.h"
#include "MassTrafficFragments.h"
#include "MassTrafficMovement.h"
#include "MassExecutionContext.h"
#include "MassReplicationSubsystem.h"
#include "MassRepresentationFragments.h"


using namespace UE::MassTraffic;

UMassTrafficInitTrailersProcessor::UMassTrafficInitTrailersProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = false;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UMassTrafficInitTrailersProcessor::ConfigureQueries()
#else
void UMassTrafficInitTrailersProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
#endif
{
	EntityQuery.AddRequirement<FMassTrafficConstrainedVehicleFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassTrafficRandomFractionFragment>(EMassFragmentAccess::ReadWrite);
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
void UMassTrafficInitTrailersProcessor::Initialize(UObject& InOwner)
{
	Super::Initialize(InOwner);
#else
void UMassTrafficInitTrailersProcessor::InitializeInternal(UObject& InOwner, const TSharedRef<FMassEntityManager>& MassEntityManager)
{
	Super::InitializeInternal(InOwner, MassEntityManager);
#endif

	MassRepresentationSubsystem = UWorld::GetSubsystem<UMassRepresentationSubsystem>(InOwner.GetWorld());
}

void UMassTrafficInitTrailersProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Cast AuxData to required FMassTrafficVehicleTrailersSpawnData
	const FInstancedStruct& AuxInput = Context.GetAuxData();
	if (!ensure(AuxInput.GetPtr<FMassTrafficVehicleTrailersSpawnData>()))
	{
		return;
	}
	const FMassTrafficVehicleTrailersSpawnData& TrailersSpawnData = AuxInput.Get<FMassTrafficVehicleTrailersSpawnData>();

	// Reset random stream used to seed FDataFragment_RandomFraction::RandomFraction
	RandomStream.Reset();

	// Init dynamic trailer data 
	int32 TrailerIndex = 0;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& QueryContext)
#else
	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& QueryContext)
#endif
	{
		const int32 NumEntities = QueryContext.GetNumEntities();
		const TArrayView<FMassTrafficConstrainedVehicleFragment> VehicleConstraintFragments = QueryContext.GetMutableFragmentView<FMassTrafficConstrainedVehicleFragment>();
		const TArrayView<FMassTrafficRandomFractionFragment> RandomFractionFragments = QueryContext.GetMutableFragmentView<FMassTrafficRandomFractionFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassTrafficConstrainedVehicleFragment& VehicleConstraintFragment = VehicleConstraintFragments[EntityIndex];
			FMassTrafficRandomFractionFragment& RandomFractionFragment = RandomFractionFragments[EntityIndex];

			// Init constraint to vehicle
			check(TrailersSpawnData.TrailerVehicles.IsValidIndex(TrailerIndex));
			VehicleConstraintFragment.Vehicle = TrailersSpawnData.TrailerVehicles[TrailerIndex];

			// Init vehicles trailer constraint to this trailer
			if (ensure(VehicleConstraintFragment.Vehicle.IsSet()))
			{
				FMassEntityView VehicleMassEntityView(EntityManager, VehicleConstraintFragment.Vehicle);
				VehicleMassEntityView.GetFragmentData<FMassTrafficConstrainedTrailerFragment>().Trailer = Context.GetEntity(EntityIndex);
			}

			// Init random fraction
			RandomFractionFragment.RandomFraction = RandomStream.GetFraction();
			
			// Advance through spawn data
			++TrailerIndex;
		}
	});
}
