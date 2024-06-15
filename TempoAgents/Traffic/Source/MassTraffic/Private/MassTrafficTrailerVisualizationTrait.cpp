// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficTrailerVisualizationTrait.h"
#include "MassTrafficFragments.h"
#include "MassTrafficTrailerRepresentationActorManagement.h"

#include "MassEntityTemplateRegistry.h"
#include "MassLODFragments.h"
#include "MassActorSubsystem.h"

UMassTrafficTrailerVisualizationTrait::UMassTrafficTrailerVisualizationTrait()
{
	Params.RepresentationActorManagementClass = UMassTrafficTrailerRepresentationActorManagement::StaticClass();
	Params.LODRepresentation[EMassLOD::High] = EMassRepresentationType::HighResSpawnedActor;
	Params.LODRepresentation[EMassLOD::Medium] = EMassRepresentationType::LowResSpawnedActor;
	Params.LODRepresentation[EMassLOD::Low] = EMassRepresentationType::StaticMeshInstance;
	Params.LODRepresentation[EMassLOD::Off] = EMassRepresentationType::None;
	Params.bKeepLowResActors = false;
	Params.bKeepActorExtraFrame = false;
	Params.bSpreadFirstVisualizationUpdate = false;
	Params.WorldPartitionGridNameContainingCollision = NAME_None;
	Params.NotVisibleUpdateRate = 0.5f;

	LODParams.BaseLODDistance[EMassLOD::High] = 0.f;
	LODParams.BaseLODDistance[EMassLOD::Medium] = 4000.f;
	LODParams.BaseLODDistance[EMassLOD::Low] = 4500.f;
	LODParams.BaseLODDistance[EMassLOD::Off] = 60000.f;

	LODParams.VisibleLODDistance[EMassLOD::High] = 0.f;
	LODParams.VisibleLODDistance[EMassLOD::Medium] = 8000.f;
	LODParams.VisibleLODDistance[EMassLOD::Low] = 10000.f;
	LODParams.VisibleLODDistance[EMassLOD::Off] = 100000.f;

	LODParams.LODMaxCount[EMassLOD::High] = 10;
	LODParams.LODMaxCount[EMassLOD::Medium] = 40;
	LODParams.LODMaxCount[EMassLOD::Low] = TNumericLimits<int32>::Max();
	LODParams.LODMaxCount[EMassLOD::Off] = TNumericLimits<int32>::Max();

	LODParams.BufferHysteresisOnDistancePercentage = 10.0f;
	LODParams.DistanceToFrustum = 0.0f;
	LODParams.DistanceToFrustumHysteresis = 0.0f;
}

void UMassTrafficTrailerVisualizationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	Super::BuildTemplate(BuildContext, World);

	BuildContext.AddTag<FMassTrafficVehicleTrailerTag>();
	
	BuildContext.RequireFragment<FMassTrafficRandomFractionFragment>();
	BuildContext.AddFragment<FMassActorFragment>();
}
