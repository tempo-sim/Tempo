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

namespace
{
	// The fragments a vehicle needs to be driven by Mass along the lane graph. Shared by
	// UMassTrafficVehicleSimulationTrait (when bMassControlled) and by the standalone
	// UMassTrafficVehicleSimulationMassControlTrait.
	void BuildMassControlledTemplate(
		FMassEntityTemplateBuildContext& BuildContext,
		const UWorld& World,
		const TSubclassOf<AWheeledVehiclePawn>& PhysicsVehicleTemplateActor,
		const UObject* TraitOuter)
	{
		FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

		BuildContext.RequireFragment<FMassTrafficVehicleSimulationParameters>();
		BuildContext.AddFragment<FMassTrafficInterpolationFragment>();
		BuildContext.AddFragment<FMassTrafficLaneOffsetFragment>();
		BuildContext.AddFragment<FMassTrafficNextVehicleFragment>();
		BuildContext.AddFragment<FMassTrafficObstacleAvoidanceFragment>();
		BuildContext.RequireFragment<FMassTrafficRandomFractionFragment>();
		BuildContext.AddFragment<FMassTrafficVehicleLaneChangeFragment>();
		BuildContext.RequireFragment<FMassTrafficVehicleLightsFragment>();
		BuildContext.AddFragment<FMassZoneGraphLaneLocationFragment>();

		UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(&World);
		check(MassTrafficSubsystem);

		if (PhysicsVehicleTemplateActor)
		{
			// Extract physics setup from PhysicsVehicleTemplateActor into shared fragment
			const FMassTrafficSimpleVehiclePhysicsTemplate* Template = MassTrafficSubsystem->GetOrExtractVehiclePhysicsTemplate(PhysicsVehicleTemplateActor);

			// Register & add shared fragment
			if (LIKELY(!BuildContext.IsInspectingData()))
			{
				const FConstSharedStruct PhysicsSharedFragment = EntityManager.GetOrCreateConstSharedFragment<FMassTrafficVehiclePhysicsSharedParameters>(FConstStructView::Make(*Template), Template);
				BuildContext.AddConstSharedFragment(PhysicsSharedFragment);
			}
			else
			{
				// in the investigation mode we only care about the fragment type
				const FConstSharedStruct PhysicsSharedFragment = EntityManager.GetOrCreateConstSharedFragment<FMassTrafficVehiclePhysicsSharedParameters>(Template);
				BuildContext.AddConstSharedFragment(PhysicsSharedFragment);
			}
		}
		else
		{
			UE_LOG(LogMassTraffic, Warning, TEXT("No PhysicsVehicleTemplateActor set for a Mass controlled traffic vehicle in %s. Vehicles will be forced to low simulation LOD!"), TraitOuter ? *TraitOuter->GetName() : TEXT("(?)"))
		}
	}
}

void UMassTrafficVehicleSimulationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(&World);
	check(MassTrafficSubsystem || BuildContext.IsInspectingData());

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
	VehicleControlFragment.bAllowLeftTurnsAtIntersections = Params.bAllowLeftTurnsAtIntersections;
	VehicleControlFragment.bAllowRightTurnsAtIntersections = Params.bAllowRightTurnsAtIntersections;
	VehicleControlFragment.bAllowGoingStraightAtIntersections = Params.bAllowGoingStraightAtIntersections;
	VehicleControlFragment.LaneChangePriorityFilters = Params.LaneChangePriorityFilters;
	VehicleControlFragment.NextLanePriorityFilters = Params.NextLanePriorityFilters;
	VehicleControlFragment.TurningLanePriorityFilters = Params.TurningLanePriorityFilters;

	// Variable tick
	BuildContext.AddFragment<FMassSimulationVariableTickFragment>();
	BuildContext.AddChunkFragment<FMassSimulationVariableTickChunkFragment>();

	const FConstSharedStruct VariableTickParamsFragment = EntityManager.GetOrCreateConstSharedFragment(VariableTickParams);
	BuildContext.AddConstSharedFragment(VariableTickParamsFragment);

	const FSharedStruct VariableTickSharedFragment = EntityManager.GetOrCreateSharedFragment<FMassSimulationVariableTickSharedFragment>(FConstStructView::Make(VariableTickParams), VariableTickParams);
	BuildContext.AddSharedFragment(VariableTickSharedFragment);

	// Various fragments
	BuildContext.AddFragment<FMassActorFragment>();
	BuildContext.AddFragment<FTransformFragment>();
	BuildContext.AddFragment<FMassTrafficAngularVelocityFragment>();
	BuildContext.AddFragment<FMassVelocityFragment>();

	IF_MASSTRAFFIC_ENABLE_DEBUG(BuildContext.RequireFragment<FMassTrafficDebugFragment>());

	if (bMassControlled)
	{
		const TSubclassOf<AWheeledVehiclePawn> ResolvedPhysicsVehicleTemplateActor = PhysicsParams.PhysicsVehicleTemplateActor
			? PhysicsParams.PhysicsVehicleTemplateActor
			: Params.PhysicsVehicleTemplateActor;

		BuildMassControlledTemplate(BuildContext, World, ResolvedPhysicsVehicleTemplateActor, GetOuter());
	}
}

UMassTrafficVehicleSimulationMassControlTrait::UMassTrafficVehicleSimulationMassControlTrait(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void UMassTrafficVehicleSimulationMassControlTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildMassControlledTemplate(BuildContext, World, PhysicsParams.PhysicsVehicleTemplateActor, GetOuter());
}
