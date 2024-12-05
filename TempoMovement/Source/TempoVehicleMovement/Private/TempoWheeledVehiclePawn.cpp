// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWheeledVehiclePawn.h"

#include "TempoChaosWheeledVehicleMovementComponent.h"

ATempoWheeledVehiclePawn::ATempoWheeledVehiclePawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTempoChaosWheeledVehicleMovementComponent>(VehicleMovementComponentName))
{
}
