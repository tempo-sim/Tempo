// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficLightVisualizationTrait.h"
#include "MassTrafficFragments.h"
#include "MassTrafficLightRepresentationActorManagement.h"

#include "MassActorSubsystem.h"
#include "MassEntityTemplateRegistry.h"
#include "MassLODFragments.h"
#include "MassRepresentationSubsystem.h"
#include "MassEntityUtils.h"


UMassTrafficLightVisualizationTrait::UMassTrafficLightVisualizationTrait()
{
	Params.RepresentationActorManagementClass = UMassTrafficLightRepresentationActorManagement::StaticClass();
	Params.LODRepresentation[EMassLOD::High] = EMassRepresentationType::LowResSpawnedActor;
	Params.LODRepresentation[EMassLOD::Medium] = EMassRepresentationType::LowResSpawnedActor;
	Params.LODRepresentation[EMassLOD::Low] = EMassRepresentationType::StaticMeshInstance;
	Params.LODRepresentation[EMassLOD::Off] = EMassRepresentationType::None;
	Params.bKeepLowResActors = true;
	Params.bKeepActorExtraFrame = false;
	Params.bSpreadFirstVisualizationUpdate = false;
	Params.WorldPartitionGridNameContainingCollision = NAME_None;
	Params.NotVisibleUpdateRate = 0.5f;

	HighResTemplateActor = AActor::StaticClass();
	LowResTemplateActor = AActor::StaticClass();

	LODParams.BaseLODDistance[EMassLOD::High] = 0.f;
	LODParams.BaseLODDistance[EMassLOD::Medium] = 30000.f;
	LODParams.BaseLODDistance[EMassLOD::Low] = 30000.f;
	LODParams.BaseLODDistance[EMassLOD::Off] = 30000.f;

	LODParams.VisibleLODDistance[EMassLOD::High] = 0.f;
	LODParams.VisibleLODDistance[EMassLOD::Medium] = 30000.f;
	LODParams.VisibleLODDistance[EMassLOD::Low] = 30000.f;
	LODParams.VisibleLODDistance[EMassLOD::Off] = 30000.f;

	LODParams.LODMaxCount[EMassLOD::High] = TNumericLimits<int32>::Max();
	LODParams.LODMaxCount[EMassLOD::Medium] = TNumericLimits<int32>::Max();
	LODParams.LODMaxCount[EMassLOD::Low] = TNumericLimits<int32>::Max();
	LODParams.LODMaxCount[EMassLOD::Off] = TNumericLimits<int32>::Max();

	LODParams.BufferHysteresisOnDistancePercentage = 10.0f;
	LODParams.DistanceToFrustum = 0.0f;
	LODParams.DistanceToFrustumHysteresis = 0.0f;

	LODParams.FilterTag = FMassTrafficIntersectionTag::StaticStruct();
}

void UMassTrafficLightVisualizationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	Super::BuildTemplate(BuildContext, World);
	
	UMassRepresentationSubsystem* RepresentationSubsystem = Cast<UMassRepresentationSubsystem>(World.GetSubsystemBase(RepresentationSubsystemClass));
	if (RepresentationSubsystem == nullptr)
	{
		UE_LOG(LogMassTraffic, Error, TEXT("UMassTrafficLightVisualizationTrait - Expecting a valid class for the representation subsystem"));
		RepresentationSubsystem = UWorld::GetSubsystem<UMassRepresentationSubsystem>(&World);
		check(RepresentationSubsystem);
	}
	
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	// Requirements
	BuildContext.RequireFragment<FMassTrafficIntersectionFragment>();

	// Make a mutable copy of Config so we can register the driver meshes and assign the description IDs
	FMassTrafficLightsParameters RegisteredTrafficLightsParams = TrafficLightsParams;
	if (IsValid(RegisteredTrafficLightsParams.TrafficLightTypesData))
	{
		for (const FMassTrafficLightTypeData& TrafficLightType : RegisteredTrafficLightsParams.TrafficLightTypesData->TrafficLightTypes)
		{
			// Register visual types
			FStaticMeshInstanceVisualizationDescHandle TrafficLightTypeStaticMeshDescHandle = RepresentationSubsystem->FindOrAddStaticMeshDesc(TrafficLightType.StaticMeshInstanceDesc);
			RegisteredTrafficLightsParams.TrafficLightTypesStaticMeshDescHandle.Add(TrafficLightTypeStaticMeshDescHandle);
		}
	}

	// Register & add shared fragment
	const FConstSharedStruct TrafficLightsParamsFragment = EntityManager.GetOrCreateConstSharedFragment(RegisteredTrafficLightsParams);
	BuildContext.AddConstSharedFragment(TrafficLightsParamsFragment);

	BuildContext.AddFragment<FMassActorFragment>();
}
