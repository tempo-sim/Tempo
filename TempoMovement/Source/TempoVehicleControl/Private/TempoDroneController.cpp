// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDroneController.h"

#include "TempoDroneMovementInterface.h"
#include "TempoVehicleControl.h"

FString ATempoDroneController::GetDroneName()
{
	check(GetPawn());

	return GetPawn()->GetActorNameOrLabel();
}

void ATempoDroneController::HandleFlyingInput(const FNormalizedFlyingInput& Input)
{
	if (ITempoDroneMovementInterface* TempoDroneMovementInterface = Cast<ITempoDroneMovementInterface>(GetPawn()))
	{
		FFlyingCommand Command;
		Command.Pitch = Input.Pitch;
		Command.Roll = Input.Roll;
		Command.Yaw = Input.Yaw;
		Command.Throttle = Input.Throttle;
		TempoDroneMovementInterface->HandleFlyingCommand(Command);
	}
}
