// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMovementController.h"

FString ATempoMovementController::GetPawnName() const
{
	if (const APawn* ControlledPawn = GetPawn())
	{
		return ControlledPawn->GetActorNameOrLabel();
	}
	return FString();
}
