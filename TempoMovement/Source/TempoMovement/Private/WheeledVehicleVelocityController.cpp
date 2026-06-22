// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "WheeledVehicleVelocityController.h"

#include "KinematicVehicleMovementComponent.h"
#include "TempoAngularVelocityInterface.h"
#include "TempoChaosWheeledVehicleMovementComponent.h"

#include "ChaosVehicleMovementComponent.h"
#include "GameFramework/Pawn.h"

namespace
{
	constexpr float NearlyStoppedSpeedCmS = 1.0f;
}

void FWheeledVehicleVelocityController::Reset()
{
	LinearVelocityIntegralError = 0.0f;
}

bool FWheeledVehicleVelocityController::TrackBodyVelocity(APawn* Pawn, float TargetLinVelCmS, float TargetYawRateDegS, float DeltaTime)
{
	if (!Pawn)
	{
		return false;
	}

	UActorComponent* MovementComponent = Pawn->GetMovementComponent();

	if (UChaosVehicleMovementComponent* ChaosMovement = Cast<UChaosVehicleMovementComponent>(MovementComponent))
	{
		const float CurrentLinVelCmS = ChaosMovement->GetForwardSpeed();
		const float NormAccel = ComputeNormalizedAcceleration(TargetLinVelCmS, CurrentLinVelCmS, DeltaTime);

		float CurrentYawRateDegS = 0.0f;
		if (const ITempoAngularVelocityInterface* AngVel = Cast<ITempoAngularVelocityInterface>(ChaosMovement))
		{
			CurrentYawRateDegS = AngVel->GetAngularVelocity().Z;
		}
		// Positive Chaos steering input = right turn = +yaw (Unreal left-handed). Same sign as the LH yaw error.
		const float YawErrorDegS = TargetYawRateDegS - CurrentYawRateDegS;
		const float NormSteer = FMath::Clamp(YawRateKp * YawErrorDegS, -1.0f, 1.0f);

		ChaosMovement->SetSteeringInput(NormSteer);
		ApplyChaosAccelInput(ChaosMovement, NormAccel);
		return true;
	}

	if (UKinematicVehicleMovementComponent* Kinematic = Cast<UKinematicVehicleMovementComponent>(MovementComponent))
	{
		const float CurrentLinVelCmS = Kinematic->GetLinearVelocity();
		const float NormAccel = ComputeNormalizedAcceleration(TargetLinVelCmS, CurrentLinVelCmS, DeltaTime);
		// Kinematic motion model is exact; use feedforward via the component's inverse model.
		const float NormSteer = Kinematic->ComputeNormalizedSteeringForYawRate(TargetYawRateDegS, CurrentLinVelCmS);

		const FVector ControlInputLocal(NormAccel + (GFrameCounter % 2 ? 1 : -1) * UE_KINDA_SMALL_NUMBER, NormSteer, 0.0);
		Pawn->AddMovementInput(Pawn->GetActorTransform().TransformVector(ControlInputLocal));
		return true;
	}

	return false;
}

void FWheeledVehicleVelocityController::ApplyDrivingInput(APawn* Pawn, const FNormalizedDrivingInput& Input) const
{
	if (!Pawn)
	{
		return;
	}

	if (UChaosVehicleMovementComponent* ChaosMovement = Cast<UChaosVehicleMovementComponent>(Pawn->GetMovementComponent()))
	{
		ChaosMovement->SetSteeringInput(Input.GetSteering());
		ApplyChaosAccelInput(ChaosMovement, Input.GetAcceleration());
		return;
	}

	const FVector ControlInputLocal(Input.GetAcceleration() + (GFrameCounter % 2 ? 1 : -1) * UE_KINDA_SMALL_NUMBER, Input.GetSteering(), 0.0);
	Pawn->AddMovementInput(Pawn->GetActorTransform().TransformVector(ControlInputLocal));
}

float FWheeledVehicleVelocityController::ComputeNormalizedAcceleration(float TargetLinVelCmS, float CurrentLinVelCmS, float DeltaTime)
{
	const float Error = TargetLinVelCmS - CurrentLinVelCmS;
	LinearVelocityIntegralError = FMath::Clamp(LinearVelocityIntegralError + Error * DeltaTime, -LinearVelocityIMax, LinearVelocityIMax);
	const float Unclamped = LinearVelocityKp * Error + LinearVelocityKi * LinearVelocityIntegralError;
	return FMath::Clamp(Unclamped, -1.0f, 1.0f);
}

void FWheeledVehicleVelocityController::ApplyChaosAccelInput(UChaosVehicleMovementComponent* Movement, float NormAccel) const
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
