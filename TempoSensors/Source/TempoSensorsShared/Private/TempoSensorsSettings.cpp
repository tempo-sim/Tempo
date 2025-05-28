// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorsSettings.h"


#if WITH_EDITOR
void UTempoSensorsSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FString PropertyChangedName = PropertyChangedEvent.Property->GetName();
	if (PropertyChangedName == GET_MEMBER_NAME_CHECKED(UTempoSensorsSettings, LabelType))
	{
		TempoSensorsLabelSettingsChangedEvent.Broadcast();
	}
}
#endif
