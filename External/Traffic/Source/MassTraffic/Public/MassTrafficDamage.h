// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficDamage.generated.h"

UENUM(BlueprintType)
enum class EMassTrafficVehicleDamageState : uint8
{
	None,

	// Minor cosmetic damage. Still permitted to LOD switch (losing damage)  
	Damaged,

	// Undriveable. No vehicle control will be applied 
	Totaled,

	// Totaled car tried to switch to lower LOD, so we're trying to repair it 
	Repairing,

	// The vehicle couldn't be repaired and should be released and the agent recycled  
	Irreparable
};
