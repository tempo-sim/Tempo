// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "TempoCoreTypes.generated.h"

UENUM(Blueprintable)
enum class ETimeMode: uint8
{
	// Time advances strictly according to the wall clock ("real time").
	WallClock = 0,
	// Time advances by fixed increments, independent of the frame rate.
	FixedStep = 1,
	Max UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(ETimeMode, ETimeMode::Max);
