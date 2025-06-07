// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorsSettings.h"

UTempoSensorsSettings::UTempoSensorsSettings()
{
	CategoryName = TEXT("Tempo");
}

#if WITH_EDITOR
FText UTempoSensorsSettings::GetSectionText() const
{
	return FText::FromString(FString(TEXT("Sensors")));
}
#endif
