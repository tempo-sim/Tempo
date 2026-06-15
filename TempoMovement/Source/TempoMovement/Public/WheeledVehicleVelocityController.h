// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoMovementTypes.h"

#include "CoreMinimal.h"

#include "WheeledVehicleVelocityController.generated.h"

class APawn;
class UChaosVehicleMovementComponent;

// Closed-loop velocity tracker for wheeled ground vehicles (Chaos or kinematic). Given a possessed
// pawn and a desired body-frame velocity (forward speed + yaw rate), it converts the error into the
// vehicle's native inputs: throttle/brake/gear and steering for a Chaos vehicle, or a normalized
// (acceleration, steering) movement input for a kinematic vehicle. Holds the PI gains and the
// linear-velocity integral state, so each owner gets its own tuning and windup state.
//
// Embedded (as a UPROPERTY) by the controllers that drive vehicles, e.g. ATempoWheeledVehicleController
// (command-driven) and ATrajectoryFollowingController (trajectory-driven), so the conversion lives in
// one place.
USTRUCT(BlueprintType)
struct TEMPOMOVEMENT_API FWheeledVehicleVelocityController
{
	GENERATED_BODY()

	// Clear the integral state. Call when (re)starting closed-loop tracking so the loop doesn't
	// carry windup from a previous setpoint.
	void Reset();

	// Track a body-frame velocity on Pawn's movement component for this tick. Only linear.x (forward
	// speed, cm/s) and angular.z (yaw rate, deg/s) are actuable for a wheeled vehicle. Returns false
	// if the pawn's movement component is neither a Chaos nor a kinematic vehicle (caller may fall back).
	bool TrackBodyVelocity(APawn* Pawn, float TargetLinVelCmS, float TargetYawRateDegS, float DeltaTime);

	// Apply an open-loop normalized driving input (throttle/steer) to Pawn's movement component.
	// Routes to native Chaos inputs for a Chaos vehicle, or a body-frame AddMovementInput otherwise.
	void ApplyDrivingInput(APawn* Pawn, const FNormalizedDrivingInput& Input) const;

	// Apply normalized throttle/brake/gear logic to a Chaos vehicle for a normalized acceleration in [-1, 1].
	void ApplyChaosAccelInput(UChaosVehicleMovementComponent* Movement, float NormAccel) const;

	// P gain for linear velocity tracking: normalized accel per cm/s of error.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velocity Control")
	float LinearVelocityKp = 0.005;

	// I gain for linear velocity tracking: normalized accel per (cm/s * s) of accumulated error.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velocity Control")
	float LinearVelocityKi = 0.002;

	// Saturation for the integral term to prevent windup (cm/s * s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velocity Control")
	float LinearVelocityIMax = 200.0;

	// P gain for Chaos vehicle yaw rate tracking: normalized steering per deg/s of error.
	// Kinematic vehicles use exact feedforward (the inverse motion model) and ignore this.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Velocity Control")
	float YawRateKp = 0.01745;

private:
	// Compute normalized acceleration in [-1, 1] from a target linear velocity (cm/s).
	float ComputeNormalizedAcceleration(float TargetLinVelCmS, float CurrentLinVelCmS, float DeltaTime);

	// Integral state for linear velocity PI loop.
	UPROPERTY(Transient)
	float LinearVelocityIntegralError = 0.0;
};
