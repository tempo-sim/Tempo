// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficVehicleSimulationTrait.h"
#include "MassTrafficFragments.h"

#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassMovementFragments.h"
#include "MassSimulationLOD.h"
#include "MassTrafficSubsystem.h"
#include "MassZoneGraphNavigationFragments.h"
#include "MassEntityUtils.h"


UMassTrafficVehicleSimulationTrait::UMassTrafficVehicleSimulationTrait(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	// Zero all tick rates by default
	for (int i = 0; i < EMassLOD::Max; ++i)
	{
		VariableTickParams.TickRates[i] = 0.0f;		
	}
	VariableTickParams.TickRates[EMassLOD::Off] = 1.0f; // 1s tick interval for Off LODs
}

void UMassTrafficVehicleSimulationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(&World);
	check(MassTrafficSubsystem);

	// Add parameters as shared fragment
	const FConstSharedStruct ParamsSharedFragment = EntityManager.GetOrCreateConstSharedFragment(Params);
	BuildContext.AddConstSharedFragment(ParamsSharedFragment);

	// Add radius
	// @todo Replace this with direct usage of FMassTrafficSimulationParameters::HalfLength
	FAgentRadiusFragment& RadiusFragment = BuildContext.AddFragment_GetRef<FAgentRadiusFragment>();
	RadiusFragment.Radius = Params.HalfLength;

	// Simulation LOD
	FMassTrafficSimulationLODFragment& SimulationLODFragment = BuildContext.AddFragment_GetRef<FMassTrafficSimulationLODFragment>();
	SimulationLODFragment.LOD = EMassLOD::Off;
	SimulationLODFragment.PrevLOD = EMassLOD::Max;
	BuildContext.AddTag<FMassOffLODTag>();

	// Vehicle control fragment
	// @todo Replace FMassTrafficVehicleControlFragment::bRestrictedToTrunkLanesOnly usage with
	//		 FMassTrafficVehicleSimulationParameters::bRestrictedToTrunkLanesOnly
	FMassTrafficVehicleControlFragment& VehicleControlFragment = BuildContext.AddFragment_GetRef<FMassTrafficVehicleControlFragment>();
	VehicleControlFragment.bRestrictedToTrunkLanesOnly = Params.bRestrictedToTrunkLanesOnly;   

	// Variable tick
	BuildContext.AddFragment<FMassSimulationVariableTickFragment>();
	BuildContext.AddChunkFragment<FMassSimulationVariableTickChunkFragment>();

	const FConstSharedStruct VariableTickParamsFragment = EntityManager.GetOrCreateConstSharedFragment(VariableTickParams);
	BuildContext.AddConstSharedFragment(VariableTickParamsFragment);
	
	const uint32 VariableTickParamsHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(VariableTickParams)); 
	const FSharedStruct VariableTickSharedFragment = EntityManager.GetOrCreateSharedFragmentByHash<FMassSimulationVariableTickSharedFragment>(VariableTickParamsHash, VariableTickParams);
	BuildContext.AddSharedFragment(VariableTickSharedFragment);

	// Various fragments
	BuildContext.AddFragment<FMassActorFragment>();
	BuildContext.AddFragment<FTransformFragment>();
	BuildContext.AddFragment<FMassTrafficAngularVelocityFragment>();
	BuildContext.AddFragment<FMassTrafficInterpolationFragment>();
	BuildContext.AddFragment<FMassTrafficLaneOffsetFragment>();
	BuildContext.AddFragment<FMassTrafficNextVehicleFragment>();
	BuildContext.AddFragment<FMassTrafficObstacleAvoidanceFragment>();	
	BuildContext.RequireFragment<FMassTrafficRandomFractionFragment>();
	BuildContext.AddFragment<FMassTrafficVehicleLaneChangeFragment>();	
	BuildContext.RequireFragment<FMassTrafficVehicleLightsFragment>();
	BuildContext.AddFragment<FMassVelocityFragment>();
	BuildContext.AddFragment<FMassZoneGraphLaneLocationFragment>();

	IF_MASSTRAFFIC_ENABLE_DEBUG(BuildContext.RequireFragment<FMassTrafficDebugFragment>());

	if (Params.PhysicsVehicleTemplateActor)
	{
		// Extract physics setup from PhysicsVehicleTemplateActor into shared fragment
		const FMassTrafficSimpleVehiclePhysicsTemplate* Template = MassTrafficSubsystem->GetOrExtractVehiclePhysicsTemplate(Params.PhysicsVehicleTemplateActor);

		// Register & add shared fragment
		const uint32 TemplateHash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(*Template));
		const FConstSharedStruct PhysicsSharedFragment = EntityManager.GetOrCreateConstSharedFragmentByHash<FMassTrafficVehiclePhysicsSharedParameters>(TemplateHash, Template);
		BuildContext.AddConstSharedFragment(PhysicsSharedFragment);
	}
	else
	{
		UE_LOG(LogMassTraffic, Warning, TEXT("No PhysicsVehicleTemplateActor set for UMassTrafficVehicleSimulationTrait in %s. Vehicles will be forced to low simulation LOD!"), GetOuter() ? *GetOuter()->GetName() : TEXT("(?)"))
	}
}
