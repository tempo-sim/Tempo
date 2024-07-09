// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "MassTraffic.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "SimpleVehicle.h"
#include "SuspensionUtility.h"
#include "WheeledVehiclePawn.h"
#include "Chaos/PBDJointConstraintTypes.h"
#include "MassTrafficPhysics.generated.h"


USTRUCT()
struct MASSTRAFFIC_API FMassTrafficSimpleVehiclePhysicsConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	float Mass = 0.0f;

	UPROPERTY(EditAnywhere)
	float PeripheralMass = 0.0f;

	UPROPERTY(EditAnywhere)
	float LinearEtherDrag = 0.01f;

	UPROPERTY(EditAnywhere)
	float MaxSteeringAngle = 0.0f;
	
	UPROPERTY(EditAnywhere)
	uint8 NumDrivenWheels = 0;
	
	UPROPERTY(EditAnywhere)
	FVector InverseMomentOfInertia = FVector::ZeroVector;
	
	UPROPERTY(EditAnywhere)
	FVector CenterOfMass = FVector::ZeroVector;

	UPROPERTY(EditAnywhere)
	FVector PeripheralCenterOfMass = FVector::ZeroVector;

	UPROPERTY(EditAnywhere)
	FQuat RotationOfMass = FQuat::Identity;

	UPROPERTY(EditAnywhere)
	FTransform BodyToActor;

	Chaos::FSimpleEngineConfig EngineConfig;
	Chaos::FSimpleDifferentialConfig DifferentialConfig;
	Chaos::FSimpleTransmissionConfig TransmissionConfig;
	Chaos::FSimpleSteeringConfig SteeringConfig;
	Chaos::FSimpleAerodynamicsConfig AerodynamicsConfig;
	
	static constexpr int32 MaxAxles = 2;
	TArray<Chaos::FAxleConfig, TFixedAllocator<MaxAxles>> AxleConfigs;
	
	static constexpr int32 MaxWheels = 6;
	TArray<Chaos::FSimpleWheelConfig, TFixedAllocator<MaxWheels>> WheelConfigs;
	TArray<Chaos::FSimpleSuspensionConfig, TFixedAllocator<MaxWheels>> SuspensionConfigs;
};

USTRUCT()
struct MASSTRAFFIC_API FMassTrafficSimpleVehiclePhysicsSim
{
	GENERATED_BODY()

private:
	
	// Default config's just to satisfy need to pass one to the sim structs below in our UStruct mandated 
	// default constructor below
	static Chaos::FSimpleEngineConfig DefaultEngineConfig; 
	static Chaos::FSimpleDifferentialConfig DefaultDifferentialConfig; 
	static Chaos::FSimpleTransmissionConfig DefaultTransmissionConfig; 
	static Chaos::FSimpleSteeringConfig DefaultSteeringConfig; 
	static Chaos::FSimpleAerodynamicsConfig DefaultAerodynamicsConfig;

public:
	
	FMassTrafficSimpleVehiclePhysicsSim(
		const FMassTrafficSimpleVehiclePhysicsConfig* SetupIn = nullptr,
		const Chaos::FSimpleEngineConfig* EngineConfig = &DefaultEngineConfig, 
		const Chaos::FSimpleDifferentialConfig* DifferentialConfig = &DefaultDifferentialConfig, 
		const Chaos::FSimpleTransmissionConfig* TransmissionConfig = &DefaultTransmissionConfig, 
		const Chaos::FSimpleSteeringConfig* SteeringConfig = &DefaultSteeringConfig, 
		const Chaos::FSimpleAerodynamicsConfig* AerodynamicsConfig = &DefaultAerodynamicsConfig)
	: SetupPtr(SetupIn)
	, EngineSim(EngineConfig)
	, DifferentialSim(DifferentialConfig)
	, TransmissionSim(TransmissionConfig)
	, SteeringSim(SteeringConfig)
	, AerodynamicsSim(AerodynamicsConfig)
	{}

	FORCEINLINE FMassTrafficSimpleVehiclePhysicsConfig& AccessSetup()
	{
		check(SetupPtr != nullptr);
		return (FMassTrafficSimpleVehiclePhysicsConfig&)(*SetupPtr);
	}

	FORCEINLINE const FMassTrafficSimpleVehiclePhysicsConfig& Setup() const
	{
		check(SetupPtr != nullptr);
		return (*SetupPtr);
	}

	const FMassTrafficSimpleVehiclePhysicsConfig* SetupPtr;

	Chaos::FSimpleEngineSim EngineSim;
	Chaos::FSimpleDifferentialSim DifferentialSim;
	Chaos::FSimpleTransmissionSim TransmissionSim;
	Chaos::FSimpleSteeringSim SteeringSim;
	Chaos::FSimpleAerodynamicsSim AerodynamicsSim;

	static constexpr int32 MaxAxles = 2;
	TArray<Chaos::FAxleSim, TFixedAllocator<MaxAxles>> AxleSims;
	
	static constexpr int32 InlineWheels = 4;
	static constexpr int32 MaxWheels = 6;
	TArray<Chaos::FSimpleWheelSim, TInlineAllocator<InlineWheels>> WheelSims;
	TArray<Chaos::FSimpleSuspensionSim, TInlineAllocator<InlineWheels>> SuspensionSims;
	
	TArray<FVector, TInlineAllocator<InlineWheels>> WheelLocalLocations;

	uint8 SleepCounter = 0;

	/**
	 * @see IncrSleepCounter
	 * @return true if SleepCounter >= SleepCounterThreshold
	 */
	FORCEINLINE bool IsSleeping() const
	{
		return SleepCounter >= GMassTrafficSleepCounterThreshold;
	}

	/** Resets SleepCounter to 0 */ 
	FORCEINLINE void WakeFromSleep()
	{
		SleepCounter = 0;
	}

	/** Increment SleepCounter and return true if we have reached SleepCounterThreshold and should now sleep */ 
	FORCEINLINE bool IncrementSleepCounter()
	{
		++SleepCounter;
		if (SleepCounter >= GMassTrafficSleepCounterThreshold)
		{
			// Clamp SleepCounter to GMassTrafficSleepCounterThreshold so we don't keep incrementing and overflow  
			SleepCounter = GMassTrafficSleepCounterThreshold;
			return true;
		}

		return false;
	}
};

/** Simple Physics Fragment */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficVehiclePhysicsFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassTrafficSimpleVehiclePhysicsSim VehicleSim;
};

/**
 * Physics config & pre-configured sim extracted from a AWheeledVehiclePawn
 * @see UMassTrafficSubsystem::GetOrExtractVehiclePhysicsTemplate
 */
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficSimpleVehiclePhysicsTemplate
{
	GENERATED_BODY()
	
	/** The AWheeledVehiclePawn this was extracted from */
	UPROPERTY()
	TSubclassOf<AWheeledVehiclePawn> PhysicsVehicleTemplateActor;

	/**
	 * FMassTrafficSimpleVehiclePhysicsConfig referred to by SimpleVehiclePhysicsFragmentTemplate's
	 * FMassTrafficSimpleVehiclePhysicsSim
	 */
	UPROPERTY()
	FMassTrafficSimpleVehiclePhysicsConfig SimpleVehiclePhysicsConfig;

	/**
	 * FMassTrafficVehiclePhysicsFragment template fragment containing a pre-configured
	 * FMassTrafficSimpleVehiclePhysicsSim using SimpleVehiclePhysicsConfig's extracted config
	 */
	UPROPERTY()
	FMassTrafficVehiclePhysicsFragment SimpleVehiclePhysicsFragmentTemplate;
};

/** Simplified version of FJointSolverGaussSeidel */  
USTRUCT()
struct MASSTRAFFIC_API FMassTrafficSimpleTrailerConstraintSolver
{
	GENERATED_BODY()

	void Init(
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
		const FTransform& XL1);

	void Update(
		const int32 It,
		const int32 NumIts,
		const Chaos::FPBDJointSolverSettings& SolverSettings,
		const FVector& P0,
		const FQuat& Q0,
		const FVector& V0,
		const FVector& W0,
		const FVector& P1,
		const FQuat& Q1,
		const FVector& V1,
		const FVector& W1);

	void ApplyConstraints(
		const float Dt,
		const Chaos::FPBDJointSolverSettings& SolverSettings,
		const Chaos::FPBDJointSettings& JointSettings);

	FORCEINLINE bool GetIsActive() const
	{
		return bIsActive;
	}

	FORCEINLINE const FVector& GetP(const int32 Index) const
	{
		return Ps[Index];
	}

	FORCEINLINE const FQuat& GetQ(const int32 Index) const
	{
		return Qs[Index];
	}

private:

	void UpdateDerivedState();

	bool UpdateIsActive();

	void ApplyPositionConstraints(
		float Dt,
		const Chaos::FPBDJointSolverSettings& SolverSettings,
		const Chaos::FPBDJointSettings& JointSettings);
	
	void ApplyRotationConstraints(float Dt,
		const Chaos::FPBDJointSolverSettings& SolverSettings,
		const Chaos::FPBDJointSettings& JointSettings);
	
	static float CalculateIterationStiffness(int32 It, int32 NumIts, const Chaos::FPBDJointSolverSettings& Settings);

	void ApplyPositionDelta(const FVector& DP0,	const FVector& DP1);
	void ApplyRotationDelta(const FVector& DR0, const FVector& DR1);

	void GetLockedRotationAxes(const FQuat& R0, const FQuat& R1, FVector& Axis0, FVector& Axis1, FVector& Axis2);

	float GetTwistStiffness(
		const Chaos::FPBDJointSolverSettings& SolverSettings,
		const Chaos::FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.TwistStiffnessOverride >= 0.0f) ? SolverSettings.TwistStiffnessOverride : JointSettings.Stiffness;
	}

	float GetSwingStiffness(
		const Chaos::FPBDJointSolverSettings& SolverSettings,
		const Chaos::FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.SwingStiffnessOverride >= 0.0f) ? SolverSettings.SwingStiffnessOverride : JointSettings.Stiffness;
	}

	void ApplyRotationConstraintDD(
		const float JointStiffness,
		const FVector& Axis,
		const float Angle);

	static void GetEllipticalConeAxisErrorLocal(
		const FQuat& R0,
		const FQuat& R1,
		const float SwingLimitY,
		const float SwingLimitZ,
		FVector& AxisLocal,
		float& Error);

	static void GetCircularConeAxisErrorLocal(
		const FQuat& R0,
		const FQuat& R1,
		const float SwingLimit,
		FVector& AxisLocal,
		float& Error);

	static FVector2D NearPointOnEllipse(const FVector2D& P, const FVector2D& R, const int MaxIts = 20, const float Tolerance = 1.e-4f);

	static bool GetEllipticalAxisError(const FVector& SwingAxisRot, const FVector& EllipseNormal, const FVector& TwistAxis, FVector& AxisLocal, float& Error);

	FORCEINLINE static void DecomposeSwingTwistLocal(const FQuat& R0, const FQuat& R1, FQuat& R01Swing, FQuat& R01Twist)
	{
		const Chaos::FRotation3 R01 = R0.Inverse() * R1;
		R01.ToSwingTwistX(R01Swing, R01Twist);
	}

	bool bIsActive = true;

	float SolverStiffness = 0.0f;
	float PositionTolerance = 0.0f;
	float AngleTolerance = 0.0f;

	/**
	 * Local-space constraint settings
	 */
	FTransform XLs[2];						// Local-space joint connector transforms
	Chaos::FVec3 InvILs[2];					// Local-space inverse inertias
	Chaos::FReal InvMs[2] = {0.0f, 0.0f};	// Inverse masses

	/**
	 * World-space constraint state
	 */
	FVector Xs[2];		// World-space joint connector positions
	FQuat Rs[2];		// World-space joint connector rotations

	/**
	 * World-space body state
	 */
	FVector Ps[2];				// World-space particle CoM positions
	FQuat Qs[2];				// World-space particle CoM rotations
	FVector Vs[2];				// World-space particle CoM velocities
	FVector Ws[2];				// World-space particle CoM angular velocities
	Chaos::FMatrix33 InvIs[2];	// World-space inverse inertias

	FVector LastPs[2];	// Positions at the beginning of the iteration
	FQuat LastQs[2];	// Rotations at the beginning of the iteration
};

namespace UE::MassTraffic
{
MASSTRAFFIC_API void ExtractPhysicsVehicleConfig(
	AWheeledVehiclePawn* PhysicsActor,
	FMassTrafficSimpleVehiclePhysicsConfig& OutVehicleConfig,
	FMassTrafficSimpleVehiclePhysicsSim& OutVehicleSim
);
}
