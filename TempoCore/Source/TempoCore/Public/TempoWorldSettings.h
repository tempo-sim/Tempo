// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoTimeWorldSettings.h"

#include "CoreMinimal.h"
#include "GameFramework/WorldSettings.h"

#include "TempoWorldSettings.generated.h"

/**
 * The base Tempo World Settings
 */
UCLASS(Config=Plugins, DefaultConfig, DisplayName="World")
class TEMPOCORE_API ATempoWorldSettings : public ATempoTimeWorldSettings
{
	GENERATED_BODY()

public:
	float GetDefaultAutoExposureBias();

protected:
	UFUNCTION(CallInEditor, Category="Lighting", DisplayName="Set Default Exposure Compensation")
	void SetDefaultAutoExposureBias();

	virtual void BeginPlay() override;
	
	UPROPERTY(EditAnywhere, Category="Lighting", DisplayName = "Default Exposure Compensation", meta = (UIMin = "-15.0", UIMax = "15.0", EditCondition = "bManualDefaultAutoExposureBias"))
	float DefaultAutoExposureBias = 0.0;

	UPROPERTY(EditAnywhere, Category="Lighting", DisplayName = "Manual Default Exposure Compensation")
	bool bManualDefaultAutoExposureBias = false;

	UPROPERTY()
	bool bHasBegunPlay = false;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
