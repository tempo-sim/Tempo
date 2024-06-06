// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreSettings.h"

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
