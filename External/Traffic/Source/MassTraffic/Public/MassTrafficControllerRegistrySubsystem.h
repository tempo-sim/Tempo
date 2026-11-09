// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "MassTrafficLights.h"
#include "MassTrafficSigns.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "MassTrafficControllerRegistrySubsystem.generated.h"

UCLASS(Blueprintable, BlueprintType)
class MASSTRAFFIC_API UMassTrafficControllerRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Traffic Light Registry Subsystem")
	int32 RegisterTrafficLightType(const FMassTrafficLightTypeData& InTrafficLightType);

	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Traffic Light Registry Subsystem")
	void RegisterTrafficLight(const FMassTrafficLightInstanceDesc& TrafficLightInstanceDesc);

	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Traffic Light Registry Subsystem")
	void RegisterTrafficSign(const FMassTrafficSignInstanceDesc& TrafficSignInstanceDesc);

	/**
	 * Registers every traffic light in a baked instances data asset, along with the light types they refer to,
	 * remapping each light's type index from TypesDataAsset to this registry's. A given data asset is registered
	 * once between calls to ClearTrafficControllers, so repeated intersection spawns don't duplicate its lights.
	 * With no TypesDataAsset, the lights are registered without a type.
	 */
	void RegisterTrafficLightsFromDataAsset(const UMassTrafficLightInstancesDataAsset& InstancesDataAsset, const UMassTrafficLightTypesDataAsset* TypesDataAsset);

	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Traffic Light Registry Subsystem")
	void ClearTrafficControllers();

	const TArray<FMassTrafficLightTypeData>& GetTrafficLightTypes() const;

	const TArray<FMassTrafficLightInstanceDesc>& GetTrafficLightInstanceDescs() const;

	const TArray<FMassTrafficSignInstanceDesc>& GetTrafficSignInstanceDescs() const;
	
protected:

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Traffic|Traffic Light Registry Subsystem")
	TArray<FMassTrafficLightTypeData> TrafficLightTypes;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Traffic|Traffic Light Registry Subsystem")
	TArray<FMassTrafficLightInstanceDesc> TrafficLightInstanceDescs;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Traffic|Traffic Light Registry Subsystem")
	TArray<FMassTrafficSignInstanceDesc> TrafficSignInstanceDescs;

	// Instances data assets registered since the last ClearTrafficControllers.
	TSet<FObjectKey> RegisteredTrafficLightDataAssets;

};
