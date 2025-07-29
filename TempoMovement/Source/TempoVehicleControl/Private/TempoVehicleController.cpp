// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoVehicleController.h"

#include "TempoMovementInterface.h"

#include "ChaosVehicleMovementComponent.h"

FString ATempoVehicleController::GetVehicleName()
{
	if (const APawn* ControlledPawn = GetPawn())
	{
		return ControlledPawn->GetActorNameOrLabel();
	}

	return FString();
}

void ATempoVehicleController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (APawn* ControlledPawn = GetPawn())
	{
		if (const FLastInput* Input = LastInput.GetPtrOrNull())
		{
			// Don't apply the input in the same frame as we already applied it
			if (Input->Frame < GFrameCounter)
			{
				// Apply some very small acceleration to avoid no-input deceleration
				const FVector ControlInputLocal(UE_KINDA_SMALL_NUMBER, Input->Input.GetSteering(), 0.0);
				ControlledPawn->AddMovementInput(ControlledPawn->GetActorTransform().TransformVector(ControlInputLocal));
			}
		}
	}
}

void ATempoVehicleController::HandleDrivingInput(const FNormalizedDrivingInput& Input)
{
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UChaosVehicleMovementComponent* ChaosVehicleMovementComponent = Cast<UChaosVehicleMovementComponent>(ControlledPawn->GetMovementComponent()))
		{
			ApplyDrivingInputToWheeledVehicle(ChaosVehicleMovementComponent, Input);
			return;
		}
		const FVector ControlInputLocal(Input.GetAcceleration() + UE_KINDA_SMALL_NUMBER, Input.GetSteering(), 0.0);
		ControlledPawn->AddMovementInput(ControlledPawn->GetActorTransform().TransformVector(ControlInputLocal));
		if (bPersistSteering)
		{
			LastInput = FLastInput(Input, GFrameCounter);
		}
	}
}

void ATempoVehicleController::ApplyDrivingInputToWheeledVehicle(UChaosVehicleMovementComponent* MovementComponent, const FNormalizedDrivingInput& Input)
{
	// An extremely rough mapping from normalized acceleration (-1 to 1) to normalized throttle and brakes (each 0 to 1).
	auto SetAccelInput = [MovementComponent](float AccelInput)
	{
		if (AccelInput > 0.0)
		{
			static constexpr float NearlyStoppedSpeed = 1.0;
			if (MovementComponent->GetForwardSpeed() > -NearlyStoppedSpeed && MovementComponent->GetCurrentGear() > -1)
			{
				MovementComponent->SetThrottleInput(AccelInput);
				MovementComponent->SetBrakeInput(0.0);
			}
			else
			{
				MovementComponent->SetTargetGear(1, false);
				MovementComponent->SetThrottleInput(0.0);
				MovementComponent->SetBrakeInput(AccelInput);
			}
		}
		else
		{
			bool bReverseEnabled = true;
			if (ITempoMovementInterface* TempoMovementInterface = Cast<ITempoMovementInterface>(MovementComponent))
			{
				bReverseEnabled = TempoMovementInterface->GetReverseEnabled();
			}
			if (MovementComponent->GetForwardSpeed() > 0.0 || !bReverseEnabled)
			{
				MovementComponent->SetBrakeInput(-AccelInput);
				MovementComponent->SetThrottleInput(0.0);
			}
			else
			{
				MovementComponent->SetTargetGear(-1, false);
				MovementComponent->SetThrottleInput(-AccelInput);
				MovementComponent->SetBrakeInput(0.0);
			}
		}
	};

	MovementComponent->SetSteeringInput(Input.GetSteering());
	SetAccelInput(Input.GetAcceleration());
}
