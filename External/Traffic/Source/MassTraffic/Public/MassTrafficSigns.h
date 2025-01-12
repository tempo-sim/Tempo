// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficSigns.generated.h"


UENUM(BlueprintType)
enum class EMassTrafficControllerSignType : uint8
{
	None = 0,
	YieldSign = 1,
	StopSign = 2
};

USTRUCT(BlueprintType)
struct MASSTRAFFIC_API FMassTrafficSignInstanceDesc
{
	GENERATED_BODY()
	FMassTrafficSignInstanceDesc()
	{}

	FMassTrafficSignInstanceDesc(const FVector& InControlledIntersectionSideMidpoint, const EMassTrafficControllerSignType InTrafficControllerSignType) :
		ControlledIntersectionSideMidpoint(InControlledIntersectionSideMidpoint),
		TrafficControllerSignType(InTrafficControllerSignType)
	{
	}
	
	UPROPERTY()
	FVector ControlledIntersectionSideMidpoint = FVector::ZeroVector;

	UPROPERTY()
	EMassTrafficControllerSignType TrafficControllerSignType = EMassTrafficControllerSignType::None;
};
