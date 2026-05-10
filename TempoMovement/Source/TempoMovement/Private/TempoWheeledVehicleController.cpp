// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWheeledVehicleController.h"

#include "KinematicVehicleMovementComponent.h"
#include "TempoChaosWheeledVehicleMovementComponent.h"

#include "ChaosVehicleMovementComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr float CmPerMeter = 100.0f;
	constexpr float NearlyStoppedSpeedCmS = 1.0f;
}

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
	LinearVelocityIntegralError = 0.0f;

	if (UChaosVehicleMovementComponent* ChaosMovement = Cast<UChaosVehicleMovementComponent>(ControlledPawn->GetMovementComponent()))
	{
		ApplyDrivingInputToChaosVehicle(ChaosMovement, Input);
		return true;
	}

	const FVector ControlInputLocal(Input.GetAcceleration() + (GFrameCounter % 2 ? 1 : -1) * UE_KINDA_SMALL_NUMBER, Input.GetSteering(), 0.0);
	ControlledPawn->AddMovementInput(ControlledPawn->GetActorTransform().TransformVector(ControlInputLocal));
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
		if (const APawn* Pawn = GetPawn())
		{
			if (const UChaosVehicleMovementComponent* ChaosMovement = Cast<UChaosVehicleMovementComponent>(Pawn->GetMovementComponent()))
			{
				VelocityTarget.Linear.X = ChaosMovement->GetForwardSpeed() / CmPerMeter;
			}
			else if (const UKinematicVehicleMovementComponent* Kinematic = Cast<UKinematicVehicleMovementComponent>(Pawn->GetMovementComponent()))
			{
				VelocityTarget.Linear.X = Kinematic->GetLinearVelocity() / CmPerMeter;
				// Seed angular target from current angular velocity (rad/s right-handed).
				VelocityTarget.Angular.Z = -FMath::DegreesToRadians(Kinematic->GetAngularVelocity().Z);
			}
		}
	}
	ControlMode = EControlMode::Acceleration;
	AccelTarget = Accel;
	LastClosedLoopCommandTime = UGameplayStatics::GetTimeSeconds(this);
	return true;
}

void ATempoWheeledVehicleController::TickDriving(APawn* Pawn)
{
	if (const FLastDrivingInput* Input = LastDrivingInput.GetPtrOrNull())
	{
		// Don't reapply on the same frame we received it.
		if (Input->Frame < GFrameCounter)
		{
			// Tiny alternating acceleration prevents no-input deceleration; preserves steering.
			const FVector ControlInputLocal((GFrameCounter % 2 ? 1 : -1) * UE_KINDA_SMALL_NUMBER, Input->Input.GetSteering(), 0.0);
			Pawn->AddMovementInput(Pawn->GetActorTransform().TransformVector(ControlInputLocal));
		}
	}
}

void ATempoWheeledVehicleController::TickClosedLoop(float DeltaTime, APawn* Pawn)
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
	const float TargetLinVelMps = VelocityTarget.Linear.X;
	const float TargetYawRateRadS = VelocityTarget.Angular.Z;

	UActorComponent* MovementComponent = Pawn->GetMovementComponent();

	if (UChaosVehicleMovementComponent* ChaosMovement = Cast<UChaosVehicleMovementComponent>(MovementComponent))
	{
		const float CurrentLinVelMps = ChaosMovement->GetForwardSpeed() / CmPerMeter;
		const float NormAccel = ComputeNormalizedAcceleration(TargetLinVelMps, CurrentLinVelMps, DeltaTime);

		// Current yaw rate in rad/s right-handed (Chaos VehicleState is in rad/s, left-handed Unreal).
		float CurrentYawRateRadS = 0.0f;
		if (const ITempoAngularVelocityInterface* AngVel = Cast<ITempoAngularVelocityInterface>(ChaosMovement))
		{
			// Interface returns deg/s left-handed; convert to rad/s right-handed.
			CurrentYawRateRadS = -FMath::DegreesToRadians(AngVel->GetAngularVelocity().Z);
		}
		const float YawErrorRadS = TargetYawRateRadS - CurrentYawRateRadS;
		// Positive Chaos steering input = right turn = +yaw_LH = -yaw_RH. Sign-flip from the
		// right-handed error.
		const float NormSteer = FMath::Clamp(-YawRateKp * YawErrorRadS, -1.0f, 1.0f);

		ChaosMovement->SetSteeringInput(NormSteer);
		ApplyChaosAccelInput(ChaosMovement, NormAccel);
		return;
	}

	if (UKinematicVehicleMovementComponent* Kinematic = Cast<UKinematicVehicleMovementComponent>(MovementComponent))
	{
		const float CurrentLinVelMps = Kinematic->GetLinearVelocity() / CmPerMeter;
		const float NormAccel = ComputeNormalizedAcceleration(TargetLinVelMps, CurrentLinVelMps, DeltaTime);
		// Kinematic motion model is exact; use feedforward via the component's inverse model.
		const float NormSteer = Kinematic->ComputeNormalizedSteeringForYawRate(TargetYawRateRadS, CurrentLinVelMps);

		const FVector ControlInputLocal(NormAccel + (GFrameCounter % 2 ? 1 : -1) * UE_KINDA_SMALL_NUMBER, NormSteer, 0.0);
		Pawn->AddMovementInput(Pawn->GetActorTransform().TransformVector(ControlInputLocal));
		return;
	}
}

float ATempoWheeledVehicleController::ComputeNormalizedAcceleration(float TargetLinVelMps, float CurrentLinVelMps, float DeltaTime)
{
	const float Error = TargetLinVelMps - CurrentLinVelMps;
	LinearVelocityIntegralError = FMath::Clamp(LinearVelocityIntegralError + Error * DeltaTime, -LinearVelocityIMax, LinearVelocityIMax);
	const float Unclamped = LinearVelocityKp * Error + LinearVelocityKi * LinearVelocityIntegralError;
	return FMath::Clamp(Unclamped, -1.0f, 1.0f);
}

void ATempoWheeledVehicleController::ApplyChaosAccelInput(UChaosVehicleMovementComponent* Movement, float NormAccel) const
{
	if (NormAccel > 0.0f)
	{
		if (Movement->GetForwardSpeed() > -NearlyStoppedSpeedCmS && Movement->GetCurrentGear() > -1)
		{
			Movement->SetThrottleInput(NormAccel);
			Movement->SetBrakeInput(0.0f);
		}
		else
		{
			Movement->SetTargetGear(1, false);
			Movement->SetThrottleInput(0.0f);
			Movement->SetBrakeInput(NormAccel);
		}
	}
	else
	{
		bool bReverseEnabled = true;
		if (const UTempoChaosWheeledVehicleMovementComponent* TempoMovement = Cast<UTempoChaosWheeledVehicleMovementComponent>(Movement))
		{
			bReverseEnabled = TempoMovement->GetReverseEnabled();
		}
		if (Movement->GetForwardSpeed() > 0.0f || !bReverseEnabled)
		{
			Movement->SetBrakeInput(-NormAccel);
			Movement->SetThrottleInput(0.0f);
		}
		else
		{
			Movement->SetTargetGear(-1, false);
			Movement->SetThrottleInput(-NormAccel);
			Movement->SetBrakeInput(0.0f);
		}
	}
}

void ATempoWheeledVehicleController::ApplyDrivingInputToChaosVehicle(UChaosVehicleMovementComponent* Movement, const FNormalizedDrivingInput& Input) const
{
	Movement->SetSteeringInput(Input.GetSteering());
	ApplyChaosAccelInput(Movement, Input.GetAcceleration());
}
