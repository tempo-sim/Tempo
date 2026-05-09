// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"

#include "TempoWheeledVehiclePawn.generated.h"

/*
 * A WheeledVehiclePawn that will use the TempoChaosWheeledVehicleMovementComponent.
 */
UCLASS()
class TEMPOMOVEMENT_API ATempoWheeledVehiclePawn : public AWheeledVehiclePawn
{
	GENERATED_BODY()

public:
	ATempoWheeledVehiclePawn(const FObjectInitializer& ObjectInitializer);
};
