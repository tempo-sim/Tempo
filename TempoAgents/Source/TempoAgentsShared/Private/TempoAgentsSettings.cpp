// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoAgentsSettings.h"

UTempoAgentsSettings::UTempoAgentsSettings()
{
	CategoryName = TEXT("Tempo");
}

#if WITH_EDITOR
FText UTempoAgentsSettings::GetSectionText() const
{
	return FText::FromString(FString(TEXT("Agents")));
}
#endif
