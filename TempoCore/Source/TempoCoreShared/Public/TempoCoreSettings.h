// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCoreTypes.h"

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "TempoCoreSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FTempoCoreTimeSettingsChanged);

UCLASS(Config=Plugins, DefaultConfig, DisplayName="Core")
class TEMPOCORESHARED_API UTempoCoreSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTempoCoreSettings();

	// Allow command-line overrides
	virtual void PostInitProperties() override;
	
	// Time Settings.
	void SetTimeMode(ETimeMode TimeModeIn);
	void SetSimulatedStepsPerSecond(int32 SimulatedStepsPerSecondIn);
	ETimeMode GetTimeMode() const { return TimeMode; }
	double GetMaxWallClockTimeStep() const { return MaxWallClockTimeStep; }
	int32 GetSimulatedStepsPerSecond() const { return SimulatedStepsPerSecond; }
	FTempoCoreTimeSettingsChanged TempoCoreTimeSettingsChangedEvent;

	// Scripting Settings.
	int32 GetScriptingPort() const { return ScriptingPort; }
	EScriptingCompressionLevel GetScriptingCompressionLevel() const { return ScriptingCompressionLevel; }
	int32 GetMaxEventProcessingTime() const { return MaxEventProcessingTimeMicroSeconds; }
	int32 GetMaxEventWaitTime() const { return MaxEventWaitTimeNanoSeconds; }

	// Packaging Settings
	bool GetAssignLevelsToIndividualChunks() const { return bAssignLevelsToIndividualChunks; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

#if WITH_EDITORONLY_DATA
	static FName GetScriptingPortMemberName() { return GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, ScriptingPort); }
	static FName GetScriptingCompressionLevelMemberName() { return GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, ScriptingCompressionLevel); }
#endif
	
private:
	UPROPERTY(EditAnywhere, Config, Category="Time")
	ETimeMode TimeMode = ETimeMode::WallClock;

	// The number of evenly-spaced steps per simulated second that will be executed in FixedStep time mode.
	UPROPERTY(EditAnywhere, Config, Category="Time|FixedStep", meta=(ClampMin=1, UIMin=1, UIMax=100))
	int32 SimulatedStepsPerSecond = 10;

	// The largest time step allowed in WallClock time mode. No limit if 0.0. Note that setting this to non-zero violates 
	// WallClock time mode's guarantee of strictly advancing along with wall clock at the step that would have exceeded the max.
	UPROPERTY(EditAnywhere, Config, Category="Time|WallClock", meta=(ClampMin=0.0, UIMin=0.0, UIMax=1.0))
	double MaxWallClockTimeStep = 0.0;

	// The port number to listen for scripting connections on.
	UPROPERTY(EditAnywhere, Config, Category="Scripting", meta=(ClampMin=1024, ClampMax=65535, UIMin=1024, UIMax=65535))
	int32 ScriptingPort = 10001;

	// The default compression level to use for Tempo Scripting messages. When the client is on the same machine no
	// compression is fastest. Otherwise, compression may help reduce network bandwidth.
	UPROPERTY(EditAnywhere, Config, Category="Scripting")
	EScriptingCompressionLevel ScriptingCompressionLevel = EScriptingCompressionLevel::None;
	
	// We will spend as much as this amount of time (in microseconds) processing events each Tick.
	// Except in FixedStep mode, where we process all received events every Tick.
	UPROPERTY(EditAnywhere, Config, Category="Scripting|Advanced", meta=(ClampMin=1, ClampMax=10000, UIMin=1, UIMax=10000))
	int32 MaxEventProcessingTimeMicroSeconds = 1000;

	// We will wait as much as this amount of time (in nanoseconds) for an event to arrive each time we check for an event.
	UPROPERTY(EditAnywhere, Config, Category="Scripting|Advanced", meta=(ClampMin=1, ClampMax=10000, UIMin=1, UIMax=10000))
	int32 MaxEventWaitTimeNanoSeconds = 1000;

	// If true, each level will be assigned to its own chunk during packaging.
	// **NOTE** Requires enabling project packaging settings UsePakFile and GenerateChunks.
	UPROPERTY(EditAnywhere, Config, Category="Packaging")
	bool bAssignLevelsToIndividualChunks = false;
};
