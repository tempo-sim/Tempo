// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCoreTypes.h"

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "TempoCoreSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FTempoCoreTimeSettingsChanged);

UCLASS(Config=Game)
class TEMPOCORESHARED_API UTempoCoreSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// Time Settings.
	void SetTimeMode(ETimeMode TimeModeIn);
	void SetSimulatedStepsPerSecond(int32 SimulatedStepsPerSecondIn);
	ETimeMode GetTimeMode() const { return TimeMode; }
	int32 GetSimulatedStepsPerSecond() const { return SimulatedStepsPerSecond; }
	FTempoCoreTimeSettingsChanged TempoCoreTimeSettingsChangedEvent;

	// Scripting Settings.
	int32 GetEngineScriptingPort() const { return EngineScriptingPort; }
	int32 GetWorldScriptingPort() const { return WorldScriptingPort; }
	int32 GetMaxEventProcessingTime() const { return MaxEventProcessingTimeMicroSeconds; }
	int32 GetMaxEventWaitTime() const { return MaxEventWaitTimeNanoSeconds; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
private:
	UPROPERTY(EditAnywhere, Config, Category="Time")
	ETimeMode TimeMode = ETimeMode::WallClock;

	// The number of evenly-spaced steps per simulated second that will be executed in FixedStep time mode.
	UPROPERTY(EditAnywhere, Config, Category="Time|FixedStep", meta=(ClampMin=1, UIMin=1, UIMax=100))
	int32 SimulatedStepsPerSecond = 10;

	// The port number to listen for engine scripting connections on.
	UPROPERTY(EditAnywhere, Config, Category="Scripting", meta=(ClampMin=1024, ClampMax=65535, UIMin=1024, UIMax=65535))
	int32 EngineScriptingPort = 10001;

	// The port number to listen for world scripting connections on.
	UPROPERTY(EditAnywhere, Config, Category="Scripting", meta=(ClampMin=1024, ClampMax=65535, UIMin=1024, UIMax=65535))
	int32 WorldScriptingPort = 10002;
	
	// We will spend as much as this amount of time (in microseconds) processing events each Tick.
	// Except in FixedStep mode, where we process all received events every Tick.
	UPROPERTY(EditAnywhere, Config, Category="Scripting|Advanced", meta=(ClampMin=1, ClampMax=10000, UIMin=1, UIMax=10000))
	int32 MaxEventProcessingTimeMicroSeconds = 1000;

	// We will wait as much as this amount of time (in nanoseconds) for an event to arrive each time we check for an event.
	UPROPERTY(EditAnywhere, Config, Category="Scripting|Advanced", meta=(ClampMin=1, ClampMax=10000, UIMin=1, UIMax=10000))
	int32 MaxEventWaitTimeNanoSeconds = 1000;
};
