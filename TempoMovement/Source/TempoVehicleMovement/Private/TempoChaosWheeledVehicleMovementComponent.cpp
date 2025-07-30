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
