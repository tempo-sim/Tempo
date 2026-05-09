// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCoreTypes.h"
#include "TempoMovementTypes.h"

#include "AIController.h"
#include "CoreMinimal.h"

#include "TempoMovementController.generated.h"

// Abstract base for any Tempo controller that can be addressed by the MovementControlService.
// Subclasses override the handlers they support; unsupported handlers return false by default,
// which the service translates to UNIMPLEMENTED.
UCLASS(Abstract)
class TEMPOMOVEMENT_API ATempoMovementController : public AAIController
{
	GENERATED_BODY()

public:
	// Name used by the movement service to route commands. Defaults to the controlled pawn's label.
	virtual FString GetPawnName() const;

	// Open-loop normalized throttle/steer input.
	virtual bool HandleDrivingInput(const FNormalizedDrivingInput& Input) { return false; }

	// Closed-loop body-frame velocity command (SI, right-handed).
	virtual bool HandleVelocityCommand(const FTempoTwist& Twist) { return false; }

	// Closed-loop body-frame acceleration command (SI, right-handed).
	virtual bool HandleAccelerationCommand(const FTempoAccel& Accel) { return false; }
};
