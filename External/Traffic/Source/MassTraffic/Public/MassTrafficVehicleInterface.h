// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassTrafficDamage.h"
#include "MassTrafficDrivers.h"
#include "MassTrafficDriverVisualizationProcessor.h"
#include "UObject/Interface.h"
#include "MassTrafficVehicleInterface.generated.h"


UINTERFACE()
class UMassTrafficVehicleInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class MASSTRAFFIC_API IMassTrafficVehicleInterface : public IInterface
{
	GENERATED_IINTERFACE_BODY()

public:

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Mass Traffic|Traffic Vehicle")
	EMassTrafficVehicleDamageState GetDamageState() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Mass Traffic|Traffic Vehicle")
	void RepairDamage();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Mass Traffic|Traffic Vehicle")
	void OnDriverRemoved(
		const FMassTrafficDriverTypeData& DriverTypeData, 
		const FMassTrafficInstancePlaybackData& DriverInstanceData,
		const FTransform& DriverTransformWorldSpace);
};
