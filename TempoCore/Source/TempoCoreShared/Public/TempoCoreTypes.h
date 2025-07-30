// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "TempoCoreTypes.generated.h"

UENUM(Blueprintable, BlueprintType)
enum class ETimeMode: uint8
{
	// Time advances strictly according to the wall clock ("real time").
	WallClock = 0,
	// Time advances by fixed increments, independent of the frame rate.
	FixedStep = 1,
	Max UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(ETimeMode, ETimeMode::Max);

UENUM(Blueprintable, BlueprintType)
enum class EScriptingCompressionLevel: uint8
{
	None = 0,
	Low = 1,
	Med = 2,
	High = 3
};

UENUM(Blueprintable, BlueprintType)
enum class EControlMode : uint8
{
	// No robot control
	None = 0,
	// User input via keyboard, joypad, etc
	User = 1,
	// Input from AI controller or other in-game control
	OpenLoop = 2,
	// External control via API
	ClosedLoop = 3,
	Max UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EControlMode, EControlMode::Max);
