// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficParkedVehicleRepresentationActorManagement.h"
#include "MassTrafficVehicleVisualizationProcessor.h"
#include "MassTrafficVehicleControlInterface.h"
#include "MassTrafficFragments.h"
#include "MassEntityView.h"
#include "MassEntityManager.h"
#include "WheeledVehiclePawn.h"
#include "Components/PrimitiveComponent.h"
#include "Rendering/MotionVectorSimulation.h"
#include "ChaosVehicleMovementComponent.h"


EMassActorSpawnRequestAction  UMassTrafficParkedVehicleRepresentationActorManagement::OnPostActorSpawn(const FMassActorSpawnRequestHandle& SpawnRequestHandle
	, FConstStructView SpawnRequest, TSharedRef<FMassEntityManager> EntityManager) const
{
	const EMassActorSpawnRequestAction Result = Super::OnPostActorSpawn(SpawnRequestHandle, SpawnRequest, EntityManager);
	
	const FMassActorSpawnRequest& MassActorSpawnRequest = SpawnRequest.Get<const FMassActorSpawnRequest>();
	check(MassActorSpawnRequest.SpawnedActor);

	FMassEntityView ParkedVehicleEntityView(*EntityManager, MassActorSpawnRequest.MassAgent);
	FMassTrafficRandomFractionFragment& RandomFractionFragment = ParkedVehicleEntityView.GetFragmentData<FMassTrafficRandomFractionFragment>();  
	FMassRepresentationFragment& RepresentationFragment = ParkedVehicleEntityView.GetFragmentData<FMassRepresentationFragment>();  

	// Set primitive component custom data
	const FMassTrafficPackedVehicleInstanceCustomData PackedCustomData = FMassTrafficVehicleInstanceCustomData::MakeParkedVehicleCustomData(RandomFractionFragment);
	MassActorSpawnRequest.SpawnedActor->ForEachComponent<UPrimitiveComponent>(/*bIncludeFromChildActors*/true, [&PackedCustomData](UPrimitiveComponent* PrimitiveComponent)
	{
		PrimitiveComponent->SetCustomPrimitiveDataFloat(/*DataIndex*/1, PackedCustomData.PackedParam1);
	});
	
	if (AWheeledVehiclePawn* VehiclePawn = Cast<AWheeledVehiclePawn>(MassActorSpawnRequest.SpawnedActor))
	{
		// Turn on the handbrake for parked cars 
		UChaosVehicleMovementComponent* VehicleMovementComponent = VehiclePawn->GetVehicleMovementComponent();
		VehicleMovementComponent->SetHandbrakeInput(true);

		// Allow reversing for drivable parked cars
		VehicleMovementComponent->bReverseAsBrake = true;

		// Force parked cars to sleep until they receive player input or are hit
		VehicleMovementComponent->SetSleeping(true);

		// Make sure we don't have an AI controller
		VehiclePawn->DetachFromControllerPendingDestroy();
	}

	// Init render scene previous frame transform
	MassActorSpawnRequest.SpawnedActor->ForEachComponent<UPrimitiveComponent>(/*bIncludeFromChildActors*/true, [&MassActorSpawnRequest, &RepresentationFragment](UPrimitiveComponent* PrimitiveComponent)
	{
		const FTransform PrimitiveComponentPreviousTransform = PrimitiveComponent->GetComponentTransform().GetRelativeTransform(MassActorSpawnRequest.SpawnedActor->GetTransform()) * RepresentationFragment.PrevTransform;
		FMotionVectorSimulation::Get().SetPreviousTransform(PrimitiveComponent, PrimitiveComponentPreviousTransform);
	});
			
	// Let the BPs know we've been spawned so they can do what's needed.
	if (MassActorSpawnRequest.SpawnedActor->Implements<UMassTrafficVehicleControlInterface>())
	{
		IMassTrafficVehicleControlInterface::Execute_OnParkedVehicleSpawned(MassActorSpawnRequest.SpawnedActor);
	}

	return Result;
}
