// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorsSettings.h"

UTempoSensorsSettings::UTempoSensorsSettings()
	:
CameraPostProcessMaterialNoDepth(FSoftObjectPath(TEXT("/TempoSensors/Materials/M_CameraPostProcess_NoDepth.M_CameraPostProcess_NoDepth"))),
CameraPostProcessMaterialWithDepth(FSoftObjectPath(TEXT("/TempoSensors/Materials/M_CameraPostProcess_WithDepth.M_CameraPostProcess_WithDepth")))
{
	CategoryName = TEXT("Tempo");
}

#if WITH_EDITOR
FText UTempoSensorsSettings::GetSectionText() const
{
	return FText::FromString(FString(TEXT("Sensors")));
}
#endif

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
