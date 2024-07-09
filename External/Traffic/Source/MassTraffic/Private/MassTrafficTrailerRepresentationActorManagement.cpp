// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficTrailerRepresentationActorManagement.h"
#include "MassTrafficSubsystem.h"
#include "MassTrafficTrailerSimulationTrait.h"
#include "MassTrafficVehicleComponent.h"
#include "MassTrafficVehicleControlInterface.h"
#include "MassTrafficVehicleVisualizationProcessor.h"

#include "MassActorSubsystem.h"
#include "MassEntityView.h"
#include "MassEntityManager.h"
#include "MassMovementFragments.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Rendering/MotionVectorSimulation.h"
#include "WheeledVehiclePawn.h"
#include "Components/SkeletalMeshComponent.h"


EMassActorSpawnRequestAction  UMassTrafficTrailerRepresentationActorManagement::OnPostActorSpawn(const FMassActorSpawnRequestHandle& SpawnRequestHandle
	, FConstStructView SpawnRequest, TSharedRef<FMassEntityManager> EntityManager) const
{
	const EMassActorSpawnRequestAction Result = Super::OnPostActorSpawn(SpawnRequestHandle, SpawnRequest, EntityManager);

	const FMassActorSpawnRequest& MassActorSpawnRequest = SpawnRequest.Get<const FMassActorSpawnRequest>();
	check(MassActorSpawnRequest.SpawnedActor);

	FMassEntityView TrailerMassEntityView(*EntityManager, MassActorSpawnRequest.MassAgent);
	FMassTrafficRandomFractionFragment& TrailerRandomFractionFragment = TrailerMassEntityView.GetFragmentData<FMassTrafficRandomFractionFragment>();  

	// Backup custom instance data in case we don't have a truck pulling us.
	FMassTrafficPackedVehicleInstanceCustomData PackedCustomData = FMassTrafficVehicleInstanceCustomData::MakeTrafficVehicleTrailerCustomData(TrailerRandomFractionFragment);

	// High LOD trailer?
	if (AWheeledVehiclePawn* TrailerPawn = Cast<AWheeledVehiclePawn>(MassActorSpawnRequest.SpawnedActor))
	{
		// Get trailer simulation config
		const FMassTrafficTrailerSimulationParameters& TrailerSimulationConfig = TrailerMassEntityView.GetConstSharedFragmentData<FMassTrafficTrailerSimulationParameters>();
		
		UChaosWheeledVehicleMovementComponent* TrailerVehicleMovementComponent = Cast<UChaosWheeledVehicleMovementComponent>(TrailerPawn->GetVehicleMovementComponent());

		// Constrain to traffic vehicle
		FMassTrafficConstrainedVehicleFragment& VehicleConstraintFragment = TrailerMassEntityView.GetFragmentData<FMassTrafficConstrainedVehicleFragment>();
		if (VehicleConstraintFragment.Vehicle.IsSet())
		{
			const FMassEntityView VehicleMassEntityView(*EntityManager, VehicleConstraintFragment.Vehicle);
			FMassActorFragment& VehicleActorFragment = VehicleMassEntityView.GetFragmentData<FMassActorFragment>();

			// We've got a vehicle pulling us along so use the movement component to see the primitive data to give us brake lights,
			// turning signals, etc.
			const FMassTrafficVehicleLightsFragment& VehicleLightsFragment = VehicleMassEntityView.GetFragmentData<FMassTrafficVehicleLightsFragment>();
			PackedCustomData = FMassTrafficVehicleInstanceCustomData::MakeTrafficVehicleCustomData(VehicleLightsFragment, TrailerRandomFractionFragment);

			if (AWheeledVehiclePawn* VehiclePawn = Cast<AWheeledVehiclePawn>(VehicleActorFragment.GetMutable()))
			{
				// Destroy any existing constraint component that may have been left behind if the actor was pooled
				if (UPhysicsConstraintComponent* ExistingConstraintComponent = VehiclePawn->FindComponentByClass<UPhysicsConstraintComponent>())
				{
					ExistingConstraintComponent->DestroyComponent();
				}

				UPhysicsConstraintComponent* ConstraintComponent = NewObject<UPhysicsConstraintComponent>(VehiclePawn);
				ConstraintComponent->SetRelativeLocation(TrailerSimulationConfig.ConstraintSettings.MountPoint);
				ConstraintComponent->AttachToComponent(VehiclePawn->GetMesh(), FAttachmentTransformRules::KeepRelativeTransform);
				ConstraintComponent->SetConstrainedComponents(VehiclePawn->GetMesh(), NAME_None, TrailerPawn->GetMesh(), NAME_None);
				ConstraintComponent->SetDisableCollision(TrailerSimulationConfig.ConstraintSettings.bDisableCollision);
				// For now we only support locked twist with limited swing to keep
				// FMassTrafficSimpleTrailerConstraintSolver simple
				ConstraintComponent->SetAngularTwistLimit(ACM_Locked, 0.0f);
				ConstraintComponent->SetAngularSwing1Limit(ACM_Limited, TrailerSimulationConfig.ConstraintSettings.AngularSwing1Limit);
				ConstraintComponent->SetAngularSwing2Limit(ACM_Limited, TrailerSimulationConfig.ConstraintSettings.AngularSwing2Limit);
				VehiclePawn->AddInstanceComponent(ConstraintComponent);
				ConstraintComponent->RegisterComponent();
				
				VehicleConstraintFragment.PhysicsConstraintComponent = ConstraintComponent; 
			}
		}

		// Set primitive component custom data
		MassActorSpawnRequest.SpawnedActor->ForEachComponent<UPrimitiveComponent>(/*bIncludeFromChildActors*/true, [&PackedCustomData](UPrimitiveComponent* PrimitiveComponent)
		{
			PrimitiveComponent->SetCustomPrimitiveDataFloat(/*DataIndex*/1, PackedCustomData.PackedParam1);

			// Init render scene previous frame transform to current transform as we're about to simulate
			// forward from here
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("SetPreviousTransform"))
			FMotionVectorSimulation::Get().SetPreviousTransform(PrimitiveComponent, PrimitiveComponent->GetComponentTransform());
		});

		// Init Chaos Vehicle
		const FMassTrafficVehiclePhysicsFragment* TrailerSimpleVehiclePhysicsFragment = TrailerMassEntityView.GetFragmentDataPtr<FMassTrafficVehiclePhysicsFragment>();
		if (TrailerSimpleVehiclePhysicsFragment)
		{
			// Init using medium LOD simple physics state
			FWheeledSnaphotData SnapshotData;
			SnapshotData.Transform = TrailerSimpleVehiclePhysicsFragment->VehicleSim.Setup().BodyToActor * TrailerPawn->GetTransform();
			SnapshotData.LinearVelocity = TrailerMassEntityView.GetFragmentData<FMassVelocityFragment>().Value;
			SnapshotData.AngularVelocity = TrailerMassEntityView.GetFragmentData<FMassTrafficAngularVelocityFragment>().AngularVelocity;
		
			SnapshotData.SelectedGear = TrailerSimpleVehiclePhysicsFragment->VehicleSim.TransmissionSim.GetCurrentGear();
			SnapshotData.EngineRPM = TrailerSimpleVehiclePhysicsFragment->VehicleSim.EngineSim.GetEngineRPM();
			SnapshotData.WheelSnapshots.SetNum(TrailerSimpleVehiclePhysicsFragment->VehicleSim.WheelSims.Num());
			for (int32 WheelIndex = 0; WheelIndex < TrailerSimpleVehiclePhysicsFragment->VehicleSim.WheelSims.Num(); ++WheelIndex)
			{
				const Chaos::FSimpleWheelSim& WheelSim = TrailerSimpleVehiclePhysicsFragment->VehicleSim.WheelSims[WheelIndex];
				const Chaos::FSimpleSuspensionSim& SuspensionSim = TrailerSimpleVehiclePhysicsFragment->VehicleSim.SuspensionSims[WheelIndex];
				const FVector& WheelLocalLocation = TrailerSimpleVehiclePhysicsFragment->VehicleSim.WheelLocalLocations[WheelIndex];
				const FVector& WheelLocalRestingPosition = TrailerSimpleVehiclePhysicsFragment->VehicleSim.SuspensionSims[WheelIndex].GetLocalRestingPosition();
				FWheelSnapshot& WheelSnapshot = SnapshotData.WheelSnapshots[WheelIndex];
		
				WheelSnapshot.SuspensionOffset = (WheelLocalRestingPosition.Z - SuspensionSim.Setup().SuspensionMaxRaise - SuspensionSim.Setup().RaycastSafetyMargin) - (WheelLocalLocation.Z - WheelSim.GetEffectiveRadius()); 
				WheelSnapshot.WheelRotationAngle = -1.0f * FMath::RadiansToDegrees(WheelSim.AngularPosition); // @see UChaosVehicleWheel::GetRotationAngle
				WheelSnapshot.SteeringAngle = WheelSim.SteeringAngle;
				WheelSnapshot.WheelRadius = WheelSim.GetEffectiveRadius();
				WheelSnapshot.WheelAngularVelocity = WheelSim.GetAngularVelocity();
			}
			
			TrailerVehicleMovementComponent->SetSnapshot(SnapshotData);
		}
		else
		{
			// Init using simple velocity 
			const FMassTrafficVehiclePhysicsSharedParameters& PhysicsSharedFragment = TrailerMassEntityView.GetConstSharedFragmentData<FMassTrafficVehiclePhysicsSharedParameters>();
			if (PhysicsSharedFragment.Template)
			{
				FBaseSnapshotData BaseSnapshotData;
				BaseSnapshotData.Transform = PhysicsSharedFragment.Template->SimpleVehiclePhysicsConfig.BodyToActor * TrailerPawn->GetTransform();
				BaseSnapshotData.LinearVelocity = TrailerMassEntityView.GetFragmentData<FMassVelocityFragment>().Value;
				BaseSnapshotData.AngularVelocity = TrailerMassEntityView.GetFragmentData<FMassTrafficAngularVelocityFragment>().AngularVelocity;
				TrailerVehicleMovementComponent->SetBaseSnapshot(BaseSnapshotData);
			}
		}
	}
	// Medium LOD trailer
	else
	{
		// Set primitive component custom data
		const FMassRepresentationFragment& TrailerRepresentationFragment = TrailerMassEntityView.GetFragmentData<FMassRepresentationFragment>();
		MassActorSpawnRequest.SpawnedActor->ForEachComponent<UPrimitiveComponent>(/*bIncludeFromChildActors*/true, [&PackedCustomData, &TrailerRepresentationFragment, &MassActorSpawnRequest](UPrimitiveComponent* PrimitiveComponent)
		{
			PrimitiveComponent->SetCustomPrimitiveDataFloat(/*DataIndex*/1, PackedCustomData.PackedParam1);

			// Init render scene previous frame transform
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("SetPreviousTransform"))
			const FTransform PrimitiveComponentPreviousTransform = PrimitiveComponent->GetComponentTransform().GetRelativeTransform(MassActorSpawnRequest.SpawnedActor->GetTransform()) * TrailerRepresentationFragment.PrevTransform;
			FMotionVectorSimulation::Get().SetPreviousTransform(PrimitiveComponent, PrimitiveComponentPreviousTransform);
		});
		
		// Has a UMassTrafficVehicleComponent with wheel mesh references?
		const FMassTrafficVehiclePhysicsFragment* TrailerSimpleVehiclePhysicsFragment = TrailerMassEntityView.GetFragmentDataPtr<FMassTrafficVehiclePhysicsFragment>();
		if (TrailerSimpleVehiclePhysicsFragment)
		{
			UMassTrafficVehicleComponent* MassTrafficVehicleComponent = MassActorSpawnRequest.SpawnedActor->FindComponentByClass<UMassTrafficVehicleComponent>();
			if (MassTrafficVehicleComponent)
			{
				// Init offsets?
				if (MassTrafficVehicleComponent->WheelOffsets.IsEmpty())
				{
					MassTrafficVehicleComponent->InitWheelAttachmentOffsets(TrailerSimpleVehiclePhysicsFragment->VehicleSim);
				}
		
				// Update
				MassTrafficVehicleComponent->UpdateWheelComponents(TrailerSimpleVehiclePhysicsFragment->VehicleSim);
			}
		}
	}

	// Let the BPs know we've been spawned so they can do what they need.
	if (MassActorSpawnRequest.SpawnedActor->Implements<UMassTrafficVehicleControlInterface>())
	{
		IMassTrafficVehicleControlInterface::Execute_OnTrafficVehicleSpawned(MassActorSpawnRequest.SpawnedActor);
	}

	return Result;
}