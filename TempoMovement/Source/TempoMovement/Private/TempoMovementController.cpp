// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMovementController.h"

FString ATempoMovementController::GetVehicleName() const
{
	if (const APawn* ControlledPawn = GetPawn())
	{
		return ControlledPawn->GetActorNameOrLabel();
	}
	return FString();
}
