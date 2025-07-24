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

void UTempoChaosWheeledVehicleMovementComponent::HandleDrivingInput(const FNormalizedDrivingInput& Input)
{
	// An extremely rough mapping from normalized acceleration (-1 to 1) to normalized throttle and brakes (each 0 to 1).
	auto SetAccelInput = [this](float AccelInput)
	{
		if (AccelInput > 0.0)
		{
			static constexpr float NearlyStoppedSpeed = 1.0;
			if (VehicleState.ForwardSpeed > -NearlyStoppedSpeed && GetCurrentGear() > -1)
			{
				SetThrottleInput(AccelInput);
				SetBrakeInput(0.0);
			}
			else
			{
				SetTargetGear(1, false);
				SetThrottleInput(0.0);
				SetBrakeInput(AccelInput);
			}
		}
		else
		{
			if (VehicleState.ForwardSpeed > 0.0 || !bReverseEnabled)
			{
				SetBrakeInput(-AccelInput);
				SetThrottleInput(0.0);
			}
			else
			{
				SetTargetGear(-1, false);
				SetThrottleInput(-AccelInput);
				SetBrakeInput(0.0);
			}
		}
	};
	
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const ITempoVehicleControlInterface* Controller = Cast<ITempoVehicleControlInterface>(Pawn->Controller.Get()))
		{
			SetSteeringInput(Input.Steering);

			if (VehicleState.ForwardSpeed > 0.0)
			{
				SetAccelInput(Input.Acceleration > 0.0 ? Input.Acceleration : Input.Acceleration);
			}
			else
			{
				SetAccelInput(Input.Acceleration);
			}
		}
	}
}
