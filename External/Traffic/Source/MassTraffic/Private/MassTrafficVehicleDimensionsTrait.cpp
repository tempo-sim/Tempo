// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficVehicleDimensionsTrait.h"
#include "MassTrafficFragments.h"
#include "MassTrafficVehicleSimulationTrait.h"
#include "MassActorSubsystem.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassEntityUtils.h"


void UMassTrafficVehicleDimensionsTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	// Add simulation config as shared fragment for length and width access.
	FMassTrafficVehicleSimulationParameters SimulationConfig;
	SimulationConfig.HalfLength = Params.HalfLength;
	SimulationConfig.HalfWidth = Params.HalfWidth;

	const FConstSharedStruct SimulationConfigSharedFragment = EntityManager.GetOrCreateConstSharedFragment(SimulationConfig);
	BuildContext.AddConstSharedFragment(SimulationConfigSharedFragment);
}
