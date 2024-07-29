// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTrafficPhysics.h"
#include "MassTraffic.h"
#include "MassTrafficVehicleControlInterface.h"

#include "WheeledVehiclePawn.h"
#include "Chaos/MassProperties.h"
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Components/SkeletalMeshComponent.h"
#include "Physics/Experimental/ChaosInterfaceUtils.h"

Chaos::FSimpleEngineConfig FMassTrafficSimpleVehiclePhysicsSim::DefaultEngineConfig;
Chaos::FSimpleDifferentialConfig FMassTrafficSimpleVehiclePhysicsSim::DefaultDifferentialConfig;
Chaos::FSimpleTransmissionConfig FMassTrafficSimpleVehiclePhysicsSim::DefaultTransmissionConfig;
Chaos::FSimpleSteeringConfig FMassTrafficSimpleVehiclePhysicsSim::DefaultSteeringConfig;
Chaos::FSimpleAerodynamicsConfig FMassTrafficSimpleVehiclePhysicsSim::DefaultAerodynamicsConfig;

void UE::MassTraffic::ExtractPhysicsVehicleConfig(
	// TSubclassOf<AWheeledVehiclePawn> PhysicsActorClass,
	AWheeledVehiclePawn* PhysicsActor,
	FMassTrafficSimpleVehiclePhysicsConfig& OutVehicleConfig,
	FMassTrafficSimpleVehiclePhysicsSim& OutVehicleSim
)
{
	// Set the speed here so we trigger the center of mass calculations.
	if (PhysicsActor->Implements<UMassTrafficVehicleControlInterface>())
	{
		IMassTrafficVehicleControlInterface::Execute_SetVehicleInputs(PhysicsActor, 0.0f, 1.0f, false, 0.0f, true);
	}

	// Artificially tick the actor for a pretend second to remove control input lag and let the center of mass
	// get moved around.
	for(int t=0; t<30; t++)
	{
		PhysicsActor->Tick(1.0f/30.0f);
	}

	UChaosWheeledVehicleMovementComponent* VehicleMovementComponent = Cast<UChaosWheeledVehicleMovementComponent>(PhysicsActor->GetVehicleMovementComponent());
	
	TUniquePtr<Chaos::FSimpleWheeledVehicle> SimpleWheeledVehicle = VehicleMovementComponent->CreatePhysicsVehicle();
	VehicleMovementComponent->SetupVehicle(SimpleWheeledVehicle);

	// Copy basic values
	FBodyInstance* BodyInstance = Cast<UPrimitiveComponent>(PhysicsActor->GetVehicleMovementComponent()->UpdatedComponent)->GetBodyInstance();
	if (!ensure(BodyInstance))
	{
		UE_LOG(LogMassTraffic, Error, TEXT("No root physics body found on %s to extract physics vehicle config from"), *VehicleMovementComponent->UpdatedComponent->GetName());
		return;
	}

	// Find the mass and center of mass for all the bodies that are NOT the root and we'll then use
	// that later to apply it. 
	OutVehicleConfig.PeripheralCenterOfMass = FVector::ZeroVector;
	OutVehicleConfig.PeripheralMass = 0.0f;
	for (FBodyInstance* BI : PhysicsActor->GetMesh()->Bodies)
	{
		if (BI != BodyInstance && BI->IsValidBodyInstance() && !BI->IsPhysicsDisabled() && BI->IsNonKinematic())
		{
			const float BodyMass = BI->GetBodyMass();
			OutVehicleConfig.PeripheralCenterOfMass += BodyMass * BI->GetCOMPosition();
			OutVehicleConfig.PeripheralMass += BodyMass;
		}
	}
	OutVehicleConfig.PeripheralCenterOfMass = PhysicsActor->GetActorTransform().InverseTransformPosition(OutVehicleConfig.PeripheralCenterOfMass);

	// Some vehicles have zero peripheral mass. Guard against a divide by zero.
	if (OutVehicleConfig.PeripheralMass > 0.0)
	{
		OutVehicleConfig.PeripheralCenterOfMass /= OutVehicleConfig.PeripheralMass;
	}

	// Remove all of this peripheral mass from our main body mass.
	OutVehicleConfig.Mass = PhysicsActor->GetMesh()->GetMass() - OutVehicleConfig.PeripheralMass;

	FTransform MassSpace = BodyInstance->GetMassSpaceToWorldSpace().GetRelativeTransform(PhysicsActor->GetActorTransform());
	OutVehicleConfig.RotationOfMass = MassSpace.GetRotation();
	OutVehicleConfig.CenterOfMass = PhysicsActor->GetActorTransform().InverseTransformPosition(PhysicsActor->GetMesh()->GetSkeletalCenterOfMass());

	// The root body may have an offset from the actor root. As we only ever simulate this single body we factor this
	// extra offset out by converting all the vehicle intrinsic transforms from body-local to actor space. This
	// way we can simply simulate the actor transform directly.
	OutVehicleConfig.BodyToActor = BodyInstance->GetUnrealWorldTransform().GetRelativeTransform(PhysicsActor->GetActorTransform());
	
	OutVehicleConfig.NumDrivenWheels = SimpleWheeledVehicle->NumDrivenWheels;
	OutVehicleConfig.LinearEtherDrag = BodyInstance->LinearDamping;
	OutVehicleConfig.InverseMomentOfInertia = FVector(1.0f) / BodyInstance->GetBodyInertiaTensor();

	// Set OutVehicleSim to use OutVehicleConfig
	OutVehicleSim.SetupPtr = &OutVehicleConfig;

	// Harvest sim parameters
	OutVehicleConfig.EngineConfig = SimpleWheeledVehicle->Engine[0].Setup();
	OutVehicleSim.EngineSim = SimpleWheeledVehicle->Engine[0];
	OutVehicleSim.EngineSim.SetupPtr = &OutVehicleConfig.EngineConfig;

	OutVehicleConfig.DifferentialConfig = SimpleWheeledVehicle->Differential[0].Setup();
	OutVehicleSim.DifferentialSim = SimpleWheeledVehicle->Differential[0];
	OutVehicleSim.DifferentialSim.SetupPtr = &OutVehicleConfig.DifferentialConfig;

	OutVehicleConfig.TransmissionConfig = SimpleWheeledVehicle->Transmission[0].Setup();
	OutVehicleSim.TransmissionSim = SimpleWheeledVehicle->Transmission[0];
	OutVehicleSim.TransmissionSim.SetupPtr = &OutVehicleConfig.TransmissionConfig;

	OutVehicleConfig.SteeringConfig = SimpleWheeledVehicle->Steering[0].Setup();
	OutVehicleSim.SteeringSim = SimpleWheeledVehicle->Steering[0];
	OutVehicleSim.SteeringSim.SetupPtr = &OutVehicleConfig.SteeringConfig;

	OutVehicleConfig.AerodynamicsConfig = SimpleWheeledVehicle->Aerodynamics[0].Setup();
	OutVehicleSim.AerodynamicsSim = SimpleWheeledVehicle->Aerodynamics[0];
	OutVehicleSim.AerodynamicsSim.SetupPtr = &OutVehicleConfig.AerodynamicsConfig;

	// Pre-allocate all Configs up-front so we have stable address for all of the to set in the sims
	OutVehicleConfig.AxleConfigs.SetNum(SimpleWheeledVehicle->Axles.Num());
	OutVehicleConfig.WheelConfigs.SetNum(SimpleWheeledVehicle->Wheels.Num());
	OutVehicleConfig.SuspensionConfigs.SetNum(SimpleWheeledVehicle->Suspension.Num());
	
	OutVehicleSim.AxleSims.Reset();
	for (int32 AxleIndex = 0; AxleIndex < SimpleWheeledVehicle->Axles.Num(); ++AxleIndex)
	{
		const Chaos::FAxleSim& AxleSim = SimpleWheeledVehicle->Axles[AxleIndex];

		Chaos::FAxleConfig& OutAxleConfig = OutVehicleConfig.AxleConfigs[AxleIndex];
		OutAxleConfig = AxleSim.Setup;

		Chaos::FAxleSim& OutAxleSim = OutVehicleSim.AxleSims.AddDefaulted_GetRef();
		OutAxleSim = AxleSim;

		// For the simulation fragment to be trivially relocatable, we redirect the SetupPtr to our stable pointer
		OutAxleSim.SetupPtr = &OutAxleConfig;
	}

	OutVehicleSim.WheelSims.Reset();
	OutVehicleConfig.MaxSteeringAngle = 0.0f;
	for (int32 WheelIndex = 0; WheelIndex < SimpleWheeledVehicle->Wheels.Num(); ++WheelIndex)
	{
		const Chaos::FSimpleWheelSim& WheelSim = SimpleWheeledVehicle->Wheels[WheelIndex];

		Chaos::FSimpleWheelConfig& OutWheelConfig = OutVehicleConfig.WheelConfigs[WheelIndex];
		OutWheelConfig = WheelSim.Setup();

		Chaos::FSimpleWheelSim& OutWheelSim = OutVehicleSim.WheelSims.Emplace_GetRef(&OutWheelConfig);
		OutWheelSim = WheelSim;
		OutWheelSim.SetupPtr = &OutWheelConfig;

		OutVehicleConfig.MaxSteeringAngle = FMath::Max(OutVehicleConfig.MaxSteeringAngle, FMath::DegreesToRadians(OutWheelConfig.MaxSteeringAngle));
	}

	OutVehicleSim.SuspensionSims.Reset();
	OutVehicleSim.WheelLocalLocations.Reset();
	for (int32 SuspensionIndex = 0; SuspensionIndex < SimpleWheeledVehicle->Suspension.Num(); ++SuspensionIndex)
	{
		const Chaos::FSimpleSuspensionSim& SuspensionSim = SimpleWheeledVehicle->Suspension[SuspensionIndex];

		Chaos::FSimpleSuspensionConfig& OutSuspensionConfig = OutVehicleConfig.SuspensionConfigs[SuspensionIndex];
		OutSuspensionConfig = SuspensionSim.Setup();

		// Add a 10m raycast safety margin so vehicles falling through the floor on a large DT can still find the
		// floor and push back up
		OutSuspensionConfig.RaycastSafetyMargin = 1000.0f;

		Chaos::FSimpleSuspensionSim& OutSuspensionSim = OutVehicleSim.SuspensionSims.Emplace_GetRef(&OutSuspensionConfig);
		OutSuspensionSim = SuspensionSim;
		OutSuspensionSim.SetupPtr = &OutSuspensionConfig;

		// The root body may have an offset from the actor root. As we only ever simulate this single body we factor this
		// extra offset out by converting all the vehicle intrinsic transforms from body-local to actor space. This
		// way we can simply simulate the actor transform directly.
		OutSuspensionSim.SetLocalRestingPosition(OutVehicleConfig.BodyToActor.TransformPosition(OutSuspensionSim.GetLocalRestingPosition()));
		
		OutVehicleSim.WheelLocalLocations.Add(OutSuspensionSim.GetLocalRestingPosition());
	}
}

void FMassTrafficSimpleTrailerConstraintSolver::Init(
	const float Dt,
	const Chaos::FPBDJointSolverSettings& SolverSettings,
	const Chaos::FPBDJointSettings& JointSettings,
	const FVector& PrevP0,
	const FVector& PrevP1,
	const FQuat& PrevQ0,
	const FQuat& PrevQ1,
	const float InvM0,
	const FVector& InvIL0,
	const float InvM1,
	const FVector& InvIL1,
	const FTransform& XL0,
	const FTransform& XL1
)
{
	XLs[0] = XL0;
	XLs[1] = XL1;
	
	InvILs[0] = JointSettings.ParentInvMassScale * InvIL0;
	InvILs[1] = InvIL1;
	InvMs[0] = JointSettings.ParentInvMassScale * InvM0;
	InvMs[1] = InvM1;
	
	check(InvMs[0] > 0.0f);
	check(InvMs[1] > 0.0f);
	
	Chaos::FPBDJointUtilities::ConditionInverseMassAndInertia(InvMs[0], InvMs[1], InvILs[0], InvILs[1], SolverSettings.MinParentMassRatio, SolverSettings.MaxInertiaRatio);
	
	// Tolerances are positional errors below visible detection. But in PBD the errors
	// we leave behind get converted to velocity, so we need to ensure that the resultant
	// movement from that erroneous velocity is less than the desired position tolerance.
	// Assume that the tolerances were defined for a 60Hz simulation, then it must be that
	// the position error is less than the position change from constant external forces
	// (e.g., gravity). So, we are saying that the tolerance was chosen because the position
	// error is less that F.dt^2. We need to scale the tolerance to work at our current dt.
	const float ToleranceScale = FMath::Min(1.f, 60.f * 60.f * Dt * Dt);
	PositionTolerance = ToleranceScale * SolverSettings.PositionTolerance;
	AngleTolerance = ToleranceScale * SolverSettings.AngleTolerance;

	// @see FJointSolverGaussSeidel::InitDerivedState();
	{
		Xs[0] = PrevP0 + PrevQ0 * XLs[0].GetTranslation();
		Rs[0] = PrevQ0 * XLs[0].GetRotation();
		InvIs[0] = Chaos::Utilities::ComputeWorldSpaceInertia(PrevQ0, InvILs[0]);

		Xs[1] = PrevP1 + PrevQ1 * XLs[1].GetTranslation();
		Rs[1] = PrevQ1 * XLs[1].GetRotation();
		Rs[1].EnforceShortestArcWith(Rs[0]);
		InvIs[1] = Chaos::Utilities::ComputeWorldSpaceInertia(PrevQ1, InvILs[1]);
	}

	bIsActive = true;

	SolverStiffness = 1.0f;
}

void FMassTrafficSimpleTrailerConstraintSolver::Update(
	const int32 It, const int32 NumIts,
	const Chaos::FPBDJointSolverSettings& SolverSettings,
	const FVector& P0, const FQuat& Q0, const FVector& V0, const FVector& W0, const FVector& P1, const FQuat& Q1,
	const FVector& V1, const FVector& W1
)
{
	// @see FJointSolverGaussSeidel::Update
	Ps[0] = P0;
	Ps[1] = P1;
	Qs[0] = Q0;
	Qs[1] = Q1;
	Qs[1].EnforceShortestArcWith(Qs[0]);

	Vs[0] = V0;
	Vs[1] = V1;
	Ws[0] = W0;
	Ws[1] = W1;

	SolverStiffness = CalculateIterationStiffness(It, NumIts, SolverSettings);
		
	UpdateDerivedState();

	UpdateIsActive();
}

void FMassTrafficSimpleTrailerConstraintSolver::ApplyConstraints(
	const float Dt,
	const Chaos::FPBDJointSolverSettings& SolverSettings,
	const Chaos::FPBDJointSettings& JointSettings)
{
	ApplyPositionConstraints(Dt, SolverSettings, JointSettings);
	ApplyRotationConstraints(Dt, SolverSettings, JointSettings);

	UpdateIsActive();
}

void FMassTrafficSimpleTrailerConstraintSolver::UpdateDerivedState()
{
	// @see FJointSolverGaussSeidel::UpdateDerivedState()
		
	// Kinematic bodies will not be moved, so we don't update derived state during iterations
	if (InvMs[0] > 0.0f)
	{
		Xs[0] = Ps[0] + Qs[0] * XLs[0].GetTranslation();
		Rs[0] = Qs[0] * XLs[0].GetRotation();
		InvIs[0] = Chaos::Utilities::ComputeWorldSpaceInertia(Qs[0], InvILs[0]);
	}
	if (InvMs[1] > 0.0f)
	{
		Xs[1] = Ps[1] + Qs[1] * XLs[1].GetTranslation();
		Rs[1] = Qs[1] * XLs[1].GetRotation();
		InvIs[1] = Chaos::Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]);
	}
	Rs[1].EnforceShortestArcWith(Rs[0]);
}

bool FMassTrafficSimpleTrailerConstraintSolver::UpdateIsActive()
{
	// NumActiveConstraints is initialized to -1, so there's no danger of getting invalid LastPs/Qs
	// We also check SolverStiffness mainly for testing when solver stiffness is 0 (so we don't exit immediately)
	if (SolverStiffness > 0.0f)
	{
		bool bIsSolved =
			Chaos::FVec3::IsNearlyEqual(Ps[0], LastPs[0], PositionTolerance)
			&& Chaos::FVec3::IsNearlyEqual(Ps[1], LastPs[1], PositionTolerance)
			&& Chaos::FRotation3::IsNearlyEqual(Qs[0], LastQs[0], 0.5f * AngleTolerance)
			&& Chaos::FRotation3::IsNearlyEqual(Qs[1], LastQs[1], 0.5f * AngleTolerance);
		bIsActive = !bIsSolved;
	}

	LastPs[0] = Ps[0];
	LastPs[1] = Ps[1];
	LastQs[0] = Qs[0];
	LastQs[1] = Qs[1];

	return bIsActive;
}

void FMassTrafficSimpleTrailerConstraintSolver::ApplyPositionConstraints(const float Dt, const Chaos::FPBDJointSolverSettings& SolverSettings, const Chaos::FPBDJointSettings& JointSettings)
{
	// @See FJointSolverGaussSeidel::ApplyPointPositionConstraintDD

	const float LinearStiffness = /*Chaos::FPBDJointUtilities::GetLinearStiffness*/(SolverSettings.LinearStiffnessOverride >= 0.0f) ? SolverSettings.LinearStiffnessOverride : JointSettings.Stiffness; 
	const float Stiffness = SolverStiffness * LinearStiffness;
	const FVector CX = Xs[1] - Xs[0];
	
	if (CX.SizeSquared() > PositionTolerance * PositionTolerance)
	{
		// Calculate constraint correction
		Chaos::FVec3 Delta0 = Xs[0] - Ps[0];
		Chaos::FVec3 Delta1 = Xs[1] - Ps[1];
		Chaos::FMatrix33 M0 = Chaos::Utilities::ComputeJointFactorMatrix(Delta0, InvIs[0], InvMs[0]);
		Chaos::FMatrix33 M1 = Chaos::Utilities::ComputeJointFactorMatrix(Delta1, InvIs[1], InvMs[1]);
		Chaos::FMatrix33 MI = (M0 + M1).Inverse();
		const FVector DX = Stiffness * Chaos::Utilities::Multiply(MI, CX);
	
		// Apply constraint correction
		const FVector DP0 = InvMs[0] * DX;
		const FVector DP1 = -InvMs[1] * DX;
		const FVector DR0 = Chaos::Utilities::Multiply(InvIs[0], FVector::CrossProduct(Xs[0] - Ps[0], DX));
		const FVector DR1 = Chaos::Utilities::Multiply(InvIs[1], FVector::CrossProduct(Xs[1] - Ps[1], -DX));
	
		ApplyPositionDelta(DP0, DP1);
		ApplyRotationDelta(DR0, DR1);
	}
}

void FMassTrafficSimpleTrailerConstraintSolver::ApplyRotationConstraints(const float Dt, const Chaos::FPBDJointSolverSettings& SolverSettings, const Chaos::FPBDJointSettings& JointSettings)
{
	// We only support a very specific constraint type useful for trailers.
	check(JointSettings.AngularMotionTypes[(int32)Chaos::EJointAngularConstraintIndex::Twist] == Chaos::EJointMotionType::Locked);
	check(JointSettings.AngularMotionTypes[(int32)Chaos::EJointAngularConstraintIndex::Swing1] == Chaos::EJointMotionType::Limited);
	check(JointSettings.AngularMotionTypes[(int32)Chaos::EJointAngularConstraintIndex::Swing2] == Chaos::EJointMotionType::Limited);
	check(!JointSettings.bSoftSwingLimitsEnabled);
	
	// @See FJointSolverGaussSeidel::ApplyRotationConstraints
	{
		// @see FJointSolverGaussSeidel::ApplyConeConstraint
		{
			FVector SwingAxisLocal;
			float DSwingAngle = 0.0f;
			
			const float Swing1Limit = FMath::Max(JointSettings.AngularLimits[(int32)Chaos::EJointAngularConstraintIndex::Swing1], 0.0f);
			const float Swing2Limit = FMath::Max(JointSettings.AngularLimits[(int32)Chaos::EJointAngularConstraintIndex::Swing2], 0.0f);
			GetEllipticalConeAxisErrorLocal(Rs[0], Rs[1], Swing2Limit, Swing1Limit, SwingAxisLocal, DSwingAngle);
			
			const FVector SwingAxis = Rs[0] * SwingAxisLocal;
			
			// Apply swing correction to each body
			if (DSwingAngle > AngleTolerance)
			{
				float SwingStiffness = GetSwingStiffness(SolverSettings, JointSettings);
				// For cone constraints, the lambda are all accumulated in Swing2

				ApplyRotationConstraintDD(SwingStiffness, SwingAxis, DSwingAngle);
			}
		}
	
		// Note: single-swing locks are already handled above so we only need to do something here if both are locked
		// @see ApplyLockedRotationConstraints(Dt, SolverSettings, JointSettings, /*bLockedTwist*/true, /*bLockedSwing*/false);
		{
			FVector Axis0, Axis1, Axis2;
			GetLockedRotationAxes(Rs[0], Rs[1], Axis0, Axis1, Axis2);

			const FQuat R01 = Rs[0].Inverse() * Rs[1];

			const float TwistStiffness = GetTwistStiffness(SolverSettings, JointSettings);
			ApplyRotationConstraintDD(TwistStiffness, Axis0, R01.X);
		}
	}
}


void FMassTrafficSimpleTrailerConstraintSolver::ApplyRotationConstraintDD(const float JointStiffness,
	const FVector& Axis, const float Angle)
{
	// @see FJointSolverGaussSeidel::ApplyRotationConstraintDD
	
	const float Stiffness = SolverStiffness * JointStiffness;

	// Joint-space inverse mass
	const FVector IA0 = Chaos::Utilities::Multiply(InvIs[0], Axis);
	const FVector IA1 = Chaos::Utilities::Multiply(InvIs[1], Axis);
	const float II0 = FVector::DotProduct(Axis, IA0);
	const float II1 = FVector::DotProduct(Axis, IA1);

	const float DR = Stiffness * Angle / (II0 + II1);
	const FVector DR0 = IA0 * DR;
	const FVector DR1 = IA1 * -DR;

	ApplyRotationDelta(DR0, DR1);
}

float FMassTrafficSimpleTrailerConstraintSolver::CalculateIterationStiffness(int32 It, int32 NumIts,
                                                                             const Chaos::FPBDJointSolverSettings& Settings)
{
	// @see FPBDJointConstraints::CalculateIterationStiffness
	
	// Linearly interpolate betwwen MinStiffness and MaxStiffness over the first few iterations,
	// then clamp at MaxStiffness for the final NumIterationsAtMaxStiffness
	float IterationStiffness = Settings.MaxSolverStiffness;
	if (NumIts > Settings.NumIterationsAtMaxSolverStiffness)
	{
		const float Interpolant = FMath::Clamp((float)It / (float)(NumIts - Settings.NumIterationsAtMaxSolverStiffness), 0.0f, 1.0f);
		IterationStiffness = FMath::Lerp(Settings.MinSolverStiffness, Settings.MaxSolverStiffness, Interpolant);
	}
	return FMath::Clamp(IterationStiffness, 0.0f, 1.0f);
}

void FMassTrafficSimpleTrailerConstraintSolver::ApplyPositionDelta(const FVector& DP0, const FVector& DP1)
{
	// @See FJointSolverGaussSeidel::ApplyPositionDelta
	
	Ps[0] += DP0;
	Ps[1] += DP1;
	
	Xs[0] += DP0;
	Xs[1] += DP1;
}

void FMassTrafficSimpleTrailerConstraintSolver::ApplyRotationDelta(const FVector& DR0, const FVector& DR1)
{
	// @See FJointSolverGaussSeidel::ApplyRotationDelta
	
	const FQuat DQ0 = (Chaos::FRotation3::FromElements(DR0, 0) * Qs[0]) * 0.5f;
	Qs[0] = (Qs[0] + DQ0).GetNormalized();
		
	const FQuat DQ1 = (Chaos::FRotation3::FromElements(DR1, 0) * Qs[1]) * 0.5f;
	Qs[1] = (Qs[1] + DQ1).GetNormalized();
	Qs[1].EnforceShortestArcWith(Qs[0]);
	
	UpdateDerivedState();
}

void FMassTrafficSimpleTrailerConstraintSolver::GetLockedRotationAxes(const FQuat& R0, const FQuat& R1, FVector& Axis0,
	FVector& Axis1, FVector& Axis2)
{
	const float W0 = R0.W;
	const float W1 = R1.W;
	const FVector V0 = FVector(R0.X, R0.Y, R0.Z);
	const FVector V1 = FVector(R1.X, R1.Y, R1.Z);

	const FVector C = V1 * W0 + V0 * W1;
	const float D0 = W0 * W1;
	const float D1 = FVector::DotProduct(V0, V1);
	const float D = D0 - D1;

	Axis0 = 0.5f * (V0 * V1.X + V1 * V0.X + FVector(D, C.Z, -C.Y));
	Axis1 = 0.5f * (V0 * V1.Y + V1 * V0.Y + FVector(-C.Z, D, C.X));
	Axis2 = 0.5f * (V0 * V1.Z + V1 * V0.Z + FVector(C.Y, -C.X, D));

	// Handle degenerate case of 180 deg swing
	if (FMath::Abs(D0 + D1) < SMALL_NUMBER)
	{
		const float Epsilon = SMALL_NUMBER;
		Axis0.X += Epsilon;
		Axis1.Y += Epsilon;
		Axis2.Z += Epsilon;
	}
}

void FMassTrafficSimpleTrailerConstraintSolver::GetEllipticalConeAxisErrorLocal(
	const FQuat& R0, const FQuat& R1,
	const float SwingLimitY, const float SwingLimitZ, FVector& AxisLocal, float& Error
)
{
	if (FMath::IsNearlyEqual(SwingLimitY, SwingLimitZ, 1.e-3f))
	{ 
		GetCircularConeAxisErrorLocal(R0, R1, SwingLimitY, AxisLocal, Error);
		return;
	}

	AxisLocal = Chaos::FJointConstants::Swing1Axis();
	Error = 0.;

	FQuat R01Twist, R01Swing;
	DecomposeSwingTwistLocal(R0, R1, R01Swing, R01Twist);

	const FVector2D SwingAngles = FVector2D(/*GetSwingAngleY*/ 4.0f * FMath::Atan2(R01Swing.Y, 1.0f + R01Swing.W), /*GetSwingAngleZ*/4.0f * FMath::Atan2(R01Swing.Z, 1.0f + R01Swing.W));
	const FVector2D SwingLimits = FVector2D(SwingLimitY, SwingLimitZ);

	// Transform onto a circle to see if we are within the ellipse
	const FVector2D CircleMappedAngles = SwingAngles / SwingLimits;
	if (CircleMappedAngles.SizeSquared() > 1.0f)
	{
		// Map the swing to a position on the elliptical limits
		const FVector2D ClampedSwingAngles = NearPointOnEllipse(SwingAngles, SwingLimits);

		// Get the ellipse normal
		const FVector2D ClampedNormal = ClampedSwingAngles / (SwingLimits * SwingLimits);

		// Calculate the axis and error
		const FVector TwistAxis = R01Swing.GetAxisX();
		const FVector SwingRotAxis = FVector(0.0f, FMath::Tan(ClampedSwingAngles.X / 4.0f), FMath::Tan(ClampedSwingAngles.Y / 4.0f));
		const FVector EllipseNormal = FVector(0.0f, ClampedNormal.X, ClampedNormal.Y);
		GetEllipticalAxisError(SwingRotAxis, EllipseNormal, TwistAxis, AxisLocal, Error);
	}
}

void FMassTrafficSimpleTrailerConstraintSolver::GetCircularConeAxisErrorLocal(
	const FQuat& R0, const FQuat& R1,
	const float SwingLimit, FVector& AxisLocal, float& Error
)
{
	// @see FPBDJointUtilities::GetCircularConeAxisErrorLocal
		
	FQuat R01Twist, R01Swing;
	DecomposeSwingTwistLocal(R0, R1, R01Swing, R01Twist);

	// @see Chaos::TRotation::ToAxisAndAngleSafe
	float Angle = R01Swing.GetAngle();
	// @see Chaos::TRotation::GetRotationAxisSafe(TVector<FReal, 3>& OutAxis, const TVector<FReal, 3>& DefaultAxis, FReal EpsilionSq = 1e-6f) const
	{
		// Tolerance must be much larger than error in normalized vector (usually ~1e-4) for the 
		// axis calculation to succeed for small angles. For small angles, W ~= 1, and
		// X, Y, Z ~= 0. If the values of X, Y, Z are around 1e-4 we are just normalizing error.
		const float LenSq = R01Swing.X * R01Swing.X + R01Swing.Y * R01Swing.Y + R01Swing.Z * R01Swing.Z;
		if (LenSq > /*EpsilionSq*/1.e-6f)
		{
			float InvLen = FMath::InvSqrt(LenSq);
			AxisLocal = FVector(R01Swing.X * InvLen, R01Swing.Y * InvLen, R01Swing.Z * InvLen);
		}
		else
		{
			AxisLocal = Chaos::FJointConstants::Swing1Axis();
		}
	}

	Error = 0.0f;
	if (Angle > SwingLimit)
	{
		Error = Angle - SwingLimit;
	}
	else if (Angle < -SwingLimit)
	{
		Error = Angle + SwingLimit;
	}
}

FVector2D FMassTrafficSimpleTrailerConstraintSolver::NearPointOnEllipse(
	const FVector2D& P, const FVector2D& R,
	const int MaxIts, const float Tolerance
)
{
	// Map point into first quadrant
	FVector2D PAbs = P.GetAbs();

	// Check for point on minor axis
	const float Epsilon = 1.e-6f;
	if (R.X >= R.Y)
	{
		if (PAbs.Y < Epsilon)
		{
			return FVector2D((P.X > 0.0f) ? R.X : -R.X, 0.0f);
		}
	}
	else
	{
		if (PAbs.X < Epsilon)
		{
			return FVector2D(0.0f, (P.Y > 0.0f)? R.Y : -R.Y);
		}
	}

	// Iterate to find nearest point
	FVector2D R2 = R * R;
	FVector2D RP = R * PAbs;
	float T = FMath::Max(RP.X - R2.X, RP.Y - R2.Y);
	FVector2D D;
	for (int32 It = 0; It < MaxIts; ++It)
	{
		D = FVector2D(1.0f / (T + R2.X), 1 / (T + R2.Y));
		FVector2D RPD = RP * D;

		FVector2D FV = RPD * RPD;
		float F = FV.X + FV.Y - 1.0f;

		if (F < Tolerance)
		{
			return (R2 * P) * D;
		}

		float DF = -2.0f * FVector2D::DotProduct(FV, D);
		T = T - F / DF;
	}

	// Too many iterations - return current value clamped
	FVector2D S = (R2 * P) * D;
	FVector2D SN = S / R;	
	return S / SN.Size();
}

bool FMassTrafficSimpleTrailerConstraintSolver::GetEllipticalAxisError(
	const FVector& SwingAxisRot,
	const FVector& EllipseNormal, const FVector& TwistAxis, FVector& AxisLocal, float& Error
)
{
	const float R2 = SwingAxisRot.SizeSquared();
	const float A = 1.0f - R2;
	const float B = 1.0f / (1.0f + R2);
	const float B2 = B * B;
	const float V1 = 2.0f * A * B2;
	const FVector V2 = FVector(A, 2.0f * SwingAxisRot.Z, -2.0f * SwingAxisRot.Y);
	const float RD = FVector::DotProduct(SwingAxisRot, EllipseNormal);
	const float DV1 = -4.0f * RD * (3.0f - R2) * B2 * B;
	const FVector DV2 = FVector(-2.0f * RD, 2.0f * EllipseNormal.Z, -2.0f * EllipseNormal.Y);
		
	const FVector Line = V1 * V2 - FVector(1, 0, 0);
	FVector Normal = V1 * DV2 + DV1 * V2;
	if (Normal.Normalize())
	{
		AxisLocal = FVector::CrossProduct(Line, Normal);
		Error = -FVector::DotProduct(FVector::CrossProduct(Line, AxisLocal), TwistAxis);
		return true;
	}
	return false;
}