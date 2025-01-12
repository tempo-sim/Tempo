// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "MassTrafficLights.h"
#include "MassTrafficSigns.h"
#include "Subsystems/WorldSubsystem.h"
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
	
};
