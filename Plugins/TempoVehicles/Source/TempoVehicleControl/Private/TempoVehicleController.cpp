// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoVehicleController.h"

#include "TempoVehicleMovementInterface.h"
#include "TempoVehicleControl.h"

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

	const float LinearVelocity = MovementInterface->GetLinearVelocity();
	
	FDrivingCommand Command;
	Command.SteeringAngle = FMath::Abs(Input.Steering) > 0.0 ?
			FMath::Min(MaxSteerAngle, Input.Steering * MaxSteerAngle) :
			FMath::Max(-MaxSteerAngle, Input.Steering * MaxSteerAngle);
	Command.Acceleration = FMath::Abs(Input.Acceleration) > 0.0 ?
			FMath::Min(MaxAcceleration, Input.Acceleration * MaxAcceleration) : LinearVelocity > 0.0 ?
			FMath::Max(-MaxDeceleration, Input.Acceleration * MaxDeceleration) :
			FMath::Min(-MaxAcceleration, Input.Acceleration * MaxAcceleration) ;
	
	MovementInterface->HandleDrivingCommand(Command);
}
