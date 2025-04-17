// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreSettings.h"

UTempoCoreSettings::UTempoCoreSettings()
{
	CategoryName = TEXT("Tempo");
}

#if WITH_EDITOR
FText UTempoCoreSettings::GetSectionText() const
{
	return FText::FromString(FString(TEXT("Core")));
}
#endif

void UTempoCoreSettings::PostInitProperties()
{
	Super::PostInitProperties();

	int32 CommandLineScriptingPort;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScriptingPort="), CommandLineScriptingPort))
	{
		ScriptingPort = CommandLineScriptingPort;
	}
}

void UTempoCoreSettings::SetTimeMode(ETimeMode TimeModeIn)
{
	TimeMode = TimeModeIn;

	TempoCoreTimeSettingsChangedEvent.Broadcast();
}

void UTempoCoreSettings::SetSimulatedStepsPerSecond(int32 SimulatedStepsPerSecondIn)
{
	SimulatedStepsPerSecond = SimulatedStepsPerSecondIn;

	TempoCoreTimeSettingsChangedEvent.Broadcast();
}

#if WITH_EDITOR
void UTempoCoreSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	if (PropertyChangedEvent.Property->GetName() == GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, TimeMode) ||
		(TimeMode == ETimeMode::FixedStep && PropertyChangedEvent.Property->GetName() == GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, SimulatedStepsPerSecond)))
	{
		TempoCoreTimeSettingsChangedEvent.Broadcast();
	}
}
#endif
