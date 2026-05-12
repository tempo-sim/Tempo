// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoMovementController.h"

#include "CoreMinimal.h"

#include "TempoWheeledVehicleController.generated.h"

class UChaosVehicleMovementComponent;

// Controller for wheeled ground vehicles (Chaos or kinematic). Handles all three command modes:
//   * NormalizedDrivingCommand: open-loop throttle/steer.
//   * VelocityCommand: closed-loop tracking of a body-frame Twist (linear.x + angular.z).
//   * AccelerationCommand: closed-loop tracking of a body-frame Accel via integrated velocity target.
//
// Only linear.x and angular.z are honored; other axes are silently ignored.
UCLASS()
class TEMPOMOVEMENT_API ATempoWheeledVehicleController : public ATempoMovementController
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;

	virtual bool HandleDrivingInput(const FNormalizedDrivingInput& Input) override;
	virtual bool HandleVelocityCommand(const FTempoTwist& Twist) override;
	virtual bool HandleAccelerationCommand(const FTempoAccel& Accel) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bPersistSteering = true;

	// Watchdog timeout for closed-loop commands. After this many seconds without a fresh
	// velocity or acceleration command, the tracked setpoint is reset to zero (the vehicle
	// decelerates to a stop). Set to 0 to disable (latch forever).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin=0.0))
	float CommandStaleTimeout = 5.0;

	// P gain for linear velocity tracking: normalized accel per cm/s of error.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float LinearVelocityKp = 0.005;

	// I gain for linear velocity tracking: normalized accel per (cm/s * s) of accumulated error.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float LinearVelocityKi = 0.002;

	// Saturation for the integral term to prevent windup (cm/s * s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float LinearVelocityIMax = 200.0;

	// P gain for Chaos vehicle yaw rate tracking: normalized steering per deg/s of error.
	// Kinematic vehicles use exact feedforward (the inverse motion model) and ignore this.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float YawRateKp = 0.01745;

private:
	enum class EControlMode : uint8
	{
		None,
		Driving,
		Velocity,
		Acceleration,
	};

	EControlMode ControlMode = EControlMode::None;

	// Direct setpoint when in Velocity mode. In Acceleration mode this is the integrated
	// target updated each tick by AccelTarget.
	FTempoTwist VelocityTarget;

	// Commanded acceleration in Acceleration mode.
	FTempoAccel AccelTarget;

	// Time (UGameplayStatics::GetTimeSeconds) of the last velocity or acceleration command.
	double LastClosedLoopCommandTime = 0.0;

	// Integral state for linear velocity PI loop.
	float LinearVelocityIntegralError = 0.0;

	struct FLastDrivingInput
	{
		FNormalizedDrivingInput Input;
		uint64 Frame = 0;
	};
	TOptional<FLastDrivingInput> LastDrivingInput;

	void TickDriving(APawn* ControlledPawn);
	void TickClosedLoop(float DeltaTime, APawn* ControlledPawn);

	// Compute normalized acceleration in [-1, 1] from a target linear velocity (cm/s).
	float ComputeNormalizedAcceleration(float TargetLinVelCmS, float CurrentLinVelCmS, float DeltaTime);

	// Apply normalized throttle/brake/gear logic to a Chaos vehicle.
	void ApplyChaosAccelInput(UChaosVehicleMovementComponent* Movement, float NormAccel) const;

	void ApplyDrivingInputToChaosVehicle(UChaosVehicleMovementComponent* Movement, const FNormalizedDrivingInput& Input) const;
};
