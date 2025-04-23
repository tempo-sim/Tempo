// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoChaosWheeledVehicleMovementComponent.h"

#include "TempoVehicleControlInterface.h"

UTempoChaosWheeledVehicleMovementComponent::UTempoChaosWheeledVehicleMovementComponent()
{
	bReverseAsBrake = false;
	bThrottleAsBrake = false;
	WheelTraceCollisionResponses.Vehicle = ECR_Ignore;
	WheelTraceCollisionResponses.Pawn = ECR_Ignore;
}

void UTempoChaosWheeledVehicleMovementComponent::HandleDrivingCommand(const FDrivingCommand& Command)
{
	// An extremely rough mapping from normalized acceleration (-1 to 1) to normalized throttle and brakes (each 0 to 1).
	auto SetAccelInput = [this](float Input)
	{
		if (Input > 0.0)
		{
			if (VehicleState.ForwardSpeed > 0.0)
			{
				SetThrottleInput(Input);
				SetBrakeInput(0.0);
			}
			else
			{
				SetTargetGear(1, false);
				SetThrottleInput(0.0);
				SetBrakeInput(Input);
			}
		}
		else
		{
			if (VehicleState.ForwardSpeed > 0.0 || !bReverseEnabled)
			{
				SetBrakeInput(-Input);
				SetThrottleInput(0.0);
			}
			else
			{
				SetTargetGear(-1, false);
				SetThrottleInput(-Input);
				SetBrakeInput(0.0);
			}
		}
	};
	
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const ITempoVehicleControlInterface* Controller = Cast<ITempoVehicleControlInterface>(Pawn->Controller.Get()))
		{
			// The TempoVehicleMovementInterface takes driving commands in real units, but the 
			// ChaosVehicleMovementComponent expects normalized inputs. So we re-normalize them here.
			const float MaxAcceleration = Controller->GetMaxAcceleration();
			const float MaxDeceleration = Controller->GetMaxDeceleration();
			const float MaxSteerAngle = Controller->GetMaxSteerAngle();

			SetSteeringInput(Command.SteeringAngle / MaxSteerAngle);

			if (VehicleState.ForwardSpeed > 0.0)
			{
				SetAccelInput(Command.Acceleration > 0.0 ? Command.Acceleration / MaxAcceleration : Command.Acceleration / MaxDeceleration);
			}
			else
			{
				SetAccelInput(Command.Acceleration / MaxAcceleration);
			}
		}
	}
}
