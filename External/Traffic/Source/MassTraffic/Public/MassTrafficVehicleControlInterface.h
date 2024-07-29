// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "MassTrafficVehicleControlInterface.generated.h"

UINTERFACE()
class UMassTrafficVehicleControlInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

/** This allows Mass Traffic to control actor representations of traffic vehicles. */
class MASSTRAFFIC_API IMassTrafficVehicleControlInterface
{
	GENERATED_IINTERFACE_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Mass Traffic|Traffic Vehicle")
	void SetVehicleInputs(const float Throttle, const float Brake, const bool bHandBrake, const float Steering, const bool bSetSteering);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Mass Traffic|Traffic Vehicle")
	void OnParkedVehicleSpawned();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Mass Traffic|Traffic Vehicle")
	void OnTrafficVehicleSpawned();
};
