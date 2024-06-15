// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficLightRepresentationActorManagement.h"
#include "MassTrafficLightVisualizationProcessor.h"
#include "MassTrafficFragments.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "MassRepresentationSubsystem.h"
#include "MassRepresentationTypes.h"
#include "MassTrafficLights.h"
#include "Components/StaticMeshComponent.h"

EMassActorSpawnRequestAction  UMassTrafficLightRepresentationActorManagement::OnPostActorSpawn(const FMassActorSpawnRequestHandle& SpawnRequestHandle
	, FConstStructView SpawnRequest, TSharedRef<FMassEntityManager> EntityManager) const
{
	const EMassActorSpawnRequestAction Result = Super::OnPostActorSpawn(SpawnRequestHandle, SpawnRequest, EntityManager);

	const FMassActorSpawnRequest& MassActorSpawnRequest = SpawnRequest.Get<const FMassActorSpawnRequest>();
	check(MassActorSpawnRequest.SpawnedActor);

	const FMassEntityView IntersectionMassEntityView(*EntityManager, MassActorSpawnRequest.MassAgent);

	UMassRepresentationSubsystem* RepresentationSubsystem = IntersectionMassEntityView.GetSharedFragmentData<FMassRepresentationSubsystemSharedFragment>().RepresentationSubsystem;
	check(RepresentationSubsystem);

	const FMassInstancedStaticMeshInfoArrayView ISMInfo = RepresentationSubsystem->GetMutableInstancedStaticMeshInfos();
	
	const FMassTrafficLightsParameters& TrafficLightsParams = IntersectionMassEntityView.GetConstSharedFragmentData<FMassTrafficLightsParameters>();
	
	const FMassTrafficIntersectionFragment& TrafficIntersectionFragment = IntersectionMassEntityView.GetFragmentData<FMassTrafficIntersectionFragment>();
	for (const FMassTrafficLight& TrafficLight : TrafficIntersectionFragment.TrafficLights)
	{
		check(TrafficLightsParams.TrafficLightTypesStaticMeshDescHandle.IsValidIndex(TrafficLight.TrafficLightTypeIndex));
		const FStaticMeshInstanceVisualizationDescHandle TrafficLightStaticMeshDescHandle = TrafficLightsParams.TrafficLightTypesStaticMeshDescHandle[TrafficLight.TrafficLightTypeIndex];
		check(ISMInfo[TrafficLightStaticMeshDescHandle.ToIndex()].GetDesc().Meshes.Num() > 0);
		const FMassStaticMeshInstanceVisualizationMeshDesc& MeshDesc = ISMInfo[TrafficLightStaticMeshDescHandle.ToIndex()].GetDesc().Meshes[0];

		// Compute actor relative transform
		FTransform IntersectionLightTransform(FRotator(0.0, TrafficLight.ZRotation, 0.0f), TrafficLight.Position);
		IntersectionLightTransform.SetToRelativeTransform(MassActorSpawnRequest.SpawnedActor->GetActorTransform());

		// Create UStaticMeshComponent for the light
		UStaticMeshComponent* TrafficLightMeshComponent = NewObject<UStaticMeshComponent>(MassActorSpawnRequest.SpawnedActor);
		TrafficLightMeshComponent->SetStaticMesh(MeshDesc.Mesh);
		TrafficLightMeshComponent->SetupAttachment(MassActorSpawnRequest.SpawnedActor->GetRootComponent());
		TrafficLightMeshComponent->SetCanEverAffectNavigation(false);
		TrafficLightMeshComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		TrafficLightMeshComponent->SetCastShadow(MeshDesc.bCastShadows);
		TrafficLightMeshComponent->Mobility = MeshDesc.Mobility;
		TrafficLightMeshComponent->SetReceivesDecals(false);
		TrafficLightMeshComponent->SetRelativeTransform(IntersectionLightTransform);
		TrafficLightMeshComponent->SetMobility(EComponentMobility::Static);
		for (int32 ElementIndex = 0; ElementIndex < MeshDesc.MaterialOverrides.Num(); ++ElementIndex)
		{
			if (UMaterialInterface* MaterialOverride = MeshDesc.MaterialOverrides[ElementIndex])
			{
				TrafficLightMeshComponent->SetMaterial(ElementIndex, MaterialOverride);
			}
		}

		// Set light mesh primitive data
		const FMassTrafficLightInstanceCustomData PackedCustomData(TrafficLight.TrafficLightStateFlags);
		TrafficLightMeshComponent->SetCustomPrimitiveDataFloat(/*DataIndex*/1, PackedCustomData.PackedParam1);
		
		TrafficLightMeshComponent->RegisterComponent();
		MassActorSpawnRequest.SpawnedActor->AddInstanceComponent(TrafficLightMeshComponent);
	}

	return Result;
}