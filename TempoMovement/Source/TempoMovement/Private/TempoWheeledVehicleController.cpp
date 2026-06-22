// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWheeledVehicleController.h"

#include "KinematicVehicleMovementComponent.h"

#include "ChaosVehicleMovementComponent.h"
#include "Kismet/GameplayStatics.h"

void ATempoWheeledVehicleController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	switch (ControlMode)
	{
	case EControlMode::Driving:
		TickDriving(ControlledPawn);
		break;
	case EControlMode::Velocity:
	case EControlMode::Acceleration:
		TickClosedLoop(DeltaTime, ControlledPawn);
		break;
	case EControlMode::None:
	default:
		break;
	}
}

bool ATempoWheeledVehicleController::HandleDrivingInput(const FNormalizedDrivingInput& Input)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return false;
	}

	ControlMode = EControlMode::Driving;
	VelocityController.Reset();

	VelocityController.ApplyDrivingInput(ControlledPawn, Input);
	if (bPersistSteering)
	{
		LastDrivingInput = FLastDrivingInput{Input, GFrameCounter};
	}
	return true;
}

bool ATempoWheeledVehicleController::HandleVelocityCommand(const FTempoTwist& Twist)
{
	if (!GetPawn())
	{
		return false;
	}
	ControlMode = EControlMode::Velocity;
	VelocityTarget = Twist;
	AccelTarget = FTempoAccel();
	LastClosedLoopCommandTime = UGameplayStatics::GetTimeSeconds(this);
	return true;
}

bool ATempoWheeledVehicleController::HandleAccelerationCommand(const FTempoAccel& Accel)
{
	if (!GetPawn())
	{
		return false;
	}
	// Switching into acceleration mode seeds the integrated velocity target with the current
	// vehicle velocity so we don't snap from whatever the prior setpoint was.
	if (ControlMode != EControlMode::Acceleration)
	{
		VelocityTarget = FTempoTwist();
		if (const APawn* ControlledPawn = GetPawn())
		{
			if (const UChaosVehicleMovementComponent* ChaosMovement = Cast<UChaosVehicleMovementComponent>(ControlledPawn->GetMovementComponent()))
			{
				VelocityTarget.Linear.X = ChaosMovement->GetForwardSpeed();
			}
			else if (const UKinematicVehicleMovementComponent* Kinematic = Cast<UKinematicVehicleMovementComponent>(ControlledPawn->GetMovementComponent()))
			{
				VelocityTarget.Linear.X = Kinematic->GetLinearVelocity();
				VelocityTarget.Angular.Z = Kinematic->GetAngularVelocity().Z;
			}
		}
	}
	ControlMode = EControlMode::Acceleration;
	AccelTarget = Accel;
	LastClosedLoopCommandTime = UGameplayStatics::GetTimeSeconds(this);
	return true;
}

void ATempoWheeledVehicleController::TickDriving(APawn* ControlledPawn)
{
	if (const FLastDrivingInput* Input = LastDrivingInput.GetPtrOrNull())
	{
		// Don't reapply on the same frame we received it.
		if (Input->Frame < GFrameCounter)
		{
			// Tiny alternating acceleration prevents no-input deceleration; preserves steering.
			const FVector ControlInputLocal((GFrameCounter % 2 ? 1 : -1) * UE_KINDA_SMALL_NUMBER, Input->Input.GetSteering(), 0.0);
			ControlledPawn->AddMovementInput(ControlledPawn->GetActorTransform().TransformVector(ControlInputLocal));
		}
	}
}

void ATempoWheeledVehicleController::TickClosedLoop(float DeltaTime, APawn* ControlledPawn)
{
	// Watchdog: if we haven't heard a new command in too long, clamp setpoints to zero.
	const double Now = UGameplayStatics::GetTimeSeconds(this);
	if (CommandStaleTimeout > 0.0f && (Now - LastClosedLoopCommandTime) > CommandStaleTimeout)
	{
		VelocityTarget = FTempoTwist();
		AccelTarget = FTempoAccel();
	}

	// In acceleration mode, integrate the commanded accel into the velocity target each tick.
	if (ControlMode == EControlMode::Acceleration)
	{
		VelocityTarget.Linear += DeltaTime * AccelTarget.Linear;
		VelocityTarget.Angular += DeltaTime * AccelTarget.Angular;
	}

	// Only linear.x and angular.z are actuable for a wheeled vehicle. Other axes are ignored.
	const float TargetLinVelCmS = VelocityTarget.Linear.X;
	const float TargetYawRateDegS = VelocityTarget.Angular.Z;

	VelocityController.TrackBodyVelocity(ControlledPawn, TargetLinVelCmS, TargetYawRateDegS, DeltaTime);
}
