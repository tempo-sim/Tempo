// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"

#include "Engine/World.h"

#include "MassTrafficVehicleDimensionsTrait.generated.h"

USTRUCT()
struct MASSTRAFFIC_API FMassTrafficVehicleDimensionsParameters
{
	GENERATED_BODY()

	/**
	 * Distance from vehicle origin to front most point or rear most point (whichever is greater). Used for vehicle
	 * avoidance collision detection.
	 */  
	UPROPERTY(EditAnywhere, Category = "Dimensions")
	float HalfLength = 0.0f;
	
	/**
	* Distance from vehicle origin to left most point or right most point (whichever is greater). Used for vehicle
	* avoidance collision detection.
	*/  
	UPROPERTY(EditAnywhere, Category = "Dimensions")
	float HalfWidth = 0.0f;
};

UCLASS(meta=(DisplayName="Traffic Vehicle Dimensions"))
class MASSTRAFFIC_API UMassTrafficVehicleDimensionsTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Mass Traffic")
	FMassTrafficVehicleDimensionsParameters Params;

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
