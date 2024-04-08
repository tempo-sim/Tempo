// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCoreTypes.h"

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "TempoCoreSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FTempoCoreTimeSettingsChanged);

UCLASS(Config=Game)
class TEMPOCORE_API UTempoCoreSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// Time Settings.
	void SetTimeMode(ETimeMode TimeModeIn);
	void SetSimulatedStepsPerSecond(int32 SimulatedStepsPerSecondIn);
	ETimeMode GetTimeMode() const { return TimeMode; }
	int32 GetSimulatedStepsPerSecond() const { return SimulatedStepsPerSecond; }
	FTempoCoreTimeSettingsChanged TempoCoreTimeSettingsChangedEvent;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
private:
	UPROPERTY(EditAnywhere, Config, Category="Time")
	ETimeMode TimeMode = ETimeMode::WallClock;

	// The number of evenly-spaced steps per simulated second that will be executed in FixedStep time mode.
	UPROPERTY(EditAnywhere, Config, Category="Time|FixedStep", meta=(ClampMin=1, UIMin=1, UIMax=100))
	int32 SimulatedStepsPerSecond = 10;
};
