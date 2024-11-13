// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreSettings.h"

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

#if WITH_EDITOR
	FPropertyChangedEvent PropertyChangedEvent(
		GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, TimeMode)));
	OnSettingChanged().Broadcast(this, PropertyChangedEvent);
#endif
}

void UTempoCoreSettings::SetSimulatedStepsPerSecond(int32 SimulatedStepsPerSecondIn)
{
	SimulatedStepsPerSecond = SimulatedStepsPerSecondIn;

#if WITH_EDITOR
	FPropertyChangedEvent PropertyChangedEvent(
		GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UTempoCoreSettings, SimulatedStepsPerSecond)));
	OnSettingChanged().Broadcast(this, PropertyChangedEvent);
#endif
}
