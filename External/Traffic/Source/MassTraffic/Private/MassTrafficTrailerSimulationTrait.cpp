// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficTrailerSimulationTrait.h"
#include "MassTrafficFragments.h"

#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassMovementFragments.h"
#include "MassSimulationLOD.h"
#include "MassTrafficSubsystem.h"
#include "MassEntityUtils.h"


UMassTrafficTrailerSimulationTrait::UMassTrafficTrailerSimulationTrait(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void UMassTrafficTrailerSimulationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	UMassTrafficSubsystem* MassTrafficSubsystem = UWorld::GetSubsystem<UMassTrafficSubsystem>(&World);
	check(MassTrafficSubsystem);

	// Cache FMassTrafficTrailerSimulationParameters::ConstraintSettings conversion to Chaos::FPBDJointSettings
	FMassTrafficTrailerSimulationParameters MutableParams = Params;
	MutableParams.ChaosJointSettings.AngularMotionTypes[static_cast<int32>(Chaos::EJointAngularConstraintIndex::Twist)] = Chaos::EJointMotionType::Locked;
	MutableParams.ChaosJointSettings.AngularMotionTypes[static_cast<int32>(Chaos::EJointAngularConstraintIndex::Swing1)] = Chaos::EJointMotionType::Limited;
	MutableParams.ChaosJointSettings.AngularMotionTypes[static_cast<int32>(Chaos::EJointAngularConstraintIndex::Swing2)] = Chaos::EJointMotionType::Limited;
	MutableParams.ChaosJointSettings.AngularLimits[static_cast<int32>(Chaos::EJointAngularConstraintIndex::Twist)] = 0.0f;
	MutableParams.ChaosJointSettings.AngularLimits[static_cast<int32>(Chaos::EJointAngularConstraintIndex::Swing1)] = FMath::DegreesToRadians(MutableParams.ConstraintSettings.AngularSwing1Limit);
	MutableParams.ChaosJointSettings.AngularLimits[static_cast<int32>(Chaos::EJointAngularConstraintIndex::Swing2)] = FMath::DegreesToRadians(MutableParams.ConstraintSettings.AngularSwing2Limit);
	MutableParams.ChaosJointSettings.ConnectorTransforms[0] = Chaos::FRigidTransform3(MutableParams.ConstraintSettings.MountPoint, Chaos::FRotation3::Identity);
	MutableParams.ChaosJointSettings.ConnectorTransforms[1] = MutableParams.ChaosJointSettings.ConnectorTransforms[0];

	// Add Params as shared fragment
	const FConstSharedStruct ParamsSharedFragment = EntityManager.GetOrCreateConstSharedFragment(MutableParams);
	BuildContext.AddConstSharedFragment(ParamsSharedFragment);

	// Simulation LOD
	FMassTrafficSimulationLODFragment& SimulationLODFragment = BuildContext.AddFragment_GetRef<FMassTrafficSimulationLODFragment>();
	SimulationLODFragment.LOD = EMassLOD::Off;
	SimulationLODFragment.PrevLOD = EMassLOD::Max;
	BuildContext.AddTag<FMassOffLODTag>();

	BuildContext.AddFragment<FMassSimulationVariableTickFragment>();
	BuildContext.AddChunkFragment<FMassSimulationVariableTickChunkFragment>();

	BuildContext.AddTag<FMassTrafficVehicleTrailerTag>();

	BuildContext.AddFragment<FMassActorFragment>();
	BuildContext.AddFragment<FTransformFragment>();
	BuildContext.AddFragment<FMassTrafficAngularVelocityFragment>();
	BuildContext.AddFragment<FMassTrafficConstrainedVehicleFragment>();
	BuildContext.AddFragment<FMassTrafficInterpolationFragment>();
	BuildContext.RequireFragment<FMassTrafficRandomFractionFragment>();
	BuildContext.AddFragment<FMassVelocityFragment>();

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
		UE_LOG(LogMassTraffic, Warning, TEXT("No PhysicsVehicleTemplateActor set for UMassTrafficTrailerSimulationTrait in %s. Trailers will be forced to low simulation LOD!"), GetOuter() ? *GetOuter()->GetName() : TEXT("(?)"))
	}
}
