// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "MassTrafficLights.h"
#include "MassTrafficLightRegistry.generated.h"

UCLASS(Blueprintable, BlueprintType)
class MASSTRAFFIC_API AMassTrafficLightRegistry : public AActor
{
	GENERATED_BODY()

public:
	
	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Traffic Light Registry")
	int32 RegisterTrafficLightType(const FMassTrafficLightTypeData& InTrafficLightType);

	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Traffic Light Registry")
	void RegisterTrafficLight(const FMassTrafficLightInstanceDesc& TrafficLightInstanceDesc);

	UFUNCTION(BlueprintCallable, Category = "Mass Traffic|Traffic Light Registry")
	void ClearTrafficLights();

	const TArray<FMassTrafficLightTypeData>& GetTrafficLightTypes() const;

	const TArray<FMassTrafficLightInstanceDesc>& GetTrafficLightInstanceDescs() const;

	static AMassTrafficLightRegistry* FindOrSpawnMassTrafficLightRegistryActor(UWorld& World);
	
protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Traffic|Traffic Light Registry")
	TArray<FMassTrafficLightTypeData> TrafficLightTypes;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Traffic|Traffic Light Registry")
	TArray<FMassTrafficLightInstanceDesc> TrafficLightInstanceDescs;
	
};
