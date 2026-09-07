// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficVehicleRepresentationActorManagement.h"
#include "MassTrafficVehicleVisualizationProcessor.h"
#include "MassTrafficVehicleComponent.h"
#include "MassTrafficVehicleControlInterface.h"
#include "MassTrafficFragments.h"
#include "MassTrafficPhysics.h"
#include "MassRepresentationSubsystem.h"
#include "MassMovementFragments.h"
#include "MassEntityView.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/World.h"
#include "Rendering/MotionVectorSimulation.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
bool UMassTrafficVehicleRepresentationActorManagement::IsActorReadyForRepresentation(const AActor& Actor) const
{
	const UWorld* World = Actor.GetWorld();
	if (!World)
	{
		return false;
	}
	// Dedicated server: nothing renders, the streaming gate is meaningless.
	if (World->IsNetMode(NM_DedicatedServer))
	{
		return true;
	}

	// A traffic vehicle is "ready" once at least one of its skinned mesh components has a valid asset
	// (i.e. the soft-pointered SkeletalMesh referenced in the actor template has been resolved by the
	// streaming manager). FindComponentByClass is fine here - a vehicle has at most a small number of
	// components and this fires per pending entity per frame, so it is on a hot path; the linear walk
	// is the cheapest reliable check we have without per-actor caching.
	const USkinnedMeshComponent* SkinnedMesh = Actor.FindComponentByClass<USkinnedMeshComponent>();
	if (SkinnedMesh != nullptr)
	{
		return SkinnedMesh->GetSkinnedAsset() != nullptr;
	}

	// No skinned component (e.g. low-res variant using only static meshes); fall back to the engine
	// default of "ready as soon as the actor exists".
	return Super::IsActorReadyForRepresentation(Actor);
}
#endif

EMassActorSpawnRequestAction UMassTrafficVehicleRepresentationActorManagement::OnPostActorSpawn(const FMassActorSpawnRequestHandle& SpawnRequestHandle
	, FConstStructView SpawnRequest, TSharedRef<FMassEntityManager> EntityManager) const
{
	const EMassActorSpawnRequestAction Result = Super::OnPostActorSpawn(SpawnRequestHandle, SpawnRequest, EntityManager);

	const FMassActorSpawnRequest& MassActorSpawnRequest = SpawnRequest.Get<const FMassActorSpawnRequest>();
	check(MassActorSpawnRequest.SpawnedActor);

	FMassEntityView EntityView(*EntityManager, MassActorSpawnRequest.MassAgent);

	UMassRepresentationSubsystem* RepresentationSubsystem = EntityView.GetSharedFragmentData<FMassRepresentationSubsystemSharedFragment>().RepresentationSubsystem;
	check(RepresentationSubsystem);
	
	FMassRepresentationFragment& RepresentationFragment = EntityView.GetFragmentData<FMassRepresentationFragment>();
	if (RepresentationFragment.HighResTemplateActorIndex != INDEX_NONE && RepresentationSubsystem->DoesActorMatchTemplate(*MassActorSpawnRequest.SpawnedActor, RepresentationFragment.HighResTemplateActorIndex))
	{
		InitHighResActor(*MassActorSpawnRequest.SpawnedActor, EntityView);
	}
	else
	{
		checkf(RepresentationFragment.LowResTemplateActorIndex != INDEX_NONE && RepresentationSubsystem->DoesActorMatchTemplate(*MassActorSpawnRequest.SpawnedActor, RepresentationFragment.LowResTemplateActorIndex), TEXT("Expecting the template to be either high res or low res"));
		InitLowResActor(*MassActorSpawnRequest.SpawnedActor, EntityView);
	}

	return Result;
}

void UMassTrafficVehicleRepresentationActorManagement::InitLowResActor(AActor& LowResActor, const FMassEntityView& EntityView) const
{
	const FMassTrafficVehicleLightsFragment& VehicleStateFragment = EntityView.GetFragmentData<FMassTrafficVehicleLightsFragment>();
	const FMassTrafficRandomFractionFragment& RandomFractionFragment = EntityView.GetFragmentData<FMassTrafficRandomFractionFragment>();
	FMassRepresentationFragment& RepresentationFragment = EntityView.GetFragmentData<FMassRepresentationFragment>();
	
	// Has a UMassTrafficVehicleComponent with wheel mesh references?
	const FMassTrafficVehiclePhysicsFragment* SimpleVehiclePhysicsFragment = EntityView.GetFragmentDataPtr<FMassTrafficVehiclePhysicsFragment>();
	if (SimpleVehiclePhysicsFragment)
	{
		UMassTrafficVehicleComponent* MassTrafficVehicleComponent = LowResActor.FindComponentByClass<UMassTrafficVehicleComponent>();
		if (MassTrafficVehicleComponent)
		{
			// Init offsets?
			if (MassTrafficVehicleComponent->WheelOffsets.IsEmpty())
			{
				MassTrafficVehicleComponent->InitWheelAttachmentOffsets(SimpleVehiclePhysicsFragment->VehicleSim);
			}
	
			// Update
			MassTrafficVehicleComponent->UpdateWheelComponents(SimpleVehiclePhysicsFragment->VehicleSim);
		}
	}
	
	// Init primitive components
	const FMassTrafficPackedVehicleInstanceCustomData PackedCustomData = FMassTrafficVehicleInstanceCustomData::MakeTrafficVehicleCustomData(VehicleStateFragment, RandomFractionFragment);
	LowResActor.ForEachComponent<UPrimitiveComponent>(/*bIncludeFromChildActors*/true, [&LowResActor, &PackedCustomData, &RepresentationFragment](UPrimitiveComponent* PrimitiveComponent)
	{
		// Init custom data 
		PrimitiveComponent->SetCustomPrimitiveDataFloat(/*DataIndex*/1, PackedCustomData.PackedParam1);

		// Init render scene previous frame transform
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("SetPreviousTransform"))
		const FTransform PrimitiveComponentPreviousTransform = PrimitiveComponent->GetComponentTransform().GetRelativeTransform(LowResActor.GetTransform()) * RepresentationFragment.PrevTransform;
		FMotionVectorSimulation::Get().SetPreviousTransform(PrimitiveComponent, PrimitiveComponentPreviousTransform);
	});
}

void UMassTrafficVehicleRepresentationActorManagement::InitHighResActor(AActor& HighResActor, const FMassEntityView& EntityView) const
{
	const FMassTrafficVehicleLightsFragment& VehicleStateFragment = EntityView.GetFragmentData<FMassTrafficVehicleLightsFragment>();
	const FMassTrafficRandomFractionFragment& RandomFractionFragment = EntityView.GetFragmentData<FMassTrafficRandomFractionFragment>();
	FMassRepresentationFragment& RepresentationFragment = EntityView.GetFragmentData<FMassRepresentationFragment>();
	
	// Init primitive components
	const FMassTrafficPackedVehicleInstanceCustomData PackedCustomData = FMassTrafficVehicleInstanceCustomData::MakeTrafficVehicleCustomData(VehicleStateFragment, RandomFractionFragment);
	HighResActor.ForEachComponent<UPrimitiveComponent>(/*bIncludeFromChildActors*/true, [&PackedCustomData, &RepresentationFragment, &HighResActor](UPrimitiveComponent* PrimitiveComponent)
	{
		// Init custom data 
		PrimitiveComponent->SetCustomPrimitiveDataFloat(/*DataIndex*/1, PackedCustomData.PackedParam1);
		
		// Init render scene previous frame transform to current transform as we're about to simulate
		// forward from here
		TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("SetPreviousTransform"))
		FMotionVectorSimulation::Get().SetPreviousTransform(PrimitiveComponent, PrimitiveComponent->GetComponentTransform());
	});

	// Init vehicle physics
	if (AWheeledVehiclePawn* VehiclePawn = Cast<AWheeledVehiclePawn>(&HighResActor))
	{
		const FMassVelocityFragment& VelocityFragment = EntityView.GetFragmentData<FMassVelocityFragment>();
		const FMassTrafficAngularVelocityFragment& AngularVelocityFragment = EntityView.GetFragmentData<FMassTrafficAngularVelocityFragment>();
		const FMassTrafficPIDVehicleControlFragment* PIDVehicleControlFragment = EntityView.GetFragmentDataPtr<FMassTrafficPIDVehicleControlFragment>();
		const FMassTrafficVehiclePhysicsFragment* SimpleVehiclePhysicsFragment = EntityView.GetFragmentDataPtr<FMassTrafficVehiclePhysicsFragment>();
	
		// Disable brake as reverse on traffic vehicles. If we need to reverse, we will temporarily re-enable this.
		UChaosWheeledVehicleMovementComponent* VehicleMovementComponent = Cast<UChaosWheeledVehicleMovementComponent>(VehiclePawn->GetVehicleMovementComponent());
		VehicleMovementComponent->bReverseAsBrake = false;

		// Init Chaos Vehicle
		if (SimpleVehiclePhysicsFragment)
		{
			// FDataFragment_SimpleVehiclePhysics and FDataFragment_PIDVehicleControl are added together 
			check(PIDVehicleControlFragment);

			// Init using medium LOD simple physics state
			FWheeledSnaphotData SnapshotData;
			SnapshotData.Transform = HighResActor.GetTransform();
			SnapshotData.LinearVelocity = VelocityFragment.Value;
			SnapshotData.AngularVelocity = AngularVelocityFragment.AngularVelocity;
		
			SnapshotData.SelectedGear = SimpleVehiclePhysicsFragment->VehicleSim.TransmissionSim.GetCurrentGear();
			SnapshotData.EngineRPM = SimpleVehiclePhysicsFragment->VehicleSim.EngineSim.GetEngineRPM();
			SnapshotData.WheelSnapshots.SetNum(SimpleVehiclePhysicsFragment->VehicleSim.WheelSims.Num());
			for (int32 WheelIndex = 0; WheelIndex < SimpleVehiclePhysicsFragment->VehicleSim.WheelSims.Num(); ++WheelIndex)
			{
				const Chaos::FSimpleWheelSim& WheelSim = SimpleVehiclePhysicsFragment->VehicleSim.WheelSims[WheelIndex];
				const Chaos::FSimpleSuspensionSim& SuspensionSim = SimpleVehiclePhysicsFragment->VehicleSim.SuspensionSims[WheelIndex];
				const FVector& WheelLocalLocation = SimpleVehiclePhysicsFragment->VehicleSim.WheelLocalLocations[WheelIndex];
				const FVector& WheelLocalRestingPosition = SimpleVehiclePhysicsFragment->VehicleSim.SuspensionSims[WheelIndex].GetLocalRestingPosition();
				FWheelSnapshot& WheelSnapshot = SnapshotData.WheelSnapshots[WheelIndex];

				WheelSnapshot.SuspensionOffset = (WheelLocalRestingPosition.Z - SuspensionSim.Setup().SuspensionMaxRaise - SuspensionSim.Setup().RaycastSafetyMargin) - (WheelLocalLocation.Z - WheelSim.GetEffectiveRadius()); 
				WheelSnapshot.WheelRotationAngle = -1.0f * FMath::RadiansToDegrees(WheelSim.AngularPosition); // @see UChaosVehicleWheel::GetRotationAngle
				WheelSnapshot.SteeringAngle = WheelSim.SteeringAngle;
				WheelSnapshot.WheelRadius = WheelSim.GetEffectiveRadius();
				WheelSnapshot.WheelAngularVelocity = WheelSim.GetAngularVelocity();
			}
			
			VehicleMovementComponent->SetSnapshot(SnapshotData);

			if (SimpleVehiclePhysicsFragment->VehicleSim.IsSleeping())
			{
				VehicleMovementComponent->SetSleeping(true);
			}

			if (HighResActor.Implements<UMassTrafficVehicleControlInterface>())
			{
				IMassTrafficVehicleControlInterface::Execute_SetVehicleInputs(&HighResActor,PIDVehicleControlFragment->Throttle,
					PIDVehicleControlFragment->Brake,
					PIDVehicleControlFragment->bHandbrake,
					PIDVehicleControlFragment->Steering, true);
			}
		}
		else
		{
			// Init using simple velocity 
			FBaseSnapshotData BaseSnapshotData;
			BaseSnapshotData.Transform = HighResActor.GetTransform();
			BaseSnapshotData.LinearVelocity = VelocityFragment.Value;
			BaseSnapshotData.AngularVelocity = AngularVelocityFragment.AngularVelocity;
			VehicleMovementComponent->SetBaseSnapshot(BaseSnapshotData);
		}
	}

	// Let the BPs know we've been spawned so they can do what they need.
	if (HighResActor.Implements<UMassTrafficVehicleControlInterface>())
	{
		IMassTrafficVehicleControlInterface::Execute_OnTrafficVehicleSpawned(&HighResActor);
	}

}