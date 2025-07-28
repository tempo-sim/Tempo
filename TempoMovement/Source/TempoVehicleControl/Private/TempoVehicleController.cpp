// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoVehicleController.h"

FString ATempoVehicleController::GetVehicleName()
{
	if (APawn* ControlledPawn = GetPawn())
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
		const FVector ControlInputLocal(Input.GetAcceleration() + UE_KINDA_SMALL_NUMBER, Input.GetSteering(), 0.0);
		ControlledPawn->AddMovementInput(ControlledPawn->GetActorTransform().TransformVector(ControlInputLocal));
		if (bPersistSteering)
		{
			LastInput = FLastInput(Input, GFrameCounter);
		}
	}
}
