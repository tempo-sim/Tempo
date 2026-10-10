// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "MassTrafficTurnType.generated.h"

/** Which way a connection through an intersection turns. */
UENUM(BlueprintType)
enum class EMassTrafficTurnType : uint8
{
	Right,
	Left,
	NoTurn,
};
