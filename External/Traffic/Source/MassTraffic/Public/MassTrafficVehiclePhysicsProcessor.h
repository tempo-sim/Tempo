// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficProcessorBase.h"
#include "MassTrafficFragments.h"
#include "MassActorSubsystem.h"
#include "MassTrafficVehiclePhysicsProcessor.generated.h"


UCLASS()
class MASSTRAFFIC_API UMassTrafficVehiclePhysicsProcessor : public UMassTrafficProcessorBase
{
	GENERATED_BODY()

public:
	UMassTrafficVehiclePhysicsProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntitySubSystem, FMassExecutionContext& Context) override;

	bool ProcessSleeping(
		const FMassTrafficVehicleControlFragment& VehicleControlFragment,
		const FMassTrafficPIDVehicleControlFragment& PIDVehicleControlFragment,
		FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment,
		const FTransform& VehicleWorldTransform,
		bool bVisLog = false
	);
	
	void PerformSuspensionTraces(
		FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment,
		const FTransform& VehicleWorldTransform,
		const FTransform& RawLaneLocationTransform,
		TArray<FHitResult, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>>& OutSuspensionTraceHitResults,
		TArray<FVector, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>>& OutSuspensionTargets,
		bool bVisLog = false,
		FColor Color = FColor::Yellow
	);

	void SimulateDriveForces(
		const float DeltaTime,
		const float GravityZ,
		const FMassTrafficPIDVehicleControlFragment& PIDVehicleControlFragment,
		FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment,
		struct FMassVelocityFragment& VelocityFragment,
		FMassTrafficAngularVelocityFragment& AngularVelocityFragment,
		FTransformFragment& TransformFragment,
		const FTransform& VehicleWorldTransform,
		const TArray<FHitResult, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>>& SuspensionTraceHitResults,
		bool bVisLog
	);

	void SolveSuspensionConstraintsIteration(
		const float DeltaTime,
		FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment,
		FMassVelocityFragment& VelocityFragment,
		FMassTrafficAngularVelocityFragment& AngularVelocityFragment,
		FTransformFragment& TransformFragment,
		const FTransform& VehicleWorldTransform,
		const TArray<FVector, TFixedAllocator<FMassTrafficSimpleVehiclePhysicsSim::MaxWheels>>& SuspensionTargets,
		bool bVisLog
	);

	void ClampLateralDeviation(
		FTransformFragment& TransformFragment,
		const FTransform& RawLaneLocationTransform
	) const;

	void UpdateCoMVelocity(
		const float DeltaTime,
		const FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment,
		const FTransformFragment& TransformFragment,
		FMassVelocityFragment& VelocityFragment,
		FMassTrafficAngularVelocityFragment& AngularVelocityFragment,
		const FTransform& VehicleWorldTransform
	);

	void SetCoMWorldTransform(
		FMassTrafficVehiclePhysicsFragment& SimplePhysicsVehicleFragment,
		FTransformFragment& TransformFragment,
		const FVector& NewVehicleWorldCenterOfMass,
		const FQuat& NewVehicleWorldRotationOfMass);

	FMassEntityQuery SimplePhysicsVehiclesQuery;

	Chaos::FPBDJointSolverSettings ChaosConstraintSolverSettings;
	FMassTrafficSimpleTrailerConstraintSolver TrailerConstraintSolver;
};
