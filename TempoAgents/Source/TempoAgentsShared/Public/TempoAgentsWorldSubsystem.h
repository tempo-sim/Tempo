// Copyright Tempo Simulation, LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TempoAgentsWorldSubsystem.generated.h"

UCLASS()
class TEMPOAGENTSSHARED_API UTempoAgentsWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:
	
	UFUNCTION(BlueprintCallable, Category = "Tempo Agents|World Subsystem|Traffic Controllers")
	void SetupTrafficControllers();

	UFUNCTION(BlueprintCallable, Category = "Tempo Agents|World Subsystem|Brightness Meter")
	void SetupBrightnessMeter();

	UFUNCTION(BlueprintCallable, Category = "Tempo Agents|World Subsystem|Traffic Controllers")
	void SetupIntersectionLaneMap();

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo Agents|World Subsystem|Brightness Meter")
	float BrightnessMeterLongitudinalOffset = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo Agents|World Subsystem|Brightness Meter")
	float BrightnessMeterLateralOffset = 500.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tempo Agents|World Subsystem|Brightness Meter")
	float BrightnessMeterVerticalOffset = 140.0f;
	
protected:
	
};
