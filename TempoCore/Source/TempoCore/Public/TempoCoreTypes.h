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
enum class EServerCompressionLevel: uint8
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

// 6-DoF velocity expressed in the body frame: linear in m/s, angular in rad/s, right-handed.
USTRUCT(BlueprintType)
struct FTempoTwist
{
	GENERATED_BODY();

	FTempoTwist() = default;
	FTempoTwist(const FVector& LinearIn, const FVector& AngularIn) : Linear(LinearIn), Angular(AngularIn) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Linear = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Angular = FVector::ZeroVector;
};

// 6-DoF acceleration expressed in the body frame: linear in m/s^2, angular in rad/s^2, right-handed.
USTRUCT(BlueprintType)
struct FTempoAccel
{
	GENERATED_BODY();

	FTempoAccel() = default;
	FTempoAccel(const FVector& LinearIn, const FVector& AngularIn) : Linear(LinearIn), Angular(AngularIn) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Linear = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Angular = FVector::ZeroVector;
};
