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

// void UTempoChaosWheeledVehicleMovementComponent::HandleDrivingInput(const FNormalizedDrivingInput& Input)
// {
// 	// An extremely rough mapping from normalized acceleration (-1 to 1) to normalized throttle and brakes (each 0 to 1).
// 	auto SetAccelInput = [this](float AccelInput)
// 	{
// 		if (AccelInput > 0.0)
// 		{
// 			static constexpr float NearlyStoppedSpeed = 1.0;
// 			if (VehicleState.ForwardSpeed > -NearlyStoppedSpeed && GetCurrentGear() > -1)
// 			{
// 				SetThrottleInput(AccelInput);
// 				SetBrakeInput(0.0);
// 			}
// 			else
// 			{
// 				SetTargetGear(1, false);
// 				SetThrottleInput(0.0);
// 				SetBrakeInput(AccelInput);
// 			}
// 		}
// 		else
// 		{
// 			if (VehicleState.ForwardSpeed > 0.0 || !bReverseEnabled)
// 			{
// 				SetBrakeInput(-AccelInput);
// 				SetThrottleInput(0.0);
// 			}
// 			else
// 			{
// 				SetTargetGear(-1, false);
// 				SetThrottleInput(-AccelInput);
// 				SetBrakeInput(0.0);
// 			}
// 		}
// 	};
//
// 	SetSteeringInput(Input.Steering);
// 	SetAccelInput(Input.Acceleration);
// }
