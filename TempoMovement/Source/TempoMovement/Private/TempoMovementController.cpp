// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMovementController.h"

#include "TempoCoreUtils.h"

FString ATempoMovementController::GetPawnName() const
{
	if (const APawn* ControlledPawn = GetPawn())
	{
		return UTempoCoreUtils::GetActorIdentifier(ControlledPawn);
	}
	return FString();
}
