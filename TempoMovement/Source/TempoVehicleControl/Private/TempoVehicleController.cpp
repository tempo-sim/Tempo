// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoVehicleController.h"

#include "TempoVehicleMovementInterface.h"
#include "TempoVehicleControl.h"

FString ATempoVehicleController::GetVehicleName()
{
	check(GetPawn());

	return GetPawn()->GetActorNameOrLabel();
}

void ATempoVehicleController::HandleDrivingInput(const FNormalizedDrivingInput& Input)
{
	if (!GetPawn())
	{
		return;
	}

	TArray<UActorComponent*> VehicleMovementComponents = GetPawn()->GetComponentsByInterface(UTempoVehicleMovementInterface::StaticClass());
	if (VehicleMovementComponents.Num() > 1)
	{
		UE_LOG(LogTempoVehicleControl, Error, TEXT("More than one vehicle movement component found on %s"), *GetPawn()->GetName());
		return;
	}
	if (VehicleMovementComponents.Num() == 0)
	{
		UE_LOG(LogTempoVehicleControl, Error, TEXT("No vehicle movement component found on %s"), *GetPawn()->GetName());
		return;
	}

	ITempoVehicleMovementInterface* MovementInterface = Cast<ITempoVehicleMovementInterface>(VehicleMovementComponents[0]);
	MovementInterface->HandleDrivingInput(Input);
}
